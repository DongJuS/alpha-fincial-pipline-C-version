#!/usr/bin/env python3
"""Gate, run, and record the C risk-batch benchmark."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import platform
import statistics
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def percentile(values: list[float], q: float) -> float:
    values = sorted(values); rank = (len(values) - 1) * q
    lo, hi = math.floor(rank), math.ceil(rank)
    return values[lo] if lo == hi else values[lo] + (values[hi] - values[lo]) * (rank - lo)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--count", type=int, default=100_000)
    parser.add_argument("--python-result", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    subprocess.run(["cmake", "--preset", "bench"], cwd=ROOT, check=True)
    subprocess.run(["cmake", "--build", "--preset", "bench", "--parallel"], cwd=ROOT, check=True)
    subprocess.run(["ctest", "--preset", "bench", "--tests-regex", "risk"], cwd=ROOT, check=True)
    runner = ROOT / "build/bench/core/risk_runner"
    raw = json.loads(subprocess.run([str(runner), "bench", str(args.count)], cwd=ROOT,
                                    check=True, capture_output=True, text=True).stdout)
    py = json.loads(args.python_result.read_text())
    if py["workload"]["count"] != raw["count"] or py["workload"]["counts"] != raw["counts"] \
            or py["workload"]["checksum"] != raw["checksum"]:
        raise ValueError("C and Python risk workloads differ")
    samples = raw["samples_ms"]
    golden = ROOT / "core/tests/golden/risk-cases.json"
    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, check=True,
                            capture_output=True, text=True).stdout.strip()
    result = {
        "case": "risk-batch", "variant": "c", "eligible": True,
        "parity": {"passed": True, "golden_sha256": sha256(golden)},
        "workload": {"count": raw["count"], "counts": raw["counts"], "checksum": raw["checksum"],
                     "checksum_matches_python": True, "concurrency": 1},
        "source": {"commit": commit},
        "environment": {"host_id": platform.node(), "cpu": platform.machine(), "os": platform.platform()},
        "build": {"runtime": "native", "flags": "cmake bench preset: -O3 -DNDEBUG -ffp-contract=off"},
        "samples_ms": samples,
        "summary": {"median_ms": statistics.median(samples), "p95_ms": percentile(samples, .95),
                    "min_ms": min(samples), "max_ms": max(samples), "stddev_ms": statistics.stdev(samples),
                    "sample_count": len(samples)},
        "resources": {"peak_rss_bytes": raw["peak_rss_bytes"], "cpu_time_ms": raw["cpu_time_ms"]},
        "errors": {"count": 0, "dropped": 0},
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
