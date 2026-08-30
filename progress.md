# progress.md — Current state

> Update this after every task. Keep under ~200 lines. Record *why*, not just *what*.

## Status: Phase 0 in progress — Python source revision pinned

The specification set is written. No source code exists yet. A developer/AI picks
up at **Phase 0**, pins the Python reference, and records a baseline before porting.

## Reference baseline (must be completed in P0)

- Python commit SHA: `3642cdc0e4026424ca9b6158125551eee1d42683`
- Python worktree: tracked files clean; untracked `HANDOFF-2026-08-03.md` present
- Dependency manifest: `requirements.txt`, SHA-256 `bb923cd220358a0d3fd6d2a5889a60a30a49e476f4bd499cd52de21b13fe0837`; no exact lock
- Schema/fixture version: unresolved; blocks golden generation and public results
- Benchmark host/toolchain: macOS 26.5.1 arm64, Apple M5 Pro (15 logical CPUs,
  48 GB), Python 3.14.6, Apple clang 21.0.0, CMake 4.4.3
- Machine-readable record: `bench/baseline/python-reference.json`

## Phase tracker

| Phase | Description | State |
|-------|-------------|-------|
| P0 | Pin Python baseline + harness + C scaffold | 🟨 in progress (source pin captured) |
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

- 2026-08-30: pinned the Python source revision, dependency-manifest checksum,
  dirty state, and benchmark host; unresolved lock/schema/fixtures remain explicit blockers.
- 2026-08-30: reframed the handoff as a benchmark-driven Python/C/selected-Rust
  comparison; added the normative benchmark protocol and aligned P0–P5 exits.
- 2026-08-30: locked the selected C/Rust libraries and retained the Python
  collector; removed direct C collector and C datalake as default targets.
- 2026-08-30: hardened single-loop fallback, FP parity, KRX-calendar lockout,
  config fixtures, migration criteria, and broker authority rules.

## Next action

Freeze the resolved Python dependencies and identify the schema/fixture versions,
then generate the first golden and measured Python baseline before C claims.
