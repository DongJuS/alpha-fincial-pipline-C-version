# P3 MVP-2 benchmark and parity harness

## Goal

Add a fail-closed, language-neutral harness for the `redis-hot-path` and
`db-read-write` stop-gate workloads. It runs Python/C/Rust command adapters at
concurrency 1/8/32 for 30 order-rotated trials, validates per-trial parity and
errors/drops, emits the normative result schema, and calculates a deterministic
95% bootstrap confidence interval. It does not create performance results until
real adapters and shared services are supplied.

## Files

- `bench/fixtures/driver-workloads.json` and `bench/fixtures/manifest.json`
- `bench/run_driver_mvp2.py`, `bench/evaluate_mvp2.py`
- `tests/test_driver_benchmark.py`
- this plan

## Contract

Follow `docs/BENCHMARK_PLAN.md` §§1, 4–7 and the MVP-2 gate in
`docs/plans/MVP_ROADMAP.md`. Python remains the pinned baseline; C and optional
Rust adapters consume the same immutable operation fixture and shared service
configuration. Driver outputs must preserve exact operation IDs and values.

## Tests

- Reject missing variants, wrong trial count/order, parity mismatch, drops,
  errors, dirty/unknown source identity, and invalid adapter output.
- Verify concurrency 1/8/32, 30 Latin rotation trials, result schema/statistics,
  deterministic bootstrap interval, and fail-closed GO/NO-GO evaluation.
- Run Python unit tests and formatting/syntax checks.

## Risks / open questions

The current Rust workspace has no Redis/SQL driver implementation. Therefore
Rust is supported by the harness but remains unavailable until an authorized P3
network challenger exists. No synthetic timing may be committed as evidence.
