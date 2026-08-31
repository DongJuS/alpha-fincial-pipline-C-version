#ifndef ALPHA_REDIS_CACHE_H
#define ALPHA_REDIS_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "alpha/errors.h"

/* Redis cache driver (hiredis) — src/utils/redis_client.py key/TTL parity plus
 * the C breaker-lockout safety enhancement. This unit uses a synchronous hiredis
 * connection for correctness and integration testing; the libwebsockets-loop
 * async integration and hot-path benchmark are a later P3 unit. */

/* latest_ticks TTL (seconds), matching Python TTL_LATEST_TICKS. */
#define ALPHA_TTL_LATEST_TICKS 60

typedef struct alpha_redis alpha_redis_t;

/* Connect to Redis. Returns ALPHA_ERR_IO on connection failure. */
alpha_err_t alpha_redis_connect(const char *host, int port, alpha_redis_t **out);
void alpha_redis_close(alpha_redis_t *redis);

/* latest_ticks:{ticker} = payload, expiring in ALPHA_TTL_LATEST_TICKS seconds.
 * Key pattern matches Python KEY_LATEST_TICKS ("redis:cache:latest_ticks:{ticker}"). */
alpha_err_t alpha_redis_set_latest_tick(alpha_redis_t *redis, const char *ticker,
                                        const char *payload);

/* Read latest_ticks:{ticker}. Missing key -> *has_value=false. On hit the value
 * is copied into `out` (truncated to n-1 + NUL) and *has_value=true. */
alpha_err_t alpha_redis_get_latest_tick(alpha_redis_t *redis, const char *ticker, char *out,
                                        size_t n, bool *has_value);

/* Breaker lockout (C safety enhancement): store the absolute expires_at (epoch
 * seconds) at risk:breaker_lockout:{scope} with TTL = expires_at - now_epoch, so
 * the flag self-clears exactly at the next trading session. Rejects
 * expires_at <= now_epoch with ALPHA_ERR_INVALID_ARG (never sets a stale/past
 * lockout, so a fail-closed calendar sentinel keeps any existing lockout). */
alpha_err_t alpha_redis_set_breaker_lockout(alpha_redis_t *redis, const char *scope,
                                            int64_t now_epoch, int64_t expires_at_epoch);

/* Check the breaker lockout. Present -> *locked=true and *expires_at_out is the
 * stored epoch; absent/expired -> *locked=false. */
alpha_err_t alpha_redis_is_breaker_locked(alpha_redis_t *redis, const char *scope, bool *locked,
                                          int64_t *expires_at_out);

/* Remove the breaker lockout (test/admin helper). */
alpha_err_t alpha_redis_clear_breaker_lockout(alpha_redis_t *redis, const char *scope);

#endif
