#!/usr/bin/env python3
"""Fail when required dev/bench compiler flags drift."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def verify_compile_commands(path: Path, profile: str) -> list[str]:
    commands = json.loads(path.read_text(encoding="utf-8"))
    owned = [
        entry["command"]
        for entry in commands
        if "/core/src/" in entry["file"] or "/core/tests/" in entry["file"]
    ]
    errors: list[str] = []
    if not owned:
        return ["compile database contains no owned core source or test files"]
    for command in owned:
        if "-ffp-contract=off" not in command:
            errors.append("owned compile command is missing -ffp-contract=off")
        if "-ffast-math" in command:
            errors.append("owned compile command contains forbidden -ffast-math")
        if profile == "dev" and "-fsanitize=address,undefined" not in command:
            errors.append("dev compile command is missing ASan/UBSan")
        if profile == "bench" and "-fsanitize=" in command:
            errors.append("bench compile command unexpectedly enables sanitizers")
        if profile == "bench" and not all(flag in command for flag in ("-O3", "-DNDEBUG")):
            errors.append("bench compile command is missing -O3 or -DNDEBUG")
    return sorted(set(errors))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("profile", choices=("dev", "bench"))
    parser.add_argument("compile_commands", type=Path)
    args = parser.parse_args()
    errors = verify_compile_commands(args.compile_commands, args.profile)
    for error in errors:
        print(error)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
