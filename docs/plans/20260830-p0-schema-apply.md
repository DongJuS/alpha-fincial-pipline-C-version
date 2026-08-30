# Apply pinned Python schema bootstrap (Phase 0)

## Goal

Prove the pinned Python schema bootstrap applies idempotently to an empty shared
PostgreSQL service without copying, importing, or reinterpreting its DDL.

## Files

- `tools/apply_python_schema.py`
- `tests/test_schema_apply.py`
- `.github/workflows/ci.yml`, `db/README.md`, `progress.md`, `MEMORY.md`

## Contract

Satisfies the Phase 0 clean schema/fixture setup gate. The tool first validates
`python-schema-lock.json`, then parses the literal `CREATE_TABLES` assignment from
the pinned `scripts/db/init_db.py` and streams those exact statements to `psql`.
It does not import the Python application or create target-owned migrations.

## Tests

- Unit-test literal extraction, expected statement count, required in-scope
  tables, and rejection of non-literal code.
- Against the pinned PostgreSQL container, apply the bootstrap twice and verify
  required tables exist after both runs.
- Run the complete baseline and shared-service test suites.

## Risks / open questions

- Runtime-created date partitions are outside `CREATE_TABLES`; later fixture
  tasks must create only the partitions their pinned datasets require.
- This gate validates the source bootstrap, not production migration history.
