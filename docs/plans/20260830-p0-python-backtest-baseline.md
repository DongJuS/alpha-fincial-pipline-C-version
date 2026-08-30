# Python backtest fixture, golden, and baseline (Phase 0)

## Goal

Create the canonical 1,000-bar `backtest-small` fixture, generate a golden from
the pinned Python implementation, and record a controlled Python 3.11 baseline.
The workload must remain reproducible without resolving unrelated API/ML packages.

## Files

- `bench/fixtures/backtest-small.json`, `bench/fixtures/manifest.json`
- `core/tests/golden/backtest-small.json`
- `bench/baseline/python-backtest-lock.json`
- `bench/run_python_backtest.py`
- `bench/results/20260830/backtest-small-python.json`
- `tools/generate_backtest_fixture.py`, `tools/generate_python_golden.py`
- `tests/test_python_baseline.py`, `progress.md`, `MEMORY.md`

## Contract

Satisfies `docs/BENCHMARK_PLAN.md` §§4–7 for `backtest-small` and the Python
sources named by `docs/MODULE_SPECS.md §2–3`. The exact source commit and source
file checksums are pinned. Python 3.11 is taken from the reference Dockerfile.

## Tests

- Fixture generation is deterministic and manifest checksums validate.
- Golden metadata and payload checksums validate against a fresh Python run.
- Benchmark output has ten or more measured trials, parity passed, no errors or
  dropped work, and the required result-schema fields.

## Risks / open questions

- The source repository lacks a canonical DB migration set. This numeric workload
  does not access DB/cache/network, so schema remains an explicit blocker only for
  later schema-dependent fixtures and results.
- The global requirements file is range-based. This workload imports only Python
  standard-library modules; the lock pins CPython 3.11 and all imported source files.
