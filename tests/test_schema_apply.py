import importlib.util
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT.parent / "alpha-financial-pipeline"
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "apply_python_schema", ROOT / "tools" / "apply_python_schema.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class SchemaApplyTests(unittest.TestCase):
    def test_extracts_exact_literal_bootstrap(self) -> None:
        statements = MODULE.extract_create_tables(SOURCE / "scripts/db/init_db.py")
        self.assertEqual(51, len(statements))
        joined = "\n".join(statements).lower()
        for table in MODULE.REQUIRED_TABLES:
            self.assertIn(f"create table if not exists {table}", joined)

    def test_rejects_executable_assignment(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "bad.py"
            source.write_text("CREATE_TABLES: list[str] = make_sql()\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                MODULE.extract_create_tables(source)

    def test_extracts_locked_incremental_migrations_without_importing(self) -> None:
        statements = MODULE.extract_locked_migrations(SOURCE)
        self.assertEqual(6, len(statements))
        joined = "\n".join(statements)
        self.assertIn("PARTITION BY RANGE (bucket_at)", joined)
        self.assertIn("ohlcv_minute_2026_04", joined)
        self.assertIn("ohlcv_minute_2026_05", joined)
        self.assertIn("ohlcv_minute_2026_06", joined)
        self.assertIn("idx_ohlcv_minute_bucket", joined)
        self.assertIn("idx_ohlcv_minute_instrument", joined)

    @unittest.skipUnless(
        os.environ.get("ALPHA_RUN_SCHEMA_APPLY") == "1",
        "set ALPHA_RUN_SCHEMA_APPLY=1 with PostgreSQL running",
    )
    def test_bootstrap_is_idempotent_and_required_tables_exist(self) -> None:
        database_url = os.environ.get(
            "ALPHA_TEST_DATABASE_URL",
            "postgresql://alpha_test:alpha_test_only@127.0.0.1:55432/alpha_test",
        )
        lock = ROOT / "bench/baseline/python-schema-lock.json"
        MODULE.apply(SOURCE, lock, database_url)
        first_catalog = MODULE.catalog_snapshot(database_url)
        MODULE.apply(SOURCE, lock, database_url)
        second_catalog = MODULE.catalog_snapshot(database_url)
        self.assertRegex(first_catalog, r"^[0-9a-f]{32}$")
        self.assertEqual(first_catalog, second_catalog)
        query = "SELECT to_regclass('public.' || name) IS NOT NULL FROM unnest(ARRAY[{}]) name".format(
            ",".join(f"'{table}'" for table in MODULE.REQUIRED_TABLES)
        )
        output = subprocess.run(
            ["psql", database_url, "--no-psqlrc", "--tuples-only", "--command", query],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.split()
        self.assertEqual(["t"] * len(MODULE.REQUIRED_TABLES), output)


if __name__ == "__main__":
    unittest.main()
