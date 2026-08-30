# Phase 3 — C nonblocking drivers (libpq / hiredis / libcurl)

## Goal
Give the core the real I/O paths selected for comparison. All languages use the
same PostgreSQL/Redis/service versions and equivalent pool, timeout, payload, and
concurrency settings. Infrastructure and Infisical are not reimplemented or timed.
The Python collector supplies raw/persisted market input; no EOD/live collector is
ported here.

## Files to create
- `core/platform/db/` (nonblocking libpq) ← `src/utils/db_client.py`,
  `src/db/queries.py`: bounded pool, prepared queries, readiness integration with
  libwebsockets, and typed queries for
  `market_data`, `ohlcv_daily`, `portfolio_positions`, `trade_history`,
  `broker_orders`, `aggregate_risk_snapshots`, `backtest_runs/daily`.
  Plus bounded pipeline mode for batch paths and `aggregate_risk.c` SQL.
- `core/platform/cache/` (hiredis async) ← `src/utils/redis_client.py`:
  `latest_ticks:{ticker}` cache, circuit-breaker lockout with absolute `expires_at`
  and TTL to the next KRX session at 09:00 KST, heartbeats, bounded pipelines;
  integrate readiness into LWS loop. Read `krx:holidays:{year}`, populated by the
  retained Python `scripts/fetch_krx_holidays.py`; missing/stale calendar fails closed.
- `core/platform/http/` (libcurl multi + yyjson):
  - `llm.c` — optional POST JSON client seam only.
  - `telegram.c` — Bot API POST ← `src/agents/notifier.py` send path.
- No C datalake; Rust implements it in P4.

## Contract
Match query semantics and Redis key patterns from source (`docs/db/redis_keys.md`).
Risk lockout flag must persist across process restarts and gate BUYs on next run.
Secrets from env only; no plaintext in logs.

## Tests (integration — Docker Postgres+Redis)
- DB round-trip: insert/select `portfolio_positions`; `aggregate_risk` totals match
  a Python golden for the same rows.
- Redis: set lockout Friday/holiday-eve → gate stays closed through non-trading
  days → expiry at the next KRX session 09:00 KST re-enables BUY. Missing/stale
  calendar must not release the lockout early.
- Input boundary: Python-collected fixture/raw rows normalize and persist with the
  same output as the Python normalization path.
- Benchmark: `db-read-write` and `redis-hot-path` at concurrency 1/8/32; add HTTP
  only for a deterministic local replay. Capture latency percentiles, CPU, RSS,
  errors, drops, and connection settings.

## Dependencies
Phases 1–2; Docker Compose from Phase 0.

## Done criteria
- Normalized Python-collected input writes/reads through Postgres; risk lockout
  round-trips through Redis.
- Integration job green in CI.
- Eligible Python/C driver result JSON is committed.
- `progress.md` P3 done; record pool/pipeline depths and LWS integration decisions
  in `MEMORY.md`.

## Gotchas
- libpq pipeline errors abort work until a sync point; maintain a bounded FIFO that
  matches every result and retries only from a confirmed transaction boundary.
- hiredis and libcurl must not start hidden loops or block the LWS service thread.
- Bounded workers are a fallback only for unavoidable blocking/CPU calls. Record
  their count/queue depth in benchmark metadata and never run risk/order logic there.
