/* Nonblocking libpq integration test. Requires the migrated Docker PostgreSQL.
 * Unavailable service is a local skip unless ALPHA_RUN_POSTGRES_INTEGRATION is
 * set; hosted CI must set it so a missing service cannot become a green test. */
#include "alpha/postgres.h"
#include "alpha/round.h"
#include "unity.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libpq-fe.h>

typedef struct {
    int calls;
    alpha_pg_result_t result;
} capture_t;

static alpha_pg_t *g_db;
static const char *g_conninfo;

void setUp(void) {}
void tearDown(void) {}

static void capture_result(const alpha_pg_result_t *result, void *user_data) {
    capture_t *capture = user_data;
    capture->calls++;
    capture->result = *result;
}

static bool service_once(alpha_pg_t *db, int timeout_ms) {
    struct pollfd descriptor = {
        .fd = alpha_pg_socket(db),
        .events = (short)((alpha_pg_wants_read(db) ? POLLIN : 0) |
                          (alpha_pg_wants_write(db) ? POLLOUT : 0)),
    };
    if (descriptor.fd < 0 || descriptor.events == 0) {
        return false;
    }
    const int ready = poll(&descriptor, 1, timeout_ms);
    if (ready < 0) {
        return false;
    }
    if (ready == 0) {
        return true;
    }
    return alpha_pg_service(db, (descriptor.revents & POLLIN) != 0,
                            (descriptor.revents & POLLOUT) != 0) == ALPHA_OK;
}

static bool drive_until_ready(alpha_pg_t *db) {
    for (int i = 0; i < 100 && alpha_pg_state(db) == ALPHA_PG_CONNECTING; ++i) {
        if (!service_once(db, 50)) {
            return false;
        }
    }
    return alpha_pg_state(db) == ALPHA_PG_READY;
}

static bool drive_until_idle(alpha_pg_t *db) {
    for (int i = 0; i < 200 && !alpha_pg_is_idle(db); ++i) {
        if (!service_once(db, 50)) {
            return false;
        }
    }
    return alpha_pg_is_idle(db);
}

static bool cleanup_ticker(const char *ticker) {
    PGconn *admin = PQconnectdb(g_conninfo);
    if (PQstatus(admin) != CONNECTION_OK) {
        PQfinish(admin);
        return false;
    }
    const char *params[] = {ticker};
    PGresult *result = PQexecParams(admin, "DELETE FROM portfolio_positions WHERE ticker=$1", 1,
                                    NULL, params, NULL, NULL, 0);
    const bool ok = PQresultStatus(result) == PGRES_COMMAND_OK;
    PQclear(result);
    PQfinish(admin);
    return ok;
}

static alpha_pg_position_t position(const char *ticker, const char *strategy, int quantity,
                                    int current_price) {
    alpha_pg_position_t p = {
        .quantity = quantity, .avg_price = 1000, .current_price = current_price, .is_paper = true};
    (void)snprintf(p.ticker, sizeof(p.ticker), "%s", ticker);
    (void)snprintf(p.name, sizeof(p.name), "P3 libpq fixture");
    (void)snprintf(p.account_scope, sizeof(p.account_scope), "paper");
    (void)snprintf(p.strategy_id, sizeof(p.strategy_id), "%s", strategy);
    return p;
}

static void test_bounded_pipeline_roundtrip_and_aggregate(void) {
    const char *ticker = "ZPGGOLDEN";
    const char *baseline_ticker = "ZPGABSENT";
    const alpha_pg_position_t one = position(ticker, "p3a", 2, 1010);
    const alpha_pg_position_t two = position(ticker, "p3b", 3, 1040);
    capture_t baseline = {0}, first = {0}, second = {0}, aggregate = {0};

    TEST_ASSERT_TRUE(cleanup_ticker(ticker));
    TEST_ASSERT_TRUE(cleanup_ticker(baseline_ticker));
    TEST_ASSERT_EQUAL_INT(
        ALPHA_OK, alpha_pg_query_exposure(g_db, baseline_ticker, capture_result, &baseline));
    TEST_ASSERT_TRUE_MESSAGE(drive_until_idle(g_db), alpha_pg_error(g_db));
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, baseline.result.status);
    TEST_ASSERT_EQUAL_INT64(0, baseline.result.value.exposure.total_quantity);
    TEST_ASSERT_EQUAL_INT64(0, baseline.result.value.exposure.total_market_value);

    TEST_ASSERT_EQUAL_size_t(2, alpha_pg_capacity(g_db));
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_pg_upsert_position(g_db, &one, capture_result, &first));
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_pg_upsert_position(g_db, &two, capture_result, &second));
    TEST_ASSERT_EQUAL_size_t(2, alpha_pg_pending(g_db));
    TEST_ASSERT_EQUAL_INT(ALPHA_ERR_RANGE,
                          alpha_pg_query_exposure(g_db, ticker, capture_result, &aggregate));

    const bool first_batch_idle = drive_until_idle(g_db);
    if (!first_batch_idle) {
        fprintf(stderr, "pipeline stalled: pending=%zu first=%d second=%d error=%s\n",
                alpha_pg_pending(g_db), first.calls, second.calls, alpha_pg_error(g_db));
    }
    TEST_ASSERT_TRUE_MESSAGE(first_batch_idle, alpha_pg_error(g_db));
    TEST_ASSERT_EQUAL_INT(1, first.calls);
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, first.result.status);
    TEST_ASSERT_EQUAL_STRING(ticker, first.result.value.position.ticker);
    TEST_ASSERT_EQUAL_INT32(2, first.result.value.position.quantity);
    TEST_ASSERT_EQUAL_INT(1, second.calls);
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, second.result.status);
    TEST_ASSERT_EQUAL_STRING("p3b", second.result.value.position.strategy_id);

    TEST_ASSERT_EQUAL_INT(ALPHA_OK,
                          alpha_pg_query_exposure(g_db, ticker, capture_result, &aggregate));
    TEST_ASSERT_TRUE_MESSAGE(drive_until_idle(g_db), alpha_pg_error(g_db));
    TEST_ASSERT_EQUAL_INT(1, aggregate.calls);
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, aggregate.result.status);
    TEST_ASSERT_EQUAL_INT64(5, aggregate.result.value.exposure.total_quantity);
    TEST_ASSERT_EQUAL_INT64(5140, aggregate.result.value.exposure.total_market_value);
    TEST_ASSERT_EQUAL_INT32(2, aggregate.result.value.exposure.strategy_count);
    const int64_t expected_aum = baseline.result.value.exposure.total_aum + 5140;
    TEST_ASSERT_EQUAL_INT64(expected_aum, aggregate.result.value.exposure.total_aum);
    TEST_ASSERT_DOUBLE_WITHIN(0.000001,
                              alpha_round_dp((double)5140 / (double)expected_aum * 100.0, 2),
                              aggregate.result.value.exposure.exposure_pct);
    TEST_ASSERT_TRUE(cleanup_ticker(ticker));
}

static void test_pipeline_abort_sync_and_reuse(void) {
    const char *bad_ticker = "ZPGBAD";
    const char *aborted_ticker = "ZPGABORT";
    alpha_pg_position_t bad = position(bad_ticker, "p3", 1, 1000);
    const alpha_pg_position_t aborted = position(aborted_ticker, "p3", 1, 1000);
    capture_t bad_result = {0}, aborted_result = {0}, recovered_result = {0};
    (void)snprintf(bad.account_scope, sizeof(bad.account_scope), "invalid");

    TEST_ASSERT_TRUE(cleanup_ticker(bad_ticker));
    TEST_ASSERT_TRUE(cleanup_ticker(aborted_ticker));
    TEST_ASSERT_EQUAL_INT(ALPHA_OK,
                          alpha_pg_upsert_position(g_db, &bad, capture_result, &bad_result));
    TEST_ASSERT_EQUAL_INT(
        ALPHA_OK, alpha_pg_upsert_position(g_db, &aborted, capture_result, &aborted_result));
    TEST_ASSERT_TRUE_MESSAGE(drive_until_idle(g_db), alpha_pg_error(g_db));
    TEST_ASSERT_EQUAL_INT(1, bad_result.calls);
    TEST_ASSERT_EQUAL_INT(ALPHA_ERR_DB, bad_result.result.status);
    TEST_ASSERT_EQUAL_INT(1, aborted_result.calls);
    TEST_ASSERT_EQUAL_INT(ALPHA_ERR_DB, aborted_result.result.status);
    TEST_ASSERT_EQUAL_INT(ALPHA_PG_READY, alpha_pg_state(g_db));

    TEST_ASSERT_EQUAL_INT(
        ALPHA_OK, alpha_pg_upsert_position(g_db, &aborted, capture_result, &recovered_result));
    TEST_ASSERT_TRUE_MESSAGE(drive_until_idle(g_db), alpha_pg_error(g_db));
    TEST_ASSERT_EQUAL_INT(1, recovered_result.calls);
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, recovered_result.result.status);
    TEST_ASSERT_EQUAL_STRING(aborted_ticker, recovered_result.result.value.position.ticker);
    TEST_ASSERT_TRUE(cleanup_ticker(aborted_ticker));
}

static void test_input_guards(void) {
    alpha_pg_position_t invalid = position("ZINVALID", "p3", 0, 1000);
    TEST_ASSERT_EQUAL_INT(ALPHA_ERR_INVALID_ARG,
                          alpha_pg_upsert_position(g_db, &invalid, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(ALPHA_ERR_INVALID_ARG, alpha_pg_query_exposure(g_db, "", NULL, NULL));
}

int main(void) {
    const char *required = getenv("ALPHA_RUN_POSTGRES_INTEGRATION");
    g_conninfo = getenv("ALPHA_POSTGRES_URL");
    if (g_conninfo == NULL) {
        g_conninfo = "host=127.0.0.1 port=55432 dbname=alpha_test user=alpha_test "
                     "password=alpha_test_only";
    }
    if (alpha_pg_connect_start(g_conninfo, 2, &g_db) != ALPHA_OK || !drive_until_ready(g_db)) {
        const char *error = g_db != NULL ? alpha_pg_error(g_db) : "allocation/start failure";
        if (required != NULL) {
            fprintf(stderr, "PostgreSQL integration required but unavailable: %s\n", error);
            alpha_pg_close(g_db);
            return 1;
        }
        fprintf(stderr, "PostgreSQL unavailable; skipping: %s\n", error);
        alpha_pg_close(g_db);
        return 0;
    }

    UNITY_BEGIN();
    RUN_TEST(test_bounded_pipeline_roundtrip_and_aggregate);
    RUN_TEST(test_pipeline_abort_sync_and_reuse);
    RUN_TEST(test_input_guards);
    const int failures = UNITY_END();
    alpha_pg_close(g_db);
    return failures;
}
