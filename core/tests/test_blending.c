/* blending unit + golden parity. Golden/fixture paths are relative to the
 * repository root (ctest WORKING_DIRECTORY). */
#include "alpha/alpha.h"
#include "alpha/blending.h"
#include "unity.h"
#include "yyjson.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FIXTURE_PATH "bench/fixtures/blend-cases.json"
#define GOLDEN_PATH "core/tests/golden/blend-cases.json"

#define MAX_INPUTS 8

static yyjson_doc *g_fixture;
static yyjson_doc *g_golden;

void setUp(void) {}
void tearDown(void) {}

static int approx(double a, double b) { return fabs(a - b) <= 1e-9 * fmax(1.0, fabs(b)); }

static alpha_signal_t sig_from_str(const char *name) {
    if (strcmp(name, "BUY") == 0) {
        return ALPHA_SIGNAL_BUY;
    }
    if (strcmp(name, "SELL") == 0) {
        return ALPHA_SIGNAL_SELL;
    }
    if (strcmp(name, "CLOSE") == 0) {
        return ALPHA_SIGNAL_CLOSE;
    }
    return ALPHA_SIGNAL_HOLD;
}

static const char *sig_str(alpha_signal_t signal) {
    switch (signal) {
    case ALPHA_SIGNAL_BUY:
        return "BUY";
    case ALPHA_SIGNAL_SELL:
        return "SELL";
    case ALPHA_SIGNAL_CLOSE:
        return "CLOSE";
    default:
        return "HOLD";
    }
}

static void check_nway(yyjson_val *fixture_case, yyjson_val *want) {
    alpha_blend_input_t inputs[MAX_INPUTS];
    size_t count = 0;
    yyjson_val *in = NULL;
    yyjson_arr_iter iter = yyjson_arr_iter_with(yyjson_obj_get(fixture_case, "inputs"));
    while ((in = yyjson_arr_iter_next(&iter)) != NULL) {
        inputs[count].signal = sig_from_str(yyjson_get_str(yyjson_obj_get(in, "signal")));
        inputs[count].confidence = yyjson_get_num(yyjson_obj_get(in, "confidence"));
        inputs[count].weight = yyjson_get_num(yyjson_obj_get(in, "weight"));
        count += 1;
    }
    const alpha_blend_result_t result = alpha_blend_signals(inputs, count);
    TEST_ASSERT_EQUAL_STRING(yyjson_get_str(yyjson_obj_get(want, "signal")),
                             sig_str(result.signal));
    TEST_ASSERT_TRUE(approx(result.confidence, yyjson_get_num(yyjson_obj_get(want, "confidence"))));
    TEST_ASSERT_TRUE(
        approx(result.weighted_score, yyjson_get_num(yyjson_obj_get(want, "weighted_score"))));
    TEST_ASSERT_EQUAL_INT(yyjson_get_bool(yyjson_obj_get(want, "conflict")), result.conflict);
}

static void check_ab(yyjson_val *fixture_case, yyjson_val *want) {
    yyjson_val *a_sig = yyjson_obj_get(fixture_case, "a_signal");
    yyjson_val *b_sig = yyjson_obj_get(fixture_case, "b_signal");
    const bool has_a = !yyjson_is_null(a_sig);
    const bool has_b = !yyjson_is_null(b_sig);
    const double a_conf = yyjson_get_num(yyjson_obj_get(fixture_case, "a_confidence"));
    const double b_conf = yyjson_get_num(yyjson_obj_get(fixture_case, "b_confidence"));
    const alpha_blend_result_t result = alpha_blend_ab(
        has_a, has_a ? sig_from_str(yyjson_get_str(a_sig)) : ALPHA_SIGNAL_HOLD, a_conf, has_b,
        has_b ? sig_from_str(yyjson_get_str(b_sig)) : ALPHA_SIGNAL_HOLD, b_conf,
        yyjson_get_num(yyjson_obj_get(fixture_case, "blend_ratio")));
    TEST_ASSERT_EQUAL_STRING(yyjson_get_str(yyjson_obj_get(want, "signal")),
                             sig_str(result.signal));
    TEST_ASSERT_TRUE(approx(result.confidence, yyjson_get_num(yyjson_obj_get(want, "confidence"))));
    TEST_ASSERT_EQUAL_INT(yyjson_get_bool(yyjson_obj_get(want, "conflict")), result.conflict);
}

static void test_blend_parity(void) {
    yyjson_val *nway = yyjson_obj_get(yyjson_doc_get_root(g_fixture), "nway");
    yyjson_val *ab = yyjson_obj_get(yyjson_doc_get_root(g_fixture), "ab");
    yyjson_val *result = yyjson_obj_get(yyjson_doc_get_root(g_golden), "result");

    size_t nway_idx = 0;
    size_t ab_idx = 0;
    yyjson_val *want = NULL;
    yyjson_arr_iter iter = yyjson_arr_iter_with(result);
    while ((want = yyjson_arr_iter_next(&iter)) != NULL) {
        const char *kind = yyjson_get_str(yyjson_obj_get(want, "kind"));
        if (strcmp(kind, "nway") == 0) {
            check_nway(yyjson_arr_get(nway, nway_idx), want);
            nway_idx += 1;
        } else {
            check_ab(yyjson_arr_get(ab, ab_idx), want);
            ab_idx += 1;
        }
    }
    TEST_ASSERT_EQUAL_size_t(yyjson_arr_size(nway), nway_idx);
    TEST_ASSERT_EQUAL_size_t(yyjson_arr_size(ab), ab_idx);
}

/* Threshold is strict: exactly +0.15 is HOLD, just above is BUY. */
static void test_threshold_is_strict(void) {
    alpha_blend_input_t at = {ALPHA_SIGNAL_BUY, 0.15, 1.0};
    TEST_ASSERT_EQUAL_INT(ALPHA_SIGNAL_HOLD, alpha_blend_signals(&at, 1).signal);
    alpha_blend_input_t above = {ALPHA_SIGNAL_BUY, 0.150001, 1.0};
    TEST_ASSERT_EQUAL_INT(ALPHA_SIGNAL_BUY, alpha_blend_signals(&above, 1).signal);
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
    RUN_TEST(test_blend_parity);
    RUN_TEST(test_threshold_is_strict);
    const int failures = UNITY_END();
    yyjson_doc_free(g_fixture);
    yyjson_doc_free(g_golden);
    return failures;
}
