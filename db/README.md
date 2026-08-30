# Shared test services

`docker-compose.yml` supplies PostgreSQL 15 and Redis 7 for integration tests.
They are shared dependencies, not port targets, and are never included in timed
regions. Data uses temporary filesystems and disappears when the containers stop.

Start and verify:

```sh
docker compose up -d --wait
python3 tools/verify_services.py
docker compose down
```

PostgreSQL is exposed only on loopback port `55432` and Redis on `56379` by
default. `ALPHA_POSTGRES_PORT` and `ALPHA_REDIS_PORT` may override them. The
checked-in credentials are test-only and must never be reused outside this local
ephemeral environment.

No schema migration is applied yet. The pinned Python repository does not contain
the schema source referenced by the handoff documentation; creating replacement
tables from prose would invent behavior. Schema-dependent integration remains
blocked until the canonical migrations are identified.
