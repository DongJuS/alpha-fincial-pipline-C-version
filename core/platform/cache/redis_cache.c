#include "alpha/redis_cache.h"
#include "alpha/market_hours.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hiredis/hiredis.h>

struct alpha_redis {
    redisContext *ctx;
};

#define KEY_CAP 96
#define SECONDS_PER_DAY INT64_C(86400)
#define MAX_HOLIDAYS 512

static alpha_err_t breaker_key(const char *scope, char key[KEY_CAP]) {
    if (scope == NULL || scope[0] == '\0') {
        return ALPHA_ERR_INVALID_ARG;
    }
    return snprintf(key, KEY_CAP, "hard_stop:lockout:%s", scope) >= KEY_CAP ? ALPHA_ERR_RANGE
                                                                            : ALPHA_OK;
}

static bool parse_epoch(const char *text, int64_t *value) {
    char *end = NULL;
    errno = 0;
    const long long parsed = strtoll(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed <= 0) {
        return false;
    }
    *value = (int64_t)parsed;
    return true;
}

static alpha_err_t redis_server_time(alpha_redis_t *redis, int64_t *epoch_out) {
    redisReply *reply = redisCommand(redis->ctx, "TIME");
    if (reply == NULL || reply->type != REDIS_REPLY_ARRAY || reply->elements != 2 ||
        reply->element[0]->type != REDIS_REPLY_STRING ||
        !parse_epoch(reply->element[0]->str, epoch_out)) {
        if (reply != NULL) {
            freeReplyObject(reply);
        }
        return ALPHA_ERR_IO;
    }
    freeReplyObject(reply);
    return ALPHA_OK;
}

/* Gregorian civil-date conversions, with epoch day 0 == 1970-01-01. */
static int64_t days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153U * (month + (month > 2 ? (unsigned)-3 : 9U)) + 2U) / 5U + day - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static int civil_year_from_days(int64_t epoch_day) {
    int64_t z = epoch_day + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    int year = (int)yoe + (int)era * 400;
    const unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    const unsigned mp = (5U * doy + 2U) / 153U;
    const unsigned month = mp + (mp < 10 ? 3U : (unsigned)-9);
    return year + (month <= 2);
}

static const char *skip_space(const char *cursor) {
    while (isspace((unsigned char)*cursor)) {
        ++cursor;
    }
    return cursor;
}

static bool parse_fixed_decimal(const char *text, size_t digits, unsigned *value) {
    unsigned parsed = 0;
    for (size_t i = 0; i < digits; ++i) {
        if (!isdigit((unsigned char)text[i])) {
            return false;
        }
        parsed = parsed * 10U + (unsigned)(text[i] - '0');
    }
    *value = parsed;
    return true;
}

static bool parse_holiday_json(const char *json, int expected_year, int64_t *days, size_t *count) {
    const char *cursor = skip_space(json);
    size_t used = 0;
    if (*cursor++ != '[') {
        return false;
    }
    cursor = skip_space(cursor);
    while (*cursor != ']') {
        unsigned year = 0;
        unsigned month = 0;
        unsigned day = 0;
        if (used == MAX_HOLIDAYS || strlen(cursor) < 12 || cursor[0] != '"' || cursor[5] != '-' ||
            cursor[8] != '-' || cursor[11] != '"' || !parse_fixed_decimal(cursor + 1, 4, &year) ||
            !parse_fixed_decimal(cursor + 6, 2, &month) ||
            !parse_fixed_decimal(cursor + 9, 2, &day) || year != (unsigned)expected_year ||
            month < 1 || month > 12 || day < 1 || day > 31) {
            return false;
        }
        const int64_t epoch_day = days_from_civil((int)year, month, day);
        if (civil_year_from_days(epoch_day) != (int)year ||
            (used > 0 && epoch_day <= days[used - 1])) {
            return false;
        }
        days[used++] = epoch_day;
        cursor = skip_space(cursor + 12);
        if (*cursor == ',') {
            cursor = skip_space(cursor + 1);
        } else if (*cursor != ']') {
            return false;
        }
    }
    cursor = skip_space(cursor + 1);
    if (*cursor != '\0') {
        return false;
    }
    *count = used;
    return true;
}

static alpha_err_t load_calendar(alpha_redis_t *redis, int year, int64_t *days, size_t *count) {
    char key[KEY_CAP];
    if (snprintf(key, sizeof(key), "krx:holidays:%d", year) >= (int)sizeof(key)) {
        return ALPHA_ERR_RANGE;
    }
    redisReply *reply = redisCommand(redis->ctx, "GET %s", key);
    alpha_err_t status = ALPHA_ERR_IO;
    if (reply != NULL && reply->type == REDIS_REPLY_STRING &&
        parse_holiday_json(reply->str, year, days, count)) {
        redisReply *ttl = redisCommand(redis->ctx, "TTL %s", key);
        if (ttl != NULL && ttl->type == REDIS_REPLY_INTEGER && ttl->integer > 0) {
            status = ALPHA_OK;
        }
        if (ttl != NULL) {
            freeReplyObject(ttl);
        }
    }
    if (reply != NULL) {
        freeReplyObject(reply);
    }
    return status;
}

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
    alpha_err_t status = breaker_key(scope, key);
    if (status != ALPHA_OK) {
        return status;
    }
    int64_t server_now = 0;
    status = redis_server_time(redis, &server_now);
    if (status != ALPHA_OK) {
        return status;
    }
    if (expires_at_epoch <= server_now) {
        return ALPHA_ERR_INVALID_ARG;
    }
    redisReply *reply = redisCommand(redis->ctx, "SET %s %lld EXAT %lld", key,
                                     (long long)expires_at_epoch, (long long)expires_at_epoch);
    return reply_is_ok(reply) ? ALPHA_OK : ALPHA_ERR_IO;
}

alpha_err_t alpha_redis_set_breaker_lockout_next_session(alpha_redis_t *redis, const char *scope,
                                                         int64_t now_epoch,
                                                         int64_t *expires_at_out) {
    if (redis == NULL || scope == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    int64_t server_now = 0;
    alpha_err_t status = redis_server_time(redis, &server_now);
    if (status != ALPHA_OK) {
        return status;
    }
    /* Reject materially stale/future caller timestamps. The calendar decision
     * is still made from the supplied event time, while Redis owns expiry. */
    if (now_epoch < server_now - 5 || now_epoch > server_now + 5) {
        return ALPHA_ERR_INVALID_ARG;
    }
    const int64_t kst_day = (now_epoch + ALPHA_KST_OFFSET_SECONDS) / SECONDS_PER_DAY;
    const int year = civil_year_from_days(kst_day);
    int64_t holidays[MAX_HOLIDAYS];
    size_t count = 0;
    status = load_calendar(redis, year, holidays, &count);
    if (status != ALPHA_OK) {
        return status;
    }
    int64_t calendar_max = days_from_civil(year, 12, 31);
    int64_t expiry =
        alpha_next_trading_session_start_holiday_aware(now_epoch, holidays, count, calendar_max);
    if (expiry == ALPHA_SESSION_CALENDAR_INSUFFICIENT) {
        int64_t next_holidays[MAX_HOLIDAYS];
        size_t next_count = 0;
        status = load_calendar(redis, year + 1, next_holidays, &next_count);
        if (status != ALPHA_OK || count + next_count > MAX_HOLIDAYS) {
            return ALPHA_ERR_IO;
        }
        memcpy(holidays + count, next_holidays, next_count * sizeof(*holidays));
        count += next_count;
        calendar_max = days_from_civil(year + 1, 12, 31);
        expiry = alpha_next_trading_session_start_holiday_aware(now_epoch, holidays, count,
                                                                calendar_max);
    }
    if (expiry == ALPHA_SESSION_CALENDAR_INSUFFICIENT || expiry <= server_now) {
        return ALPHA_ERR_IO;
    }
    status = alpha_redis_set_breaker_lockout(redis, scope, now_epoch, expiry);
    if (status == ALPHA_OK && expires_at_out != NULL) {
        *expires_at_out = expiry;
    }
    return status;
}

alpha_err_t alpha_redis_is_breaker_locked(alpha_redis_t *redis, const char *scope, bool *locked,
                                          int64_t *expires_at_out) {
    if (redis == NULL || scope == NULL || locked == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    char key[KEY_CAP];
    alpha_err_t status = breaker_key(scope, key);
    if (status != ALPHA_OK) {
        return status;
    }
    redisReply *reply = redisCommand(redis->ctx, "GET %s", key);
    if (reply == NULL) {
        return ALPHA_ERR_IO;
    }
    status = ALPHA_OK;
    if (reply->type == REDIS_REPLY_NIL) {
        *locked = false;
    } else if (reply->type == REDIS_REPLY_STRING) {
        *locked = true;
        int64_t expiry = 0;
        int64_t server_now = 0;
        if (!parse_epoch(reply->str, &expiry) ||
            redis_server_time(redis, &server_now) != ALPHA_OK) {
            status = ALPHA_ERR_IO;
        } else {
            redisReply *ttl_reply = redisCommand(redis->ctx, "TTL %s", key);
            if (ttl_reply == NULL || ttl_reply->type != REDIS_REPLY_INTEGER ||
                ttl_reply->integer <= 0 || expiry <= server_now ||
                expiry - server_now < ttl_reply->integer ||
                expiry - server_now > ttl_reply->integer + 1) {
                status = ALPHA_ERR_IO;
            } else if (expires_at_out != NULL) {
                *expires_at_out = expiry;
            }
            if (ttl_reply != NULL) {
                freeReplyObject(ttl_reply);
            }
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
    alpha_err_t status = breaker_key(scope, key);
    if (status != ALPHA_OK) {
        return status;
    }
    redisReply *reply = redisCommand(redis->ctx, "DEL %s", key);
    if (reply == NULL) {
        return ALPHA_ERR_IO;
    }
    freeReplyObject(reply);
    return ALPHA_OK;
}
