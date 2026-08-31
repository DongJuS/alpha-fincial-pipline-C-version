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


# ── risk gates ──────────────────────────────────────────────────────────────
# The live portfolio_manager methods are async and DB-coupled, so they cannot be
# invoked directly. The functions below transcribe the pure decision arithmetic
# line-for-line from src/agents/portfolio_manager.py @ the pinned commit
# (_check_rule_based_exits, _check_portfolio_drawdown, _is_daily_loss_blocked,
# process_signal max-position gate). This transcription dependency is recorded in
# MEMORY.md.


def risk_l1(pos: dict, cfg: dict) -> str:
    if pos["quantity"] <= 0:
        return "NONE"
    avg = pos["avg_fill_price"]
    cur = pos["current_price"]
    if avg <= 0 or cur <= 0:
        return "NONE"
    pnl = (cur - avg) / avg * 100.0
    if pnl >= cfg["take_profit_pct"]:
        return "TAKE_PROFIT"
    if pnl <= -cfg["individual_stop_loss_pct"]:
        return "STOP_LOSS"
    return "NONE"


def risk_l2(case: dict, cfg: dict) -> dict:
    ce = case["current_equity"]
    be = case["baseline_equity"]
    if ce <= 0 or be <= 0:
        return {"dd_pct": None, "selected": []}
    dd = (ce - be) / be * 100.0
    if dd > -cfg["portfolio_drawdown_limit_pct"]:
        return {"dd_pct": dd, "selected": []}
    candidates: list[tuple[float, int]] = []
    for i, pos in enumerate(case["positions"]):
        if pos["quantity"] <= 0:
            continue
        avg = pos["avg_fill_price"]
        cur = pos["current_price"]
        if avg <= 0 or cur <= 0:
            continue
        candidates.append(((cur - avg) / avg * 100.0, i))
    candidates.sort(key=lambda item: item[0])  # stable, ascending pnl
    return {"dd_pct": dd, "selected": [i for _, i in candidates[:2]]}


def risk_max_position(case: dict, cfg: dict) -> dict:
    if case["is_paper"]:
        denom = max(case["total_value"], case["paper_seed_capital"], 1)
    else:
        denom = max(case["total_value"] + case["intended_buy_value"], 1)
    next_value = case["existing_position_value"] + case["intended_buy_value"]
    next_weight_pct = next_value / denom * 100.0
    return {
        "allowed": not (next_weight_pct > cfg["max_position_pct"]),
        "next_weight_pct": next_weight_pct,
    }


def build_risk(fixture: dict) -> dict:
    cfg = fixture["config"]
    return {
        "l1": [{"name": c["name"], "kind": risk_l1(c, cfg)} for c in fixture["l1"]],
        "l2": [{"name": c["name"], **risk_l2(c, cfg)} for c in fixture["l2"]],
        "l3": [
            {"name": c["name"], "blocked": c["daily_realized_pnl_pct"] <= -cfg["daily_loss_limit_pct"]}
            for c in fixture["l3"]
        ],
        "max_position": [
            {"name": c["name"], **risk_max_position(c, cfg)} for c in fixture["max_position"]
        ],
    }


# ── screener ────────────────────────────────────────────────────────────────
# screener.py imports DB/logging at module load, so (like the risk gates) the
# reference below transcribes the pure _score_ticker + selection arithmetic
# line-for-line from src/agents/screener.py @ the pinned commit.


def screener_score(bars: list[dict], vol_th: float, pct_th: float) -> dict:
    if len(bars) < 5:  # _MIN_DATA_DAYS
        return {"passes": False, "score": 0.0}
    today = bars[0]
    past = bars[1:]
    avg = sum(b["volume"] for b in past) / len(past) if past else 0.0
    ratio = today["volume"] / avg if avg > 0 else 0.0
    change = abs(today.get("change_pct") or 0.0)
    return {"passes": ratio >= vol_th or change >= pct_th, "score": ratio / vol_th + change / pct_th}


def screener_select(candidates: list[dict], cap: int) -> list[int]:
    passed = [(i, c["score"]) for i, c in enumerate(candidates) if c["passes"]]
    passed.sort(key=lambda item: item[1], reverse=True)  # stable descending
    return [i for i, _ in passed[:cap]]


def build_screener(fixture: dict) -> dict:
    vol_th = fixture["thresholds"]["volume_surge_ratio"]
    pct_th = fixture["thresholds"]["change_pct_threshold"]
    return {
        "score": [
            {"name": c["name"], **screener_score(c["bars"], vol_th, pct_th)}
            for c in fixture["score"]
        ],
        "select": [
            {"name": c["name"], "selected": screener_select(c["candidates"], c["cap"])}
            for c in fixture["select"]
        ],
    }


def build_golden(source: Path, fixture_path: Path, case: str) -> dict:
    assert_pinned_source(source)
    fixture_bytes = fixture_path.read_bytes()
    fixture = json.loads(fixture_bytes)
    if case == "blend-cases":
        result = build_blend(load_modules(source), fixture)
    elif case == "market-data-cases":
        result = build_market(load_modules(source), fixture)
    elif case == "screener-cases":
        result = build_screener(fixture)
    else:
        result = build_risk(fixture)
    golden = {
        "format_version": 1,
        "case": case,
        "python_commit": PINNED_COMMIT,
        "generator_version": GENERATOR_VERSION,
        "input_sha256": sha256_bytes(fixture_bytes),
        "result": result,
    }
    if case == "risk-cases":
        golden["config_sha256"] = sha256_bytes(canonical_bytes(fixture["config"]))
        golden["transcribed_from_python"] = True
    return golden


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path("../alpha-financial-pipeline"))
    parser.add_argument("--fixtures", type=Path, default=Path("bench/fixtures"))
    parser.add_argument("--output", type=Path, default=Path("core/tests/golden"))
    args = parser.parse_args()
    for case in ("blend-cases", "market-data-cases", "risk-cases", "screener-cases"):
        golden = build_golden(args.source, args.fixtures / f"{case}.json", case)
        out = args.output / f"{case}.json"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(canonical_bytes(golden))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
