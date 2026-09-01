from __future__ import annotations

import importlib.util
import json
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("driver_protocol", ROOT / "bench/run_driver_mvp2.py")
assert SPEC and SPEC.loader
run = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(run)
FIXTURE = ROOT / "bench/fixtures/driver-workloads.json"


class DriverProtocolTest(unittest.TestCase):
    def test_numeric_validation_rejects_bool_nan_and_infinity(self):
        self.assertFalse(run.positive_number(True))
        self.assertFalse(run.positive_number(float("nan")))
        self.assertFalse(run.positive_number(float("inf")))
        self.assertFalse(run.positive_int(True))
        self.assertTrue(run.positive_number(1.5))
        self.assertTrue(run.positive_int(1))
        self.assertFalse(run.exact_value(True, 1))

    @mock.patch.object(run, "run_adapter")
    def test_measured_trial_below_one_second_fails_before_publication(self, adapter):
        adapter.return_value = {"elapsed_ms": 999.999}
        with self.assertRaisesRegex(ValueError, "timer-noise floor"):
            run.execute(
                FIXTURE,
                {"python": "py", "c": "c", "rust": "rust"},
                {"python": "p:clean", "c": "c:clean", "rust": "r:clean"},
                1,
                minimum_trials=1,
                minimum_elapsed_ms=1000.0,
            )

    def test_fixture_has_fixed_duration_sized_operation_trials(self):
        fixture = json.loads(FIXTURE.read_bytes())
        self.assertEqual(21000, fixture["cases"]["redis-hot-path"]["repeat"] * 3)
        self.assertEqual(9000, fixture["cases"]["db-read-write"]["repeat"] * 3)

    def test_environment_attestation_has_stable_required_fields(self):
        environment = run.host_environment()
        required = {
            "host_id", "system", "kernel_release", "kernel_version", "machine",
            "cpu_model", "logical_cpu_count", "ram_bytes", "python_runtime",
            "runner_name", "runner_os", "runner_arch", "runner_image",
        }
        self.assertEqual(required, set(environment))
        self.assertTrue(run.positive_int(environment["logical_cpu_count"]))
        self.assertTrue(run.positive_int(environment["ram_bytes"]))

    @mock.patch.object(run, "adapter_attestation")
    @mock.patch.object(run, "inspect_source")
    def test_post_run_reattestation_rejects_source_or_artifact_drift(self, source, artifact):
        source.return_value = "new:clean"
        artifact.return_value = {"artifact_sha256": "same"}
        with self.assertRaisesRegex(ValueError, "source changed"):
            run.verify_post_run_attestation(
                {"c": "cmd"}, {"c": "."}, {"c": "old:clean"},
                {"c": {"artifact_sha256": "same"}},
            )

        source.return_value = "old:clean"
        artifact.return_value = {"artifact_sha256": "new"}
        with self.assertRaisesRegex(ValueError, "artifact or command changed"):
            run.verify_post_run_attestation(
                {"c": "cmd"}, {"c": "."}, {"c": "old:clean"},
                {"c": {"artifact_sha256": "old"}},
            )


if __name__ == "__main__":
    unittest.main()
