/* Golden parity: run the C engine on the pinned fixture and compare the full
 * BacktestResult to the committed Python golden. Paths are relative to the
 * repository root (ctest WORKING_DIRECTORY). Tolerances per BUILD_AND_TEST.md:
 * integers exact, snapshot floats within 1e-9*max(1,|b|), metrics at Python
 * rounding, signals/sides exact. */
#include "alpha/alpha.h"
#include "alpha/backtest.h"
#include "alpha/date.h"
#include "backtest_fixture.h"
#include "unity.h"
#include "yyjson.h"

#include <math.h>
#include <stdio.h>

#define FIXTURE_PATH "bench/fixtures/backtest-small.json"
#define GOLDEN_PATH "core/tests/golden/backtest-small.json"

static yyjson_doc *g_golden;
static alpha_fixture_t g_fixture;
static alpha_backtest_result_t g_result;

void setUp(void) {}
void tearDown(void) {}

static int approx(double a, double b) { return fabs(a - b) <= 1e-9 * fmax(1.0, fabs(b)); }

static double gnum(yyjson_val *obj, const char *key) {
    return yyjson_get_num(yyjson_obj_get(obj, key));
}

static void assert_snapshots(yyjson_val *result_obj) {
    yyjson_val *arr = yyjson_obj_get(result_obj, "daily_snapshots");
    TEST_ASSERT_TRUE(yyjson_is_arr(arr));
    TEST_ASSERT_EQUAL_size_t(yyjson_arr_size(arr), g_result.snapshot_count);

    size_t i = 0;
    yyjson_val *item = NULL;
    yyjson_arr_iter iter = yyjson_arr_iter_with(arr);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        const alpha_daily_snapshot_t *s = &g_result.snapshots[i];
        int64_t golden_date = 0;
        TEST_ASSERT_EQUAL_INT(
            ALPHA_OK, alpha_date_parse(yyjson_get_str(yyjson_obj_get(item, "date")), &golden_date));
        TEST_ASSERT_EQUAL_INT64(golden_date, s->date);
        TEST_ASSERT_EQUAL_INT64(yyjson_get_sint(yyjson_obj_get(item, "position_qty")),
                                s->position_qty);
        TEST_ASSERT_TRUE(approx(s->close_price, gnum(item, "close_price")));
        TEST_ASSERT_TRUE(approx(s->cash, gnum(item, "cash")));
        TEST_ASSERT_TRUE(approx(s->position_value, gnum(item, "position_value")));
        TEST_ASSERT_TRUE(approx(s->portfolio_value, gnum(item, "portfolio_value")));
        TEST_ASSERT_TRUE(approx(s->daily_return_pct, gnum(item, "daily_return_pct")));
        i += 1;
    }
}

static void assert_trades(yyjson_val *result_obj) {
    yyjson_val *arr = yyjson_obj_get(result_obj, "trades");
    TEST_ASSERT_TRUE(yyjson_is_arr(arr));
    TEST_ASSERT_EQUAL_size_t(yyjson_arr_size(arr), g_result.trade_count);

    size_t i = 0;
    yyjson_val *item = NULL;
    yyjson_arr_iter iter = yyjson_arr_iter_with(arr);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        const alpha_trade_record_t *t = &g_result.trades[i];
        int64_t golden_date = 0;
        TEST_ASSERT_EQUAL_INT(
            ALPHA_OK, alpha_date_parse(yyjson_get_str(yyjson_obj_get(item, "date")), &golden_date));
        TEST_ASSERT_EQUAL_INT64(golden_date, t->date);
        const char *side = yyjson_get_str(yyjson_obj_get(item, "side"));
        TEST_ASSERT_EQUAL_STRING(t->side == ALPHA_SIDE_BUY ? "BUY" : "SELL", side);
        TEST_ASSERT_EQUAL_STRING(yyjson_get_str(yyjson_obj_get(item, "ticker")), t->ticker);
        TEST_ASSERT_EQUAL_INT64(yyjson_get_sint(yyjson_obj_get(item, "quantity")), t->quantity);
        TEST_ASSERT_TRUE(approx(t->price, gnum(item, "price")));
        TEST_ASSERT_TRUE(approx(t->commission, gnum(item, "commission")));
        TEST_ASSERT_TRUE(approx(t->tax, gnum(item, "tax")));
        TEST_ASSERT_TRUE(approx(t->slippage_cost, gnum(item, "slippage_cost")));
        TEST_ASSERT_TRUE(approx(t->total_cost, gnum(item, "total_cost")));
        TEST_ASSERT_TRUE(approx(t->pnl, gnum(item, "pnl")));
        i += 1;
    }
}

static void assert_metrics(yyjson_val *result_obj) {
    yyjson_val *m = yyjson_obj_get(result_obj, "metrics");
    TEST_ASSERT_TRUE(yyjson_is_obj(m));
    const alpha_backtest_metrics_t *c = &g_result.metrics;
    TEST_ASSERT_TRUE(approx(c->total_return_pct, gnum(m, "total_return_pct")));
    TEST_ASSERT_TRUE(approx(c->annual_return_pct, gnum(m, "annual_return_pct")));
    TEST_ASSERT_TRUE(approx(c->sharpe_ratio, gnum(m, "sharpe_ratio")));
    TEST_ASSERT_TRUE(approx(c->max_drawdown_pct, gnum(m, "max_drawdown_pct")));
    TEST_ASSERT_TRUE(approx(c->win_rate, gnum(m, "win_rate")));
    TEST_ASSERT_EQUAL_INT64(yyjson_get_sint(yyjson_obj_get(m, "total_trades")), c->total_trades);
    TEST_ASSERT_TRUE(approx(c->avg_holding_days, gnum(m, "avg_holding_days")));
    TEST_ASSERT_TRUE(approx(c->baseline_return_pct, gnum(m, "baseline_return_pct")));
    TEST_ASSERT_TRUE(approx(c->excess_return_pct, gnum(m, "excess_return_pct")));
}

static void test_backtest_small_parity(void) {
    yyjson_val *result_obj = yyjson_obj_get(yyjson_doc_get_root(g_golden), "result");
    TEST_ASSERT_NOT_NULL(result_obj);
    assert_snapshots(result_obj);
    assert_trades(result_obj);
    assert_metrics(result_obj);
}

int main(void) {
    if (alpha_initialize() != ALPHA_OK) {
        return 1;
    }
    g_golden = yyjson_read_file(GOLDEN_PATH, 0, NULL, NULL);
    if (g_golden == NULL) {
        fprintf(stderr, "cannot open golden: %s (run from repo root)\n", GOLDEN_PATH);
        return 1;
    }
    if (alpha_fixture_load(FIXTURE_PATH, &g_fixture) != ALPHA_OK) {
        fprintf(stderr, "cannot load fixture: %s\n", FIXTURE_PATH);
        return 1;
    }
    alpha_replay_source_t replay = alpha_fixture_replay(&g_fixture);
    const alpha_cost_model_t cost =
        alpha_cost_model_make(g_fixture.config.commission_rate_pct, g_fixture.config.tax_rate_pct,
                              g_fixture.config.slippage_bps);
    if (alpha_backtest_run(&g_fixture.config, g_fixture.prices, g_fixture.dates,
                           g_fixture.bar_count, alpha_replay_signal, &replay, &cost,
                           &g_result) != ALPHA_OK) {
        return 1;
    }

    UNITY_BEGIN();
    RUN_TEST(test_backtest_small_parity);
    const int failures = UNITY_END();

    alpha_backtest_result_free(&g_result);
    alpha_fixture_free(&g_fixture);
    yyjson_doc_free(g_golden);
    return failures;
}
