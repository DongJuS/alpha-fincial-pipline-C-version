# MEMORY.md — Active rules & key decisions

> Durable decisions future agents must know. Keep under ~200 lines. Newest on top.

## Port scope & language
- **This is a comparison project, not an automatic Python replacement.** The
  operating Python implementation remains the pinned behavioral/performance
  baseline until evidence supports a later migration decision.
- **C owns normalization and deterministic decisions.** Pure-C networking uses
  libwebsockets as the sole event-loop owner. Rust owns the selected S3/Parquet
  datalake and is optional for network-edge comparisons, behind C-owned contracts.
- **RL model/training/testing is out of scope.** Only the `SignalSource` interface
  seam is kept, so an external RL signal can be *replayed* into backtest/live later.
  Rationale: RL is a separately-evolving lane; porting it adds huge surface for no
  near-term value.
- **Infrastructure and Infisical are out of port scope.** Python/C/Rust comparisons
  reuse equivalent PostgreSQL, Redis, S3/MinIO, and runtime conditions. Secret
  retrieval is completed before timing and is never included in speed claims.
- **The Python collector remains authoritative.** Do not port FDR/yfinance/KRX/KIS
  collection or live ingest in the main path. C begins at normalization; recorded
  WS frames may be used only for an optional edge benchmark.

## External dependencies
- C: yyjson; nonblocking libpq with bounded pipeline mode; hiredis integrated into
  the libwebsockets-owned loop; libcurl for required outbound HTTP.
- Rust: Tokio, axum+tower, tokio-tungstenite, SQLx, fred. Datalake uses
  `aws-sdk-s3` with a configurable S3-compatible endpoint and Rust Arrow/Parquet.
- The self-hosted object store is the target. AWS cloud is not required; integration
  tests pin endpoint URL, path-style addressing, multipart behavior, and checksums.

## Behavior fidelity
- `backtest-small-v1` is the canonical numeric anchor: 1,000 deterministic weekday
  bars, 20 replayed round trips, CPython 3.11.15, and no external package imports.
  Its fixture/source/golden checksums are pinned under `bench/`; DB schema remains
  unresolved and must not be inferred from this schema-free workload.
- A benchmark is eligible only after its parity case passes against goldens made
  from the Python SHA recorded in `progress.md`. See `docs/BENCHMARK_PLAN.md`.
- Idiomatic re-architecture, but **numeric behavior must match the Python source**
  within the parity tolerance in `docs/BUILD_AND_TEST.md`.
- Backtest cost model constants: commission 0.015%, tax 0.18% (SELL only),
  slippage 3 bps. Blend threshold 0.15. Risk: max single position 20%, daily-loss
  circuit breaker −3% (Redis lockout), per-position stop −7% / take +5%, portfolio
  drawdown −8%. Verbatim in `docs/MODULE_SPECS.md`.
- Standard parity/performance builds forbid `-ffast-math`, use
  `-ffp-contract=off` + `FE_TONEAREST`, and implement Python ties-to-even rounding.
- Risk/blend config is a checksummed input fixture, not an assumed default.

## Safety invariants
- Order authority isolated to the broker edge module.
- Risk gates are enforced below the signal layer. Defaults match Python; only
  validated portfolio config may change them, never a signal/strategy payload.
- Paper trading default; real trading needs explicit flag + confirmation.
- Risk/blend/sizing have one C implementation. C/Rust broker challengers are
  paper-only; exactly one separately approved adapter may load real credentials.
- Breaker expiry is the next KRX session at 09:00 KST using
  `krx:holidays:{year}`; missing/stale calendar fails closed.
- Numeric speedup alone never justifies migration; production-relevant E2E/resource/
  recovery/safety benefit must outweigh multi-language maintenance.
