#!/usr/bin/env python3
"""Verify the exact libwebsockets revision, build options, and staged artifact."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


def cache_values(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        key_and_type, value = line.split("=", 1)
        key = key_and_type.split(":", 1)[0]
        values[key] = value
    return values


def verify(lock_path: Path, source: Path, build: Path, prefix: Path) -> None:
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    assert lock["schema_version"] == 1
    head = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    assert head == lock["commit"], (head, lock["commit"])

    cache = cache_values(build / "CMakeCache.txt")
    for option, expected in lock["cmake"].items():
        assert cache.get(option) == expected, (option, cache.get(option), expected)

    libraries = list((prefix / "lib").glob("libwebsockets.a")) + list(
        (prefix / "lib64").glob("libwebsockets.a")
    )
    assert len(libraries) == 1, libraries
    dynamic = list((prefix / "lib").glob("libwebsockets.so*"))
    dynamic += list((prefix / "lib64").glob("libwebsockets.so*"))
    dynamic += list((prefix / "lib").glob("libwebsockets.dylib"))
    dynamic += list((prefix / "lib64").glob("libwebsockets.dylib"))
    assert not dynamic, dynamic
    assert (prefix / "include" / "libwebsockets.h").is_file()
    pc_dirs = [prefix / "lib" / "pkgconfig", prefix / "lib64" / "pkgconfig"]
    pc_file = next(path / "libwebsockets.pc" for path in pc_dirs if path.is_dir())
    pc_version = next(
        line.split(":", 1)[1].strip()
        for line in pc_file.read_text(encoding="utf-8").splitlines()
        if line.startswith("Version:")
    )
    assert pc_version == lock["pkg_config_version"], (
        pc_version,
        lock["pkg_config_version"],
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    args = parser.parse_args()
    verify(args.lock, args.source, args.build, args.prefix)


if __name__ == "__main__":
    main()
