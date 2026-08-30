#ifndef ALPHA_COST_MODEL_H
#define ALPHA_COST_MODEL_H

#include <stdint.h>

#include "alpha/domain.h"

/* Korean-market trade cost model (src/backtest/cost_model.py).
 * Rates are stored as ratios: commission_pct/100, tax_pct/100, bps/10000. */
typedef struct {
    double commission_rate;
    double tax_rate;
    double slippage_rate;
} alpha_cost_model_t;

/* One trade's cost breakdown (TradeCost). */
typedef struct {
    double commission;
    double tax;
    double slippage_cost;
    double total;
} alpha_trade_cost_t;

/* Build a cost model from percent/bps inputs (CostModel.__init__). */
alpha_cost_model_t alpha_cost_model_make(double commission_rate_pct, double tax_rate_pct,
                                         int slippage_bps);

/* Cost model with the constants.py defaults. */
alpha_cost_model_t alpha_cost_model_default(void);

/* Compute trade cost: tax applies to SELL only (CostModel.calculate). */
alpha_trade_cost_t alpha_cost_calculate(const alpha_cost_model_t *model, alpha_side_t side,
                                        double price, int64_t quantity);

#endif
