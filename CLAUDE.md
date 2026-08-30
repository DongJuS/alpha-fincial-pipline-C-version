# 🚀 CLAUDE.md — Agent behavior code (C-version port)

> **Every agent must read this file first.**
> This repository re-implements the *deterministic core backend processor* of the
> Python project `alpha-financial-pipeline` in **C (base language)**, with a
> selected Rust datalake and optional Rust async/network comparisons. It is a **handoff
> spec**: another AI/developer builds the code from the `docs/` in this repo.

---

## 📌 Project overview

- **Name:** alpha-financial-pipeline — C version (`alpha-core`)
- **Goal:** Keep the operating Python collector/system as the reference, port
  selected application paths from normalized data onward (normalization →
  indicators → risk → portfolio → blending → backtest →
  order path → representative API paths) to C, and compare correctness,
  performance, resource use, and operational complexity. Use real drivers
  (nonblocking/pipelined libpq, async hiredis, libcurl-multi) where driver
  performance is being measured.
- **Base language:** C11. **Optional edge language:** Rust (see `docs/EDGE_OPTIONS.md`).
- **Source of truth for behavior/schema:** the Python repo at
  `../alpha-financial-pipeline` at the commit recorded in `progress.md` (read-only
  reference). Do not generate goldens until that commit is pinned.

### Comparison model

- **Python is the baseline**, not a component to delete during this project.
- **C11 is the mandatory comparison implementation** and owns all deterministic
  decision logic.
- **Pure-C vs Rust is an optional network-edge comparison.** Pure-C networking
  uses a libwebsockets-owned event loop. Rust implements the selected datalake
  adapter and may implement comparison network edges; all consume the same
  C-owned normalized/domain contracts.
- Correctness parity is a gate: a result that does not match the pinned Python
  behavior is not eligible for a performance claim.
- Benchmark rules, workloads, metrics, and reporting live in
  `docs/BENCHMARK_PLAN.md` and are normative.

### Out of scope (do NOT port)
- Infrastructure definitions and deployment platform (reuse the same PostgreSQL,
  Redis, S3/MinIO, container/cluster environment for all compared variants).
- Infisical and secret-management integration. Inject equivalent benchmark
  credentials/config into each variant; exclude secret lookup from timings.
- Python market-data collection (FDR/yfinance/KRX/KIS collection and live ingest).
  It remains authoritative; the C/Rust comparison begins at recorded/raw input
  normalization or data already written by the Python collector.
- RL model, RL training, RL evaluation/testing (`src/agents/rl_*.py`,
  `rl_environment`, `rl_dataset_builder*`, `rl_trading*`, `rl_walk_forward`,
  `rl_hyperopt`, `rl_dreamer`, `rl_*`). Keep only the thin `SignalSource`
  interface seam so an *external* RL signal can be replayed later.
- React UI, blog auto-posting, LangGraph internals (replaced by a native state
  machine), Telegram bot logic beyond a simple HTTP POST notifier.

---

## 🧭 Agent behavior rules

1. **Before any task**, read `progress.md` to learn current state.
2. **Detailed rules** start at `.agent/` :
   - Tech-stack constraints (allowed libs): `.agent/tech_stack.md`
   - Code conventions (C11 + Rust): `.agent/conventions.md`
   - Roadmap / milestones: `.agent/roadmap.md`
3. **Architecture** is described in `architecture.md`.
4. **Module contracts / algorithms to reproduce** are in `docs/MODULE_SPECS.md`.
5. **Python → target mapping** is in `docs/PORTING_MAP.md`.
6. **Benchmark method** is in `docs/BENCHMARK_PLAN.md`. Establish the Python
   baseline before interpreting or publishing C/Rust performance results.
7. **Every task produces a plan md** in `docs/plans/` *before* coding
   (`docs/plans/README.md` explains the discipline). **After every task, update
   `progress.md`.** Record durable technical decisions in `MEMORY.md`.
8. **Do not add packages** outside `.agent/tech_stack.md`. If a new lib is truly
   needed, update `tech_stack.md` in the same change with the rationale.
9. Never hardcode absolute paths or system binary paths in tests; use relative
   paths from the test file and `PATH`-resolved tools.

---

## 🔐 Security rules (carried over from the source system)

1. **Order authority is isolated.** Only the order-execution path (`edge` broker
   module) may place broker orders. Risk/blend/collector modules never call the broker.
2. **Risk rules are enforced below the signal layer.** Python-compatible values
   come only from validated portfolio config and cannot be overridden by a signal
   or strategy payload. See `docs/MODULE_SPECS.md §risk`.
3. **Paper trading is the default.** Real trading requires an explicit config flag
   plus a separate confirmation step.
   C/Rust broker challengers are paper/replay-only and cannot load real credentials;
   a deployment enables exactly one real-order adapter after separate safety approval.
4. **No secrets in code or logs.** Credentials come from environment / `.env` only.
5. **Fail safe:** on missing data → HOLD; on component failure → hold positions.
6. **Single decision source:** risk/blend/sizing exist only in C. Numeric speedup
   alone never authorizes migration or production broker replacement.

---

## 🔧 Build & test entrypoints

- C core: CMake. `cmake --preset dev && cmake --build build && ctest --test-dir build`
- Rust datalake/network adapters: `cargo build --workspace && cargo test --workspace`
- Parity harness (vs. Python): see `docs/BUILD_AND_TEST.md §parity`.
- Format/lint gates: `clang-format`, `clang-tidy` (C); `rustfmt`, `clippy` (Rust).

Full details: `docs/BUILD_AND_TEST.md`.

---

## 🔗 Quick reference

- Module contracts & exact algorithms: `docs/MODULE_SPECS.md`
- Benchmark contract and result schema: `docs/BENCHMARK_PLAN.md`
- Python→target file map: `docs/PORTING_MAP.md`
- Edge (async) C-vs-Rust options: `docs/EDGE_OPTIONS.md`
- DB schema: `docs/DATABASE_TABLES.md` (mirrors source `docs/db/`)
- Dependency map: `docs/DEPENDENCY_MAP.md`
- Phase-by-phase build guides: `docs/plans/phase-0-scaffold.md` … `phase-5-e2e.md`

*Handoff document — implementation is performed by the receiving developer/AI.*
