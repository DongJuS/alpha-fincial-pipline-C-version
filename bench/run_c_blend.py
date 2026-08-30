#!/usr/bin/env python3
"""Measure the C blend-batch workload on the parity-passing bench artifact.

Builds the bench preset, runs the blending golden-parity ctest (so the timed
binary is parity-eligible), times core/blend_runner, and cross-checks the
buy_checksum against the Python blend-batch result when present.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import platform
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def percentile(values: list[float], q: float) -> float:
    ordered = sorted(values)
    rank = (len(ordered) - 1) * q
    lo = math.floor(rank)
    hi = math.ceil(rank)
    if lo == hi:
        return ordered[lo]
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (rank - lo)


def canonical_bytes(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git_commit() -> str:
    return subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, check=True, capture_output=True, text=True
    ).stdout.strip()


def build_and_gate() -> None:
    subprocess.run(["cmake", "--preset", "bench"], cwd=ROOT, check=True)
    subprocess.run(["cmake", "--build", "--preset", "bench", "--parallel"], cwd=ROOT, check=True)
    subprocess.run(
        ["ctest", "--preset", "bench", "--tests-regex", "blending"], cwd=ROOT, check=True
    )


def run_benchmark(count: int, golden: Path, python_result: Path | None) -> dict:
    build_and_gate()
    runner = ROOT / "build" / "bench" / "core" / "blend_runner"
    timing = json.loads(
        subprocess.run(
            [str(runner), "bench", str(count)], cwd=ROOT, check=True, capture_output=True, text=True
        ).stdout
    )
    checksum = timing["buy_checksum"]
    checksum_matches_python = None
    if python_result is not None and python_result.exists():
        py = json.loads(python_result.read_bytes())
        checksum_matches_python = py["workload"]["buy_checksum"] == checksum
        if not checksum_matches_python:
            raise ValueError("C and Python blend-batch checksums differ; not the same workload")
    samples_ms = timing["samples_ms"]
    return {
        "case": "blend-batch",
        "variant": "c",
        "eligible": True,
        "parity": {"passed": True, "golden_sha256": sha256_file(golden)},
        "workload": {
            "count": count,
            "buy_checksum": checksum,
            "checksum_matches_python": checksum_matches_python,
            "concurrency": 1,
        },
        "source": {"commit": git_commit()},
        "environment": {"host_id": platform.node(), "cpu": platform.machine(), "os": platform.platform()},
        "build": {"runtime": "native", "flags": "cmake bench preset: -O3 -DNDEBUG -ffp-contract=off"},
        "samples_ms": samples_ms,
        "summary": {
            "median_ms": statistics.median(samples_ms),
            "p95_ms": percentile(samples_ms, 0.95),
            "min_ms": min(samples_ms),
            "max_ms": max(samples_ms),
            "stddev_ms": statistics.stdev(samples_ms),
            "sample_count": len(samples_ms),
        },
        "resources": {"peak_rss_bytes": timing["peak_rss_bytes"], "cpu_time_ms": timing["cpu_time_ms"]},
        "errors": {"count": 0, "dropped": 0},
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--count", type=int, default=200_000)
    parser.add_argument("--golden", type=Path, default=ROOT / "core/tests/golden/blend-cases.json")
    parser.add_argument("--python-result", type=Path, default=None)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = run_benchmark(args.count, args.golden, args.python_result)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(canonical_bytes(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
