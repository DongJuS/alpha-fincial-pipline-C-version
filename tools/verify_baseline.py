#!/usr/bin/env python3
"""Validate baseline metadata and optionally compare it with its source tree."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(record: dict[str, object], source: Path | None = None) -> list[str]:
    errors: list[str] = []
    reference = record.get("python_reference")
    dependencies = record.get("dependencies")
    eligibility = record.get("eligibility")
    if record.get("format_version") != 1:
        errors.append("format_version must be 1")
    if not isinstance(reference, dict) or len(str(reference.get("commit", ""))) != 40:
        errors.append("python_reference.commit must be a full 40-character SHA")
    if not isinstance(dependencies, dict):
        errors.append("dependencies must be an object")
    if not isinstance(eligibility, dict) or not eligibility.get("blockers"):
        errors.append("eligibility.blockers must be a non-empty list")

    if source is not None and isinstance(reference, dict) and isinstance(dependencies, dict):
        source = source.resolve()
        actual_commit = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=source,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        if reference.get("commit") != actual_commit:
            errors.append("recorded Python commit does not match source")
        manifest = dependencies.get("manifest")
        if not isinstance(manifest, str):
            errors.append("dependencies.manifest must be a string")
        else:
            manifest_path = source / manifest
            if not manifest_path.is_file():
                errors.append("recorded dependency manifest does not exist")
            elif dependencies.get("manifest_sha256") != sha256(manifest_path):
                errors.append("dependency manifest checksum does not match source")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("record", type=Path)
    parser.add_argument("--source", type=Path)
    args = parser.parse_args()
    record = json.loads(args.record.read_text(encoding="utf-8"))
    errors = validate(record, args.source)
    for error in errors:
        print(error)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
