#!/usr/bin/env python3
"""Generate backtest-small output from the pinned Python reference."""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import subprocess
import sys
from datetime import date
from pathlib import Path

GENERATOR_VERSION = 1
PINNED_COMMIT = "3642cdc0e4026424ca9b6158125551eee1d42683"


def canonical_bytes(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def assert_pinned_source(source: Path) -> None:
    commit = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=source,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    if commit != PINNED_COMMIT:
        raise ValueError(f"Python source is {commit}, expected {PINNED_COMMIT}")
    tracked = subprocess.run(
        ["git", "status", "--porcelain=v1", "--untracked-files=no"],
        cwd=source,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    if tracked:
        raise ValueError("Python source has tracked modifications")


def load_reference(source: Path):
    source_text = str(source.resolve())
    if source_text not in sys.path:
        sys.path.insert(0, source_text)
    from src.backtest.cost_model import CostModel
    from src.backtest.engine import BacktestEngine
    from src.backtest.models import BacktestConfig
    from src.backtest.signal_source import ReplaySignalSource

    return BacktestConfig, BacktestEngine, CostModel, ReplaySignalSource


def json_value(value: object) -> object:
    if isinstance(value, date):
        return value.isoformat()
    if dataclasses.is_dataclass(value):
        return {field.name: json_value(getattr(value, field.name)) for field in dataclasses.fields(value)}
    if isinstance(value, list):
        return [json_value(item) for item in value]
    if isinstance(value, dict):
        return {str(key): json_value(item) for key, item in value.items()}
    return value


def run_fixture(source: Path, fixture: dict[str, object]) -> object:
    BacktestConfig, BacktestEngine, CostModel, ReplaySignalSource = load_reference(source)
    config_value = dict(fixture["config"])
    for field in ("train_start", "train_end", "test_start", "test_end"):
        config_value[field] = date.fromisoformat(config_value[field])
    config = BacktestConfig(**config_value)
    signals = {
        date.fromisoformat(key): value for key, value in dict(fixture["signals"]).items()
    }
    bars = list(fixture["bars"])
    result = BacktestEngine(config, ReplaySignalSource(signals), CostModel()).run(
        [float(bar["close"]) for bar in bars],
        [date.fromisoformat(str(bar["date"])) for bar in bars],
    )
    return json_value(result)


def build_golden(source: Path, fixture_path: Path) -> dict[str, object]:
    assert_pinned_source(source)
    fixture_bytes = fixture_path.read_bytes()
    fixture = json.loads(fixture_bytes)
    result = run_fixture(source, fixture)
    result_bytes = canonical_bytes(result)
    return {
        "format_version": 1,
        "case": "backtest-small",
        "python_commit": PINNED_COMMIT,
        "generator_version": GENERATOR_VERSION,
        "timezone": "Asia/Seoul",
        "input_sha256": sha256_bytes(fixture_bytes),
        "result_sha256": sha256_bytes(result_bytes),
        "result": result,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path("../alpha-financial-pipeline"))
    parser.add_argument("--fixture", type=Path, default=Path("bench/fixtures/backtest-small.json"))
    parser.add_argument("--output", type=Path, default=Path("core/tests/golden/backtest-small.json"))
    args = parser.parse_args()
    golden = build_golden(args.source, args.fixture)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(canonical_bytes(golden))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
