from __future__ import annotations

import argparse
import importlib.util
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "collect_mvp2_environment", ROOT / "bench/collect_mvp2_environment.py"
)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class CollectMvp2EnvironmentTest(unittest.TestCase):
    def test_source_requires_label_and_path(self) -> None:
        label, path = MODULE.parse_source("c=.")
        self.assertEqual("c", label)
        self.assertEqual(ROOT, path)
        with self.assertRaises(argparse.ArgumentTypeError):
            MODULE.parse_source("missing-path")

    @mock.patch.object(MODULE, "optional_command", return_value="one\ntwo")
    @mock.patch.object(MODULE, "command")
    def test_container_identity_excludes_environment(self, command_mock, _optional) -> None:
        command_mock.side_effect = [
            "/project-postgres-1|sha256:aaa|postgres:15.14-alpine|running",
            "/project-redis-1|sha256:bbb|redis:7.4.11-alpine|running",
        ]
        containers = MODULE.container_metadata()
        self.assertEqual(["project-postgres-1", "project-redis-1"], [c["name"] for c in containers])
        self.assertEqual("sha256:aaa", containers[0]["image_id"])
        self.assertTrue(all("environment" not in container for container in containers))


if __name__ == "__main__":
    unittest.main()
