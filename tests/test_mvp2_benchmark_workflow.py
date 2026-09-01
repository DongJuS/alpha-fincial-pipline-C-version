from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class Mvp2BenchmarkWorkflowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = (ROOT / ".github/workflows/mvp2-benchmark.yml").read_text(
            encoding="utf-8"
        )

    def test_is_manual_single_authoritative_runner_job(self) -> None:
        self.assertIn("workflow_dispatch:", self.workflow)
        self.assertIn("runs-on: [self-hosted, linux, x64, alpha-bench]", self.workflow)
        self.assertIn("group: mvp2-authoritative-alpha-bench", self.workflow)
        self.assertNotIn("strategy:", self.workflow)

    def test_uses_exact_release_artifacts_for_smoke_and_matrix(self) -> None:
        self.assertIn(
            "cmake --preset bench -DALPHA_WITH_DRIVERS=ON -DALPHA_ENABLE_MVP2_INTEGRATION=ON",
            self.workflow,
        )
        self.assertIn("ctest --preset bench", self.workflow)
        self.assertIn("cargo test --manifest-path edge/Cargo.toml", self.workflow)
        self.assertIn("--smoke --trials 1", self.workflow)
        self.assertIn("--trials 30", self.workflow)
        self.assertGreaterEqual(self.workflow.count("build/bench/core/c_driver_adapter"), 3)
        self.assertGreaterEqual(self.workflow.count("edge/target/release/rust_driver_adapter"), 3)

    def test_preserves_both_decisions_and_diagnostics_without_committing(self) -> None:
        self.assertIn("--candidate c", self.workflow)
        self.assertIn("--candidate rust", self.workflow)
        self.assertIn("if: always()", self.workflow)
        self.assertIn("actions/upload-artifact@v4", self.workflow)
        self.assertIn("retention-days: 90", self.workflow)
        self.assertNotIn("git commit", self.workflow)
        self.assertNotIn("git push", self.workflow)


if __name__ == "__main__":
    unittest.main()
