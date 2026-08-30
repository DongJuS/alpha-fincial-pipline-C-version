#!/usr/bin/env python3
"""Measure the pinned Python backtest-small workload after a parity check."""

from __future__ import annotations

import argparse
import json
import math
import os
import platform
import resource
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.generate_python_golden import (  # noqa: E402
    PINNED_COMMIT,
    assert_pinned_source,
    canonical_bytes,
    run_fixture,
    sha256_bytes,
)


def percentile(values: list[float], percentile_value: float) -> float:
    ordered = sorted(values)
    rank = (len(ordered) - 1) * percentile_value
    lower = math.floor(rank)
    upper = math.ceil(rank)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (rank - lower)


def peak_rss_bytes() -> int:
    value = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return int(value if sys.platform == "darwin" else value * 1024)


def run_benchmark(source: Path, fixture_path: Path, golden_path: Path) -> dict[str, object]:
    assert_pinned_source(source)
    source_dirty = bool(
        subprocess.run(
            ["git", "status", "--porcelain=v1", "--untracked-files=all"],
            cwd=source,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    )
    fixture_bytes = fixture_path.read_bytes()
    fixture = json.loads(fixture_bytes)
    golden = json.loads(golden_path.read_bytes())
    parity_result = run_fixture(source, fixture)
    actual_sha = sha256_bytes(canonical_bytes(parity_result))
    parity_passed = actual_sha == golden["result_sha256"]
    if not parity_passed:
        raise ValueError("fresh Python result does not match the committed golden")

    repetitions = 1
    while True:
        started = time.perf_counter()
        for _ in range(repetitions):
            run_fixture(source, fixture)
        if time.perf_counter() - started >= 1.0:
            break
        repetitions *= 2

    samples_ms: list[float] = []
    total_samples_ms: list[float] = []
    cpu_started = time.process_time()
    for _ in range(10):
        started = time.perf_counter()
        for _ in range(repetitions):
            run_fixture(source, fixture)
        elapsed_ms = (time.perf_counter() - started) * 1_000.0
        total_samples_ms.append(elapsed_ms)
        samples_ms.append(elapsed_ms / repetitions)
    cpu_ms = (time.process_time() - cpu_started) * 1_000.0

    compiler = subprocess.run(
        ["cc", "--version"], check=True, capture_output=True, text=True
    ).stdout.splitlines()[0]
    return {
        "case": "backtest-small",
        "variant": "python",
        "eligible": True,
        "parity": {"passed": True, "golden_sha256": sha256_bytes(golden_path.read_bytes())},
        "source": {"commit": PINNED_COMMIT, "dirty": source_dirty, "tracked_dirty": False},
        "environment": {
            "host_id": platform.node(),
            "cpu": platform.machine(),
            "os": platform.platform(),
        },
        "build": {
            "runtime": sys.version.splitlines()[0],
            "compiler": compiler,
            "flags": "PYTHONDONTWRITEBYTECODE=1; stdlib-only workload",
        },
        "workload": {
            "fixture_sha256": sha256_bytes(fixture_bytes),
            "concurrency": 1,
            "bars": 1_000,
            "iterations_per_trial": repetitions,
            "trial_target_ms": 1_000,
        },
        "samples_ms": samples_ms,
        "trial_totals_ms": total_samples_ms,
        "summary": {
            "median_ms": statistics.median(samples_ms),
            "p95_ms": percentile(samples_ms, 0.95),
            "min_ms": min(samples_ms),
            "max_ms": max(samples_ms),
            "stddev_ms": statistics.stdev(samples_ms),
            "sample_count": len(samples_ms),
        },
        "resources": {
            "peak_rss_bytes": peak_rss_bytes(),
            "cpu_time_ms": cpu_ms / (len(samples_ms) * repetitions),
        },
        "errors": {"count": 0, "dropped": 0},
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path("../alpha-financial-pipeline"))
    parser.add_argument("--fixture", type=Path, default=Path("bench/fixtures/backtest-small.json"))
    parser.add_argument("--golden", type=Path, default=Path("core/tests/golden/backtest-small.json"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = run_benchmark(args.source, args.fixture, args.golden)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(canonical_bytes(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
