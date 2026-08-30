#!/usr/bin/env python3
"""Apply the pinned Python CREATE_TABLES bootstrap through PATH-resolved psql."""

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


def apply(source: Path, lock: Path, database_url: str) -> None:
    verify(lock, source)
    statements = extract_create_tables(source / "scripts/db/init_db.py")
    sql = "\\set ON_ERROR_STOP on\nCREATE EXTENSION IF NOT EXISTS pgcrypto;\n" + "\n".join(statements)
    environment = os.environ.copy()
    environment["PGOPTIONS"] = "-c client_min_messages=warning"
    subprocess.run(
        ["psql", database_url, "--no-psqlrc", "--quiet"],
        input=sql,
        check=True,
        env=environment,
        text=True,
    )


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
