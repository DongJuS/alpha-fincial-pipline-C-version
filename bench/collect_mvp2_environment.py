#!/usr/bin/env python3
"""Collect reproducibility metadata for an authoritative MVP-2 run."""
from __future__ import annotations

import argparse
import json
import os
import platform
import subprocess
from pathlib import Path


def command(argv: list[str]) -> str:
    result = subprocess.run(argv, check=True, capture_output=True, text=True)
    return result.stdout.strip() or result.stderr.strip()


def optional_command(argv: list[str]) -> str | None:
    try:
        return command(argv)
    except (OSError, subprocess.CalledProcessError):
        return None


def source_metadata(label: str, path: Path) -> dict[str, object]:
    status = command(["git", "-C", str(path), "status", "--porcelain"])
    return {
        "label": label,
        "commit": command(["git", "-C", str(path), "rev-parse", "HEAD"]),
        "dirty": bool(status),
    }


def cpu_model() -> str:
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.exists():
        for line in cpuinfo.read_text(encoding="utf-8").splitlines():
            if line.lower().startswith("model name"):
                return line.split(":", 1)[1].strip()
    return platform.processor() or "unknown"


def memory_bytes() -> int:
    meminfo = Path("/proc/meminfo")
    if meminfo.exists():
        for line in meminfo.read_text(encoding="utf-8").splitlines():
            if line.startswith("MemTotal:"):
                return int(line.split()[1]) * 1024
    raise RuntimeError("Linux /proc/meminfo is required")


def container_metadata() -> list[dict[str, str]]:
    identifiers = optional_command(["docker", "compose", "ps", "-q"])
    if not identifiers:
        return []
    containers = []
    for identifier in identifiers.splitlines():
        rendered = command([
            "docker", "inspect", "--format",
            "{{.Name}}|{{.Image}}|{{.Config.Image}}|{{.State.Status}}", identifier,
        ])
        name, image_id, image_name, state = rendered.split("|", 3)
        containers.append({
            "name": name.removeprefix("/"),
            "image_id": image_id,
            "image_name": image_name,
            "state": state,
        })
    return sorted(containers, key=lambda item: item["name"])


def parse_source(value: str) -> tuple[str, Path]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("source must be LABEL=PATH")
    label, raw_path = value.split("=", 1)
    if not label or not raw_path:
        raise argparse.ArgumentTypeError("source must be LABEL=PATH")
    return label, Path(raw_path).resolve()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase", choices=("before", "after"), required=True)
    parser.add_argument("--source", action="append", type=parse_source, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    metadata = {
        "format_version": 1,
        "phase": args.phase,
        "host": {
            "id": platform.node(),
            "os": platform.platform(),
            "kernel": platform.release(),
            "machine": platform.machine(),
            "cpu_model": cpu_model(),
            "cpu_count": os.cpu_count(),
            "memory_bytes": memory_bytes(),
            "load_average": list(os.getloadavg()),
            "affinity": sorted(os.sched_getaffinity(0)),
        },
        "toolchains": {
            "python": platform.python_version(),
            "clang": command(["clang", "--version"]).splitlines()[0],
            "cmake": command(["cmake", "--version"]).splitlines()[0],
            "rustc": command(["rustc", "--version"]),
            "cargo": command(["cargo", "--version"]),
            "docker": command(["docker", "--version"]),
            "docker_compose": command(["docker", "compose", "version"]),
        },
        "dependencies": {
            "pip_freeze": command(["python", "-m", "pip", "freeze"]).splitlines(),
            "pkg_config": {
                package: command(["pkg-config", "--modversion", package])
                for package in ("libpq", "hiredis", "libcurl", "libwebsockets")
            },
        },
        "sources": [source_metadata(label, path) for label, path in args.source],
        "containers": container_metadata(),
    }
    if any(source["dirty"] for source in metadata["sources"]):
        raise RuntimeError("benchmark source is dirty")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(metadata, sort_keys=True, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

