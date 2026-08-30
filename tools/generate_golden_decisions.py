#!/usr/bin/env python3
"""Generate blend + market_data goldens from the pinned Python reference.

Reads the committed decision fixtures under bench/fixtures/, runs the pinned
Python functions, and writes goldens with the pinned SHA, generator version, and
input checksum — the same discipline as tools/generate_python_golden.py.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

GENERATOR_VERSION = 1
PINNED_COMMIT = "3642cdc0e4026424ca9b6158125551eee1d42683"

# Import the shared pinned-source assertion.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.generate_python_golden import assert_pinned_source, canonical_bytes  # noqa: E402


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _load_file(path: Path, name: str):
    """Load a single leaf module by path, bypassing the src package __init__
    (which imports pydantic-backed config/logging not needed here)."""
    import importlib.util

    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module  # dataclass processing needs the module registered
    spec.loader.exec_module(module)
    return module


def load_modules(source: Path):
    blending = _load_file(source / "src/agents/blending.py", "alpha_ref_blending")
    market = _load_file(source / "src/utils/market_data.py", "alpha_ref_market_data")
    return {
        "BlendInput": blending.BlendInput,
        "blend_signals": blending.blend_signals,
        "blend_strategy_signals": blending.blend_strategy_signals,
        "to_instrument_id": market.to_instrument_id,
        "from_instrument_id": market.from_instrument_id,
        "sanitize_change_pct": market.sanitize_change_pct,
        "compute_change_pct": market.compute_change_pct,
    }


def optional(value) -> dict:
    return {"has_value": value is not None, "value": value if value is not None else 0.0}


def build_blend(mod, fixture: dict) -> list:
    cases = []
    for case in fixture["nway"]:
        inputs = [
            mod["BlendInput"](
                strategy=item["strategy"],
                signal=item["signal"],
                confidence=item["confidence"],
                weight=item["weight"],
            )
            for item in case["inputs"]
        ]
        result = mod["blend_signals"](inputs)
        cases.append(
            {
                "name": case["name"],
                "kind": "nway",
                "signal": result.signal,
                "confidence": result.confidence,
                "weighted_score": result.weighted_score,
                "conflict": result.conflict,
            }
        )
    for case in fixture["ab"]:
        result = mod["blend_strategy_signals"](
            case["a_signal"],
            case["a_confidence"],
            case["b_signal"],
            case["b_confidence"],
            case["blend_ratio"],
        )
        cases.append(
            {
                "name": case["name"],
                "kind": "ab",
                "signal": result.combined_signal,
                "confidence": result.combined_confidence,
                "conflict": result.conflict,
            }
        )
    return cases


def build_market(mod, fixture: dict) -> dict:
    to_iid = [
        {"name": c["name"], "instrument_id": mod["to_instrument_id"](c["ticker"], c["market"])}
        for c in fixture["to_instrument_id"]
    ]
    from_iid = []
    for c in fixture["from_instrument_id"]:
        raw, market = mod["from_instrument_id"](c["instrument_id"])
        from_iid.append({"name": c["name"], "raw": raw, "market": market})
    sanitize = [
        {"name": c["name"], **optional(mod["sanitize_change_pct"](c["value"]))}
        for c in fixture["sanitize_change_pct"]
    ]
    change = [
        {"name": c["name"], **optional(mod["compute_change_pct"](c["current"], c["previous"]))}
        for c in fixture["compute_change_pct"]
    ]
    return {
        "to_instrument_id": to_iid,
        "from_instrument_id": from_iid,
        "sanitize_change_pct": sanitize,
        "compute_change_pct": change,
    }


def build_golden(source: Path, fixture_path: Path, case: str) -> dict:
    assert_pinned_source(source)
    mod = load_modules(source)
    fixture_bytes = fixture_path.read_bytes()
    fixture = json.loads(fixture_bytes)
    result = build_blend(mod, fixture) if case == "blend-cases" else build_market(mod, fixture)
    return {
        "format_version": 1,
        "case": case,
        "python_commit": PINNED_COMMIT,
        "generator_version": GENERATOR_VERSION,
        "input_sha256": sha256_bytes(fixture_bytes),
        "result": result,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path("../alpha-financial-pipeline"))
    parser.add_argument("--fixtures", type=Path, default=Path("bench/fixtures"))
    parser.add_argument("--output", type=Path, default=Path("core/tests/golden"))
    args = parser.parse_args()
    for case in ("blend-cases", "market-data-cases"):
        golden = build_golden(args.source, args.fixtures / f"{case}.json", case)
        out = args.output / f"{case}.json"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(canonical_bytes(golden))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
