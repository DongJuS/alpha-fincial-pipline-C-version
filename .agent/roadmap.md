# roadmap.md — Milestones (Phases 0–5)

Detailed build guides live in `docs/plans/phase-*.md`. This is the milestone map.

## P0 — Pin baseline, harness & scaffold
Pin the Python commit/schema, define port exclusions and benchmark host, generate
versioned fixtures/goldens, measure at least one Python baseline case, then create
the CMake/CI skeleton. Reuse shared infrastructure; do not port Infisical.
**Exit:** baseline metadata + raw result exist; C skeleton builds; `ctest` runs; CI green.

## P1 — C numeric core (anchor slice)
`domain` (enums, value types, constants), `cost_model`, `backtest` (models,
metrics, signal_source interface + ReplaySignalSource, engine).
**Exit:** backtest matches Python, and the first valid Python/C result is emitted
under `BENCHMARK_PLAN.md`. No I/O.

## P2 — C risk & portfolio
`market_data` normalization, `blending` (N-way weighted score), `risk` gates
(per-position stop/take, portfolio drawdown, daily-loss circuit breaker),
portfolio sizing (max-position %).
**Exit:** risk/blend decisions match Python and batch benchmark results are emitted.

## P3 — C nonblocking drivers
`platform/db` uses nonblocking libpq and bounded pipeline mode; `platform/cache`
uses hiredis async; required outbound HTTP uses libcurl multi. All integrate with
the libwebsockets-owned loop. The Python collector supplies input.
**Exit:** normalized input→DB and lockout round-trips work; controlled Python/C
driver results use identical shared services.

## P4 — Async/data edges
Pure-C API/broker/orchestrator use the libwebsockets loop. Selected Rust comparisons
use axum+tower, tokio-tungstenite, SQLx, and fred. Rust datalake uses aws-sdk-s3
against the self-hosted S3-compatible endpoint plus Arrow/Parquet encoding. The
operating Python collector is retained; WS ingest is replay-only/optional.
**Exit:** pure-C dry run passes and eligible edge benchmarks compare Python/C;
selected Rust variants are added only with a recorded justification.

## P5 — E2E + comparison report
Run the deterministic paper-trading replay against shared services, consolidate
parity, performance/resource results, implementation size, and operational
tradeoffs; finalize `progress.md` + `MEMORY.md`.
**Exit:** E2E green; parity passes; reproducible Python/C/(selected Rust) report published.
