# P3 Redis/HTTP recovery closure

## Goal

Close the Redis and HTTP recovery gaps required before the MVP-2 driver gate:
bounded async Redis commands with typed latest-tick helpers and explicit reconnect,
plus deterministic HTTP saturation, timeout, non-2xx, transport-failure, and reuse
evidence.

## Files

- `core/include/alpha/redis_async.h`
- `core/platform/cache/redis_async.c`
- `core/tests/test_redis_cache.c`
- `core/include/alpha/http_client.h`
- `core/platform/http/http_client.c`
- `core/tests/test_http_client.c`

## Contract

Match `docs/MODULE_SPECS.md` cache keys and the pinned Python
`src/utils/redis_client.py` latest-tick behavior. Keep hiredis and libcurl-multi
caller-driven: no internal poll, sleep, thread, or event loop. Bound outstanding
ownership and preserve caller `user_data` through success and failure.

## Tests

- Redis: typed set/get, deterministic saturation, server-side disconnect,
  explicit reconnect, and post-reconnect reuse against Docker Redis.
- HTTP: bounded saturation, timeout, non-2xx, refused transport, completion
  identity, and successful reuse after every failure.
- Run focused integration, format, clang-tidy, and the driver test group.

## Risks / open questions

Disconnect callbacks may be delivered while hiredis invalidates its context;
the adapter must never dereference that context afterward. Reconnect is explicit
so the LWS owner controls retry policy and backoff.
