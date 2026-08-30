# conventions.md — Code style & conventions (C base + optional Rust)

> Rules the implementing agent must follow. C11 is the base; Rust applies only to
> the optional async edges.

---

## 🇨 C conventions (base)

### Standard & build
- **Language:** C11 (`-std=c11`), compiled with `-Wall -Wextra -Werror`.
- **Build:** CMake (≥3.20), out-of-source in `build/`. Static lib `libalpha_core`.
- **Format:** `clang-format` (LLVM base, 4-space indent, 100 col).
- **Static analysis:** `clang-tidy` clean; run ASan/UBSan in debug/test builds.

### Naming
- **Files:** snake_case (`cost_model.c`, `cost_model.h`).
- **Public API:** prefix everything with `alpha_` and expose in `core/include/alpha/`.
  e.g. `alpha_cost_calculate()`, `alpha_backtest_run()`, `struct alpha_trade_cost`.
- **Types:** `snake_case_t` for typedef structs/enums (`alpha_signal_t`).
- **Enums:** `ALPHA_SIGNAL_BUY`, `ALPHA_SIDE_SELL`.
- **Constants/macros:** `UPPER_SNAKE_CASE` (`ALPHA_BACKTEST_SLIPPAGE_BPS`).
- **Internal (file-local) functions:** `static`, no `alpha_` prefix required.

### Error handling
- Public functions return `alpha_err_t` (an enum, `ALPHA_OK == 0`) and write
  results through out-parameters. No silent failure, no `abort()` in library code.
- Float outputs use `double`. Money/quantities that are integral in the source
  (share counts, KRW cash) use `int64_t`. Match the Python type per module spec.
- Ownership: caller allocates where size is known; functions that allocate expose a
  matching `alpha_*_free()`. Document ownership in the header.

### Memory & safety
- No global mutable state in the numeric core (pure functions / caller-owned state
  structs). This keeps it thread-safe and embeddable.
- Every allocation path has a free path; tests run under ASan/LSan.
- No VLAs on untrusted sizes; bound all loops from validated inputs.

### Tests
- Framework: **CMocka** (or Unity), one `test_<module>.c` per module in `core/tests/`.
- Add a **golden-file parity test** wherever a Python counterpart exists
  (`docs/BUILD_AND_TEST.md §parity`).
- No hardcoded absolute paths / system-binary paths; resolve fixtures relative to
  the test file, tools via `PATH`.

---

## 🦀 Rust conventions (optional edges only)

- **Edition:** 2021. `cargo fmt` + `cargo clippy -- -D warnings` clean.
- **Workspace:** `edge/` with crates for selected modules (`broker`, `api`,
  `orchestrator`, optional `ws-replay`, selected `datalake`) plus `core-sys`.
- **Errors:** `Result<T, E>` with `thiserror`; never `unwrap()` on external I/O.
- **Async:** `tokio`; no blocking calls inside async tasks. API uses axum+tower,
  PostgreSQL uses SQLx, Redis uses fred, and WS replay uses tokio-tungstenite.
- **FFI:** call `libalpha_core` via `core-sys` (bindgen-generated). All decisions
  (risk/blend/portfolio) go through the C core — do not reimplement them in Rust.
- **Naming:** `snake_case` fns/modules, `CamelCase` types, `SCREAMING_SNAKE` consts.

---

## 🏗️ Shared architecture rules
- Single responsibility per function; keep the numeric core free of I/O.
- No magic numbers — constants live in `alpha/constants.h` (C) mirrored from the
  Python `src/constants.py`.
- Explicit error handling everywhere (no silent fail).
- Order authority only in the broker edge module.
- libwebsockets is the sole C event-loop owner. Integrate libpq/hiredis/libcurl
  readiness into it; do not start a hidden libuv/libevent loop or blocking I/O on
  the service thread.
- A small bounded worker pool is permitted only for an operation with no usable
  nonblocking API or for isolated CPU work. Workers never run risk/order decisions,
  never own sockets/event loops, and return results through a bounded queue to the
  LWS thread. Queue saturation must apply explicit backpressure or fail safe.
- Bound PostgreSQL pipeline depth and Redis pipeline/queue depth. Every queued
  request must have explicit ownership, cancellation, and result matching.

## 📝 Commit messages
```
feat: … | fix: … | docs: … | refactor: … | test: … | chore: …
```
Reference the phase (e.g. `feat(P1): cost model + backtest engine`).

## 🗂️ Discussion/plan docs
- One plan md per task in `docs/plans/`, filename `YYYYMMDD-topic-slug.md`, one
  topic each. Fold conclusions into permanent docs (`architecture.md`, `MEMORY.md`,
  `progress.md`) when done.
