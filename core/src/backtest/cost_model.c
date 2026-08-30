#include "alpha/cost_model.h"

#include "alpha/constants.h"

alpha_cost_model_t alpha_cost_model_make(double commission_rate_pct, double tax_rate_pct,
                                         int slippage_bps) {
    alpha_cost_model_t model;
    model.commission_rate = commission_rate_pct / 100.0;
    model.tax_rate = tax_rate_pct / 100.0;
    model.slippage_rate = (double)slippage_bps / 10000.0;
    return model;
}

alpha_cost_model_t alpha_cost_model_default(void) {
    return alpha_cost_model_make(ALPHA_BACKTEST_COMMISSION_RATE_PCT, ALPHA_BACKTEST_TAX_RATE_PCT,
                                 ALPHA_BACKTEST_SLIPPAGE_BPS);
}

alpha_trade_cost_t alpha_cost_calculate(const alpha_cost_model_t *model, alpha_side_t side,
                                        double price, int64_t quantity) {
    const double notional = price * (double)quantity;
    alpha_trade_cost_t cost;
    cost.commission = notional * model->commission_rate;
    cost.tax = (side == ALPHA_SIDE_SELL) ? notional * model->tax_rate : 0.0;
    cost.slippage_cost = notional * model->slippage_rate;
    cost.total = cost.commission + cost.tax + cost.slippage_cost;
    return cost;
}
