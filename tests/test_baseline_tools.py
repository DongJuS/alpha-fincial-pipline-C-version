from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.capture_baseline import capture
from tools.verify_baseline import validate


class BaselineToolsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        self.source = Path(self.tempdir.name)
        subprocess.run(["git", "init", "-q"], cwd=self.source, check=True)
        subprocess.run(
            ["git", "config", "user.email", "test@example.invalid"],
            cwd=self.source,
            check=True,
        )
        subprocess.run(
            ["git", "config", "user.name", "Baseline Test"],
            cwd=self.source,
            check=True,
        )
        (self.source / "requirements.txt").write_text("pytest==8.0.0\n")
        subprocess.run(["git", "add", "requirements.txt"], cwd=self.source, check=True)
        subprocess.run(["git", "commit", "-qm", "fixture"], cwd=self.source, check=True)

    def tearDown(self) -> None:
        self.tempdir.cleanup()

    def test_capture_and_validate_clean_repository(self) -> None:
        record = capture(self.source)
        self.assertEqual([], validate(record, self.source))
        self.assertFalse(record["python_reference"]["dirty"])
        self.assertFalse(record["eligibility"]["performance_publication"])

    def test_capture_reports_untracked_files(self) -> None:
        (self.source / "note.txt").write_text("not behavioral input\n")
        record = capture(self.source)
        self.assertTrue(record["python_reference"]["dirty"])
        self.assertFalse(record["python_reference"]["tracked_dirty"])
        self.assertEqual(["note.txt"], record["python_reference"]["untracked_paths"])

    def test_validate_rejects_changed_manifest(self) -> None:
        record = capture(self.source)
        (self.source / "requirements.txt").write_text("pytest==8.1.0\n")
        self.assertIn(
            "dependency manifest checksum does not match source",
            validate(record, self.source),
        )

    def test_record_is_json_serializable(self) -> None:
        json.dumps(capture(self.source))


if __name__ == "__main__":
    unittest.main()
