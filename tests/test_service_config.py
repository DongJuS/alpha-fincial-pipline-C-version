from __future__ import annotations

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class ServiceConfigTest(unittest.TestCase):
    def setUp(self) -> None:
        self.compose = (ROOT / "docker-compose.yml").read_text(encoding="utf-8")

    def test_images_are_version_pinned(self) -> None:
        self.assertIn("image: postgres:15.14-alpine", self.compose)
        self.assertIn("image: redis:7.4.11-alpine", self.compose)
        self.assertNotIn(":latest", self.compose)

    def test_services_have_healthchecks_and_bounded_resources(self) -> None:
        self.assertEqual(2, self.compose.count("healthcheck:"))
        self.assertEqual(2, self.compose.count("mem_limit:"))
        self.assertEqual(2, self.compose.count("cpus:"))

    def test_default_ports_are_nonstandard_and_loopback_only(self) -> None:
        self.assertIn("127.0.0.1:${ALPHA_POSTGRES_PORT:-55432}:5432", self.compose)
        self.assertIn("127.0.0.1:${ALPHA_REDIS_PORT:-56379}:6379", self.compose)

    def test_storage_is_ephemeral(self) -> None:
        self.assertIn("/var/lib/postgresql/data", self.compose)
        self.assertIn("/data", self.compose)
        self.assertNotIn("volumes:", self.compose)


if __name__ == "__main__":
    unittest.main()
