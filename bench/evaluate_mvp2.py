#!/usr/bin/env python3
"""Evaluate the MVP-2 stop gate from paired, attested 30-trial results."""
from __future__ import annotations

import argparse
import json
import math
import random
import statistics
from pathlib import Path

CASES = ("redis-hot-path", "db-read-write")
CONCURRENCIES = (1, 8, 32)
VARIANTS = ("python", "c", "rust")
TRIALS = 30


def percentile(values: list[float], q: float) -> float:
    ordered = sorted(values)
    rank = (len(ordered) - 1) * q
    lo, hi = math.floor(rank), math.ceil(rank)
    return ordered[lo] if lo == hi else ordered[lo] + (ordered[hi] - ordered[lo]) * (rank - lo)


def _positive(value: object) -> bool:
    return (not isinstance(value, bool) and isinstance(value, (int, float)) and
            math.isfinite(value) and value > 0)


def bootstrap_speedup(python: list[float], candidate: list[float], seed: int = 20260831) -> dict:
    if len(python) != TRIALS or len(candidate) != TRIALS:
        raise ValueError("paired Python/candidate samples require exactly 30 trials")
    if any(not _positive(value) for value in python + candidate):
        raise ValueError("samples must be finite and positive")
    rng = random.Random(seed)
    estimates = []
    for _ in range(10_000):
        indexes = [rng.randrange(TRIALS) for _ in range(TRIALS)]
        estimates.append(statistics.median([python[index] for index in indexes]) /
                         statistics.median([candidate[index] for index in indexes]))
    point = statistics.median(python) / statistics.median(candidate)
    return {"speedup": point, "ci95_low": percentile(estimates, 0.025),
            "ci95_high": percentile(estimates, 0.975)}


def _load(path: Path) -> dict:
    def reject_constant(value: str) -> None:
        raise ValueError(f"non-finite JSON constant: {value}")

    value = json.loads(path.read_bytes(), parse_constant=reject_constant)
    if not isinstance(value, dict):
        raise ValueError(f"result must be an object: {path}")
    return value


def _validate_attestation(source: object) -> None:
    if not isinstance(source, dict) or source.get("dirty") is not False:
        raise ValueError("dirty or malformed source fails the stop gate")
    if not isinstance(source.get("commit"), str) or not source["commit"]:
        raise ValueError("source commit is required")
    adapter = source.get("adapter")
    if not isinstance(adapter, dict) or not isinstance(adapter.get("artifact"), str) or not adapter["artifact"]:
        raise ValueError("adapter attestation is required")
    for key in ("command_sha256", "artifact_sha256"):
        digest = adapter.get(key)
        if (not isinstance(digest, str) or len(digest) != 64 or
                any(char not in "0123456789abcdef" for char in digest)):
            raise ValueError("adapter attestation digest is invalid")


def _validate_doc(doc: dict) -> dict:
    workload = doc.get("workload")
    if not isinstance(workload, dict) or workload.get("trials") != TRIALS or workload.get("rotation") != "latin-3":
        raise ValueError("workload requires exactly 30 Latin-3 trials")
    samples = doc.get("samples_ms")
    if not isinstance(samples, list) or len(samples) != TRIALS or any(not _positive(value) for value in samples):
        raise ValueError("samples_ms requires exactly 30 finite positive samples")
    variant = doc.get("variant")
    positions = {"python": [0, 2, 1] * 10, "c": [1, 0, 2] * 10, "rust": [2, 1, 0] * 10}
    if doc.get("trial_order_index") != positions.get(variant):
        raise ValueError("trial order is not the required Latin-3 rotation")
    if doc.get("eligible") is not True or doc.get("parity", {}).get("passed") is not True:
        raise ValueError("ineligible or parity-failing input")
    if doc.get("errors") != {"count": 0, "dropped": 0}:
        raise ValueError("errors/drops fail the stop gate")
    _validate_attestation(doc.get("source"))
    build = doc.get("build")
    if (not isinstance(build, dict) or
            any(not isinstance(build.get(key), str) or not build[key]
                for key in ("runtime", "compiler", "flags")) or
            not isinstance(build.get("dependencies"), dict) or not build["dependencies"]):
        raise ValueError("complete build attestation is required")

    config = workload.get("configuration")
    operation_count = config.get("operation_count") if isinstance(config, dict) else None
    if isinstance(operation_count, bool) or not isinstance(operation_count, int) or operation_count <= 0:
        raise ValueError("positive operation_count is required")
    latency_trials = doc.get("operation_latency_ns_samples")
    if not isinstance(latency_trials, list) or len(latency_trials) != TRIALS:
        raise ValueError("operation latency requires exactly 30 trials")
    p95, p99 = [], []
    for trial in latency_trials:
        if (not isinstance(trial, list) or len(trial) != operation_count or
                any(isinstance(value, bool) or not isinstance(value, int) or value <= 0 for value in trial)):
            raise ValueError("operation latency trial is invalid")
        p95.append(percentile(trial, 0.95))
        p99.append(percentile(trial, 0.99))

    resource_samples = doc.get("resource_samples")
    if not isinstance(resource_samples, list) or len(resource_samples) != TRIALS:
        raise ValueError("resources require exactly 30 trial samples")
    rss_samples, cpu_samples = [], []
    for sample in resource_samples:
        if (not isinstance(sample, dict) or not _positive(sample.get("peak_rss_bytes")) or
                not _positive(sample.get("cpu_time_ms"))):
            raise ValueError("resource trial sample is invalid")
        rss_samples.append(sample["peak_rss_bytes"])
        cpu_samples.append(sample["cpu_time_ms"])
    if doc.get("resources") != {"peak_rss_bytes": max(rss_samples), "cpu_time_ms": sum(cpu_samples)}:
        raise ValueError("resource aggregate does not match trial samples")
    return {"p95_samples_ns": p95, "p99_samples_ns": p99,
            "p95_summary_ns": {"median": statistics.median(p95), "max": max(p95)},
            "p99_summary_ns": {"median": statistics.median(p99), "max": max(p99)}}


def evaluate(paths: list[Path], candidate: str, max_rss_ratio: float = 1.0,
             max_cpu_ratio: float = 1.0) -> dict:
    if candidate not in ("c", "rust"):
        raise ValueError("candidate must be c or rust")
    if not _positive(max_rss_ratio) or not _positive(max_cpu_ratio):
        raise ValueError("resource-ratio limits must be finite and positive")
    docs = [_load(path) for path in paths]
    keys = [(doc.get("case"), doc.get("workload", {}).get("concurrency"), doc.get("variant")) for doc in docs]
    if len(keys) != len(set(keys)):
        raise ValueError("duplicate result cell")
    supplied = {key[2] for key in keys}
    if supplied not in ({"python", candidate}, set(VARIANTS)):
        raise ValueError("results must contain the candidate pair (12 files) or full matrix (18 files)")
    expected = {(case, concurrency, variant) for case in CASES for concurrency in CONCURRENCIES
                for variant in supplied}
    if set(keys) != expected:
        raise ValueError("missing or unexpected result cell")

    latency = {key: _validate_doc(doc) for key, doc in zip(keys, docs)}
    indexed = dict(zip(keys, docs))
    environments = [doc.get("environment") for doc in docs]
    if (not environments or not isinstance(environments[0], dict) or not environments[0] or
            any(environments[0] != item for item in environments[1:])):
        raise ValueError("all results must use the same environment")
    for variant in supplied:
        variant_docs = [doc for doc in docs if doc["variant"] == variant]
        for field in ("build", "source"):
            if any(doc.get(field) != variant_docs[0].get(field) for doc in variant_docs[1:]):
                raise ValueError(f"{variant} {field} attestation drift")
    fixtures = {doc["workload"].get("fixture_sha256") for doc in docs}
    goldens = {doc["parity"].get("golden_sha256") for doc in docs}
    if len(fixtures) != 1 or fixtures != goldens:
        raise ValueError("fixture/golden attestation mismatch")

    decisions = []
    for case in CASES:
        result_hashes = {indexed[(case, concurrency, variant)]["parity"].get("result_sha256")
                         for concurrency in CONCURRENCIES for variant in supplied}
        if len(result_hashes) != 1 or not next(iter(result_hashes), None):
            raise ValueError(f"{case}: result golden mismatch")
        for concurrency in CONCURRENCIES:
            py = indexed[(case, concurrency, "python")]
            other = indexed[(case, concurrency, candidate)]
            py_config = dict(py["workload"].get("configuration", {}))
            candidate_config = dict(other["workload"].get("configuration", {}))
            py_config.pop("event_loop_mode", None)
            candidate_config.pop("event_loop_mode", None)
            if py_config != candidate_config:
                raise ValueError("candidate workload configuration mismatch")
            ci = bootstrap_speedup(py["samples_ms"], other["samples_ms"])
            rss_ratio = other["resources"]["peak_rss_bytes"] / py["resources"]["peak_rss_bytes"]
            cpu_ratio = other["resources"]["cpu_time_ms"] / py["resources"]["cpu_time_ms"]
            decisions.append({"case": case, "concurrency": concurrency, **ci,
                              "rss_ratio": rss_ratio, "cpu_ratio": cpu_ratio,
                              "operation_latency": {
                                  "python": latency[(case, concurrency, "python")],
                                  "candidate": latency[(case, concurrency, candidate)]},
                              "go": ci["ci95_low"] > 1.0 and rss_ratio <= max_rss_ratio and
                                    cpu_ratio <= max_cpu_ratio})
    return {"candidate": candidate, "primary_metric": "paired batch-throughput speedup",
            "decision": "GO" if all(d["go"] for d in decisions) else "NO-GO",
            "comparisons": decisions}


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
