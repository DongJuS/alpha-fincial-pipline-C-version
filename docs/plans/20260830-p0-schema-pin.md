# Python schema/migration identity pin (Phase 0)

## Goal

Identify the pinned Python revision's authoritative database bootstrap/migration
state without inventing a schema, record a reproducible version/checksum, and add
a hard-failing validator so schema-dependent fixtures can be created later.

## Files

- `bench/baseline/python-schema-lock.json`
- `tools/capture_schema_lock.py`, `tools/verify_schema_lock.py`
- `tests/test_schema_lock.py`
- `bench/baseline/python-reference.json`, `progress.md`, `MEMORY.md`

## Contract

Satisfies the schema/migration identity requirement in `docs/BENCHMARK_PLAN.md
§1` and the Phase 0 baseline gate. The pinned Python tree is read-only. If it has
no migration framework/version table, the lock identifies its actual bootstrap
sources by Python SHA and content checksums rather than assigning a fictitious
migration number.

## Tests

- Verify every locked source path and SHA-256 against the pinned Python tree.
- Reject wrong Python revisions, missing files, changed bytes, and malformed lock
  records.
- Run the full Python tool test suite; no schema is applied in this task.

## Risks / open questions

- A content-addressed bootstrap identity is not proof that the scripts apply to a
  clean database; schema application remains a separate shared-service task.
- Hosted CI cannot be confirmed until `origin/main` is reachable.
