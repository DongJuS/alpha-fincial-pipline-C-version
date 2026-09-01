# progress.md — Current state

> Update this after every task. Keep under ~200 lines. Record *why*, not just *what*.

## Status: Phase 3 in progress — MVP-2 controlled matrix pending

P0 contracts and P1 numeric core are closed. P2 ports the full deterministic
decision layer — market_data normalization, N-way/2-way blending, the risk gates
(L1 stop/take, L2 drawdown weakest-two, L3 daily breaker), max-position sizing,
and the screener filter — each golden-gated against the pinned Python source
under both `dev` (ASan) and `bench` (`-O3`) presets, with blend and risk batch
benchmarks. P3 drivers and all three real adapters pass hosted parity smoke; the
controlled 30-trial matrix and MVP-2 decision remain unfinished.

## Reference baseline (must be completed in P0)

- Python commit SHA: `3642cdc0e4026424ca9b6158125551eee1d42683`
- Python worktree: tracked files clean; untracked `HANDOFF-2026-08-03.md` present
- Global dependency manifest: `requirements.txt`, SHA-256 `bb923cd220358a0d3fd6d2a5889a60a30a49e476f4bd499cd52de21b13fe0837`; no exact lock
- Numeric workload lock: CPython 3.11.15, standard-library-only imported path,
  source-file checksums in `bench/baseline/python-backtest-lock.json`
- Fixture: `backtest-small-v1`, 1,000 bars, SHA-256 `594b6175ce32fd33576c3a38cd4ac8231a0dd2316cd2483672a5d1dc434331f1`
- Schema identity: `bootstrap-sha256:c8b12a9e…383a4d0`; the Python source has no
  version table, so `python-schema-lock.json` pins the full bootstrap plus the two
  in-scope migrations named by its deployment job by path and SHA-256
- Benchmark host/toolchain: macOS 26.5.1 arm64, Apple M5 Pro (15 logical CPUs,
  48 GB), Python 3.14.6, Apple clang 21.0.0, CMake 4.4.3
- Machine-readable record: `bench/baseline/python-reference.json`

## Phase tracker

| Phase | Description | State |
|-------|-------------|-------|
| P0 | Pin Python baseline + harness + C scaffold | ✅ done (hosted CI green) |
| P1 | C numeric slice + first Python/C benchmark | ✅ done (parity dev+bench, ~179× bench) |
| P2 | C decision core + parity/performance | ✅ done (decisions+risk+screener; blend/risk benchmarks) |
| P3 | C drivers against shared infrastructure | 🟨 in progress (18-cell smoke green; 30-trial gate pending) |
| P4 | Pure-C edges + Rust datalake + optional Rust network comparisons | ⬜ not started |
| P5 | E2E + consolidated comparison report | ⬜ not started |

## Decisions locked (see MEMORY.md)

- C owns normalization/decisions; Rust owns the selected S3/Parquet datalake and
  is optional for network-edge comparisons.
- RL model/training/testing is out of scope; keep only the `SignalSource` seam.
- Selected stack: yyjson; nonblocking/pipelined libpq; hiredis; libwebsockets-owned
  event loop/API; axum+tower, tokio-tungstenite, SQLx, fred; Rust S3/Parquet.
- Python collector remains authoritative; C begins at normalization.
- Idiomatic re-architecture; behavior must match Python within test tolerance.
- Infrastructure and Infisical are excluded from the port and timed regions.
- Performance claims require parity and the controlled `BENCHMARK_PLAN.md` protocol.
- Single LWS loop has a P0 exit spike; only bounded non-I/O workers are an escape
  hatch. Risk/order decisions never run there.
- Standard eligible builds pin safe FP semantics; breaker expiry uses the next KRX
  session calendar; risk/blend config is checksummed input.
- Numeric wins alone do not justify migration. Risk decisions stay single-source
  C, and production enables exactly one separately approved broker adapter.

## MVP overlay (vertical slices over P0–P5)

Target end-state = measured, parity-gated **hybrid with replay-shadow evidence for a separately approved cutover**
that overcomes Python + comparison-only-C + pure-pro-C + Rust-everywhere. Ladder:
MVP-0 rig → MVP-1 numeric win → **MVP-2 driver I/O (GO/NO-GO)** → MVP-3 vertical E2E
beats Python → MVP-4 shadow hybrid + cutover. See `docs/plans/MVP_ROADMAP.md` and
the debate in `docs/plans/20260830-mvp-strategy-discussion.md`.

MVP-2 GO requires a statistically repeatable primary-metric win over at least 30
order-rotated trials (95% bootstrap CI excludes parity) with no correctness,
error, drop, resource, or safety regression. MVP-4 uses replay-shadow evidence;
live shadowing or order activation is not authorized by this project.

## Documentation completed

- 2026-09-01: **MVP-2 native adapter smoke.** Real Python, C (pinned LWS/libpq/
  hiredis), and Rust (Tokio/SQLx/fred) emit 3,000 completion/latency samples,
  namespaced state, terminal goldens, and strict attestations at truthful depth 1.
  Hosted Linux CI `33459011368` passed all 18 real-service smoke cells. This is
  parity/readiness evidence only; no benchmark decision is recorded.
- 2026-08-31: **P3 driver recovery + production LWS runtime.** Corrected the
  Python `hard_stop:lockout:{scope}` key and absolute `EXAT` validation; added
  bounded typed hiredis async reconnect, bounded curl-multi failure/cancellation
  handling, and a nonblocking libpq FIFO that drains buffered pipeline boundaries.
  A reusable bounded runtime adopts duplicate driver descriptors into the exact
  pinned static LWS loop, preserving driver ownership. Docker integration runs
  PostgreSQL, Redis, and HTTP concurrently on one service thread. Added the
  fail-closed 30-trial Python/C/Rust benchmark orchestrator/bootstrap evaluator
  and a real asyncio/asyncpg/redis baseline adapter; C and Rust adapters and raw
  results remain pending, so MVP-2 has no decision yet.
- 2026-08-31: **P3 unit 2 — Redis cache driver (hiredis).** Added
  `core/platform/cache/redis_cache.c` (`alpha/redis_cache.h`): `latest_ticks:{ticker}`
  set/get with the Python TTL (60s, key pattern `redis:cache:latest_ticks:{ticker}`)
  and the breaker-lockout safety enhancement — store the absolute `expires_at` from
  unit 1's next-session calendar with TTL = expires_at − now, so it self-clears at
  the next session; a past/sentinel expiry is rejected (fail-closed, keeps any
  existing lockout). Drivers build behind the opt-in `-DALPHA_WITH_DRIVERS=ON`
  (the driverless `c-build` jobs are unchanged); the integration test connects to
  the docker-compose Redis and runs in the `shared-services` CI job
  (`ALPHA_RUN_REDIS_INTEGRATION=1`), skipping gracefully when Redis is absent.
  Verified locally against Docker Redis. Next: libpq (nonblocking) + libcurl HTTP.
- 2026-08-31: **P3 unit 1 — KRX trading-calendar core** (pure, no I/O). Ported
  `next_trading_day_start` as `alpha_next_trading_session_start` (weekend-only,
  exact epoch parity with the pinned Python) plus a holiday-aware safety variant
  that skips supplied holidays and returns a fail-closed sentinel when the
  candidate day is beyond calendar coverage (so a missing/stale
  `krx:holidays:{year}` never releases a breaker early). KST is treated as a
  fixed UTC+9, so the math is exact integer epoch arithmetic. Golden parity
  (`test_market_hours`) under dev+bench. This is the expiry the P3 Redis breaker
  lockout will use; drivers (hiredis/libpq/libcurl) follow per
  `docs/plans/20260831-p3-drivers.md`.
- 2026-08-31: **P2 part 3 (final) — screener filter.** Ported the non-RL daily
  screener decision logic (`core/src/indicators/screener.c`): `_score_ticker`
  (volume-surge ratio + change-pct, `>=` thresholds, `<5`-bar skip) and the
  passing/score-descending/top-10 selection (stable on ties). Golden parity
  (`test_screener`) against a Python transcription (screener.py imports DB/logging
  at module load) under dev+bench, covering both threshold boundaries, volume-only
  vs change-only, insufficient data, null change, and selection tie order.
  Completes the P2 §7 contract; ranking_calculator stays with P3 aggregate SQL.

- 2026-08-31: **P2 part 2 — risk gates + max-position sizing.** Ported the
  safety-critical decision gates as pure, no-I/O functions (`core/src/risk/gates.c`,
  `core/src/portfolio/sizing.c`, header `risk.h`): L1 per-position stop/take, L2
  portfolio-drawdown weakest-two (stable two-pointer = Python stable sort +
  `candidates[:2]`), L3 daily-loss breaker decision, and the paper/real
  max-position BUY gate. Golden parity (`test_risk`) against a Python transcription
  of the pinned `portfolio_manager` arithmetic (the live methods are async/DB-
  coupled — see MEMORY) under dev+bench, with take/stop/dd/daily boundaries, tie
  selection, and paper/real denominators. Risk config is a checksummed fixture;
  the test asserts it equals the Python-compatible defaults. Remaining P2:
  indicators/ranking/screener (non-RL deterministic filters).
- 2026-08-31: **P2 part 1 — market_data + blending.** Ported instrument-id maps,
  `sanitize_change_pct`/`compute_change_pct` (`core/src/market_data`) and the
  N-way + 2-way blending (`core/src/portfolio/blending.c`). New golden generator
  `tools/generate_golden_decisions.py` dumps Python outputs for committed
  `bench/fixtures/{blend,market-data}-cases.json`; C tests
  (`test_market_data`, `test_blending`) match them under dev+bench. Covers the
  ±0.15 strict threshold, zero-weight equal fallback, conflict, clamping, dotted
  raw-code split, and change_pct clamp/round/none paths. blend-batch benchmark
  (`core/apps/blend_runner.c` + `bench/run_{python,c}_blend.py`, buy-count
  cross-check): C median ~35 ms vs Python ~526 ms (~15×) over 200k 3-way sets.
  Remaining P2: risk gates (L1/L2/L3), max-position sizing, indicators/ranking.
- 2026-08-31: **P1 numeric core.** Ported `cost_model`, `engine`, `metrics`, and
  ReplaySignalSource to C11 (`core/src/{domain,backtest}`, headers in
  `core/include/alpha/`). Golden parity (`core/tests/test_parity_backtest.c`)
  matches the pinned Python `BacktestResult` — snapshots, trades, and 4dp/1dp
  metrics — under both `dev` (ASan) and `bench` (`-O3 -ffp-contract=off`) presets.
  Python `round` reproduced via `snprintf`+`strtod`; dates as Hinnant epoch-days.
  Unit suites cover cost/metrics/engine edge cases. First eligible C benchmark
  `bench/results/20260831/backtest-small-c.json`: C median ≈0.0133 ms vs Python
  2.3716 ms (~179×), peak RSS 1.9 MB vs 29 MB. clang-format/clang-tidy extended
  to the new sources; `.clang-tidy` tuned (four checks disabled, see MEMORY).
- 2026-08-30: proved the locked Python `CREATE_TABLES` bootstrap applies twice to
  an empty PostgreSQL 15 service. The gate parses literal SQL from the pinned
  source instead of copying or importing application code, verifies seven
  in-scope tables, and leaves date partitions to versioned fixture tasks.
- 2026-08-30: replaced the nonexistent schema revision with a reproducible,
  content-addressed bootstrap identity. A hard-failing validator locks the Python
  SHA, complete `init_db.py`, deployment migration job, and its two in-scope
  migrations. No migration number or schema behavior was invented.
- 2026-08-30: proved the P0 single-libwebsockets service-loop design with
  nonblocking libpq, hiredis async, and curl multi against pinned shared services.
  Machine-checked evidence covers PG pipeline abort/sync/reuse, Redis
  disconnect/reconnect, HTTP, bounded backpressure, cancellation, request IDs,
  one service thread, zero workers, clean shutdown, and no linked libuv/libevent.
  The conservative 1 ms readiness probe is feasibility evidence only, not a
  throughput result or Phase 3 production-driver design.
- 2026-08-30: pinned ephemeral PostgreSQL 15.14 and Redis 7.4.11 shared test
  services with loopback-only ports, health checks, resource bounds, and CI. Both
  containers passed real `SELECT 1`/`PING`; no unverified schema was created.
- 2026-08-30: added hard-failing CI jobs for the pinned Python golden, dev/bench
  C builds and tests, compiler-flag verification, formatting, and clang-tidy.
  Local workflow parsing plus all 17 Python/C checks pass; hosted status awaits push.
- 2026-08-30: added C11 `libalpha_core`, public domain/error/constants headers,
  pinned yyjson 0.10.0 and Unity 2.6.1, and JSON/ABI/FP smoke tests. Both sanitizer
  `dev` and optimized `bench` presets build and pass; bench forbids fast-math.
- 2026-08-30: generated the deterministic `backtest-small-v1` fixture and pinned
  Python golden; recorded an eligible 10-trial Python baseline (median 2.3716 ms,
  p95 2.3805 ms per 1,000-bar run). This is baseline evidence, not a speed claim.
- 2026-08-30: pinned the Python source revision, dependency-manifest checksum,
  dirty state, and benchmark host. The global range manifest remains unsuitable
  as an exact environment; each eligible workload must carry its own lock.
- 2026-08-30: reframed the handoff as a benchmark-driven Python/C/selected-Rust
  comparison; added the normative benchmark protocol and aligned P0–P5 exits.
- 2026-08-30: locked the selected C/Rust libraries and retained the Python
  collector; removed direct C collector and C datalake as default targets.
- 2026-08-30: hardened single-loop fallback, FP parity, KRX-calendar lockout,
  config fixtures, migration criteria, and broker authority rules.
- 2026-08-30: added `bench` preset + parity-on-benchmarked-artifact rule; wrote the
  3-persona MVP strategy discussion and `MVP_ROADMAP.md` (hybrid end-state).

## Next action

Run the controlled 30-trial matrix on one pinned Linux host, commit all 18 result
files, and evaluate C/Rust. Keep P4/P5 stopped until the evidence-based gate.

## 2026-08-31 — P2 risk benchmark
- Added identical 100,000-snapshot Python/C risk workloads covering stop/take,
  daily loss, and paper/real max-position decisions. All four counts and the
  combined checksum match; the C benchmark is parity-gated before timing.
- Recorded 10 raw trials: Python median 28.7343 ms and C median 0.4110 ms on this
  host. This is arithmetic-only evidence using the documented transcription,
  not a live-I/O or migration claim.
