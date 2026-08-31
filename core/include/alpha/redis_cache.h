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
 * seconds) at the Python-owned hard_stop:lockout:{scope} key and use Redis EXAT,
 * so expiry does not depend on caller clock arithmetic. `now_epoch` is retained
 * as a contract guard; Redis server time is also checked before the write. */
alpha_err_t alpha_redis_set_breaker_lockout(alpha_redis_t *redis, const char *scope,
                                            int64_t now_epoch, int64_t expires_at_epoch);

/* Load fresh `krx:holidays:{year}` JSON calendars from Redis, compute the next
 * KRX session, and install the lockout atomically with respect to its expiry.
 * Missing, expired, malformed, or insufficient calendars return an error and
 * never clear/shorten an existing lockout (fail closed). */
alpha_err_t alpha_redis_set_breaker_lockout_next_session(alpha_redis_t *redis, const char *scope,
                                                         int64_t now_epoch,
                                                         int64_t *expires_at_out);

/* Check the breaker lockout. Stored values must be a canonical, future absolute
 * epoch consistent with the key TTL. Malformed/inconsistent values fail closed:
 * *locked remains true and ALPHA_ERR_IO is returned. */
alpha_err_t alpha_redis_is_breaker_locked(alpha_redis_t *redis, const char *scope, bool *locked,
                                          int64_t *expires_at_out);

/* Remove the breaker lockout (test/admin helper). */
alpha_err_t alpha_redis_clear_breaker_lockout(alpha_redis_t *redis, const char *scope);

#endif
