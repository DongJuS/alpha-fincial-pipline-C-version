# Shared PostgreSQL and Redis test services (Phase 0)

## Goal

Provide pinned PostgreSQL 15 and Redis 7 containers with deterministic ports,
health checks, bounded resources, and a schema-independent readiness verifier.

## Files

- `docker-compose.yml`
- `db/README.md`
- `tools/verify_services.py`
- `tests/test_service_config.py`
- `.github/workflows/ci.yml`, `progress.md`

## Contract

Satisfies the shared-service health portion of `docs/plans/phase-0-scaffold.md`.
Infrastructure remains excluded from port targets and timed regions. No database
schema is invented while the reference migration source is unresolved.

## Tests

- `docker compose config` accepts the file and pins major/minor image tags.
- Containers become healthy; verifier executes PostgreSQL `SELECT 1` and Redis PING.
- Unit tests reject mutable image tags, missing health checks, and exposed default ports.
- CI integration job repeats container health/readiness checks.

## Risks / open questions

- The compose file proves service readiness only, not application schema parity.
- Fixed loopback ports `55432`/`56379` avoid common local defaults but can still be
  overridden through environment variables if another process owns them.
