/* Redis cache driver integration test (hiredis). Requires a running Redis.
 * Connects to 127.0.0.1:$ALPHA_REDIS_PORT (default 56379, the docker-compose
 * mapping). If Redis is unreachable the test skips (returns success) unless
 * ALPHA_RUN_REDIS_INTEGRATION is set, in which case it is a hard failure. */
#include "alpha/redis_cache.h"
#include "unity.h"

#include <stdio.h>
#include <stdlib.h>

static alpha_redis_t *g_redis;
static const char *const SCOPE = "test_paper";

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

    const int64_t now = 1000000;
    const int64_t expiry = now + 3600;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_set_breaker_lockout(g_redis, SCOPE, now, expiry));

    locked = false;
    expires_at = 0;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK,
                          alpha_redis_is_breaker_locked(g_redis, SCOPE, &locked, &expires_at));
    TEST_ASSERT_TRUE(locked);
    TEST_ASSERT_EQUAL_INT64(expiry, expires_at);

    /* Fail closed: a past/sentinel expiry is rejected and the prior lockout stays. */
    TEST_ASSERT_EQUAL_INT(ALPHA_ERR_INVALID_ARG,
                          alpha_redis_set_breaker_lockout(g_redis, SCOPE, now, now));
    locked = false;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_is_breaker_locked(g_redis, SCOPE, &locked, NULL));
    TEST_ASSERT_TRUE(locked);

    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_redis_clear_breaker_lockout(g_redis, SCOPE));
}

int main(void) {
    const char *require = getenv("ALPHA_RUN_REDIS_INTEGRATION");
    const char *port_str = getenv("ALPHA_REDIS_PORT");
    const int port = port_str != NULL ? atoi(port_str) : 56379;

    if (alpha_redis_connect("127.0.0.1", port, &g_redis) != ALPHA_OK) {
        if (require != NULL) {
            fprintf(stderr, "ALPHA_RUN_REDIS_INTEGRATION set but Redis 127.0.0.1:%d unreachable\n",
                    port);
            return 1;
        }
        fprintf(stderr,
                "Redis unavailable; skipping (set ALPHA_RUN_REDIS_INTEGRATION to require)\n");
        return 0;
    }

    UNITY_BEGIN();
    RUN_TEST(test_latest_tick_roundtrip);
    RUN_TEST(test_breaker_lockout_lifecycle);
    const int failures = UNITY_END();
    alpha_redis_close(g_redis);
    return failures;
}
