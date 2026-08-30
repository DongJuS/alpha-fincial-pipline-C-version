from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from tools.verify_build_flags import verify_compile_commands


class BuildFlagsTest(unittest.TestCase):
    def verify(self, profile: str, command: str) -> list[str]:
        with tempfile.TemporaryDirectory() as directory:
            database = Path(directory) / "compile_commands.json"
            database.write_text(
                json.dumps([{"file": "/repo/core/src/alpha.c", "command": command}])
            )
            return verify_compile_commands(database, profile)

    def test_accepts_dev_contract(self) -> None:
        self.assertEqual(
            [],
            self.verify(
                "dev",
                "cc -ffp-contract=off -fsanitize=address,undefined -c alpha.c",
            ),
        )

    def test_accepts_bench_contract(self) -> None:
        self.assertEqual(
            [], self.verify("bench", "cc -O3 -DNDEBUG -ffp-contract=off -c alpha.c")
        )

    def test_rejects_fast_math(self) -> None:
        errors = self.verify(
            "bench", "cc -O3 -DNDEBUG -ffp-contract=off -ffast-math -c alpha.c"
        )
        self.assertIn("owned compile command contains forbidden -ffast-math", errors)

    def test_rejects_missing_sanitizers(self) -> None:
        errors = self.verify("dev", "cc -ffp-contract=off -c alpha.c")
        self.assertIn("dev compile command is missing ASan/UBSan", errors)


if __name__ == "__main__":
    unittest.main()
