import importlib.util
import json
import shutil
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / "bench" / "baseline" / "python-schema-lock.json"
SOURCE = ROOT.parent / "alpha-financial-pipeline"
SPEC = importlib.util.spec_from_file_location("verify_schema_lock", ROOT / "tools" / "verify_schema_lock.py")
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)
CAPTURE_SPEC = importlib.util.spec_from_file_location(
    "capture_schema_lock", ROOT / "tools" / "capture_schema_lock.py"
)
CAPTURE_MODULE = importlib.util.module_from_spec(CAPTURE_SPEC)
assert CAPTURE_SPEC.loader is not None
CAPTURE_SPEC.loader.exec_module(CAPTURE_MODULE)


class SchemaLockTests(unittest.TestCase):
    def test_pinned_python_schema_sources_match(self) -> None:
        MODULE.verify(LOCK_PATH, SOURCE)

    def test_committed_lock_matches_fresh_capture(self) -> None:
        committed = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
        self.assertEqual(committed, CAPTURE_MODULE.capture(SOURCE))

    def test_changed_source_is_rejected(self) -> None:
        lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as directory:
            copied = Path(directory)
            for item in lock["sources"]:
                destination = copied / item["path"]
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(SOURCE / item["path"], destination)
            changed = copied / lock["sources"][0]["path"]
            changed.write_bytes(changed.read_bytes() + b"\nchanged")
            with self.assertRaises(AssertionError):
                MODULE.verify(LOCK_PATH, copied, check_revision=False)


if __name__ == "__main__":
    unittest.main()
