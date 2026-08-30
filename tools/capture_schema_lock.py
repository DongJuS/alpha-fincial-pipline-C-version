#!/usr/bin/env python3
"""Emit a content-addressed identity for the Python schema bootstrap bundle."""

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

SCHEMA_SOURCES = (
    "k8s/base/migration-job.yaml",
    "scripts/db/init_db.py",
    "scripts/db/migrate_ohlcv_minute.py",
    "scripts/db/migrate_to_v2_instruments.py",
)


def capture(source: Path) -> dict[str, object]:
    sources = [
        {"path": path, "sha256": hashlib.sha256((source / path).read_bytes()).hexdigest()}
        for path in SCHEMA_SOURCES
    ]
    payload = "".join(f"{item['path']}\0{item['sha256']}\n" for item in sources)
    revision = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    return {
        "schema_identity": f"bootstrap-sha256:{hashlib.sha256(payload.encode()).hexdigest()}",
        "identity_kind": "content-addressed-bootstrap-bundle",
        "python_commit": revision,
        "version_table": None,
        "sources": sources,
        "notes": [
            "The pinned Python repository has no Alembic-style revision or schema version table.",
            "init_db.py is the full idempotent bootstrap; the deployment job names the two in-scope incremental migrations locked here.",
            "This identity pins source bytes only; applying the schema to a clean database is a separate gate.",
        ],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path("../alpha-financial-pipeline"))
    args = parser.parse_args()
    print(json.dumps(capture(args.source), indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
