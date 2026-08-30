#!/usr/bin/env python3
"""Capture reproducible metadata for the Python reference implementation."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import subprocess
from pathlib import Path


def run(*args: str, cwd: Path | None = None) -> str:
    return subprocess.run(
        args,
        cwd=cwd,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    ).stdout.strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def capture(source: Path) -> dict[str, object]:
    source = source.resolve()
    requirements = source / "requirements.txt"
    if not requirements.is_file():
        raise ValueError(f"missing dependency manifest: {requirements}")

    porcelain = run(
        "git", "status", "--porcelain=v1", "--untracked-files=all", cwd=source
    ).splitlines()
    tracked_dirty = any(not line.startswith("?? ") for line in porcelain)
    untracked = sorted(line[3:] for line in porcelain if line.startswith("?? "))

    return {
        "format_version": 1,
        "python_reference": {
            "commit": run("git", "rev-parse", "HEAD", cwd=source),
            "branch": run("git", "branch", "--show-current", cwd=source),
            "dirty": bool(porcelain),
            "tracked_dirty": tracked_dirty,
            "untracked_paths": untracked,
        },
        "dependencies": {
            "manifest": "requirements.txt",
            "manifest_sha256": sha256(requirements),
            "exact_lock_available": False,
            "resolved_environment": None,
        },
        "data_contract": {
            "schema_version": None,
            "fixture_version": None,
            "status": "unresolved",
        },
        "environment": {
            "os": platform.platform(),
            "machine": platform.machine(),
            "logical_cpu_count": os.cpu_count(),
            "python": platform.python_version(),
            "c_compiler": run("cc", "--version").splitlines()[0],
            "cmake": run("cmake", "--version").splitlines()[0],
        },
        "eligibility": {
            "golden_generation": False,
            "performance_publication": False,
            "blockers": [
                "exact Python dependency environment is not frozen",
                "schema version is unresolved",
                "fixture dataset version is unresolved",
            ],
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source", type=Path, default=Path("../alpha-financial-pipeline")
    )
    args = parser.parse_args()
    print(json.dumps(capture(args.source), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
