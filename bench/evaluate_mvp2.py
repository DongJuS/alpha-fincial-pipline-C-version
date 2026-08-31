#!/usr/bin/env python3
"""Evaluate the MVP-2 stop gate from paired 30-trial result files."""
from __future__ import annotations

import argparse
import json
import math
import random
import statistics
from pathlib import Path


def percentile(values: list[float], q: float) -> float:
    ordered = sorted(values)
    rank = (len(ordered) - 1) * q
    lo, hi = math.floor(rank), math.ceil(rank)
    return ordered[lo] if lo == hi else ordered[lo] + (ordered[hi] - ordered[lo]) * (rank - lo)


def bootstrap_speedup(python: list[float], candidate: list[float], seed: int = 20260831) -> dict:
    if len(python) != len(candidate) or len(python) < 30:
        raise ValueError("paired Python/candidate samples require at least 30 trials")
    if any(value <= 0 or not math.isfinite(value) for value in python + candidate):
        raise ValueError("samples must be finite and positive")
    rng = random.Random(seed)
    estimates = []
    for _ in range(10_000):
        indexes = [rng.randrange(len(python)) for _ in python]
        estimates.append(
            statistics.median([python[index] for index in indexes])
            / statistics.median([candidate[index] for index in indexes])
        )
    point = statistics.median(python) / statistics.median(candidate)
    return {"speedup": point, "ci95_low": percentile(estimates, 0.025), "ci95_high": percentile(estimates, 0.975)}


def evaluate(paths: list[Path], candidate: str, max_rss_ratio: float = 1.0, max_cpu_ratio: float = 1.0) -> dict:
    if max_rss_ratio <= 0 or max_cpu_ratio <= 0:
        raise ValueError("resource-ratio limits must be positive")
    docs = [json.loads(path.read_bytes()) for path in paths]
    indexed = {(doc["case"], doc["workload"]["concurrency"], doc["variant"]): doc for doc in docs}
    decisions = []
    for case in ("redis-hot-path", "db-read-write"):
        for concurrency in (1, 8, 32):
            py = indexed.get((case, concurrency, "python"))
            other = indexed.get((case, concurrency, candidate))
            if py is None or other is None:
                raise ValueError(f"missing result for {case}/c{concurrency}")
            for doc in (py, other):
                if not doc["eligible"] or not doc["parity"]["passed"]:
                    raise ValueError("ineligible or parity-failing input")
                if doc["errors"] != {"count": 0, "dropped": 0}:
                    raise ValueError("errors/drops fail the stop gate")
                if doc["source"].get("dirty", True):
                    raise ValueError("dirty source fails the stop gate")
            if py["workload"]["fixture_sha256"] != other["workload"]["fixture_sha256"]:
                raise ValueError("fixture mismatch")
            ci = bootstrap_speedup(py["samples_ms"], other["samples_ms"])
            py_rss, candidate_rss = py["resources"]["peak_rss_bytes"], other["resources"]["peak_rss_bytes"]
            py_cpu, candidate_cpu = py["resources"]["cpu_time_ms"], other["resources"]["cpu_time_ms"]
            if min(py_rss, candidate_rss, py_cpu, candidate_cpu) <= 0:
                raise ValueError("positive CPU/RSS measurements are required")
            rss_ratio = candidate_rss / py_rss
            cpu_ratio = candidate_cpu / py_cpu
            decisions.append({
                "case": case, "concurrency": concurrency, **ci,
                "rss_ratio": rss_ratio, "cpu_ratio": cpu_ratio,
                "go": ci["ci95_low"] > 1.0 and rss_ratio <= max_rss_ratio and cpu_ratio <= max_cpu_ratio,
            })
    return {
        "candidate": candidate,
        "primary_metric": "paired batch-throughput speedup",
        "decision": "GO" if all(d["go"] for d in decisions) else "NO-GO",
        "comparisons": decisions,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--result", type=Path, action="append", required=True)
    parser.add_argument("--candidate", choices=("c", "rust"), required=True)
    parser.add_argument("--max-rss-ratio", type=float, default=1.0)
    parser.add_argument("--max-cpu-ratio", type=float, default=1.0)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = evaluate(args.result, args.candidate, args.max_rss_ratio, args.max_cpu_ratio)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
