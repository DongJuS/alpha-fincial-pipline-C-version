# P3 PostgreSQL contract and recovery closure

## Goal

Close the thin MVP-2 libpq operation contract: deterministic position fixture
cleanup, exact Python `check_total_exposure` output semantics, and reusable
pipeline behavior after a statement abort and its following sync marker.

## Files

- `core/include/alpha/postgres.h`
- `core/platform/db/postgres.c`
- `core/tests/test_postgres.c`
- `docs/plans/20260831-p3-postgres-contract-recovery.md`

## Contract

Match pinned Python `src/db/queries.py::save_position` for positive quantities
and `src/utils/aggregate_risk.py::check_total_exposure`: sum only positive
positions, use all positive positions as the AUM denominator, count default
strategy identity, and round exposure percentage to two decimal places. Keep
nonblocking libpq pipeline ownership in the caller's event loop and preserve
FIFO callback identity through `PGRES_FATAL_ERROR`,
`PGRES_PIPELINE_ABORTED`, and `PGRES_PIPELINE_SYNC`.

## Tests

- Integration round trip with fixed, transactionally isolated fixture data.
- Golden expected position/exposure values derived from the pinned Python
  formulas, including two-decimal rounding and zero-AUM behavior.
- Intentional constraint failure followed by an aborted request and then a
  successful request after the sync boundary.
- Bounded queue and invalid-input tests; format and clang-tidy.

## Risks / open questions

The thin P3 API intentionally keeps Python's delete-on-nonpositive operation out
of scope. The test must never delete unrelated rows and must not depend on
pre-existing database contents when calculating its golden denominator.
