#!/usr/bin/env python3
"""Benchmark the reviewed Python transcription of the pinned risk arithmetic."""
from __future__ import annotations

import argparse
import json
import math
import platform
import statistics
import sys
import time
from pathlib import Path

PINNED_COMMIT = "3642cdc0e4026424ca9b6158125551eee1d42683"


def batch(count: int) -> dict[str, int]:
    out = {"take_profit": 0, "stop_loss": 0, "daily_blocked": 0, "buy_allowed": 0}
    for i in range(count):
        pnl = float((90 + i % 21) - 100)
        out["take_profit"] += pnl >= 5
        out["stop_loss"] += pnl <= -7
        out["daily_blocked"] += i % 9 - 5 <= -3
        existing = (i % 25) * 100.0
        intended = ((i * 7) % 20 + 1) * 100.0
        total = 10000.0 + (i % 100) * 100.0
        denominator = max(total, 10000.0, 1.0) if i % 2 == 0 else max(total + intended, 1.0)
        out["buy_allowed"] += (existing + intended) / denominator * 100.0 <= 20.0
    return out


def checksum(counts: dict[str, int]) -> int:
    return (counts["take_profit"] * 1000000009 + counts["stop_loss"] * 1000003
            + counts["daily_blocked"] * 1009 + counts["buy_allowed"])


def percentile(values: list[float], q: float) -> float:
    ordered = sorted(values)
    rank = (len(ordered) - 1) * q
    lo, hi = math.floor(rank), math.ceil(rank)
    return ordered[lo] if lo == hi else ordered[lo] + (ordered[hi] - ordered[lo]) * (rank - lo)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--count", type=int, default=100_000)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    counts = batch(args.count)
    samples: list[float] = []
    cpu_start = time.process_time()
    for _ in range(10):
        start = time.perf_counter()
        batch(args.count)
        samples.append((time.perf_counter() - start) * 1000.0)
    result = {
        "case": "risk-batch", "variant": "python", "eligible": True,
        "workload": {"count": args.count, "counts": counts, "checksum": checksum(counts), "concurrency": 1},
        "source": {"commit": PINNED_COMMIT, "kind": "reviewed arithmetic transcription"},
        "environment": {"host_id": platform.node(), "cpu": platform.machine(), "os": platform.platform()},
        "build": {"runtime": sys.version.splitlines()[0], "flags": "stdlib-only workload"},
        "samples_ms": samples,
        "summary": {"median_ms": statistics.median(samples), "p95_ms": percentile(samples, .95),
                    "min_ms": min(samples), "max_ms": max(samples), "stddev_ms": statistics.stdev(samples),
                    "sample_count": len(samples)},
        "resources": {"cpu_time_ms": (time.process_time() - cpu_start) * 100.0},
        "errors": {"count": 0, "dropped": 0},
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
