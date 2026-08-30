# MEMORY.md — Active rules & key decisions

> Durable decisions future agents must know. Keep under ~200 lines. Newest on top.

## Port scope & language
- **This is a comparison project, not an automatic Python replacement.** The
  operating Python implementation remains the pinned behavioral/performance
  baseline until evidence supports a later migration decision.
- **Target end-state = measured, parity-gated HYBRID with replay-shadow evidence
  for a separately approved cutover** (`docs/plans/MVP_ROADMAP.md`, discussion
  `20260830-mvp-strategy-discussion.md`): C decision core + Rust
  async/order/datalake edges + Python
  retained where porting isn't worth it. Chosen over pure-Python (perf/cost),
  pure pro-C (edge safety/maintenance), and Rust-everywhere (FFI/toolchain tax on
  the core). Delivered as 5 vertical MVPs; **MVP-2 (driver I/O) is the go/no-go** —
  if C/Rust drivers do not show a statistically repeatable primary-metric win,
  ship the C numeric core as a library and keep Python for I/O. The gate requires
  at least 30 order-rotated trials and a 95% bootstrap CI excluding parity, with
  no correctness, error, drop, resource, or safety regression.
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
- The P0 LWS spike passed with one LWS service thread scheduling 1 ms,
  zero-timeout readiness probes for nonblocking libpq/hiredis/curl-multi. This is
  feasibility evidence only: Phase 3 must select and benchmark a production
  LWS-owned foreign-fd mechanism and repeat abort/reconnect/backpressure tests.
  The spike uses no worker threads and dynamically links no libuv/libevent.
- Phase 0 vendors yyjson 0.10.0 and the permitted Unity 2.6.1 test framework so
  dev/bench builds do not depend on unpinned host packages. Upstream commit IDs
  and licenses are recorded in `third_party/README.md`.
- C: yyjson; nonblocking libpq with bounded pipeline mode; hiredis integrated into
  the libwebsockets-owned loop; libcurl for required outbound HTTP.
- Rust: Tokio, axum+tower, tokio-tungstenite, SQLx, fred. Datalake uses
  `aws-sdk-s3` with a configurable S3-compatible endpoint and Rust Arrow/Parquet.
- The self-hosted object store is the target. AWS cloud is not required; integration
  tests pin endpoint URL, path-style addressing, multipart behavior, and checksums.

## Behavior fidelity
- The pinned Python source has no migration framework revision/version table.
  Its schema identity is the SHA-256 of a sorted source bundle: `init_db.py`, the
  deployed migration job, and its two in-scope migrations. The schema lock binds
  those bytes to the Python commit; never invent a numeric migration version.
  Applying them cleanly remains a separate gate.
- The locked Python schema bootstrap is applied by AST-extracting the literal
  `CREATE_TABLES` list and piping those exact bytes to `psql`; never import the
  Python app or reconstruct DDL from docs. The idempotency gate applies it twice.
  Date partitions are created only when a versioned fixture needs them.
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
- **Python `round(x, n)` parity is reproduced with `snprintf("%.*f")` + `strtod`**
  under verified `FE_TONEAREST` (`core/src/domain/round.c`), not C `round()`.
  In default rounding mode both do correctly-rounded ties-to-even decimal
  conversion, so C matches CPython; the `backtest-small` golden confirms all
  4dp/1dp metrics bit-for-bit within 1e-9.
- **Dates are proleptic-Gregorian epoch-day integers** (Hinnant days-from-civil,
  `core/src/domain/date.h/.c`); differences reproduce Python `(d2-d1).days` for
  FIFO holding-day metrics. Only diffs/equality are relied on, so the epoch choice
  is irrelevant to parity.
- **P1 numeric core is done and parity-gated.** cost_model/metrics/engine +
  ReplaySignalSource match the Python golden under **both** `dev` (ASan) and
  `bench` (`-O3`) presets. First eligible C benchmark: `backtest-small` C median
  ≈0.0133 ms vs Python 2.3716 ms (~179×), peak RSS 1.9 MB vs 29 MB —
  `bench/results/20260831/backtest-small-c.json`. This is a numeric-core result
  only; per §migration it does not by itself justify replacing Python.
- Risk/blend config is a checksummed input fixture, not an assumed default.

## Toolchain / lint
- Project `.clang-tidy` disables four checks project-wide with rationale:
  `readability-identifier-length` and `readability-math-missing-parentheses`
  (single-letter indices and the Hinnant date algorithm are idiomatic),
  `bugprone-easily-swappable-parameters` (contract signatures mirror Python; also
  already disabled for the LWS spike), and
  `clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling` (it
  demands Annex-K `snprintf_s`, unavailable on glibc; bounded `snprintf` is
  correct). CI (`c-quality`) runs clang-tidy over all `core/src/**` and
  `core/apps/**` production sources, not just the scaffold.

## Safety invariants
- MVP-4 validation is an order-disabled deterministic replay shadow. Live shadow
  access, production deployment, and real-order activation require separate approval.
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
