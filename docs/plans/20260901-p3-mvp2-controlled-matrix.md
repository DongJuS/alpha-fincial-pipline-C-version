# P3 MVP-2 controlled matrix and stop-gate decision

## Goal

Produce the missing parity-eligible 30-trial Python/C/selected-Rust driver
matrix on one Linux host, evaluate both candidates, and record the MVP-2 stop
gate without advancing P4/P5 unless the required decision permits it.

## Preconditions

- Reconfirm the pinned Python source, fixture/schema locks, adapter artifacts,
  shared PostgreSQL/Redis versions, and clean worktrees before timing.
- Build C optimized without fast-math and Rust `--release --locked`; run the
  exact artifacts through the real-service parity smoke before measurements.
- Record host/kernel/CPU/RAM/toolchain and reject material background-load,
  error, drop, retry, saturation, parity, or configuration drift.

## Implementation

- Add a manual Linux benchmark workflow that runs all variants sequentially in
  Latin-rotated order against the same pinned services and uploads raw evidence.
- Harden the orchestrator/evaluator so every result binds the exact artifacts,
  terminal golden, operation-latency samples, resource samples, and environment.
- Run 30 trials for both workloads at concurrency 1/8/32, retain all 18 result
  JSON files, and create separate deterministic C and Rust gate reports.

## Verification and handoff

- Unit-test malformed/missing/cross-host and resource aggregation failures.
- Validate every result and gate report, review absolute measurements, run
  format/lint/full CI, and commit the evidence on an independent branch.
- Update `progress.md` and durable decisions in `MEMORY.md`, merge to `main`,
  push, and verify hosted main CI. A failed criterion records NO-GO; it is never
  rewritten as missing work or used to bypass the MVP-2 stop gate.
