# Phase 1 — C numeric core (anchor slice)

## Goal
Port the self-contained numeric core with **no I/O**: domain types, cost model,
and the full backtest engine + metrics. This is the correctness anchor and must
produce the first valid Python/C performance comparison before scope expands.

## Files to create
- `core/include/alpha/domain.h` — enums (`alpha_signal_t`, `alpha_side_t`,
  `alpha_market_t`), `alpha_err_t`, `PredictionSignal` struct.
- `core/include/alpha/constants.h` — verbatim from `src/constants.py`.
- `core/include/alpha/backtest.h` + `core/src/backtest/`:
  - `cost_model.c` ← `src/backtest/cost_model.py`
  - `models` structs (config/trade/snapshot/metrics) ← `src/backtest/models.py`
  - `metrics.c` ← `src/backtest/metrics.py`
  - `signal_source.c` — interface + **ReplaySignalSource only** ← `signal_source.py`
  - `engine.c` ← `src/backtest/engine.py`
  - `cli.c` — thin CLI ← `src/backtest/{cli,__main__}.py`

## Contract
`docs/MODULE_SPECS.md §0 domain, §2 cost_model, §3 backtest`. Reproduce the
open/close/snapshot algorithm and metric formulas **exactly**, including the
integer-share all-cash buy and the rounding (4 dp / 1 dp / 6 dp) in metrics.

## Tests (parity is mandatory here)
- Unit: cost model (BUY/SELL), metrics edge cases (empty snapshots, single bar,
  `total_return <= -1` guard), engine state transitions.
- Parity golden files: `cost_model.calculate`, `engine.run` over a fixture
  price/date series → `BacktestResult`, `compute_backtest_metrics`. Tolerances per
  `docs/BUILD_AND_TEST.md`.
- Benchmark: run `backtest-small`, `backtest-large`, and cost/metrics batches using
  the P0 Python baseline fixtures and `docs/BENCHMARK_PLAN.md` protocol.

## Dependencies
Phase 0 build system. No DB/Redis/HTTP.

## Done criteria
- Backtest of a fixture series matches Python `BacktestResult` (trades, snapshots,
  metrics) within tolerance.
- All unit tests pass under ASan/LSan; lint/format clean.
- Release-mode raw Python/C result JSON is committed; speedup is reported only
  for parity-passing cases.
- `progress.md` P1 done; note any rounding subtlety in `MEMORY.md`.

## Gotchas
- `floor(cash/effective)` integer-share sizing + the rounding guard (`qty -= 1`).
- Sharpe uses **sample** variance (n−1) and sqrt(252); std≤0 → 0.
- MDD is ≤ 0 (peak-to-trough). Annual return guarded when `total_return <= -1`.
