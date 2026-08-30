#!/usr/bin/env python3
"""Generate the deterministic 1,000-bar backtest-small fixture and manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
from datetime import date, timedelta
from pathlib import Path

GENERATOR_VERSION = 1


def canonical_bytes(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def business_days(start: date, count: int) -> list[date]:
    days: list[date] = []
    current = start
    while len(days) < count:
        if current.weekday() < 5:
            days.append(current)
        current += timedelta(days=1)
    return days


def build_fixture() -> dict[str, object]:
    dates = business_days(date(2021, 1, 4), 1_000)
    bars: list[dict[str, object]] = []
    signals: dict[str, str] = {}
    for index, traded_at in enumerate(dates):
        trend = index * 9
        cycle = ((index * 97) % 2_400) - 1_200
        close = 50_000 + trend + cycle
        bars.append({"date": traded_at.isoformat(), "close": float(close)})
        phase = index % 50
        if phase == 1:
            signals[traded_at.isoformat()] = "BUY"
        elif phase == 31:
            signals[traded_at.isoformat()] = "SELL"

    return {
        "fixture_version": "backtest-small-v1",
        "generator_version": GENERATOR_VERSION,
        "timezone": "Asia/Seoul",
        "config": {
            "ticker": "005930",
            "strategy": "A",
            "train_start": "2019-01-02",
            "train_end": "2020-12-30",
            "test_start": dates[0].isoformat(),
            "test_end": dates[-1].isoformat(),
            "initial_capital": 10_000_000,
            "commission_rate_pct": 0.015,
            "tax_rate_pct": 0.18,
            "slippage_bps": 3,
        },
        "bars": bars,
        "signals": signals,
    }


def write_outputs(output_dir: Path) -> tuple[Path, Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    fixture = build_fixture()
    fixture_bytes = canonical_bytes(fixture)
    fixture_path = output_dir / "backtest-small.json"
    fixture_path.write_bytes(fixture_bytes)
    manifest = {
        "format_version": 1,
        "fixtures": {
            "backtest-small": {
                "path": "backtest-small.json",
                "sha256": sha256_bytes(fixture_bytes),
                "bars": 1_000,
                "generator_version": GENERATOR_VERSION,
            }
        },
    }
    manifest_path = output_dir / "manifest.json"
    manifest_path.write_bytes(canonical_bytes(manifest))
    return fixture_path, manifest_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, default=Path("bench/fixtures"))
    args = parser.parse_args()
    write_outputs(args.output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
