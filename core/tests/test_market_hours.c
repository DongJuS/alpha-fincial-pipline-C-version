/* KRX trading-calendar: golden parity for the weekend-only next-session
 * (transcribed from Python market_hours.next_trading_day_start) plus unit
 * coverage of the holiday-aware fail-closed variant. Paths relative to repo root. */
#include "alpha/alpha.h"
#include "alpha/date.h"
#include "alpha/market_hours.h"
#include "unity.h"
#include "yyjson.h"

#include <stdio.h>

#define GOLDEN_PATH "core/tests/golden/market-hours-cases.json"

static yyjson_doc *g_golden;

void setUp(void) {}
void tearDown(void) {}

static int64_t session_start(int64_t epoch_day) {
    return epoch_day * INT64_C(86400) + 9 * 3600 - ALPHA_KST_OFFSET_SECONDS;
}

static void test_weekend_only_parity(void) {
    yyjson_val *cases =
        yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(g_golden), "result"), "weekend_only");
    yyjson_val *item = NULL;
    yyjson_arr_iter iter = yyjson_arr_iter_with(cases);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        const int64_t from_epoch = yyjson_get_sint(yyjson_obj_get(item, "from_epoch"));
        const int64_t expected = yyjson_get_sint(yyjson_obj_get(item, "expected_epoch"));
        TEST_ASSERT_EQUAL_INT64(expected, alpha_next_trading_session_start(from_epoch));
    }
}

static void test_holiday_aware_skips_and_fails_closed(void) {
    int64_t friday = 0;
    int64_t monday = 0;
    int64_t tuesday = 0;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_date_parse("2024-01-05", &friday));
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_date_parse("2024-01-08", &monday));
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_date_parse("2024-01-09", &tuesday));
    const int64_t from_epoch = friday * INT64_C(86400) + 12 * 3600 - ALPHA_KST_OFFSET_SECONDS;

    /* No holidays: matches the weekend-only result (Monday 09:00 KST). */
    TEST_ASSERT_EQUAL_INT64(session_start(monday), alpha_next_trading_session_start_holiday_aware(
                                                       from_epoch, NULL, 0, tuesday));

    /* Monday is a holiday and the calendar covers Tuesday: roll to Tuesday. */
    const int64_t holidays[1] = {monday};
    TEST_ASSERT_EQUAL_INT64(session_start(tuesday), alpha_next_trading_session_start_holiday_aware(
                                                        from_epoch, holidays, 1, tuesday));

    /* Monday is a holiday but the calendar ends before it: fail closed. */
    TEST_ASSERT_EQUAL_INT64(
        ALPHA_SESSION_CALENDAR_INSUFFICIENT,
        alpha_next_trading_session_start_holiday_aware(from_epoch, holidays, 1, monday - 1));
}

int main(void) {
    if (alpha_initialize() != ALPHA_OK) {
        return 1;
    }
    g_golden = yyjson_read_file(GOLDEN_PATH, 0, NULL, NULL);
    if (g_golden == NULL) {
        fprintf(stderr, "cannot open golden (run from repo root)\n");
        return 1;
    }
    UNITY_BEGIN();
    RUN_TEST(test_weekend_only_parity);
    RUN_TEST(test_holiday_aware_skips_and_fails_closed);
    const int failures = UNITY_END();
    yyjson_doc_free(g_golden);
    return failures;
}
