# progress.md — Current state

> Update this after every task. Keep under ~200 lines. Record *why*, not just *what*.

## Status: Phase 0 in progress — Python source revision pinned

The Python numeric baseline and initial C11 build scaffold are implemented. Phase
0 continues with CI, shared-service setup, and the single-event-loop feasibility spike.

## Reference baseline (must be completed in P0)

- Python commit SHA: `3642cdc0e4026424ca9b6158125551eee1d42683`
- Python worktree: tracked files clean; untracked `HANDOFF-2026-08-03.md` present
- Global dependency manifest: `requirements.txt`, SHA-256 `bb923cd220358a0d3fd6d2a5889a60a30a49e476f4bd499cd52de21b13fe0837`; no exact lock
- Numeric workload lock: CPython 3.11.15, standard-library-only imported path,
  source-file checksums in `bench/baseline/python-backtest-lock.json`
- Fixture: `backtest-small-v1`, 1,000 bars, SHA-256 `594b6175ce32fd33576c3a38cd4ac8231a0dd2316cd2483672a5d1dc434331f1`
- Schema version: unresolved; blocks schema-dependent fixtures and results
- Benchmark host/toolchain: macOS 26.5.1 arm64, Apple M5 Pro (15 logical CPUs,
  48 GB), Python 3.14.6, Apple clang 21.0.0, CMake 4.4.3
- Machine-readable record: `bench/baseline/python-reference.json`

## Phase tracker

| Phase | Description | State |
|-------|-------------|-------|
| P0 | Pin Python baseline + harness + C scaffold | 🟨 in progress (baseline + C scaffold) |
| P1 | C numeric slice + first Python/C benchmark | ⬜ not started |
| P2 | C decision core + parity/performance | ⬜ not started |
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

## Documentation completed

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
  dirty state, and benchmark host; unresolved lock/schema/fixtures remain explicit blockers.
- 2026-08-30: reframed the handoff as a benchmark-driven Python/C/selected-Rust
  comparison; added the normative benchmark protocol and aligned P0–P5 exits.
- 2026-08-30: locked the selected C/Rust libraries and retained the Python
  collector; removed direct C collector and C datalake as default targets.
- 2026-08-30: hardened single-loop fallback, FP parity, KRX-calendar lockout,
  config fixtures, migration criteria, and broker authority rules.

## Next action

Implement the Phase 0 single-libwebsockets-loop feasibility spike for nonblocking
libpq, hiredis async, and libcurl multi, including required failure/recovery cases.
