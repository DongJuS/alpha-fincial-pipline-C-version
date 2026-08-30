# Phase 2 — C risk & portfolio

## Goal
Port the decision-safety core (still no live I/O): market-data normalization,
N-way blending, risk gates enforced below signals, and position sizing.

## Files to create
- `core/src/market_data/market_data.c` ← `src/utils/market_data.py`
  (instrument-id maps, `sanitize_change_pct`, `compute_change_pct`).
- `core/src/portfolio/blending.c` ← `src/agents/blending.py` (N-way + 2-way wrapper).
- `core/src/portfolio/sizing.c` — max-position gate ← `portfolio_manager.process_signal`.
- `core/src/risk/gates.c` ← `portfolio_manager` layered exits:
  `_check_rule_based_exits` (L1 stop/take), `_check_portfolio_drawdown` (L2),
  `_is_daily_loss_blocked` / `_hard_stop_scan` (L3 daily circuit breaker — the
  **decision** only; the Redis lockout persistence is wired in P3).
- `core/src/indicators/{ranking.c,screener.c}` ← `ranking_calculator.py`,
  `screener.py` (non-RL filters; screener constants from `constants.h`).

## Contract
`docs/MODULE_SPECS.md §1 market_data, §4 blending, §5 risk, §7 indicators`.
Validated config defaults: max_position 20, daily_loss 3, stop 7, take 5,
portfolio dd 8. Risk gates take validated config + position/price inputs as pure
function parameters; signals and strategy payloads cannot supply those values.

## Tests
- Unit + parity golden files for: blend (conflict, zero-weight equal fallback,
  threshold boundaries at ±0.15), max-position skip boundary, L1 stop (−7) / take
  (+5), L2 drawdown (−8, weakest-two selection), L3 daily breaker (−3).
- market_data: change_pct clamp (`>999.999` → none), rounding to 3 dp,
  instrument-id round-trip incl. `.` in raw code (split on last `.`).
- Benchmark: `blend-batch` and `risk-batch` with identical input buffers and
  outputs consumed/checksummed so compilers/runtimes cannot elide work.

## Dependencies
Phase 1 (domain, constants). No DB/Redis yet — feed positions/prices as inputs.

## Done criteria
- Blend + all risk-gate decisions match Python golden files exactly.
- Eligible Python/C batch results are emitted under `bench/results/`.
- `progress.md` P2 done.

## Gotchas
- Blend clamps confidence to [0,1] and weights to ≥0 before normalizing; zero total
  weight → equal weights.
- L2 sells the **two lowest-pnl** positions, not all.
- L3 has both a "today −3%" trigger and a "persistent lockout" pre-check; the flag
  storage is Redis (P3), but the decision logic lives here.
