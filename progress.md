# progress.md — Current state

> Update this after every task. Keep under ~200 lines. Record *why*, not just *what*.

## Status: Phase 1 complete — parity-gated C numeric core

P0 is closed (hosted CI green, run `33318345071`). P1 ports the deterministic
numeric core — cost model, backtest engine, metrics, ReplaySignalSource — with
golden parity against the pinned Python source under both the `dev` (ASan) and
`bench` (`-O3`) presets, unit tests, and the first eligible C benchmark.

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
| P2 | C decision core + parity/performance | 🟨 in progress (market_data + blending done) |
| P3 | C drivers against shared infrastructure | ⬜ not started |
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
- 2026-08-30: first hosted run `33315962850` exposed three portability defects:
  Linux `libm` linkage, missing per-job Python checkout, and clang-analyzer
  rejection of C buffer APIs. Fixes use UNIX `m`, a pinned detached checkout,
  aggregate initialization, and `PQconnectStartParams`; hosted rerun is pending.
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

Start **P2 (risk & portfolio)**: `market_data` normalization, N-way `blending`,
`risk` gates (stop/take/drawdown/daily breaker), and portfolio sizing, each with
golden parity and the config-checksum discipline (`docs/MODULE_SPECS.md §1,4,5`,
`docs/plans/phase-2-risk-portfolio.md`). Then P3 drivers up to the **MVP-2**
driver-I/O GO/NO-GO gate.
