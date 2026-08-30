from __future__ import annotations

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class CiWorkflowTest(unittest.TestCase):
    def setUp(self) -> None:
        self.workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")

    def test_pins_reference_and_current_action_major(self) -> None:
        self.assertIn("3642cdc0e4026424ca9b6158125551eee1d42683", self.workflow)
        self.assertIn("actions/checkout@v6", self.workflow)
        self.assertIn("actions/setup-python@v6", self.workflow)

    def test_runs_both_c_presets_and_flag_verifier(self) -> None:
        self.assertIn("preset: [dev, bench]", self.workflow)
        self.assertIn("ctest --preset", self.workflow)
        self.assertIn("tools/verify_build_flags.py dev", self.workflow)
        self.assertIn("tools/verify_build_flags.py bench", self.workflow)

    def test_quality_gates_are_not_soft_failed(self) -> None:
        self.assertIn("clang-format --dry-run --Werror", self.workflow)
        self.assertIn("clang-tidy -p build", self.workflow)
        self.assertNotIn("continue-on-error", self.workflow)

    def test_shared_services_run_schema_and_event_loop_gates(self) -> None:
        self.assertEqual(2, self.workflow.count("Check out pinned Python reference"))
        self.assertIn('ALPHA_RUN_SCHEMA_APPLY: "1"', self.workflow)
        self.assertIn("tests.test_schema_apply", self.workflow)
        self.assertIn('ALPHA_RUN_EVENT_LOOP_SPIKE: "1"', self.workflow)
        self.assertIn("tests.test_event_loop_spike", self.workflow)


if __name__ == "__main__":
    unittest.main()
