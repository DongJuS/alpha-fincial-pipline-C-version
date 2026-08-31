/* screener unit + golden parity (transcribed from the pinned Python; see
 * tools/generate_golden_decisions.py / MEMORY.md). Paths relative to repo root. */
#include "alpha/alpha.h"
#include "alpha/screener.h"
#include "unity.h"
#include "yyjson.h"

#include <math.h>
#include <stdio.h>

#define FIXTURE_PATH "bench/fixtures/screener-cases.json"
#define GOLDEN_PATH "core/tests/golden/screener-cases.json"

#define MAX_BARS 32
#define MAX_CANDIDATES 32

static yyjson_doc *g_fixture;
static yyjson_doc *g_golden;
static double g_vol_th;
static double g_pct_th;

void setUp(void) {}
void tearDown(void) {}

static int approx(double a, double b) { return fabs(a - b) <= 1e-9 * fmax(1.0, fabs(b)); }

static yyjson_val *fx(const char *key) {
    return yyjson_obj_get(yyjson_doc_get_root(g_fixture), key);
}
static yyjson_val *gd(const char *key) {
    return yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(g_golden), "result"), key);
}

static void test_score(void) {
    yyjson_val *cases = fx("score");
    yyjson_val *golden = gd("score");
    size_t ci = 0;
    yyjson_val *item = NULL;
    yyjson_arr_iter iter = yyjson_arr_iter_with(cases);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        alpha_screener_bar_t bars[MAX_BARS];
        size_t n = 0;
        yyjson_val *bar = NULL;
        yyjson_arr_iter biter = yyjson_arr_iter_with(yyjson_obj_get(item, "bars"));
        while ((bar = yyjson_arr_iter_next(&biter)) != NULL) {
            bars[n].volume = yyjson_get_num(yyjson_obj_get(bar, "volume"));
            yyjson_val *chg = yyjson_obj_get(bar, "change_pct");
            bars[n].has_change_pct = chg != NULL && !yyjson_is_null(chg);
            bars[n].change_pct = bars[n].has_change_pct ? yyjson_get_num(chg) : 0.0;
            n += 1;
        }
        const alpha_screener_score_t got = alpha_screener_score(bars, n, g_vol_th, g_pct_th);
        yyjson_val *want = yyjson_arr_get(golden, ci);
        TEST_ASSERT_EQUAL_INT(yyjson_get_bool(yyjson_obj_get(want, "passes")), got.passes);
        TEST_ASSERT_TRUE(approx(got.score, yyjson_get_num(yyjson_obj_get(want, "score"))));
        ci += 1;
    }
}

static void test_select(void) {
    yyjson_val *cases = fx("select");
    yyjson_val *golden = gd("select");
    size_t ci = 0;
    yyjson_val *item = NULL;
    yyjson_arr_iter iter = yyjson_arr_iter_with(cases);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        alpha_screener_score_t candidates[MAX_CANDIDATES];
        size_t n = 0;
        yyjson_val *cand = NULL;
        yyjson_arr_iter citer = yyjson_arr_iter_with(yyjson_obj_get(item, "candidates"));
        while ((cand = yyjson_arr_iter_next(&citer)) != NULL) {
            candidates[n].passes = yyjson_get_bool(yyjson_obj_get(cand, "passes"));
            candidates[n].score = yyjson_get_num(yyjson_obj_get(cand, "score"));
            n += 1;
        }
        const size_t cap = (size_t)yyjson_get_sint(yyjson_obj_get(item, "cap"));
        size_t out_indices[MAX_CANDIDATES];
        const size_t selected = alpha_screener_select(candidates, n, cap, out_indices);

        yyjson_val *want = yyjson_obj_get(yyjson_arr_get(golden, ci), "selected");
        TEST_ASSERT_EQUAL_size_t(yyjson_arr_size(want), selected);
        for (size_t k = 0; k < selected; ++k) {
            TEST_ASSERT_EQUAL_INT64(yyjson_get_sint(yyjson_arr_get(want, k)),
                                    (int64_t)out_indices[k]);
        }
        ci += 1;
    }
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
    yyjson_val *thresholds = fx("thresholds");
    g_vol_th = yyjson_get_num(yyjson_obj_get(thresholds, "volume_surge_ratio"));
    g_pct_th = yyjson_get_num(yyjson_obj_get(thresholds, "change_pct_threshold"));

    UNITY_BEGIN();
    RUN_TEST(test_score);
    RUN_TEST(test_select);
    const int failures = UNITY_END();
    yyjson_doc_free(g_fixture);
    yyjson_doc_free(g_golden);
    return failures;
}
