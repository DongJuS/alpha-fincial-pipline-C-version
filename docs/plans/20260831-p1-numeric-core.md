# P1 — C numeric core (anchor slice)  (Phase 1)

## Goal
Port the self-contained numeric core with **no I/O**: domain date type,
cost model, backtest engine + metrics + ReplaySignalSource. Deliver the first
parity-gated Python/C comparison. Done = C `BacktestResult` matches the pinned
Python golden (`core/tests/golden/backtest-small.json`) under **both** `dev`
(ASan) and `bench` presets; unit tests green; lint/format clean; a `bench`
release benchmark JSON is emitted for the parity-passing artifact.

## Files
New public headers (`core/include/alpha/`):
- `date.h` — epoch-day (proleptic Gregorian) parse/diff for holding-days + ISO.
- `round.h` — Python ties-to-even decimal rounding (`alpha_round_dp`).
- `cost_model.h` — `alpha_cost_model_t`, `alpha_trade_cost_t`, calculate.
- `backtest.h` — config/trade/snapshot/metrics/result structs, `alpha_signal_fn`,
  ReplaySignalSource, `alpha_backtest_run`, `alpha_compute_metrics`.

New sources:
- `core/src/domain/date.c`, `core/src/domain/round.c`
- `core/src/backtest/cost_model.c`, `metrics.c`, `signal_source.c`, `engine.c`
- `core/apps/backtest_runner.c` — reads fixture JSON, runs backtest, emits
  `BacktestResult` JSON and/or timing (parity + benchmark driver).

New tests (`core/tests/`):
- `test_cost_model.c`, `test_metrics.c`, `test_engine.c` (unit, Unity)
- `test_parity_backtest.c` — loads fixture + golden via yyjson, asserts parity.

Tooling:
- `bench/run_c_backtest.py` — build `bench`, parity-check, time the C runner,
  emit `bench/results/<date>/backtest-small-c.json` per `BENCHMARK_PLAN §7`.

Changed: `core/CMakeLists.txt` (new lib sources, tests, runner), `.github/
workflows/ci.yml` (format/tidy new files), `progress.md`, `MEMORY.md`.

## Contract
`docs/MODULE_SPECS.md §0 domain, §2 cost_model, §3 backtest`. Python source of
truth `../alpha-financial-pipeline` @ `3642cdc…` `src/backtest/{cost_model,
models,metrics,engine,signal_source}.py`. Reproduce open/close/snapshot and
metric formulas exactly: integer-share all-cash buy, rounding guard, sample
(n−1) Sharpe × sqrt(252), peak-to-trough MDD ≤ 0, FIFO holding days,
4dp/1dp rounding.

## Tests / tolerances (`docs/BUILD_AND_TEST.md`)
- Integer fields (qty, trade count, snapshot qty): exact.
- Metric floats: compare at Python rounding (4dp / 1dp).
- Snapshot floats (cash, values, daily_return_pct): `|a-b| <= 1e-9*max(1,|b|)`.
- Signals: exact enum. Parity must pass under `dev` **and** `bench`.

## Risks / open questions
- Python `round()` ties-to-even at 4dp: implement via `snprintf("%.*f")`+`strtod`
  under verified `FE_TONEAREST`; validate against golden metrics. Record any
  deviation in `MEMORY.md`.
- Date diff must equal Python `(d2-d1).days`; use days-from-civil algorithm.
