/* risk gates + sizing: unit + golden parity. Paths relative to the repo root
 * (ctest WORKING_DIRECTORY). The golden is transcribed from the pinned Python
 * decision arithmetic (see tools/generate_golden_decisions.py / MEMORY.md). */
#include "alpha/alpha.h"
#include "alpha/risk.h"
#include "unity.h"
#include "yyjson.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FIXTURE_PATH "bench/fixtures/risk-cases.json"
#define GOLDEN_PATH "core/tests/golden/risk-cases.json"

#define MAX_POSITIONS 16

static yyjson_doc *g_fixture;
static yyjson_doc *g_golden;
static alpha_risk_config_t g_cfg;

void setUp(void) {}
void tearDown(void) {}

static int approx(double a, double b) { return fabs(a - b) <= 1e-9 * fmax(1.0, fabs(b)); }

static int cfg_int(yyjson_val *cfg, const char *key) {
    return (int)yyjson_get_sint(yyjson_obj_get(cfg, key));
}

static yyjson_val *fx(const char *key) {
    return yyjson_obj_get(yyjson_doc_get_root(g_fixture), key);
}
static yyjson_val *gd(const char *key) {
    return yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(g_golden), "result"), key);
}

static const char *exit_str(alpha_exit_kind_t kind) {
    switch (kind) {
    case ALPHA_EXIT_TAKE_PROFIT:
        return "TAKE_PROFIT";
    case ALPHA_EXIT_STOP_LOSS:
        return "STOP_LOSS";
    default:
        return "NONE";
    }
}

static alpha_position_t read_position(yyjson_val *item) {
    alpha_position_t pos = {0};
    pos.quantity = yyjson_get_sint(yyjson_obj_get(item, "quantity"));
    pos.avg_fill_price = yyjson_get_num(yyjson_obj_get(item, "avg_fill_price"));
    pos.current_price = yyjson_get_num(yyjson_obj_get(item, "current_price"));
    return pos;
}

/* The fixture config must equal the Python-compatible defaults. */
static void test_config_matches_defaults(void) {
    const alpha_risk_config_t d = alpha_risk_config_default();
    TEST_ASSERT_EQUAL_INT(d.max_position_pct, g_cfg.max_position_pct);
    TEST_ASSERT_EQUAL_INT(d.daily_loss_limit_pct, g_cfg.daily_loss_limit_pct);
    TEST_ASSERT_EQUAL_INT(d.individual_stop_loss_pct, g_cfg.individual_stop_loss_pct);
    TEST_ASSERT_EQUAL_INT(d.take_profit_pct, g_cfg.take_profit_pct);
    TEST_ASSERT_EQUAL_INT(d.portfolio_drawdown_limit_pct, g_cfg.portfolio_drawdown_limit_pct);
}

static void test_l1_exits(void) {
    yyjson_val *cases = fx("l1");
    yyjson_val *golden = gd("l1");
    size_t i = 0;
    yyjson_val *item = NULL;
    yyjson_arr_iter iter = yyjson_arr_iter_with(cases);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        const alpha_position_t pos = read_position(item);
        const alpha_exit_kind_t kind = alpha_check_rule_based_exit(&pos, &g_cfg, NULL);
        TEST_ASSERT_EQUAL_STRING(yyjson_get_str(yyjson_obj_get(yyjson_arr_get(golden, i), "kind")),
                                 exit_str(kind));
        i += 1;
    }
}

static void test_l2_drawdown(void) {
    yyjson_val *cases = fx("l2");
    yyjson_val *golden = gd("l2");
    size_t i = 0;
    yyjson_val *item = NULL;
    yyjson_arr_iter iter = yyjson_arr_iter_with(cases);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        alpha_position_t positions[MAX_POSITIONS];
        size_t n = 0;
        yyjson_val *pos = NULL;
        yyjson_arr_iter piter = yyjson_arr_iter_with(yyjson_obj_get(item, "positions"));
        while ((pos = yyjson_arr_iter_next(&piter)) != NULL) {
            positions[n++] = read_position(pos);
        }
        size_t out_indices[2];
        double dd_pct = 0.0;
        const size_t selected = alpha_check_portfolio_drawdown(
            positions, n, yyjson_get_num(yyjson_obj_get(item, "current_equity")),
            yyjson_get_num(yyjson_obj_get(item, "baseline_equity")), &g_cfg, out_indices, &dd_pct);

        yyjson_val *want = yyjson_arr_get(golden, i);
        yyjson_val *want_sel = yyjson_obj_get(want, "selected");
        TEST_ASSERT_EQUAL_size_t(yyjson_arr_size(want_sel), selected);
        for (size_t k = 0; k < selected; ++k) {
            TEST_ASSERT_EQUAL_INT64(yyjson_get_sint(yyjson_arr_get(want_sel, k)),
                                    (int64_t)out_indices[k]);
        }
        yyjson_val *want_dd = yyjson_obj_get(want, "dd_pct");
        if (!yyjson_is_null(want_dd)) {
            TEST_ASSERT_TRUE(approx(dd_pct, yyjson_get_num(want_dd)));
        }
        i += 1;
    }
}

static void test_l3_daily_breaker(void) {
    yyjson_val *cases = fx("l3");
    yyjson_val *golden = gd("l3");
    size_t i = 0;
    yyjson_val *item = NULL;
    yyjson_arr_iter iter = yyjson_arr_iter_with(cases);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        const bool blocked = alpha_is_daily_loss_blocked(
            yyjson_get_num(yyjson_obj_get(item, "daily_realized_pnl_pct")), &g_cfg);
        TEST_ASSERT_EQUAL_INT(yyjson_get_bool(yyjson_obj_get(yyjson_arr_get(golden, i), "blocked")),
                              blocked);
        i += 1;
    }
}

static void test_max_position(void) {
    yyjson_val *cases = fx("max_position");
    yyjson_val *golden = gd("max_position");
    size_t i = 0;
    yyjson_val *item = NULL;
    yyjson_arr_iter iter = yyjson_arr_iter_with(cases);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        const double denom = alpha_max_position_denominator(
            yyjson_get_bool(yyjson_obj_get(item, "is_paper")),
            yyjson_get_num(yyjson_obj_get(item, "total_value")),
            yyjson_get_num(yyjson_obj_get(item, "paper_seed_capital")),
            yyjson_get_num(yyjson_obj_get(item, "intended_buy_value")));
        double next_weight = 0.0;
        const bool allowed = alpha_max_position_allows_buy(
            &g_cfg, yyjson_get_num(yyjson_obj_get(item, "existing_position_value")),
            yyjson_get_num(yyjson_obj_get(item, "intended_buy_value")), denom, &next_weight);
        yyjson_val *want = yyjson_arr_get(golden, i);
        TEST_ASSERT_EQUAL_INT(yyjson_get_bool(yyjson_obj_get(want, "allowed")), allowed);
        TEST_ASSERT_TRUE(
            approx(next_weight, yyjson_get_num(yyjson_obj_get(want, "next_weight_pct"))));
        i += 1;
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
    yyjson_val *cfg = yyjson_obj_get(yyjson_doc_get_root(g_fixture), "config");
    g_cfg.max_position_pct = cfg_int(cfg, "max_position_pct");
    g_cfg.daily_loss_limit_pct = cfg_int(cfg, "daily_loss_limit_pct");
    g_cfg.individual_stop_loss_pct = cfg_int(cfg, "individual_stop_loss_pct");
    g_cfg.take_profit_pct = cfg_int(cfg, "take_profit_pct");
    g_cfg.portfolio_drawdown_limit_pct = cfg_int(cfg, "portfolio_drawdown_limit_pct");

    UNITY_BEGIN();
    RUN_TEST(test_config_matches_defaults);
    RUN_TEST(test_l1_exits);
    RUN_TEST(test_l2_drawdown);
    RUN_TEST(test_l3_daily_breaker);
    RUN_TEST(test_max_position);
    const int failures = UNITY_END();
    yyjson_doc_free(g_fixture);
    yyjson_doc_free(g_golden);
    return failures;
}
