# Shared test services

`docker-compose.yml` supplies PostgreSQL 15 and Redis 7 for integration tests.
They are shared dependencies, not port targets, and are never included in timed
regions. Data uses temporary filesystems and disappears when the containers stop.

Start and verify:

```sh
docker compose up -d --wait
python3 tools/verify_services.py
ALPHA_RUN_SCHEMA_APPLY=1 python3 -m unittest tests.test_schema_apply -v
docker compose down
```

PostgreSQL is exposed only on loopback port `55432` and Redis on `56379` by
default. `ALPHA_POSTGRES_PORT` and `ALPHA_REDIS_PORT` may override them. The
checked-in credentials are test-only and must never be reused outside this local
ephemeral environment.

The schema gate validates `bench/baseline/python-schema-lock.json`, extracts the
literal `CREATE_TABLES` list directly from the pinned Python `init_db.py`, and
applies its exact SQL twice. No target-owned schema is inferred from prose. The
source bootstrap's runtime-created date partitions are intentionally deferred
until a versioned fixture requires them.
