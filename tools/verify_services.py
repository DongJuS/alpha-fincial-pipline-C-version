#!/usr/bin/env python3
"""Verify schema-independent PostgreSQL and Redis readiness through Compose."""

from __future__ import annotations

import argparse
import subprocess
import time


def compose_exec(service: str, *command: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["docker", "compose", "exec", "-T", service, *command],
        capture_output=True,
        text=True,
    )


def verify(timeout_seconds: float) -> list[str]:
    deadline = time.monotonic() + timeout_seconds
    last_errors: list[str] = []
    while time.monotonic() < deadline:
        postgres = compose_exec(
            "postgres",
            "psql",
            "-U",
            "alpha_test",
            "-d",
            "alpha_test",
            "-Atqc",
            "SELECT 1",
        )
        redis = compose_exec("redis", "redis-cli", "--raw", "PING")
        last_errors = []
        if postgres.returncode != 0 or postgres.stdout.strip() != "1":
            last_errors.append(f"PostgreSQL readiness failed: {postgres.stderr.strip()}")
        if redis.returncode != 0 or redis.stdout.strip() != "PONG":
            last_errors.append(f"Redis readiness failed: {redis.stderr.strip()}")
        if not last_errors:
            return []
        time.sleep(1)
    return last_errors or ["service readiness timed out"]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()
    errors = verify(args.timeout)
    for error in errors:
        print(error)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
