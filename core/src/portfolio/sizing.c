#include "alpha/risk.h"

double alpha_max_position_denominator(bool is_paper, double total_value, double paper_seed_capital,
                                      double intended_buy_value) {
    if (is_paper) {
        double denom = total_value;
        if (paper_seed_capital > denom) {
            denom = paper_seed_capital;
        }
        if (1.0 > denom) {
            denom = 1.0;
        }
        return denom;
    }
    const double denom = total_value + intended_buy_value;
    return denom > 1.0 ? denom : 1.0;
}

bool alpha_max_position_allows_buy(const alpha_risk_config_t *config,
                                   double existing_position_value, double intended_buy_value,
                                   double denominator, double *next_weight_pct_out) {
    const double next_value = existing_position_value + intended_buy_value;
    const double next_weight_pct = next_value / denominator * 100.0;
    if (next_weight_pct_out != NULL) {
        *next_weight_pct_out = next_weight_pct;
    }
    /* Python blocks the BUY when next_weight_pct > max_position_pct. */
    return next_weight_pct <= (double)config->max_position_pct;
}
