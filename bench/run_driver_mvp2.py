#!/usr/bin/env python3
"""Run parity-gated, order-rotated P3 driver trials against real adapters.

Each adapter command receives ``--fixture``, ``--case``, ``--concurrency`` and
``--trial`` and ``--namespace``. It must print one JSON object with elapsed_ms,
fixture/terminal checksums, every completion token, per-operation latency,
errors, drops, and resource/build metadata.
Timing and service setup stay inside the adapter so setup can be excluded.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import shlex
import shutil
import statistics
import subprocess
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONCURRENCIES = (1, 8, 32)
CASES = ("redis-hot-path", "db-read-write")
EVENT_LOOPS = {"python": "asyncio", "c": "lws", "rust": "tokio"}
MIN_MEASURED_TRIAL_MS = 1000.0


def canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False) + "\n").encode()


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def positive_number(value: object) -> bool:
    """JSON number check which deliberately rejects bool (a subclass of int)."""
    return type(value) in (int, float) and math.isfinite(value) and value > 0


def positive_int(value: object) -> bool:
    return type(value) is int and value > 0


def exact_value(actual: object, expected: object) -> bool:
    """Compare scalar JSON values without allowing True == 1 coercion."""
    return type(actual) is type(expected) and actual == expected


def host_environment() -> dict[str, object]:
    cpu_model = platform.processor().strip()
    cpuinfo = Path("/proc/cpuinfo")
    if not cpu_model and cpuinfo.is_file():
        for line in cpuinfo.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.lower().startswith(("model name", "hardware")) and ":" in line:
                cpu_model = line.split(":", 1)[1].strip()
                break
    logical_cpus = os.cpu_count()
    page_size = os.sysconf("SC_PAGE_SIZE") if hasattr(os, "sysconf") else None
    page_count = os.sysconf("SC_PHYS_PAGES") if hasattr(os, "sysconf") else None
    ram_bytes = page_size * page_count if positive_int(page_size) and positive_int(page_count) else None
    return {
        "host_id": platform.node(),
        "system": platform.system(),
        "kernel_release": platform.release(),
        "kernel_version": platform.version(),
        "machine": platform.machine(),
        "cpu_model": cpu_model or "unknown",
        "logical_cpu_count": logical_cpus,
        "ram_bytes": ram_bytes,
        "python_runtime": platform.python_version(),
        "runner_name": os.getenv("RUNNER_NAME", "local"),
        "runner_os": os.getenv("RUNNER_OS", platform.system()),
        "runner_arch": os.getenv("RUNNER_ARCH", platform.machine()),
        "runner_image": os.getenv("ImageOS", "unreported"),
    }


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


def adapter_attestation(command: str) -> dict[str, str]:
    argv = shlex.split(command)
    command_sha = hashlib.sha256(canonical(argv)).hexdigest()
    candidates = [Path(token) for token in argv[1:] if Path(token).is_file()]
    executable = shutil.which(argv[0])
    if not candidates and executable is not None:
        candidates = [Path(executable)]
    if len(candidates) != 1:
        raise ValueError(f"adapter command must resolve exactly one artifact: {command}")
    artifact = candidates[0].resolve()
    return {"command_sha256": command_sha, "artifact_sha256": sha256_file(artifact),
            "artifact": str(artifact.relative_to(ROOT)) if artifact.is_relative_to(ROOT) else artifact.name}


def verify_post_run_attestation(
    adapters: dict[str, str], source_paths: dict[str, str],
    sources: dict[str, str], attestations: dict[str, dict[str, str]],
) -> None:
    post_sources = {variant: inspect_source(path) for variant, path in source_paths.items()}
    post_attestations = {variant: adapter_attestation(command) for variant, command in adapters.items()}
    if post_sources != sources:
        raise ValueError("source changed during benchmark")
    if post_attestations != attestations:
        raise ValueError("adapter artifact or command changed during benchmark")


def run_adapter(command: str, fixture: Path, case: str, concurrency: int, trial: int, namespace: str) -> dict:
    argv = shlex.split(command) + [
        "--fixture", str(fixture), "--case", case,
        "--concurrency", str(concurrency), "--trial", str(trial),
        "--namespace", namespace,
    ]
    proc = subprocess.run(argv, cwd=ROOT, check=False, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"adapter failed ({proc.returncode}): {' '.join(argv)}\n{proc.stderr}")
    try:
        value = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise ValueError(f"adapter did not emit one JSON object: {proc.stdout!r}") from exc
    required = {
        "elapsed_ms", "fixture_sha256", "completed_tokens", "operation_latency_ns",
        "terminal", "result_sha256",
        "errors", "dropped", "configuration", "resources", "build",
    }
    if not isinstance(value, dict) or not required <= value.keys():
        raise ValueError(f"adapter output missing fields: {sorted(required - set(value))}")
    if not positive_number(value["elapsed_ms"]):
        raise ValueError("elapsed_ms must be finite and positive")
    resources = value["resources"]
    if (
        not isinstance(resources, dict)
        or not positive_int(resources.get("peak_rss_bytes"))
        or not positive_number(resources.get("cpu_time_ms"))
    ):
        raise ValueError("positive peak_rss_bytes and cpu_time_ms are required")
    return value


def execute(
    fixture: Path, adapters: dict[str, str], sources: dict[str, str], trials: int,
    attestations: dict[str, dict[str, str]] | None = None,
    *, minimum_trials: int = 30, minimum_elapsed_ms: float = 0.0,
) -> list[dict]:
    if trials < minimum_trials:
        raise ValueError(f"MVP-2 requires at least {minimum_trials} trials")
    variants = list(adapters)
    if variants != ["python", "c", "rust"]:
        raise ValueError("adapters must be supplied once and in python,c,rust order")
    if set(sources) != set(variants) or any(not value.endswith(":clean") for value in sources.values()):
        raise ValueError("every adapter requires --source VARIANT=COMMIT:clean")
    fixture_sha = sha256_file(fixture)
    fixture_data = json.loads(fixture.read_bytes())
    contract = fixture_data["contract"]
    if hashlib.sha256(canonical(contract["service_config"])).hexdigest() != contract["service_config_sha256"]:
        raise ValueError("fixture service configuration checksum mismatch")
    schema_lock = ROOT / "bench/baseline/python-schema-lock.json"
    if sha256_file(schema_lock) != contract["schema_sha256"]:
        raise ValueError("fixture schema checksum mismatch")
    attestations = attestations or {variant: {"command_sha256": "test", "artifact_sha256": "test", "artifact": "test"} for variant in variants}
    environment = host_environment()
    invariant_by_cell: dict[tuple[str, int, str], bytes] = {}
    records: list[dict] = []
    for case in CASES:
        case_config = fixture_data["cases"][case]
        expected_tokens = [f"{iteration:06d}:{operation['id']}"
                           for iteration in range(case_config["repeat"])
                           for operation in case_config["operations"]]
        case_result_hash: str | None = None
        for concurrency in CONCURRENCIES:
            for trial in range(trials):
                order = variants[trial % len(variants):] + variants[:trial % len(variants)]
                baseline_hash: str | None = None
                for order_index, variant in enumerate(order):
                    namespace = f"mvp2-{case}-c{concurrency}-t{trial}"
                    out = run_adapter(adapters[variant], fixture, case, concurrency, trial, namespace)
                    if float(out["elapsed_ms"]) < minimum_elapsed_ms:
                        raise ValueError(
                            f"{variant}: measured trial {out['elapsed_ms']}ms is below "
                            f"the {minimum_elapsed_ms}ms timer-noise floor"
                        )
                    if out["fixture_sha256"] != fixture_sha:
                        raise ValueError(f"{variant}: fixture checksum mismatch")
                    if out["completed_tokens"] != expected_tokens:
                        raise ValueError(f"{variant}: completion tokens differ or are out of order")
                    latencies = out["operation_latency_ns"]
                    if (not isinstance(latencies, list) or len(latencies) != len(expected_tokens) or
                            any(not positive_int(value) for value in latencies)):
                        raise ValueError(f"{variant}: operation latency samples are invalid")
                    if out["terminal"] != case_config["terminal"]:
                        raise ValueError(f"{variant}: terminal payload differs from committed golden")
                    if out["result_sha256"] != case_config["terminal_sha256"]:
                        raise ValueError(f"{variant}: terminal checksum differs from committed golden")
                    if hashlib.sha256(canonical(out["terminal"])).hexdigest() != out["result_sha256"]:
                        raise ValueError(f"{variant}: terminal payload/checksum mismatch")
                    if type(out["errors"]) is not int or type(out["dropped"]) is not int:
                        raise ValueError(f"{variant}: errors/drops must be exact integers")
                    if out["errors"] != 0 or out["dropped"] != 0:
                        raise ValueError(f"{variant}: errors/drops make the trial ineligible")
                    config = out["configuration"]
                    expected_config = {
                        "concurrency": concurrency,
                        "operation_count": len(case_config["operations"]) * case_config["repeat"],
                        "pipeline_depth": contract["pipeline_depth"],
                        "timeout_ms": case_config["timeout_ms"],
                        "worker_count": 1,
                        "queue_depth": contract["queue_depth"],
                        "connection_count": case_config["connection_count"],
                        "retry_policy": contract["retry_policy"],
                        "saturation_policy": contract["saturation_policy"],
                        "service_config_sha256": contract["service_config_sha256"],
                        "schema_sha256": contract["schema_sha256"],
                    }
                    if not isinstance(config, dict) or any(
                        not exact_value(config.get(key), value) for key, value in expected_config.items()
                    ):
                        raise ValueError(f"{variant}: workload configuration mismatch")
                    if config.get("event_loop_mode") != EVENT_LOOPS[variant]:
                        raise ValueError(f"{variant}: event_loop_mode mismatch")
                    build = out["build"]
                    if (not isinstance(build, dict) or
                            any(type(build.get(key)) is not str or not build[key]
                                for key in ("runtime", "compiler", "flags")) or
                            not isinstance(build.get("dependencies"), dict) or not build["dependencies"] or
                            any(not isinstance(value, str) or not value for value in build["dependencies"].values())):
                        raise ValueError(f"{variant}: incomplete build/dependency metadata")
                    invariant = canonical({"configuration": config, "build": build})
                    cell = (case, concurrency, variant)
                    if cell in invariant_by_cell and invariant_by_cell[cell] != invariant:
                        raise ValueError(f"{variant}: build/configuration drift within {case}/c{concurrency}")
                    invariant_by_cell[cell] = invariant
                    if variant == "python":
                        baseline_hash = out["result_sha256"]
                    records.append({
                        "case": case, "concurrency": concurrency, "trial": trial,
                        "order_index": order_index, "variant": variant, "adapter": out,
                        "attestation": attestations[variant],
                        "environment": environment,
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
            "source": {"commit": commit, "dirty": False, "adapter": group[0]["attestation"]},
            "environment": group[0]["environment"],
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
            "operation_latency_ns_samples": [item["adapter"]["operation_latency_ns"] for item in group],
            "errors": {"count": 0, "dropped": 0},
        })
    return results


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", type=Path, default=ROOT / "bench/fixtures/driver-workloads.json")
    parser.add_argument("--adapter", action="append", default=[], help="python|c|rust=COMMAND")
    parser.add_argument("--source", action="append", default=[], help="VARIANT=GIT_WORKTREE")
    parser.add_argument("--trials", type=int, default=30)
    parser.add_argument(
        "--smoke", action="store_true",
        help="run one parity trial per cell without producing benchmark results",
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    adapters = parse_assignments(args.adapter, "adapter")
    source_paths = parse_assignments(args.source, "source")
    sources = {variant: inspect_source(path) for variant, path in source_paths.items()}
    attestations = {variant: adapter_attestation(command) for variant, command in adapters.items()}
    if args.smoke and args.trials != 1:
        parser.error("--smoke requires --trials 1")
    records = execute(
        args.fixture, adapters, sources, args.trials, attestations,
        minimum_trials=1 if args.smoke else 30,
        minimum_elapsed_ms=0.0 if args.smoke else MIN_MEASURED_TRIAL_MS,
    )
    verify_post_run_attestation(adapters, source_paths, sources, attestations)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    if args.smoke:
        evidence = {
            "mode": "smoke",
            "fixture_sha256": sha256_file(args.fixture),
            "cells": [
                {
                    "case": record["case"],
                    "concurrency": record["concurrency"],
                    "variant": record["variant"],
                    "result_sha256": record["adapter"]["result_sha256"],
                    "completed_count": len(record["adapter"]["completed_tokens"]),
                    "latency_sample_count": len(record["adapter"]["operation_latency_ns"]),
                    "attestation": record["attestation"],
                }
                for record in records
            ],
        }
        (args.output_dir / "smoke.json").write_bytes(canonical(evidence))
        return 0
    results = build_results(records, args.fixture, sources)
    for result in results:
        path = args.output_dir / f"{result['case']}-c{result['workload']['concurrency']}-{result['variant']}.json"
        path.write_bytes(canonical(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
