#!/usr/bin/env python3
"""Run parity-gated, order-rotated P3 driver trials against real adapters.

Each adapter command receives ``--fixture``, ``--case``, ``--concurrency`` and
``--trial``. It must print one JSON object with elapsed_ms, fixture_sha256,
completed_ids, result_sha256, errors, dropped, and optional resource/build data.
Timing and service setup stay inside the adapter so setup can be excluded.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import platform
import shlex
import statistics
import subprocess
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONCURRENCIES = (1, 8, 32)
CASES = ("redis-hot-path", "db-read-write")


def canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False) + "\n").encode()


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def percentile(values: list[float], q: float) -> float:
    ordered = sorted(values)
    rank = (len(ordered) - 1) * q
    lo, hi = math.floor(rank), math.ceil(rank)
    return ordered[lo] if lo == hi else ordered[lo] + (ordered[hi] - ordered[lo]) * (rank - lo)


def summary(values: list[float], unit: str = "ms") -> dict[str, float | int]:
    return {
        f"median_{unit}": statistics.median(values),
        f"p95_{unit}": percentile(values, 0.95),
        f"p99_{unit}": percentile(values, 0.99),
        f"min_{unit}": min(values),
        f"max_{unit}": max(values),
        f"stddev_{unit}": statistics.stdev(values),
        "sample_count": len(values),
    }


def parse_assignments(items: list[str], label: str) -> dict[str, str]:
    parsed: dict[str, str] = {}
    for item in items:
        if "=" not in item:
            raise ValueError(f"{label} must be VARIANT=VALUE: {item}")
        key, value = item.split("=", 1)
        if not key or not value or key in parsed:
            raise ValueError(f"invalid/duplicate {label}: {item}")
        parsed[key] = value
    return parsed


def inspect_source(path_text: str) -> str:
    path = Path(path_text).resolve()
    commit = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"], check=True,
        capture_output=True, text=True,
    ).stdout.strip()
    status = subprocess.run(
        ["git", "-C", str(path), "status", "--porcelain"], check=True,
        capture_output=True, text=True,
    ).stdout
    if status:
        raise ValueError(f"benchmark source is dirty: {path}")
    return f"{commit}:clean"


def run_adapter(command: str, fixture: Path, case: str, concurrency: int, trial: int) -> dict:
    argv = shlex.split(command) + [
        "--fixture", str(fixture), "--case", case,
        "--concurrency", str(concurrency), "--trial", str(trial),
    ]
    proc = subprocess.run(argv, cwd=ROOT, check=False, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"adapter failed ({proc.returncode}): {' '.join(argv)}\n{proc.stderr}")
    try:
        value = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise ValueError(f"adapter did not emit one JSON object: {proc.stdout!r}") from exc
    required = {
        "elapsed_ms", "fixture_sha256", "completed_ids", "result_sha256",
        "errors", "dropped", "configuration", "resources", "build",
    }
    if not isinstance(value, dict) or not required <= value.keys():
        raise ValueError(f"adapter output missing fields: {sorted(required - set(value))}")
    if not isinstance(value["elapsed_ms"], (int, float)) or value["elapsed_ms"] <= 0:
        raise ValueError("elapsed_ms must be finite and positive")
    if not math.isfinite(float(value["elapsed_ms"])):
        raise ValueError("elapsed_ms must be finite and positive")
    resources = value["resources"]
    if (
        not isinstance(resources, dict)
        or not isinstance(resources.get("peak_rss_bytes"), int)
        or resources["peak_rss_bytes"] <= 0
        or not isinstance(resources.get("cpu_time_ms"), (int, float))
        or resources["cpu_time_ms"] <= 0
    ):
        raise ValueError("positive peak_rss_bytes and cpu_time_ms are required")
    return value


def execute(
    fixture: Path, adapters: dict[str, str], sources: dict[str, str], trials: int
) -> list[dict]:
    if trials < 30:
        raise ValueError("MVP-2 requires at least 30 trials")
    variants = list(adapters)
    if variants != ["python", "c", "rust"]:
        raise ValueError("adapters must be supplied once and in python,c,rust order")
    if set(sources) != set(variants) or any(not value.endswith(":clean") for value in sources.values()):
        raise ValueError("every adapter requires --source VARIANT=COMMIT:clean")
    fixture_sha = sha256_file(fixture)
    fixture_data = json.loads(fixture.read_bytes())
    records: list[dict] = []
    for case in CASES:
        operation_ids = [op["id"] for op in fixture_data["cases"][case]["operations"]]
        case_config = fixture_data["cases"][case]
        case_result_hash: str | None = None
        for concurrency in CONCURRENCIES:
            for trial in range(trials):
                order = variants[trial % len(variants):] + variants[:trial % len(variants)]
                baseline_hash: str | None = None
                for order_index, variant in enumerate(order):
                    out = run_adapter(adapters[variant], fixture, case, concurrency, trial)
                    if out["fixture_sha256"] != fixture_sha:
                        raise ValueError(f"{variant}: fixture checksum mismatch")
                    if out["completed_ids"] != operation_ids:
                        raise ValueError(f"{variant}: completed IDs differ or are out of order")
                    if out["errors"] != 0 or out["dropped"] != 0:
                        raise ValueError(f"{variant}: errors/drops make the trial ineligible")
                    config = out["configuration"]
                    expected_config = {
                        "concurrency": concurrency,
                        "operation_count": len(operation_ids) * case_config["repeat"],
                        "pipeline_depth": case_config["pipeline_depth"],
                        "timeout_ms": case_config["timeout_ms"],
                        "worker_count": 1,
                        "queue_depth": case_config["pipeline_depth"],
                        "connection_count": case_config["connection_count"],
                    }
                    if not isinstance(config, dict) or any(
                        config.get(key) != value for key, value in expected_config.items()
                    ):
                        raise ValueError(f"{variant}: workload configuration mismatch")
                    if not isinstance(config.get("event_loop_mode"), str) or not config["event_loop_mode"]:
                        raise ValueError(f"{variant}: event_loop_mode is required")
                    if variant == "python":
                        baseline_hash = out["result_sha256"]
                    records.append({
                        "case": case, "concurrency": concurrency, "trial": trial,
                        "order_index": order_index, "variant": variant, "adapter": out,
                    })
                if baseline_hash is None:
                    raise AssertionError("Python baseline did not run")
                trial_records = records[-len(variants):]
                if any(item["adapter"]["result_sha256"] != baseline_hash for item in trial_records):
                    raise ValueError(f"{case}/c{concurrency}/trial{trial}: parity mismatch")
                if case_result_hash is None:
                    case_result_hash = baseline_hash
                elif baseline_hash != case_result_hash:
                    raise ValueError(f"{case}: nondeterministic result checksum")
    return records


def build_results(records: list[dict], fixture: Path, sources: dict[str, str]) -> list[dict]:
    grouped: dict[tuple[str, int, str], list[dict]] = defaultdict(list)
    for record in records:
        grouped[(record["case"], record["concurrency"], record["variant"])].append(record)
    fixture_sha = sha256_file(fixture)
    results = []
    for (case, concurrency, variant), group in sorted(grouped.items()):
        group.sort(key=lambda item: item["trial"])
        samples = [float(item["adapter"]["elapsed_ms"]) for item in group]
        operation_count = group[0]["adapter"]["configuration"]["operation_count"]
        throughput_samples = [operation_count * 1000.0 / sample for sample in samples]
        commit, _clean = sources[variant].rsplit(":", 1)
        results.append({
            "case": case,
            "variant": variant,
            "eligible": True,
            "parity": {
                "passed": True,
                "golden_sha256": fixture_sha,
                "result_sha256": group[0]["adapter"]["result_sha256"],
            },
            "source": {"commit": commit, "dirty": False},
            "environment": {"host_id": platform.node(), "cpu": platform.machine(), "os": platform.platform()},
            "build": group[0]["adapter"]["build"],
            "workload": {
                "fixture_sha256": fixture_sha, "concurrency": concurrency,
                "trials": len(group), "rotation": "latin-3",
                "configuration": group[0]["adapter"]["configuration"],
            },
            "samples_ms": samples,
            "throughput_ops_s_samples": throughput_samples,
            "trial_order_index": [item["order_index"] for item in group],
            "summary": summary(samples),
            "throughput_summary": summary(throughput_samples, "ops_s"),
            "resources": {
                "peak_rss_bytes": max(item["adapter"].get("resources", {}).get("peak_rss_bytes", 0) for item in group),
                "cpu_time_ms": sum(item["adapter"].get("resources", {}).get("cpu_time_ms", 0) for item in group),
            },
            "resource_samples": [item["adapter"].get("resources", {}) for item in group],
            "errors": {"count": 0, "dropped": 0},
        })
    return results


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", type=Path, default=ROOT / "bench/fixtures/driver-workloads.json")
    parser.add_argument("--adapter", action="append", default=[], help="python|c|rust=COMMAND")
    parser.add_argument("--source", action="append", default=[], help="VARIANT=GIT_WORKTREE")
    parser.add_argument("--trials", type=int, default=30)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    adapters = parse_assignments(args.adapter, "adapter")
    source_paths = parse_assignments(args.source, "source")
    sources = {variant: inspect_source(path) for variant, path in source_paths.items()}
    records = execute(args.fixture, adapters, sources, args.trials)
    results = build_results(records, args.fixture, sources)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for result in results:
        path = args.output_dir / f"{result['case']}-c{result['workload']['concurrency']}-{result['variant']}.json"
        path.write_bytes(canonical(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
