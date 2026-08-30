#include "alpha/alpha.h"
#include "unity.h"
#include "yyjson.h"

#include <fenv.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_public_contract(void) {
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_initialize());
    TEST_ASSERT_EQUAL_INT(FE_TONEAREST, fegetround());
    TEST_ASSERT_EQUAL_INT(1, alpha_abi_version());
    TEST_ASSERT_EQUAL_INT64(INT64_C(10000000), ALPHA_PAPER_TRADING_INITIAL_CAPITAL);
    TEST_ASSERT_EQUAL_INT(40, ALPHA_MAX_TICKERS_PER_WS);
    TEST_ASSERT_EQUAL_INT(10, ALPHA_MAX_SCREENED_TICKERS);
    TEST_ASSERT_EQUAL_INT(100, ALPHA_DEFAULT_COLLECTOR_DAILY_LIMIT);
    TEST_ASSERT_EQUAL_INT(3, ALPHA_BACKTEST_SLIPPAGE_BPS);
    TEST_ASSERT_EQUAL_INT(ALPHA_SIGNAL_HOLD, 2);
    TEST_ASSERT_EQUAL_DOUBLE(0.015, ALPHA_BACKTEST_COMMISSION_RATE_PCT);
    TEST_ASSERT_EQUAL_DOUBLE(0.18, ALPHA_BACKTEST_TAX_RATE_PCT);
    TEST_ASSERT_EQUAL_DOUBLE(2.0, ALPHA_SCREENER_VOLUME_SURGE_RATIO);
    TEST_ASSERT_EQUAL_DOUBLE(3.0, ALPHA_SCREENER_CHANGE_PCT_THRESHOLD);
}

static void test_yyjson_round_trip(void) {
    const char *input = "{\"signal\":\"HOLD\",\"confidence\":0.5}";
    yyjson_doc *document = yyjson_read(input, strlen(input), 0);
    TEST_ASSERT_NOT_NULL(document);
    yyjson_val *root = yyjson_doc_get_root(document);
    TEST_ASSERT_EQUAL_STRING("HOLD", yyjson_get_str(yyjson_obj_get(root, "signal")));

    yyjson_mut_doc *output_document = yyjson_doc_mut_copy(document, NULL);
    TEST_ASSERT_NOT_NULL(output_document);
    char *output = yyjson_mut_write(output_document, 0, NULL);
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "\"signal\":\"HOLD\""));

    free(output);
    yyjson_mut_doc_free(output_document);
    yyjson_doc_free(document);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_public_contract);
    RUN_TEST(test_yyjson_round_trip);
    return UNITY_END();
}
