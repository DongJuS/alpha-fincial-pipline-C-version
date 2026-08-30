#!/usr/bin/env python3
"""Validate the content-addressed Python schema identity."""

import argparse
import hashlib
import json
import subprocess
from pathlib import Path


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def bundle_sha256(sources: list[dict[str, str]]) -> str:
    payload = "".join(f"{item['path']}\0{item['sha256']}\n" for item in sources)
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def verify(lock_path: Path, source: Path, check_revision: bool = True) -> None:
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    sources = lock["sources"]
    assert sources == sorted(sources, key=lambda item: item["path"])
    assert lock["identity_kind"] == "content-addressed-bootstrap-bundle"
    assert lock["version_table"] is None
    for item in sources:
        assert file_sha256(source / item["path"]) == item["sha256"], item["path"]
    assert lock["schema_identity"] == f"bootstrap-sha256:{bundle_sha256(sources)}"
    if check_revision:
        revision = subprocess.run(
            ["git", "-C", str(source), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        assert revision == lock["python_commit"]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lock", type=Path, default=Path("bench/baseline/python-schema-lock.json"))
    parser.add_argument("--source", type=Path, default=Path("../alpha-financial-pipeline"))
    args = parser.parse_args()
    verify(args.lock, args.source)
    print("python schema lock: PASS")


if __name__ == "__main__":
    main()
