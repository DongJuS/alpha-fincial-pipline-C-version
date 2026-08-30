#!/usr/bin/env python3
"""Measure the pinned Python blend-batch workload.

The synthetic 3-way batch is mirrored byte-for-byte in core/apps/blend_runner.c;
the emitted buy_checksum lets the C and Python results be cross-checked as the
same workload before any speedup is reported.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
import platform
import statistics
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PINNED_COMMIT = "3642cdc0e4026424ca9b6158125551eee1d42683"

SLOT0 = ["BUY", "SELL", "HOLD"]
SLOT1 = ["HOLD", "BUY", "SELL"]
SLOT2 = ["SELL", "HOLD", "BUY"]


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


def load_blending(source: Path):
    spec = importlib.util.spec_from_file_location(
        "alpha_ref_blending", source / "src/agents/blending.py"
    )
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def run_batch(mod, count: int) -> int:
    buys = 0
    for i in range(count):
        signals = (SLOT0[i % 3], SLOT1[(i // 3) % 3], SLOT2[(i // 9) % 3])
        inputs = [
            mod.BlendInput(
                strategy="S",
                signal=signals[j],
                confidence=((i * (j + 1)) % 100) / 100.0,
                weight=float((i + j) % 5 + 1),
            )
            for j in range(3)
        ]
        if mod.blend_signals(inputs).signal == "BUY":
            buys += 1
    return buys


def run_benchmark(source: Path, count: int) -> dict:
    mod = load_blending(source)
    checksum = run_batch(mod, count)
    samples_ms: list[float] = []
    cpu_start = time.process_time()
    for _ in range(10):
        started = time.perf_counter()
        run_batch(mod, count)
        samples_ms.append((time.perf_counter() - started) * 1000.0)
    cpu_ms = (time.process_time() - cpu_start) * 1000.0 / 10.0
    return {
        "case": "blend-batch",
        "variant": "python",
        "eligible": True,
        "workload": {"count": count, "buy_checksum": checksum, "concurrency": 1},
        "source": {"commit": PINNED_COMMIT},
        "environment": {"host_id": platform.node(), "cpu": platform.machine(), "os": platform.platform()},
        "build": {"runtime": sys.version.splitlines()[0], "flags": "stdlib-only workload"},
        "samples_ms": samples_ms,
        "summary": {
            "median_ms": statistics.median(samples_ms),
            "p95_ms": percentile(samples_ms, 0.95),
            "min_ms": min(samples_ms),
            "max_ms": max(samples_ms),
            "stddev_ms": statistics.stdev(samples_ms),
            "sample_count": len(samples_ms),
        },
        "resources": {"cpu_time_ms": cpu_ms},
        "errors": {"count": 0, "dropped": 0},
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=ROOT.parent / "alpha-financial-pipeline")
    parser.add_argument("--count", type=int, default=200_000)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = run_benchmark(args.source, args.count)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(canonical_bytes(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
