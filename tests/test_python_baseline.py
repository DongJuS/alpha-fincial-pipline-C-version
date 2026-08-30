from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.generate_backtest_fixture import build_fixture, canonical_bytes, write_outputs
from tools.generate_python_golden import build_golden

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT.parent / "alpha-financial-pipeline"


class PythonBaselineTest(unittest.TestCase):
    def test_fixture_shape_and_signals(self) -> None:
        fixture = build_fixture()
        self.assertEqual("backtest-small-v1", fixture["fixture_version"])
        self.assertEqual(1_000, len(fixture["bars"]))
        self.assertEqual(20, list(fixture["signals"].values()).count("BUY"))
        self.assertEqual(20, list(fixture["signals"].values()).count("SELL"))

    def test_fixture_generation_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            first_fixture, first_manifest = write_outputs(Path(first))
            second_fixture, second_manifest = write_outputs(Path(second))
            self.assertEqual(first_fixture.read_bytes(), second_fixture.read_bytes())
            self.assertEqual(first_manifest.read_bytes(), second_manifest.read_bytes())

    def test_manifest_matches_committed_fixture(self) -> None:
        fixture_path = ROOT / "bench/fixtures/backtest-small.json"
        manifest = json.loads((ROOT / "bench/fixtures/manifest.json").read_bytes())
        actual = hashlib.sha256(fixture_path.read_bytes()).hexdigest()
        self.assertEqual(actual, manifest["fixtures"]["backtest-small"]["sha256"])

    def test_committed_golden_matches_fresh_reference_run(self) -> None:
        fixture_path = ROOT / "bench/fixtures/backtest-small.json"
        committed = json.loads((ROOT / "core/tests/golden/backtest-small.json").read_bytes())
        fresh = build_golden(SOURCE, fixture_path)
        self.assertEqual(committed["input_sha256"], fresh["input_sha256"])
        self.assertEqual(committed["result_sha256"], fresh["result_sha256"])
        self.assertEqual(canonical_bytes(committed["result"]), canonical_bytes(fresh["result"]))

    def test_workload_lock_matches_fixture(self) -> None:
        lock = json.loads((ROOT / "bench/baseline/python-backtest-lock.json").read_bytes())
        fixture = (ROOT / "bench/fixtures/backtest-small.json").read_bytes()
        self.assertEqual(hashlib.sha256(fixture).hexdigest(), lock["fixture"]["sha256"])
        self.assertEqual([], lock["dependencies"]["external_packages"])
        self.assertFalse(lock["schema"]["required"])
        for source_file, expected in lock["source"]["files_sha256"].items():
            content = subprocess.run(
                ["git", "show", f"{lock['source']['commit']}:{source_file}"],
                cwd=SOURCE,
                check=True,
                capture_output=True,
            ).stdout
            self.assertEqual(expected, hashlib.sha256(content).hexdigest())

    def test_committed_result_is_eligible_and_controlled(self) -> None:
        result = json.loads(
            (ROOT / "bench/results/20260830/backtest-small-python.json").read_bytes()
        )
        self.assertTrue(result["eligible"])
        self.assertTrue(result["parity"]["passed"])
        self.assertEqual(10, result["summary"]["sample_count"])
        self.assertTrue(all(sample >= 1_000 for sample in result["trial_totals_ms"]))
        self.assertEqual({"count": 0, "dropped": 0}, result["errors"])
        self.assertTrue(result["source"]["dirty"])


if __name__ == "__main__":
    unittest.main()
