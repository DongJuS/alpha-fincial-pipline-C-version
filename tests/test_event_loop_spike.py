import importlib.util
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERIFY_PATH = ROOT / "spike" / "event_loop" / "verify_result.py"


class EventLoopSpikeTests(unittest.TestCase):
    def test_verifier_rejects_missing_recovery(self) -> None:
        spec = importlib.util.spec_from_file_location("verify_result", VERIFY_PATH)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            result = Path(directory) / "bad.json"
            result.write_text(json.dumps({"schema_version": 1}), encoding="utf-8")
            with self.assertRaises((AssertionError, KeyError)):
                module.verify(result, Path("unused"))

    @unittest.skipUnless(
        os.environ.get("ALPHA_RUN_EVENT_LOOP_SPIKE") == "1",
        "set ALPHA_RUN_EVENT_LOOP_SPIKE=1 with PostgreSQL and Redis running",
    )
    def test_live_single_loop_evidence(self) -> None:
        subprocess.run([str(ROOT / "spike" / "event_loop" / "run.sh")], check=True)
        result = json.loads((ROOT / "build" / "event-loop" / "result.json").read_text())
        self.assertTrue(result["clean_shutdown"])


if __name__ == "__main__":
    unittest.main()
