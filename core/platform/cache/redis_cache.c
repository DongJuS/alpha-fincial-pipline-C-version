#include "alpha/redis_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hiredis/hiredis.h>

struct alpha_redis {
    redisContext *ctx;
};

#define KEY_CAP 96

alpha_err_t alpha_redis_connect(const char *host, int port, alpha_redis_t **out) {
    if (host == NULL || out == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    redisContext *ctx = redisConnect(host, port);
    if (ctx == NULL || ctx->err) {
        if (ctx != NULL) {
            redisFree(ctx);
        }
        return ALPHA_ERR_IO;
    }
    alpha_redis_t *redis = (alpha_redis_t *)malloc(sizeof(*redis));
    if (redis == NULL) {
        redisFree(ctx);
        return ALPHA_ERR_IO;
    }
    redis->ctx = ctx;
    *out = redis;
    return ALPHA_OK;
}

void alpha_redis_close(alpha_redis_t *redis) {
    if (redis == NULL) {
        return;
    }
    if (redis->ctx != NULL) {
        redisFree(redis->ctx);
    }
    free(redis);
}

/* True when a reply is a "+OK" status. Frees the reply. */
static bool reply_is_ok(redisReply *reply) {
    if (reply == NULL) {
        return false;
    }
    const bool ok = (bool)(reply->type == REDIS_REPLY_STATUS && strcmp(reply->str, "OK") == 0);
    freeReplyObject(reply);
    return ok;
}

alpha_err_t alpha_redis_set_latest_tick(alpha_redis_t *redis, const char *ticker,
                                        const char *payload) {
    if (redis == NULL || ticker == NULL || payload == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    char key[KEY_CAP];
    if (snprintf(key, sizeof(key), "redis:cache:latest_ticks:%s", ticker) >= (int)sizeof(key)) {
        return ALPHA_ERR_RANGE;
    }
    redisReply *reply =
        redisCommand(redis->ctx, "SET %s %s EX %d", key, payload, ALPHA_TTL_LATEST_TICKS);
    return reply_is_ok(reply) ? ALPHA_OK : ALPHA_ERR_IO;
}

alpha_err_t alpha_redis_get_latest_tick(alpha_redis_t *redis, const char *ticker, char *out,
                                        size_t n, bool *has_value) {
    if (redis == NULL || ticker == NULL || out == NULL || has_value == NULL || n == 0) {
        return ALPHA_ERR_INVALID_ARG;
    }
    char key[KEY_CAP];
    if (snprintf(key, sizeof(key), "redis:cache:latest_ticks:%s", ticker) >= (int)sizeof(key)) {
        return ALPHA_ERR_RANGE;
    }
    redisReply *reply = redisCommand(redis->ctx, "GET %s", key);
    if (reply == NULL) {
        return ALPHA_ERR_IO;
    }
    alpha_err_t status = ALPHA_OK;
    if (reply->type == REDIS_REPLY_NIL) {
        *has_value = false;
    } else if (reply->type == REDIS_REPLY_STRING) {
        size_t len = reply->len;
        if (len >= n) {
            len = n - 1;
        }
        memcpy(out, reply->str, len);
        out[len] = '\0';
        *has_value = true;
    } else {
        status = ALPHA_ERR_IO;
    }
    freeReplyObject(reply);
    return status;
}

alpha_err_t alpha_redis_set_breaker_lockout(alpha_redis_t *redis, const char *scope,
                                            int64_t now_epoch, int64_t expires_at_epoch) {
    if (redis == NULL || scope == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    /* Never set a lockout that is already expired (a fail-closed calendar
     * sentinel is negative and rejected here, keeping any existing lockout). */
    if (expires_at_epoch <= now_epoch) {
        return ALPHA_ERR_INVALID_ARG;
    }
    char key[KEY_CAP];
    if (snprintf(key, sizeof(key), "risk:breaker_lockout:%s", scope) >= (int)sizeof(key)) {
        return ALPHA_ERR_RANGE;
    }
    const long long ttl = (long long)(expires_at_epoch - now_epoch);
    redisReply *reply =
        redisCommand(redis->ctx, "SET %s %lld EX %lld", key, (long long)expires_at_epoch, ttl);
    return reply_is_ok(reply) ? ALPHA_OK : ALPHA_ERR_IO;
}

alpha_err_t alpha_redis_is_breaker_locked(alpha_redis_t *redis, const char *scope, bool *locked,
                                          int64_t *expires_at_out) {
    if (redis == NULL || scope == NULL || locked == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    char key[KEY_CAP];
    if (snprintf(key, sizeof(key), "risk:breaker_lockout:%s", scope) >= (int)sizeof(key)) {
        return ALPHA_ERR_RANGE;
    }
    redisReply *reply = redisCommand(redis->ctx, "GET %s", key);
    if (reply == NULL) {
        return ALPHA_ERR_IO;
    }
    alpha_err_t status = ALPHA_OK;
    if (reply->type == REDIS_REPLY_NIL) {
        *locked = false;
    } else if (reply->type == REDIS_REPLY_STRING) {
        *locked = true;
        if (expires_at_out != NULL) {
            *expires_at_out = (int64_t)strtoll(reply->str, NULL, 10);
        }
    } else {
        status = ALPHA_ERR_IO;
    }
    freeReplyObject(reply);
    return status;
}

alpha_err_t alpha_redis_clear_breaker_lockout(alpha_redis_t *redis, const char *scope) {
    if (redis == NULL || scope == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    char key[KEY_CAP];
    if (snprintf(key, sizeof(key), "risk:breaker_lockout:%s", scope) >= (int)sizeof(key)) {
        return ALPHA_ERR_RANGE;
    }
    redisReply *reply = redisCommand(redis->ctx, "DEL %s", key);
    if (reply == NULL) {
        return ALPHA_ERR_IO;
    }
    freeReplyObject(reply);
    return ALPHA_OK;
}
