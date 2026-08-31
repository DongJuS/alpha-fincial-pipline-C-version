#!/usr/bin/env python3
"""Apply and assert the complete, locked Python schema bundle using psql.

The reference scripts import application/runtime dependencies.  This tool never
imports them: it verifies their locked bytes and extracts literal SQL with AST.
"""

import argparse
import ast
import os
import subprocess
from pathlib import Path

from verify_schema_lock import verify

REQUIRED_TABLES = (
    "market_data",
    "ohlcv_daily",
    "tick_data",
    "portfolio_positions",
    "trade_history",
    "broker_orders",
    "portfolio_config",
)

V2_MIGRATION = "scripts/db/migrate_to_v2_instruments.py"
MINUTE_MIGRATION = "scripts/db/migrate_ohlcv_minute.py"


def extract_create_tables(source_file: Path) -> list[str]:
    tree = ast.parse(source_file.read_text(encoding="utf-8"), filename=str(source_file))
    assignments = [
        node
        for node in tree.body
        if isinstance(node, ast.AnnAssign)
        and isinstance(node.target, ast.Name)
        and node.target.id == "CREATE_TABLES"
    ]
    if len(assignments) != 1:
        raise ValueError("expected exactly one CREATE_TABLES assignment")
    try:
        statements = ast.literal_eval(assignments[0].value)
    except (ValueError, TypeError) as error:
        raise ValueError("CREATE_TABLES must contain literals only") from error
    if not isinstance(statements, list) or not all(isinstance(sql, str) for sql in statements):
        raise ValueError("CREATE_TABLES must be a list of SQL strings")
    return statements


def extract_literal(source_file: Path, name: str):
    """Return one module-level literal assignment without executing the file."""
    tree = ast.parse(source_file.read_text(encoding="utf-8"), filename=str(source_file))
    values = []
    for node in tree.body:
        target = None
        value = None
        if isinstance(node, ast.Assign) and len(node.targets) == 1:
            target, value = node.targets[0], node.value
        elif isinstance(node, ast.AnnAssign):
            target, value = node.target, node.value
        if isinstance(target, ast.Name) and target.id == name:
            values.append(value)
    if len(values) != 1:
        raise ValueError(f"expected exactly one {name} assignment")
    try:
        return ast.literal_eval(values[0])
    except (ValueError, TypeError) as error:
        raise ValueError(f"{name} must contain literals only") from error


def extract_locked_migrations(source: Path) -> list[str]:
    """Validate the v2 contract and return the exact minute-bar migration SQL."""
    v2 = source / V2_MIGRATION
    expected_drops = extract_literal(v2, "_INSTRUMENTS_DROP_COLUMNS")
    universe_ddl = extract_literal(v2, "_TRADING_UNIVERSE_DDL")
    if "raw_code" in expected_drops or "ticker" in expected_drops:
        raise ValueError("v2 migration must preserve the ticker column")
    if not isinstance(expected_drops, list) or not all(isinstance(item, str) for item in expected_drops):
        raise ValueError("_INSTRUMENTS_DROP_COLUMNS must be a list of strings")
    if not isinstance(universe_ddl, str) or "CREATE TABLE IF NOT EXISTS trading_universe" not in universe_ddl:
        raise ValueError("v2 migration lacks literal trading_universe DDL")

    minute = source / MINUTE_MIGRATION
    table = extract_literal(minute, "CREATE_TABLE_SQL")
    partitions = extract_literal(minute, "PARTITION_SQLS")
    indexes = extract_literal(minute, "INDEX_SQLS")
    statements = [table, *partitions, *indexes]
    if not all(isinstance(sql, str) for sql in statements):
        raise ValueError("minute migration SQL must contain strings only")
    return statements


CATALOG_ASSERTIONS = r"""
DO $$
BEGIN
  IF EXISTS (
    SELECT 1 FROM information_schema.columns
    WHERE table_schema = 'public' AND table_name = 'instruments'
      AND column_name IN ('raw_code', 'name', 'name_en', 'sector', 'industry',
                          'asset_type', 'isin', 'listed_at', 'delisted_at',
                          'market_cap', 'total_shares')
  ) THEN RAISE EXCEPTION 'instruments is not the v2 column contract'; END IF;
  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
    WHERE table_schema = 'public' AND table_name = 'instruments' AND column_name = 'ticker'
  ) THEN RAISE EXCEPTION 'instruments.ticker is missing'; END IF;
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conrelid = 'public.instruments'::regclass
      AND conname = 'uq_instruments_market_ticker' AND contype = 'u'
  ) THEN RAISE EXCEPTION 'v2 instruments unique constraint is missing'; END IF;
  IF NOT EXISTS (
    SELECT 1 FROM pg_index i JOIN pg_class c ON c.oid = i.indexrelid
    WHERE c.relnamespace = 'public'::regnamespace
      AND c.relname = 'idx_instruments_active' AND i.indpred IS NOT NULL
      AND pg_get_expr(i.indpred, i.indrelid) = '(is_active = true)'
  ) THEN RAISE EXCEPTION 'instruments active partial index is missing'; END IF;
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint WHERE conrelid = 'public.trading_universe'::regclass
      AND contype = 'f' AND confrelid = 'public.trading_accounts'::regclass
  ) OR NOT EXISTS (
    SELECT 1 FROM pg_constraint WHERE conrelid = 'public.trading_universe'::regclass
      AND contype = 'f' AND confrelid = 'public.instruments'::regclass
  ) THEN RAISE EXCEPTION 'trading_universe foreign keys are missing'; END IF;
  IF NOT EXISTS (
    SELECT 1 FROM pg_partitioned_table
    WHERE partrelid = 'public.ohlcv_minute'::regclass AND partstrat = 'r'
  ) THEN RAISE EXCEPTION 'ohlcv_minute is not range partitioned'; END IF;
  IF (SELECT count(*) FROM pg_inherits
      WHERE inhparent = 'public.ohlcv_minute'::regclass) <> 3
  THEN RAISE EXCEPTION 'ohlcv_minute must have exactly three locked partitions'; END IF;
  IF (SELECT count(*) FROM pg_inherits i JOIN pg_class c ON c.oid = i.inhrelid
      WHERE i.inhparent = 'public.ohlcv_minute'::regclass
        AND (c.relname, pg_get_expr(c.relpartbound, c.oid)) IN (
          ('ohlcv_minute_2026_04', 'FOR VALUES FROM (''2026-04-01 00:00:00+00'') TO (''2026-05-01 00:00:00+00'')'),
          ('ohlcv_minute_2026_05', 'FOR VALUES FROM (''2026-05-01 00:00:00+00'') TO (''2026-06-01 00:00:00+00'')'),
          ('ohlcv_minute_2026_06', 'FOR VALUES FROM (''2026-06-01 00:00:00+00'') TO (''2026-07-01 00:00:00+00'')')
        )) <> 3
  THEN RAISE EXCEPTION 'ohlcv_minute partition bounds differ from the lock'; END IF;
  IF (SELECT count(*) FROM pg_indexes WHERE schemaname = 'public'
      AND indexname IN ('idx_ohlcv_minute_bucket', 'idx_ohlcv_minute_instrument')) <> 2
  THEN RAISE EXCEPTION 'ohlcv_minute indexes are missing'; END IF;
END $$;
DO $$
BEGIN
  BEGIN
    INSERT INTO ohlcv_minute
      (instrument_id, bucket_at, open, high, low, close)
    VALUES ('__rejection_probe__', '2026-07-01T00:00:00Z', 1, 1, 1, 1);
    RAISE EXCEPTION 'out-of-range minute row was unexpectedly accepted';
  EXCEPTION WHEN check_violation THEN
    NULL;
  END;
END $$;
BEGIN;
INSERT INTO ohlcv_minute
  (instrument_id, bucket_at, open, high, low, close)
VALUES ('__schema_probe__', '2026-05-15T00:00:00Z', 1, 1, 1, 1);
DO $$ BEGIN
  IF (SELECT tableoid::regclass::text FROM ohlcv_minute
      WHERE instrument_id = '__schema_probe__') <> 'ohlcv_minute_2026_05'
  THEN RAISE EXCEPTION 'minute row routed to wrong partition'; END IF;
END $$;
ROLLBACK;
"""


CATALOG_SNAPSHOT_QUERY = r"""
SELECT md5(string_agg(item, E'\n' ORDER BY item))
FROM (
  SELECT 'column|' || table_name || '|' || ordinal_position || '|' || column_name || '|' ||
         data_type || '|' || is_nullable || '|' || coalesce(column_default, '') AS item
  FROM information_schema.columns
  WHERE table_schema = 'public'
  UNION ALL
  SELECT 'constraint|' || conrelid::regclass::text || '|' || conname || '|' ||
         pg_get_constraintdef(oid, true)
  FROM pg_constraint WHERE connamespace = 'public'::regnamespace
  UNION ALL
  SELECT 'index|' || tablename || '|' || indexname || '|' || indexdef
  FROM pg_indexes WHERE schemaname = 'public'
  UNION ALL
  SELECT 'partition|' || inhparent::regclass::text || '|' || inhrelid::regclass::text || '|' ||
         pg_get_expr(c.relpartbound, c.oid)
  FROM pg_inherits JOIN pg_class c ON c.oid = inhrelid
) catalog;
"""


def _psql(database_url: str, sql: str, *, capture: bool = False) -> str:
    environment = os.environ.copy()
    environment["PGOPTIONS"] = "-c client_min_messages=warning"
    result = subprocess.run(
        ["psql", database_url, "--no-psqlrc", "--quiet", "--tuples-only", "--no-align"],
        input="\\set ON_ERROR_STOP on\n" + sql,
        check=True,
        env=environment,
        text=True,
        capture_output=capture,
    )
    return result.stdout.strip() if capture else ""


def catalog_snapshot(database_url: str) -> str:
    """Return a stable digest of the asserted public catalog."""
    return _psql(database_url, CATALOG_SNAPSHOT_QUERY, capture=True)


def apply(source: Path, lock: Path, database_url: str) -> None:
    verify(lock, source)
    statements = extract_create_tables(source / "scripts/db/init_db.py")
    migrations = extract_locked_migrations(source)
    sql = "CREATE EXTENSION IF NOT EXISTS pgcrypto;\n" + "\n".join([*statements, *migrations])
    _psql(database_url, sql + CATALOG_ASSERTIONS)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path("../alpha-financial-pipeline"))
    parser.add_argument("--lock", type=Path, default=Path("bench/baseline/python-schema-lock.json"))
    parser.add_argument(
        "--database-url",
        default=os.environ.get(
            "ALPHA_TEST_DATABASE_URL",
            "postgresql://alpha_test:alpha_test_only@127.0.0.1:55432/alpha_test",
        ),
    )
    args = parser.parse_args()
    apply(args.source, args.lock, args.database_url)
    print("python schema bootstrap: PASS")


if __name__ == "__main__":
    main()
