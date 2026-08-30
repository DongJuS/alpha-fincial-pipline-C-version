#include "alpha/backtest.h"
#include "unity.h"

#include <math.h>

void setUp(void) {}
void tearDown(void) {}

static alpha_daily_snapshot_t snap(double close, double portfolio, double daily_return_pct) {
    alpha_daily_snapshot_t s = {0};
    s.close_price = close;
    s.portfolio_value = portfolio;
    s.daily_return_pct = daily_return_pct;
    return s;
}

static void test_empty_snapshots_are_zero(void) {
    const alpha_backtest_metrics_t m = alpha_compute_metrics(NULL, 0, NULL, 0, 10000000);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, m.total_return_pct);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, m.annual_return_pct);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, m.sharpe_ratio);
    TEST_ASSERT_EQUAL_INT64(0, m.total_trades);
}

static void test_single_snapshot_no_annualization(void) {
    const alpha_daily_snapshot_t s[1] = {snap(100.0, 10000000.0, 0.0)};
    const alpha_backtest_metrics_t m = alpha_compute_metrics(s, 1, NULL, 0, 10000000);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, m.total_return_pct);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, m.annual_return_pct); /* n not > 1 */
    TEST_ASSERT_EQUAL_DOUBLE(0.0, m.sharpe_ratio);      /* fewer than 2 returns */
}

static void test_total_return_at_or_below_minus_one_guards_annual(void) {
    const alpha_daily_snapshot_t s[2] = {snap(100.0, 10000000.0, 0.0), snap(100.0, 0.0, -100.0)};
    const alpha_backtest_metrics_t m = alpha_compute_metrics(s, 2, NULL, 0, 10000000);
    TEST_ASSERT_EQUAL_DOUBLE(-100.0, m.total_return_pct); /* total_return == -1 */
    TEST_ASSERT_EQUAL_DOUBLE(0.0, m.annual_return_pct);
}

static void test_mdd_is_peak_to_trough(void) {
    const alpha_daily_snapshot_t s[3] = {snap(100.0, 100.0, 0.0), snap(100.0, 120.0, 20.0),
                                         snap(100.0, 90.0, -25.0)};
    const alpha_backtest_metrics_t m = alpha_compute_metrics(s, 3, NULL, 0, 100);
    /* trough 90 vs peak 120 -> -25% */
    TEST_ASSERT_EQUAL_DOUBLE(-25.0, m.max_drawdown_pct);
}

static void test_sharpe_matches_sample_formula(void) {
    const alpha_daily_snapshot_t s[3] = {snap(100.0, 100.0, 0.0), snap(100.0, 100.0, 1.0),
                                         snap(100.0, 100.0, 2.0)};
    const alpha_backtest_metrics_t m = alpha_compute_metrics(s, 3, NULL, 0, 100);
    /* fractions 0,0.01,0.02 -> mean 0.01, sample std 0.01, sharpe = sqrt(252). */
    const double expected = sqrt(252.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, expected, m.sharpe_ratio);
}

static void test_win_rate_and_holding_days(void) {
    alpha_trade_record_t t[4] = {0};
    t[0].side = ALPHA_SIDE_BUY;
    t[0].date = 0;
    t[1].side = ALPHA_SIDE_SELL;
    t[1].date = 10;
    t[1].pnl = 5.0; /* win */
    t[2].side = ALPHA_SIDE_BUY;
    t[2].date = 20;
    t[3].side = ALPHA_SIDE_SELL;
    t[3].date = 24;
    t[3].pnl = -3.0; /* loss */
    const alpha_daily_snapshot_t s[1] = {snap(100.0, 100.0, 0.0)};
    const alpha_backtest_metrics_t m = alpha_compute_metrics(s, 1, t, 4, 100);
    TEST_ASSERT_EQUAL_DOUBLE(50.0, m.win_rate);        /* 1 of 2 sells */
    TEST_ASSERT_EQUAL_DOUBLE(7.0, m.avg_holding_days); /* (10 + 4) / 2 */
    TEST_ASSERT_EQUAL_INT64(4, m.total_trades);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_snapshots_are_zero);
    RUN_TEST(test_single_snapshot_no_annualization);
    RUN_TEST(test_total_return_at_or_below_minus_one_guards_annual);
    RUN_TEST(test_mdd_is_peak_to_trough);
    RUN_TEST(test_sharpe_matches_sample_formula);
    RUN_TEST(test_win_rate_and_holding_days);
    return UNITY_END();
}
