import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERIFY_PATH = ROOT / "tools" / "verify_pinned_libwebsockets.py"


class LibwebsocketsPinTests(unittest.TestCase):
    def setUp(self) -> None:
        spec = importlib.util.spec_from_file_location("verify_lws", VERIFY_PATH)
        self.module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(self.module)

    def test_lock_names_exact_single_loop_options(self) -> None:
        lock = json.loads(
            (ROOT / "third_party" / "libwebsockets.lock.json").read_text()
        )
        self.assertEqual(lock["tag"], "v4.3.3")
        self.assertEqual(lock["pkg_config_version"], "4.3.3-v4.3.3")
        self.assertEqual(len(lock["commit"]), 40)
        for option in ("LWS_WITH_LIBUV", "LWS_WITH_LIBEVENT", "LWS_WITH_GLIB"):
            self.assertEqual(lock["cmake"][option], "OFF")
        self.assertEqual(lock["cmake"]["LWS_WITH_SHARED"], "OFF")
        self.assertEqual(lock["cmake"]["LWS_WITH_STATIC"], "ON")
        self.assertEqual(lock["cmake"]["LWS_WITH_SSL"], "OFF")

    def test_verifier_rejects_wrong_revision(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            subprocess.run(["git", "init", "-q", str(source)], check=True)
            subprocess.run(
                ["git", "-C", str(source), "-c", "user.name=test", "-c",
                 "user.email=test@example.invalid", "commit", "--allow-empty", "-qm",
                 "fixture"],
                check=True,
            )
            lock = root / "lock.json"
            lock.write_text(
                json.dumps({"schema_version": 1, "commit": "0" * 40, "cmake": {}})
            )
            with self.assertRaises(AssertionError):
                self.module.verify(lock, source, root / "build", root / "prefix")


if __name__ == "__main__":
    unittest.main()
