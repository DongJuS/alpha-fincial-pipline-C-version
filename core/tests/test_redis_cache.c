/* Redis cache driver integration test (hiredis). Requires a running Redis.
 * Connects to 127.0.0.1:$ALPHA_REDIS_PORT (default 56379, the docker-compose
 * mapping). If Redis is unreachable the test skips (returns success) unless
 * ALPHA_RUN_REDIS_INTEGRATION is set, in which case it is a hard failure. */
#include "alpha/market_hours.h"
#include "alpha/redis_async.h"
#include "alpha/redis_cache.h"
#include "unity.h"

#include <hiredis/hiredis.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static alpha_redis_t *g_redis;
static const char *const SCOPE = "test_paper";
static int g_port;

typedef struct {
    int socket_fd;
    alpha_redis_watch_t watch;
    int connected;
    int connection_events;
    int replied;
    alpha_err_t status;
    long long integer;
    void *user_data;
    char text[128];
} async_state_t;

static void async_watch(void *ctx, int socket_fd, alpha_redis_watch_t watch) {
    async_state_t *state = ctx;
    state->socket_fd = socket_fd;
    state->watch = watch;
}
static void async_timer(void *ctx, long timeout_ms) {
    (void)ctx;
    (void)timeout_ms;
}
static void async_connected(void *ctx, alpha_err_t status) {
    async_state_t *state = ctx;
    state->connection_events++;
    state->connected = status == ALPHA_OK;
    state->status = status;
}
static void async_replied(void *ctx, alpha_err_t status, const char *text, long long integer,
                          void *user_data) {
    async_state_t *state = ctx;
    state->replied = 1;
    state->status = status;
    state->integer = integer;
    state->user_data = user_data;
    if (text != NULL)
        (void)snprintf(state->text, sizeof(state->text), "%s", text);
}

static void drive_async(alpha_redis_async_t *redis, async_state_t *state, int *done) {
    for (int attempt = 0; attempt < 100 && !*done; ++attempt) {
        struct pollfd fd = {.fd = state->socket_fd,
                            .events =
                                (short)(((state->watch & ALPHA_REDIS_WATCH_READ) ? POLLIN : 0) |
                                        ((state->watch & ALPHA_REDIS_WATCH_WRITE) ? POLLOUT : 0))};
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, fd.fd);
        TEST_ASSERT_GREATER_THAN_INT(0, poll(&fd, 1, 100));
        alpha_redis_watch_t ready = ALPHA_REDIS_WATCH_NONE;
        if ((fd.revents & POLLIN) != 0)
            ready = (alpha_redis_watch_t)(ready | ALPHA_REDIS_WATCH_READ);
        if ((fd.revents & POLLOUT) != 0)
            ready = (alpha_redis_watch_t)(ready | ALPHA_REDIS_WATCH_WRITE);
        TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_async_service(redis, ready));
    }
    TEST_ASSERT_TRUE(*done);
}

static redisContext *raw_connect(void) {
    redisContext *ctx = redisConnect("127.0.0.1", g_port);
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_EQUAL_INT(0, ctx->err);
    return ctx;
}

static void raw_ok(redisContext *ctx, const char *command) {
    redisReply *reply = redisCommand(ctx, command);
    TEST_ASSERT_NOT_NULL(reply);
    TEST_ASSERT_EQUAL_INT(REDIS_REPLY_STATUS, reply->type);
    TEST_ASSERT_EQUAL_STRING("OK", reply->str);
    freeReplyObject(reply);
}

static int year_for_epoch(int64_t epoch) {
    const time_t value = (time_t)(epoch + ALPHA_KST_OFFSET_SECONDS);
    struct tm broken_down;
    TEST_ASSERT_NOT_NULL(gmtime_r(&value, &broken_down));
    return broken_down.tm_year + 1900;
}

static void calendar_set(redisContext *ctx, int year, const char *json) {
    redisReply *reply = redisCommand(ctx, "SET krx:holidays:%d %s EX 3600", year, json);
    TEST_ASSERT_NOT_NULL(reply);
    TEST_ASSERT_EQUAL_INT(REDIS_REPLY_STATUS, reply->type);
    freeReplyObject(reply);
}

void setUp(void) {}
void tearDown(void) {}

static void test_latest_tick_roundtrip(void) {
    const char *payload = "{\"price\":50000,\"ts\":1}";
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_set_latest_tick(g_redis, "005930", payload));

    char buf[128];
    bool has_value = false;
    TEST_ASSERT_EQUAL_INT(
        ALPHA_OK, alpha_redis_get_latest_tick(g_redis, "005930", buf, sizeof(buf), &has_value));
    TEST_ASSERT_TRUE(has_value);
    TEST_ASSERT_EQUAL_STRING(payload, buf);

    bool missing = true;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_get_latest_tick(g_redis, "ZZZ_NO_SUCH_TICKER", buf,
                                                                sizeof(buf), &missing));
    TEST_ASSERT_FALSE(missing);
}

static void test_breaker_lockout_lifecycle(void) {
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_clear_breaker_lockout(g_redis, SCOPE));

    bool locked = true;
    int64_t expires_at = 0;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK,
                          alpha_redis_is_breaker_locked(g_redis, SCOPE, &locked, &expires_at));
    TEST_ASSERT_FALSE(locked);

    const int64_t now = (int64_t)time(NULL);
    const int64_t expiry = now + 3600;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_set_breaker_lockout(g_redis, SCOPE, now, expiry));

    locked = false;
    expires_at = 0;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK,
                          alpha_redis_is_breaker_locked(g_redis, SCOPE, &locked, &expires_at));
    TEST_ASSERT_TRUE(locked);
    TEST_ASSERT_EQUAL_INT64(expiry, expires_at);

    redisContext *raw = raw_connect();
    redisReply *ttl = redisCommand(raw, "TTL hard_stop:lockout:%s", SCOPE);
    TEST_ASSERT_NOT_NULL(ttl);
    TEST_ASSERT_EQUAL_INT(REDIS_REPLY_INTEGER, ttl->type);
    TEST_ASSERT_INT_WITHIN(1, 3600, ttl->integer);
    freeReplyObject(ttl);

    /* The lockout is server state and survives a client restart. */
    alpha_redis_close(g_redis);
    g_redis = NULL;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_connect("127.0.0.1", g_port, &g_redis));
    locked = false;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK,
                          alpha_redis_is_breaker_locked(g_redis, SCOPE, &locked, &expires_at));
    TEST_ASSERT_TRUE(locked);

    /* Fail closed: a past/sentinel expiry is rejected and the prior lockout stays. */
    TEST_ASSERT_EQUAL_INT(ALPHA_ERR_INVALID_ARG,
                          alpha_redis_set_breaker_lockout(g_redis, SCOPE, now, now));
    locked = false;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_is_breaker_locked(g_redis, SCOPE, &locked, NULL));
    TEST_ASSERT_TRUE(locked);

    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_clear_breaker_lockout(g_redis, SCOPE));

    /* A corrupt/non-absolute value fails closed rather than becoming unlocked. */
    raw_ok(raw, "SET hard_stop:lockout:test_paper true EX 60");
    locked = false;
    TEST_ASSERT_EQUAL_INT(ALPHA_ERR_IO,
                          alpha_redis_is_breaker_locked(g_redis, SCOPE, &locked, NULL));
    TEST_ASSERT_TRUE(locked);
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_clear_breaker_lockout(g_redis, SCOPE));
    redisFree(raw);
}

static void test_calendar_driven_weekend_holiday_and_missing_fail_closed(void) {
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_clear_breaker_lockout(g_redis, SCOPE));
    const int64_t now = (int64_t)time(NULL);
    const int current_year = year_for_epoch(now);
    const int next_year = current_year + 1;
    redisContext *raw = raw_connect();
    calendar_set(raw, current_year, "[]");
    calendar_set(raw, next_year, "[]");

    int64_t expiry = 0;
    TEST_ASSERT_EQUAL_INT(
        ALPHA_OK, alpha_redis_set_breaker_lockout_next_session(g_redis, SCOPE, now, &expiry));
    TEST_ASSERT_EQUAL_INT64(alpha_next_trading_session_start(now), expiry);
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_clear_breaker_lockout(g_redis, SCOPE));

    /* Mark that otherwise-next session as a holiday; release must move later. */
    const time_t candidate =
        (time_t)(alpha_next_trading_session_start(now) + ALPHA_KST_OFFSET_SECONDS);
    struct tm candidate_tm;
    TEST_ASSERT_NOT_NULL(gmtime_r(&candidate, &candidate_tm));
    char holiday_json[32];
    TEST_ASSERT_TRUE(snprintf(holiday_json, sizeof(holiday_json), "[\"%04d-%02d-%02d\"]",
                              candidate_tm.tm_year + 1900, candidate_tm.tm_mon + 1,
                              candidate_tm.tm_mday) < (int)sizeof(holiday_json));
    calendar_set(raw, candidate_tm.tm_year + 1900, holiday_json);
    TEST_ASSERT_EQUAL_INT(
        ALPHA_OK, alpha_redis_set_breaker_lockout_next_session(g_redis, SCOPE, now, &expiry));
    TEST_ASSERT_TRUE(expiry > (int64_t)candidate - ALPHA_KST_OFFSET_SECONDS);

    /* Missing/stale calendar never replaces or clears the active lockout. */
    redisReply *deleted =
        redisCommand(raw, "DEL krx:holidays:%d krx:holidays:%d", current_year, next_year);
    TEST_ASSERT_NOT_NULL(deleted);
    freeReplyObject(deleted);
    TEST_ASSERT_EQUAL_INT(ALPHA_ERR_IO,
                          alpha_redis_set_breaker_lockout_next_session(g_redis, SCOPE, now, NULL));
    bool locked = false;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK,
                          alpha_redis_is_breaker_locked(g_redis, SCOPE, &locked, &expiry));
    TEST_ASSERT_TRUE(locked);
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_clear_breaker_lockout(g_redis, SCOPE));
    redisFree(raw);
}

static void test_hiredis_async_is_caller_loop_driven(void) {
    async_state_t state = {.socket_fd = -1};
    alpha_redis_async_t *redis = NULL;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_async_connect("127.0.0.1", g_port, async_watch,
                                                              async_timer, async_connected,
                                                              async_replied, &state, &redis));
    drive_async(redis, &state, &state.connected);
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, state.status);
    const char *args[] = {"PING"};
    const size_t lengths[] = {4};
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_async_command(redis, 1, args, lengths, NULL));
    drive_async(redis, &state, &state.replied);
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, state.status);
    TEST_ASSERT_EQUAL_STRING("PONG", state.text);
    alpha_redis_async_close(redis);
}

static void test_hiredis_async_typed_queue_disconnect_and_reconnect(void) {
    async_state_t state = {.socket_fd = -1};
    alpha_redis_async_t *redis = NULL;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_async_connect("127.0.0.1", g_port, async_watch,
                                                              async_timer, async_connected,
                                                              async_replied, &state, &redis));
    drive_async(redis, &state, &state.connected);

    int set_id = 11;
    state.replied = 0;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_async_set_latest_tick(redis, "ASYNC_TEST",
                                                                      "{\"price\":123}", &set_id));
    drive_async(redis, &state, &state.replied);
    TEST_ASSERT_EQUAL_PTR(&set_id, state.user_data);
    TEST_ASSERT_EQUAL_STRING("OK", state.text);

    int get_id = 12;
    state.replied = 0;
    state.text[0] = '\0';
    TEST_ASSERT_EQUAL_INT(ALPHA_OK,
                          alpha_redis_async_get_latest_tick(redis, "ASYNC_TEST", &get_id));
    drive_async(redis, &state, &state.replied);
    TEST_ASSERT_EQUAL_PTR(&get_id, state.user_data);
    TEST_ASSERT_EQUAL_STRING("{\"price\":123}", state.text);

    /* Queue ownership is deterministic even before the owner services readiness. */
    const char *ping[] = {"PING"};
    const size_t ping_lengths[] = {4};
    for (size_t index = 0; index < ALPHA_REDIS_ASYNC_MAX_PENDING; ++index) {
        TEST_ASSERT_EQUAL_INT(ALPHA_OK,
                              alpha_redis_async_command(redis, 1, ping, ping_lengths, NULL));
    }
    TEST_ASSERT_EQUAL_size_t(ALPHA_REDIS_ASYNC_MAX_PENDING, alpha_redis_async_pending(redis));
    TEST_ASSERT_EQUAL_INT(ALPHA_ERR_RANGE,
                          alpha_redis_async_command(redis, 1, ping, ping_lengths, NULL));
    while (alpha_redis_async_pending(redis) > 0) {
        state.replied = 0;
        drive_async(redis, &state, &state.replied);
    }

    /* Ask Redis for this connection id, then kill it from an independent client. */
    const char *client_id[] = {"CLIENT", "ID"};
    const size_t client_id_lengths[] = {6, 2};
    state.replied = 0;
    state.integer = 0;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK,
                          alpha_redis_async_command(redis, 2, client_id, client_id_lengths, NULL));
    drive_async(redis, &state, &state.replied);
    TEST_ASSERT_GREATER_THAN_INT64(0, state.integer);
    redisContext *raw = raw_connect();
    redisReply *killed = redisCommand(raw, "CLIENT KILL ID %lld", state.integer);
    TEST_ASSERT_NOT_NULL(killed);
    TEST_ASSERT_EQUAL_INT(REDIS_REPLY_INTEGER, killed->type);
    TEST_ASSERT_EQUAL_INT64(1, killed->integer);
    freeReplyObject(killed);
    redisFree(raw);

    for (int attempt = 0; attempt < 100 && state.connected; ++attempt) {
        struct pollfd descriptor = {.fd = state.socket_fd, .events = POLLIN | POLLOUT};
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, poll(&descriptor, 1, 100));
        alpha_redis_watch_t ready = ALPHA_REDIS_WATCH_NONE;
        if ((descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            ready = (alpha_redis_watch_t)(ready | ALPHA_REDIS_WATCH_READ);
        }
        if ((descriptor.revents & POLLOUT) != 0) {
            ready = (alpha_redis_watch_t)(ready | ALPHA_REDIS_WATCH_WRITE);
        }
        (void)alpha_redis_async_service(redis, ready);
    }
    TEST_ASSERT_FALSE(state.connected);
    TEST_ASSERT_EQUAL_INT(-1, alpha_redis_async_socket(redis));
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_async_reconnect(redis));
    drive_async(redis, &state, &state.connected);
    state.replied = 0;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK,
                          alpha_redis_async_get_latest_tick(redis, "ASYNC_TEST", &get_id));
    drive_async(redis, &state, &state.replied);
    TEST_ASSERT_EQUAL_STRING("{\"price\":123}", state.text);
    alpha_redis_async_close(redis);
}

int main(void) {
    const char *require = getenv("ALPHA_RUN_REDIS_INTEGRATION");
    const char *port_str = getenv("ALPHA_REDIS_PORT");
    g_port = port_str != NULL ? atoi(port_str) : 56379;

    if (alpha_redis_connect("127.0.0.1", g_port, &g_redis) != ALPHA_OK) {
        if (require != NULL) {
            fprintf(stderr, "ALPHA_RUN_REDIS_INTEGRATION set but Redis 127.0.0.1:%d unreachable\n",
                    g_port);
            return 1;
        }
        fprintf(stderr,
                "Redis unavailable; skipping (set ALPHA_RUN_REDIS_INTEGRATION to require)\n");
        return 0;
    }

    UNITY_BEGIN();
    RUN_TEST(test_latest_tick_roundtrip);
    RUN_TEST(test_breaker_lockout_lifecycle);
    RUN_TEST(test_calendar_driven_weekend_holiday_and_missing_fail_closed);
    RUN_TEST(test_hiredis_async_is_caller_loop_driven);
    RUN_TEST(test_hiredis_async_typed_queue_disconnect_and_reconnect);
    const int failures = UNITY_END();
    alpha_redis_close(g_redis);
    return failures;
}
