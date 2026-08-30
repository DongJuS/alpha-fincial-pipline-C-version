# Phase 0 — Pin baseline, benchmark harness, and scaffold

## Goal
A pinned, measured Python reference plus a buildable C skeleton. Establish
correctness/performance evidence before business logic is ported.

## Baseline first
- Record the Python commit SHA, dirty state, schema version, dependency lock, and
  benchmark host/toolchain in `progress.md`.
- Write a tracked golden generator and fixture manifest with SHA-256 checksums.
- Record Python results for `backtest-small` (and harness overhead/no-op where
  useful) using `docs/BENCHMARK_PLAN.md`.
- Document the included application paths and confirm infrastructure + Infisical
  are excluded from the port and timed regions.

## Files to create
- `core/CMakeLists.txt`, `CMakePresets.json` (`dev` = Debug+ASan/UBSan; `release` =
  RelWithDebInfo; `bench` = `-O3 -DNDEBUG -ffp-contract=off`, no `-ffast-math`,
  sanitizers off — the benchmarked/shipped build per `BENCHMARK_PLAN §5.2`).
- Pin/build yyjson and prove a trivial parse/serialize test.
- `core/include/alpha/` — empty public headers: `alpha.h`, `constants.h`,
  `domain.h`, `errors.h` (with `alpha_err_t`).
- `core/src/` subdirs (`domain/ market_data/ indicators/ risk/ portfolio/ backtest/`)
  and `core/platform/` (`db/ cache/ http/`) with `.gitkeep`.
- `core/tests/` with one trivial CMocka test wired to `ctest`.
- `db/` integration setup — reuse the pinned source migrations where practical;
  add only benchmark-specific fixtures or compatibility migrations. Do not fork
  the entire schema without a measured need.
- `docker-compose.yml` — optional local wrapper for the same Postgres 15 + Redis 7
  versions used by all variants; it is test infrastructure, not a port target.
- `.github/workflows/ci.yml` — build+test under `dev` and `bench` presets (parity
  suite runs on both), format/lint gates, optional Rust job.
- `edge/` placeholder; create the Rust workspace with datalake feature boundaries
  and selected dependency pins, but defer implementation.
- `core/tests/golden/`, `bench/fixtures/manifest.json`, `bench/results/`, and a
  working tracked golden/benchmark runner.

## Contract
No module contract yet; this phase establishes the structure referenced by
`architecture.md` and `docs/BUILD_AND_TEST.md`.

## Tests
- `ctest` runs the trivial test and passes.
- `cmake --build build` produces `libalpha_core` (empty archive is fine).
- Shared/pinned migrations and benchmark fixture setup apply cleanly.
- Python baseline emits a schema-valid raw result JSON.

## Done criteria
- `cmake --preset dev && cmake --build build && ctest --test-dir build` green;
  the `bench` preset also builds and `ctest` passes on it (parity runs on both).
- `clang-format`/`clang-tidy` gates run in CI and pass on the skeleton.
- Shared Postgres+Redis test services are healthy and fixture setup applies.
- Python SHA and baseline result are recorded; benchmark protocol is reproducible.
- A feasibility spike proves nonblocking libpq and hiredis readiness can be driven
  by the single libwebsockets event loop without hidden blocking/second loops. It
  must also drive one libcurl-multi request and exercise disconnect, pipeline
  abort/sync recovery, cancellation, backpressure, and clean shutdown.
- If unavoidable blocking/CPU work is found, prove the bounded-worker escape hatch:
  fixed thread/queue limits, result marshalling to LWS, saturation behavior, and
  proof that workers cannot call risk decisions or broker order placement.
- `progress.md` P0 marked done.

## Notes
- Keep `constants.h` values byte-for-byte from `src/constants.py`.
- Do not add libuv/libevent: libwebsockets is the selected C event-loop owner.
- P0 does not pass if the integration spike deadlocks, loses result/request
  correspondence, blocks the LWS service thread, or requires an unbounded queue.
