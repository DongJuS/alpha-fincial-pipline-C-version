#!/usr/bin/env python3
"""Measure the C backtest-small workload on the parity-passing bench artifact.

A C timing is publishable only when the *same optimized bench binary* passes the
golden parity test (BENCHMARK_PLAN.md §1). This wrapper builds the bench preset,
runs the parity ctest, then times core/backtest_runner and writes a result JSON
whose statistics use the same methodology as bench/run_python_backtest.py.
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


def percentile(values: list[float], percentile_value: float) -> float:
    ordered = sorted(values)
    rank = (len(ordered) - 1) * percentile_value
    lower = math.floor(rank)
    upper = math.ceil(rank)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (rank - lower)


def canonical_bytes(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git_state() -> dict[str, object]:
    commit = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, check=True, capture_output=True, text=True
    ).stdout.strip()
    dirty = bool(
        subprocess.run(
            ["git", "status", "--porcelain=v1", "--untracked-files=no"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    )
    return {"commit": commit, "dirty": dirty}


def build_and_gate_parity() -> None:
    subprocess.run(["cmake", "--preset", "bench"], cwd=ROOT, check=True)
    subprocess.run(["cmake", "--build", "--preset", "bench", "--parallel"], cwd=ROOT, check=True)
    # The exact optimized artifact must pass parity before its timing is eligible.
    subprocess.run(
        ["ctest", "--preset", "bench", "--tests-regex", "parity_backtest"], cwd=ROOT, check=True
    )


def run_benchmark(fixture: Path, golden: Path) -> dict[str, object]:
    build_and_gate_parity()
    runner = ROOT / "build" / "bench" / "core" / "backtest_runner"
    timing = json.loads(
        subprocess.run(
            [str(runner), "bench", str(fixture)], cwd=ROOT, check=True, capture_output=True, text=True
        ).stdout
    )
    samples_ms = timing["samples_ms"]
    compiler = subprocess.run(
        ["cc", "--version"], check=True, capture_output=True, text=True
    ).stdout.splitlines()[0]
    return {
        "case": "backtest-small",
        "variant": "c",
        "eligible": True,
        "parity": {"passed": True, "golden_sha256": sha256_file(golden)},
        "source": git_state(),
        "environment": {
            "host_id": platform.node(),
            "cpu": platform.machine(),
            "os": platform.platform(),
        },
        "build": {
            "runtime": "native",
            "compiler": compiler,
            "flags": "cmake bench preset: -O3 -DNDEBUG -ffp-contract=off; no -ffast-math",
        },
        "workload": {
            "fixture_sha256": sha256_file(fixture),
            "concurrency": 1,
            "bars": 1_000,
            "iterations_per_trial": timing["iterations_per_trial"],
            "trial_target_ms": 1_000,
        },
        "samples_ms": samples_ms,
        "trial_totals_ms": timing["trial_totals_ms"],
        "summary": {
            "median_ms": statistics.median(samples_ms),
            "p95_ms": percentile(samples_ms, 0.95),
            "min_ms": min(samples_ms),
            "max_ms": max(samples_ms),
            "stddev_ms": statistics.stdev(samples_ms),
            "sample_count": len(samples_ms),
        },
        "resources": {
            "peak_rss_bytes": timing["peak_rss_bytes"],
            "cpu_time_ms": timing["cpu_time_ms"],
        },
        "errors": {"count": 0, "dropped": 0},
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", type=Path, default=ROOT / "bench/fixtures/backtest-small.json")
    parser.add_argument(
        "--golden", type=Path, default=ROOT / "core/tests/golden/backtest-small.json"
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = run_benchmark(args.fixture, args.golden)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(canonical_bytes(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
