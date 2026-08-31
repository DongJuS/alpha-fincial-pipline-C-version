# P2 risk-batch benchmark (Phase 2)

## Goal

Close the missing performance evidence for the implemented risk/sizing decision
core with an identical deterministic 100,000-snapshot Python/C workload whose
outputs are checksum-cross-checked and whose C artifact passes risk parity first.

## Files

- `core/apps/risk_runner.c`, `core/CMakeLists.txt`
- `bench/run_python_risk.py`, `bench/run_c_risk.py`
- `bench/results/20260831/risk-batch-{python,c}.json`
- `progress.md`, `MEMORY.md`

## Contract

`docs/BENCHMARK_PLAN.md` risk-batch and `docs/plans/phase-2-risk-portfolio.md`.
The batch exercises L1 stop/take, L3 daily breaker, and paper/real max-position
decisions. L2 weakest-two remains covered by golden parity; its variable-sized
snapshot benchmark belongs with the completed indicators/ranking P2 batch.

## Tests

- Build the exact `bench` preset and run `ctest -R risk` before timing C.
- Require identical decision checksum for Python and C.
- Emit 10 raw samples, median/p95/min/max/stddev, CPU time, RSS, and zero errors.
- Run dev/bench tests plus format/clang-tidy.

## Risks / open questions

- The Python gate arithmetic is the same reviewed transcription used by the risk
golden because the live methods are async/DB-coupled; this limitation remains
explicit and prevents claiming live-I/O performance.
