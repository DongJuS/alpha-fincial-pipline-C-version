# AGENTS.md — How to work in this repo (dev-AI guide)

This repo is a **benchmark-driven specification handoff**. The operating Python
system in `../alpha-financial-pipeline` remains the reference. Selected backend
paths are ported to **C (base)**, with optional **Rust** edge variants, to compare
parity, performance, resource use, and operating complexity. Read `CLAUDE.md`
first, then this file.

## Working model

1. Pick the current phase from `docs/plans/` (start at `phase-0-scaffold.md`).
   P0 must pin and measure the Python baseline before implementation claims.
2. **Write a plan md** for the task in `docs/plans/` before coding (see
   `docs/plans/README.md`). One topic per plan, filename `YYYYMMDD-topic-slug.md`.
3. Implement against the contracts in `docs/MODULE_SPECS.md`. Do not invent
   behavior — if a contract is ambiguous, read the referenced Python source in
   `../alpha-financial-pipeline` and match it.
4. Write tests as you go (CMocka for C, `cargo test` for Rust). Every in-scope
   module must have a unit test and, where a Python counterpart exists, a
   **golden-file parity test** (`docs/BUILD_AND_TEST.md §parity`).
5. **Update `progress.md`** when the task is done. Record durable decisions in
   `MEMORY.md`.
6. Follow `docs/BENCHMARK_PLAN.md`. Do not publish a speedup for a workload whose
   parity gate fails, and do not compare variants under different service/config
   conditions.

## Phase order (see `.agent/roadmap.md`)

- **P0** pin Python baseline + benchmark harness + C scaffold
- **P1** C numeric vertical slice + first Python/C benchmark
- **P2** C decision core + parity/performance benchmarks
- **P3** C nonblocking DB/Redis drivers using shared infrastructure
- **P4** pure-C async edges + selected Rust datalake; optional Rust network comparisons
- **P5** E2E + consolidated Python/C/(selected Rust) report

## Non-negotiables

- C is the deterministic base. Rust implements the selected S3/Parquet datalake
  adapter and is optional for network-edge comparisons, behind stable C-owned
  data/decision contracts.
- The Python collector remains in service; do not port FDR/yfinance/KRX/KIS
  collection. C processing starts at normalization.
- Infrastructure and Infisical are excluded from the port and timed workloads.
- Python is the pinned behavioral/performance baseline throughout the project.
- Order authority stays in the broker edge module only.
- Risk/blend/sizing remain one C implementation. Broker challengers are paper-only;
  exactly one separately approved adapter may have real-order authority.
- Risk defaults match Python and are enforced below signals; only validated
  portfolio config may change them.
- No dependency outside `.agent/tech_stack.md`.
- Keep AI-read docs (`progress.md`, `architecture.md`, `MEMORY.md`) under ~200 lines.
- Numeric speedup alone is not a migration criterion; follow the production-impact
  decision rule in `docs/BENCHMARK_PLAN.md §9`.

## Definition of done (per module)

- Contract in `docs/MODULE_SPECS.md` satisfied.
- Unit tests pass; parity test (if applicable) matches Python within tolerance.
- Relevant benchmark runs under the controlled protocol and emits a result file;
  no speed claim is made unless parity passed first.
- `clang-tidy`/`clippy` clean; formatted.
- `progress.md` updated.
