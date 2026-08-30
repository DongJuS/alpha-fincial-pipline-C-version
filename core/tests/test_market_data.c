/* market_data unit + golden parity. Golden/fixture paths are relative to the
 * repository root (ctest WORKING_DIRECTORY). */
#include "alpha/alpha.h"
#include "alpha/market_data.h"
#include "unity.h"
#include "yyjson.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FIXTURE_PATH "bench/fixtures/market-data-cases.json"
#define GOLDEN_PATH "core/tests/golden/market-data-cases.json"

static yyjson_doc *g_fixture;
static yyjson_doc *g_golden;

void setUp(void) {}
void tearDown(void) {}

static int approx(double a, double b) { return fabs(a - b) <= 1e-9 * fmax(1.0, fabs(b)); }

static alpha_market_t market_from_str(const char *name) {
    if (strcmp(name, "KOSPI") == 0) {
        return ALPHA_MARKET_KOSPI;
    }
    if (strcmp(name, "KOSDAQ") == 0) {
        return ALPHA_MARKET_KOSDAQ;
    }
    if (strcmp(name, "NYSE") == 0) {
        return ALPHA_MARKET_NYSE;
    }
    return ALPHA_MARKET_NASDAQ;
}

static const char *market_str(alpha_market_t market) {
    switch (market) {
    case ALPHA_MARKET_KOSPI:
        return "KOSPI";
    case ALPHA_MARKET_KOSDAQ:
        return "KOSDAQ";
    case ALPHA_MARKET_NYSE:
        return "NYSE";
    default:
        return "NASDAQ";
    }
}

static yyjson_val *fx(const char *key) {
    return yyjson_obj_get(yyjson_doc_get_root(g_fixture), key);
}
static yyjson_val *gd(const char *key) {
    return yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(g_golden), "result"), key);
}

static void test_to_instrument_id(void) {
    yyjson_val *cases = fx("to_instrument_id");
    yyjson_val *golden = gd("to_instrument_id");
    size_t i = 0;
    yyjson_val *item = NULL;
    yyjson_arr_iter iter = yyjson_arr_iter_with(cases);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        char out[32];
        const alpha_market_t market =
            market_from_str(yyjson_get_str(yyjson_obj_get(item, "market")));
        TEST_ASSERT_EQUAL_INT(ALPHA_OK,
                              alpha_to_instrument_id(yyjson_get_str(yyjson_obj_get(item, "ticker")),
                                                     market, out, sizeof(out)));
        const char *want =
            yyjson_get_str(yyjson_obj_get(yyjson_arr_get(golden, i), "instrument_id"));
        TEST_ASSERT_EQUAL_STRING(want, out);
        i += 1;
    }
}

static void test_from_instrument_id(void) {
    yyjson_val *cases = fx("from_instrument_id");
    yyjson_val *golden = gd("from_instrument_id");
    size_t i = 0;
    yyjson_val *item = NULL;
    yyjson_arr_iter iter = yyjson_arr_iter_with(cases);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        char raw[32];
        alpha_market_t market;
        TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_from_instrument_id(
                                            yyjson_get_str(yyjson_obj_get(item, "instrument_id")),
                                            raw, sizeof(raw), &market));
        yyjson_val *want = yyjson_arr_get(golden, i);
        TEST_ASSERT_EQUAL_STRING(yyjson_get_str(yyjson_obj_get(want, "raw")), raw);
        TEST_ASSERT_EQUAL_STRING(yyjson_get_str(yyjson_obj_get(want, "market")),
                                 market_str(market));
        i += 1;
    }
}

static void test_sanitize_change_pct(void) {
    yyjson_val *cases = fx("sanitize_change_pct");
    yyjson_val *golden = gd("sanitize_change_pct");
    size_t i = 0;
    yyjson_val *item = NULL;
    yyjson_arr_iter iter = yyjson_arr_iter_with(cases);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        bool has_value = false;
        double value = 0.0;
        TEST_ASSERT_EQUAL_INT(
            ALPHA_OK, alpha_sanitize_change_pct(yyjson_get_num(yyjson_obj_get(item, "value")),
                                                &has_value, &value));
        yyjson_val *want = yyjson_arr_get(golden, i);
        TEST_ASSERT_EQUAL_INT(yyjson_get_bool(yyjson_obj_get(want, "has_value")), has_value);
        if (has_value) {
            TEST_ASSERT_TRUE(approx(value, yyjson_get_num(yyjson_obj_get(want, "value"))));
        }
        i += 1;
    }
}

static void test_compute_change_pct(void) {
    yyjson_val *cases = fx("compute_change_pct");
    yyjson_val *golden = gd("compute_change_pct");
    size_t i = 0;
    yyjson_val *item = NULL;
    yyjson_arr_iter iter = yyjson_arr_iter_with(cases);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        yyjson_val *prev = yyjson_obj_get(item, "previous");
        const bool has_prev = !yyjson_is_null(prev);
        bool has_value = false;
        double value = 0.0;
        TEST_ASSERT_EQUAL_INT(
            ALPHA_OK,
            alpha_compute_change_pct(yyjson_get_num(yyjson_obj_get(item, "current")), has_prev,
                                     has_prev ? yyjson_get_num(prev) : 0.0, &has_value, &value));
        yyjson_val *want = yyjson_arr_get(golden, i);
        TEST_ASSERT_EQUAL_INT(yyjson_get_bool(yyjson_obj_get(want, "has_value")), has_value);
        if (has_value) {
            TEST_ASSERT_TRUE(approx(value, yyjson_get_num(yyjson_obj_get(want, "value"))));
        }
        i += 1;
    }
}

/* Non-finite inputs cannot be encoded in JSON goldens; cover them directly. */
static void test_non_finite_inputs_are_none(void) {
    bool has_value = true;
    double value = 0.0;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_sanitize_change_pct(INFINITY, &has_value, &value));
    TEST_ASSERT_FALSE(has_value);
    has_value = true;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_sanitize_change_pct(NAN, &has_value, &value));
    TEST_ASSERT_FALSE(has_value);
    has_value = true;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_compute_change_pct(NAN, true, 100.0, &has_value, &value));
    TEST_ASSERT_FALSE(has_value);
}

int main(void) {
    if (alpha_initialize() != ALPHA_OK) {
        return 1;
    }
    g_fixture = yyjson_read_file(FIXTURE_PATH, 0, NULL, NULL);
    g_golden = yyjson_read_file(GOLDEN_PATH, 0, NULL, NULL);
    if (g_fixture == NULL || g_golden == NULL) {
        fprintf(stderr, "cannot open fixture/golden (run from repo root)\n");
        return 1;
    }
    UNITY_BEGIN();
    RUN_TEST(test_to_instrument_id);
    RUN_TEST(test_from_instrument_id);
    RUN_TEST(test_sanitize_change_pct);
    RUN_TEST(test_compute_change_pct);
    RUN_TEST(test_non_finite_inputs_are_none);
    const int failures = UNITY_END();
    yyjson_doc_free(g_fixture);
    yyjson_doc_free(g_golden);
    return failures;
}
