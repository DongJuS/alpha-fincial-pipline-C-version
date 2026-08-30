#include "alpha/cost_model.h"
#include "unity.h"

#include "alpha/constants.h"

void setUp(void) {}
void tearDown(void) {}

/* Defaults mirror constants.py: 0.015% + 0.18% (SELL) + 3 bps. */
static void test_buy_has_no_tax(void) {
    const alpha_cost_model_t model = alpha_cost_model_default();
    const alpha_trade_cost_t cost = alpha_cost_calculate(&model, ALPHA_SIDE_BUY, 50000.0, 10);
    const double notional = 50000.0 * 10.0;
    TEST_ASSERT_EQUAL_DOUBLE(notional * 0.00015, cost.commission);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, cost.tax);
    TEST_ASSERT_EQUAL_DOUBLE(notional * 0.0003, cost.slippage_cost);
    TEST_ASSERT_EQUAL_DOUBLE(cost.commission + cost.slippage_cost, cost.total);
}

static void test_sell_adds_tax(void) {
    const alpha_cost_model_t model = alpha_cost_model_default();
    const alpha_trade_cost_t cost = alpha_cost_calculate(&model, ALPHA_SIDE_SELL, 50000.0, 10);
    const double notional = 50000.0 * 10.0;
    TEST_ASSERT_EQUAL_DOUBLE(notional * 0.0018, cost.tax);
    TEST_ASSERT_EQUAL_DOUBLE(cost.commission + cost.tax + cost.slippage_cost, cost.total);
}

static void test_zero_quantity_is_zero_cost(void) {
    const alpha_cost_model_t model = alpha_cost_model_default();
    const alpha_trade_cost_t cost = alpha_cost_calculate(&model, ALPHA_SIDE_SELL, 50000.0, 0);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, cost.total);
}

static void test_rates_from_percent_and_bps(void) {
    const alpha_cost_model_t model = alpha_cost_model_make(0.015, 0.18, 3);
    TEST_ASSERT_EQUAL_DOUBLE(0.00015, model.commission_rate);
    TEST_ASSERT_EQUAL_DOUBLE(0.0018, model.tax_rate);
    TEST_ASSERT_EQUAL_DOUBLE(0.0003, model.slippage_rate);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_buy_has_no_tax);
    RUN_TEST(test_sell_adds_tax);
    RUN_TEST(test_zero_quantity_is_zero_cost);
    RUN_TEST(test_rates_from_percent_and_bps);
    return UNITY_END();
}
