# BUILD_AND_TEST.md — Build system, tests, and the parity harness

Correctness gates are defined here; controlled performance measurement is defined
in `docs/BENCHMARK_PLAN.md`. A parity failure makes the corresponding performance
result ineligible.

## Layout & build system

- **C core (`core/`)** — CMake (≥3.20), out-of-source in `build/`.
  - `libalpha_core` static lib from `core/src/**` + `core/platform/**`.
  - Public headers in `core/include/alpha/`.
  - Tests in `core/tests/` via CMocka, registered with `ctest`.
  - `CMakePresets.json`: `dev` (Debug + ASan/UBSan), `release` (RelWithDebInfo),
    `bench` (the benchmark/ship build: `-O3 -DNDEBUG -ffp-contract=off`, no
    `-ffast-math`, sanitizers off — carries the `BENCHMARK_PLAN §5.2` flags).
```
cmake --preset dev
cmake --build build
ctest --test-dir build --output-on-failure
```
- **Rust async/data adapters (`edge/`)** — Cargo workspace; selected datalake plus
  optional network comparisons. `core-sys` binds `libalpha_core` via bindgen
  (headers from `core/include/alpha/`).
```
cargo build --workspace
cargo test --workspace
```
- **Formatting/lint gates:** `clang-format --dry-run --Werror`, `clang-tidy`;
  `cargo fmt --check`, `cargo clippy -- -D warnings`.

## CMake/Cargo wiring

- CMake builds `libalpha_core` and pure-C edges. Cargo builds the selected Rust
  datalake and optional Rust network adapters.
- A top-level build script/target runs CMake first, verifies the exported C ABI
  header/version/hash, then runs Cargo/bindgen and both test suites.
- Cargo never recompiles or reimplements numeric/risk logic; it links the exact
  `libalpha_core` artifact produced by the same source revision.

## CI (set up in Phase 0)
Matrix: build+test C under `dev` (ASan on) **and** `bench` (optimized) presets —
the parity suite runs on both; format/lint gates; and (if present) Rust
build+test+clippy. Integration job spins up Postgres+Redis via Docker Compose
for Phase 3+ driver tests. Benchmark timings run only on `bench` artifacts that
passed parity.

## Test tiers
1. **Unit** — one `test_<module>.c` (CMocka) / `#[cfg(test)]` per module.
2. **Parity (golden files)** — see below; the correctness anchor.
3. **Integration** — driver round-trips against Docker Postgres/Redis.
4. **E2E** — full pipeline dry run (Phase 5).

## Parity harness (vs. Python) — the anchor

Goal: prove the C/Rust output matches the Python source for the same input.

**Fixtures:** reuse the Python repo's fixtures under `../alpha-financial-pipeline/test/`
where possible; otherwise generate inputs (price series, signal sets, risk configs).

**Golden generation (scripted, versioned, committed):**
```
# from ../alpha-financial-pipeline (its own venv)
python -m tools.dump_golden
```
Phase 0 must add the actual generator if the Python repository does not yet have
one. Do not use untracked ad-hoc generation for committed goldens. Each output
records the pinned Python SHA, input checksum, generator version, and timezone.
Write each Python module's output to `core/tests/golden/<case>.json`
(e.g. backtest `BacktestResult`, blend `NWayBlendResult`, risk decisions,
`compute_change_pct`).

**Assertion in C/Rust test:** run the ported function on the same input, serialize
to JSON, and compare to the golden file with tolerances:
- Integer fields (share qty, cash-as-int where applicable, trade counts): **exact**.
- Float fields: metrics already rounded in Python (`metrics.py`) — compare at the
  **same rounding** (4 dp for returns/sharpe/mdd/win/baseline/excess; 1 dp holding;
  6 dp weighted_score; 4 dp confidence). Elsewhere use `|a-b| <= 1e-9 * max(1,|b|)`.
- Enum/string signals: **exact** (`BUY`/`SELL`/`HOLD`/`CLOSE`).

**Floating-point reproducibility:** parity and standard release builds forbid
`-ffast-math`, compile C with `-ffp-contract=off`, set/verify `FE_TONEAREST`, and
record compiler/target/flags. Implement Python ties-to-even decimal rounding
explicitly; C `round()` is not equivalent. Pin JSON behavior for `-0.0`, NaN, and
infinity. Unsafe-math/FMA experiments run separately and are never parity evidence.

**Parity runs on the benchmarked artifact.** FP contraction/FMA only appears at
`-O2/-O3`, so a `dev` (`-O0`) pass does not prove parity of the shipped/timed
build. The parity suite **must pass under the `bench` preset** — the same optimized
artifact used for `BENCHMARK_PLAN` timings — not only under `dev`. A benchmark
result is eligible only if its exact `bench` binary passed parity.

**Priority parity cases (build first):**
1. `cost_model.calculate` (BUY/SELL, various price×qty).
2. `backtest.engine.run` over a fixture price/date series → full `BacktestResult`.
3. `metrics.compute_backtest_metrics` on crafted snapshots (incl. empty).
4. `blending.blend_signals` (N inputs, conflict, zero-weight fallback).
5. risk gates: max-position skip, −7% stop, +5% take, −8% dd, −3% daily breaker.
6. `market_data.compute_change_pct` / instrument-id round-trip.
7. risk/blend with identical default/custom/boundary/invalid config fixtures and
   exact normalized-config checksum.

## Done criteria for the suite
All 7 priority cases pass at the tolerances above — under **both** the `dev`
(ASan/LSan) and the `bench` preset — unit tests green under ASan/LSan; lint/format
clean. Record any intentional numeric deviation (and why) in `MEMORY.md`.

Release benchmarks are separate jobs: sanitizers off, optimized `bench` builds,
pinned fixtures, raw JSON results, and the run protocol in `docs/BENCHMARK_PLAN.md`.
Only a `bench` artifact that passed parity is benchmark-eligible.
