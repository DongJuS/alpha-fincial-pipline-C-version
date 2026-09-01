# P3 MVP-2 C and Rust driver adapters

## Goal

Implement the two missing real adapters required by the fail-closed MVP-2
orchestrator: C11 on the pinned LWS/libpq/hiredis runtime and Rust on pinned
Tokio/SQLx/fred. Match the real Python adapter's terminal-state checksums.

## Contract

- Execute `redis-hot-path` and `db-read-write` from the committed fixture at
  concurrency 1/8/32, one shared connection, truthful depth 1, and zero retries.
- Keep connection setup, warmup, targeted fixture cleanup, and terminal reads
  outside timing. Hash actual service state; never hash expected fixture output.
- C uses only the pinned LWS owner. Rust uses the locked Tokio/SQLx/fred stack.
- Emit exact adapter schema, measured CPU/RSS/errors/drops, compiler/runtime flags,
  and stable completion IDs. A mismatch or missing dependency fails closed.

## Verification

- Unit/integration tests compare both result hashes with the real Python adapter.
- Run all six workload/concurrency cells once before the 30-trial matrix.
- Format/lint (`clang-tidy`, `rustfmt`, `clippy`), sanitizer/release builds, full
  repository tests, hosted CI, then the controlled order-rotated benchmark.

## Stop gate

Only the committed 30-trial result set and deterministic bootstrap evaluator may
record GO/NO-GO. Until then P3 remains in progress and P4/P5 stay stopped.

## Result (2026-09-01)

Python, C, and selected Rust adapters pass the strict 18-cell real-service smoke
in hosted Linux CI (`33459011368`). The controlled 30-trial matrix and bootstrap
decision remain pending, so this plan has not crossed the stop gate.
