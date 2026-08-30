#include "alpha/backtest.h"
#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Per-bar signal driver: ctx is an array indexed by bar (n-1). */
static alpha_signal_t bar_signal(void *ctx, int64_t day, const double *prices, size_t n,
                                 int64_t position) {
    (void)day;
    (void)prices;
    (void)position;
    const alpha_signal_t *arr = (const alpha_signal_t *)ctx;
    return arr[n - 1];
}

static alpha_backtest_config_t make_config(void) {
    alpha_backtest_config_t cfg = {0};
    strcpy(cfg.ticker, "005930");
    strcpy(cfg.strategy, "A");
    cfg.train_start = -10;
    cfg.train_end = -1;
    cfg.test_start = 0;
    cfg.test_end = 100;
    cfg.initial_capital = 10000000;
    cfg.commission_rate_pct = 0.015;
    cfg.tax_rate_pct = 0.18;
    cfg.slippage_bps = 3;
    return cfg;
}

static void test_open_then_close(void) {
    const alpha_backtest_config_t cfg = make_config();
    const alpha_cost_model_t cost = alpha_cost_model_default();
    const double prices[2] = {48800.0, 49000.0};
    const int64_t dates[2] = {0, 1};
    alpha_signal_t sig[2] = {ALPHA_SIGNAL_BUY, ALPHA_SIGNAL_SELL};

    alpha_backtest_result_t result;
    TEST_ASSERT_EQUAL_INT(
        ALPHA_OK, alpha_backtest_run(&cfg, prices, dates, 2, bar_signal, sig, &cost, &result));
    TEST_ASSERT_EQUAL_size_t(2, result.snapshot_count);
    TEST_ASSERT_EQUAL_size_t(2, result.trade_count);
    TEST_ASSERT_EQUAL_INT(ALPHA_SIDE_BUY, result.trades[0].side);
    TEST_ASSERT_EQUAL_INT64(204, result.trades[0].quantity); /* floor(1e7 / 48821.96) */
    TEST_ASSERT_EQUAL_DOUBLE(0.0, result.trades[0].tax);
    TEST_ASSERT_EQUAL_INT(ALPHA_SIDE_SELL, result.trades[1].side);
    TEST_ASSERT_EQUAL_INT64(204, result.trades[1].quantity);
    TEST_ASSERT_TRUE(result.trades[1].tax > 0.0);
    TEST_ASSERT_EQUAL_INT64(0, result.snapshots[1].position_qty);
    TEST_ASSERT_TRUE(result.snapshots[0].cash >= 0.0);
    alpha_backtest_result_free(&result);
}

static void test_buy_while_holding_is_ignored(void) {
    const alpha_backtest_config_t cfg = make_config();
    const alpha_cost_model_t cost = alpha_cost_model_default();
    const double prices[4] = {48800.0, 48900.0, 49000.0, 49100.0};
    const int64_t dates[4] = {0, 1, 2, 3};
    alpha_signal_t sig[4] = {ALPHA_SIGNAL_HOLD, ALPHA_SIGNAL_BUY, ALPHA_SIGNAL_BUY,
                             ALPHA_SIGNAL_SELL};

    alpha_backtest_result_t result;
    TEST_ASSERT_EQUAL_INT(
        ALPHA_OK, alpha_backtest_run(&cfg, prices, dates, 4, bar_signal, sig, &cost, &result));
    TEST_ASSERT_EQUAL_size_t(2, result.trade_count); /* second BUY ignored */
    TEST_ASSERT_EQUAL_INT64(1, result.trades[0].date);
    TEST_ASSERT_EQUAL_INT64(3, result.trades[1].date);
    alpha_backtest_result_free(&result);
}

static void test_sell_while_flat_is_ignored(void) {
    const alpha_backtest_config_t cfg = make_config();
    const alpha_cost_model_t cost = alpha_cost_model_default();
    const double prices[2] = {48800.0, 49000.0};
    const int64_t dates[2] = {0, 1};
    alpha_signal_t sig[2] = {ALPHA_SIGNAL_SELL, ALPHA_SIGNAL_HOLD};

    alpha_backtest_result_t result;
    TEST_ASSERT_EQUAL_INT(
        ALPHA_OK, alpha_backtest_run(&cfg, prices, dates, 2, bar_signal, sig, &cost, &result));
    TEST_ASSERT_EQUAL_size_t(0, result.trade_count);
    alpha_backtest_result_free(&result);
}

static void test_invalid_train_test_window(void) {
    alpha_backtest_config_t cfg = make_config();
    cfg.train_end = cfg.test_start; /* train_end >= test_start is rejected */
    const alpha_cost_model_t cost = alpha_cost_model_default();
    const double prices[1] = {48800.0};
    const int64_t dates[1] = {0};
    alpha_signal_t sig[1] = {ALPHA_SIGNAL_HOLD};

    alpha_backtest_result_t result;
    TEST_ASSERT_EQUAL_INT(
        ALPHA_ERR_INVALID_ARG,
        alpha_backtest_run(&cfg, prices, dates, 1, bar_signal, sig, &cost, &result));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_open_then_close);
    RUN_TEST(test_buy_while_holding_is_ignored);
    RUN_TEST(test_sell_while_flat_is_ignored);
    RUN_TEST(test_invalid_train_test_window);
    return UNITY_END();
}
