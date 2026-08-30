# Python baseline pin (Phase 0)

## Goal

Pin the reference Python revision and capture enough source, dependency, and host
metadata to make later fixtures and benchmark results attributable. Provide a
validator so incomplete or accidentally changed metadata fails before use.

## Files

- `bench/baseline/python-reference.json`
- `tools/capture_baseline.py`
- `tools/verify_baseline.py`
- `tests/test_baseline_tools.py`
- `progress.md`

## Contract

Satisfies the baseline metadata gate in `docs/BENCHMARK_PLAN.md §1` and the
baseline-first portion of `docs/plans/phase-0-scaffold.md`. The reference source
is `../alpha-financial-pipeline`; this task does not generate goldens or claim a
reproducible performance baseline.

## Tests

- Capture metadata from a temporary Git repository and verify its SHA/checksums.
- Reject malformed metadata and metadata with a mismatched dependency checksum.
- Validate the committed reference record against the adjacent Python repository.

## Risks / open questions

- The Python repository has no exact dependency lock; its requirements checksum
  is recorded, but baseline execution remains blocked until resolved packages are
  frozen.
- No canonical schema migration or fixture dataset is present, so both versions
  remain explicitly unresolved and block golden/performance publication.
