#include "alpha/risk.h"

alpha_risk_config_t alpha_risk_config_default(void) {
    alpha_risk_config_t config;
    config.max_position_pct = 20;
    config.daily_loss_limit_pct = 3;
    config.individual_stop_loss_pct = 7;
    config.take_profit_pct = 5;
    config.portfolio_drawdown_limit_pct = 8;
    return config;
}

alpha_exit_kind_t alpha_check_rule_based_exit(const alpha_position_t *position,
                                              const alpha_risk_config_t *config,
                                              double *pnl_pct_out) {
    if (position->quantity <= 0) {
        return ALPHA_EXIT_NONE;
    }
    const double avg_fill = position->avg_fill_price;
    const double current = position->current_price;
    if (avg_fill <= 0.0 || current <= 0.0) {
        return ALPHA_EXIT_NONE;
    }
    const double pnl_pct = (current - avg_fill) / avg_fill * 100.0;
    if (pnl_pct >= (double)config->take_profit_pct) {
        if (pnl_pct_out != NULL) {
            *pnl_pct_out = pnl_pct;
        }
        return ALPHA_EXIT_TAKE_PROFIT;
    }
    if (pnl_pct <= -(double)config->individual_stop_loss_pct) {
        if (pnl_pct_out != NULL) {
            *pnl_pct_out = pnl_pct;
        }
        return ALPHA_EXIT_STOP_LOSS;
    }
    return ALPHA_EXIT_NONE;
}

size_t alpha_check_rule_based_exits(const alpha_position_t *positions, size_t count,
                                    const alpha_risk_config_t *config, size_t *out_indices,
                                    alpha_exit_kind_t *out_kinds) {
    size_t fired = 0;
    for (size_t i = 0; i < count; ++i) {
        double pnl_pct = 0.0;
        const alpha_exit_kind_t kind = alpha_check_rule_based_exit(&positions[i], config, &pnl_pct);
        if (kind != ALPHA_EXIT_NONE) {
            out_indices[fired] = i;
            out_kinds[fired] = kind;
            fired += 1;
        }
    }
    return fired;
}

size_t alpha_check_portfolio_drawdown(const alpha_position_t *positions, size_t count,
                                      double current_equity, double baseline_equity,
                                      const alpha_risk_config_t *config, size_t *out_indices,
                                      double *dd_pct_out) {
    if (current_equity <= 0.0 || baseline_equity <= 0.0) {
        return 0;
    }
    const double dd_pct = (current_equity - baseline_equity) / baseline_equity * 100.0;
    if (dd_pct_out != NULL) {
        *dd_pct_out = dd_pct;
    }
    if (dd_pct > -(double)config->portfolio_drawdown_limit_pct) {
        return 0;
    }

    /* Select the weakest two by pnl_pct, stable on ties (earliest index first) —
     * equivalent to Python's stable sort + candidates[:2] without allocating. */
    long first = -1;
    long second = -1;
    double first_pnl = 0.0;
    double second_pnl = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const double avg_fill = positions[i].avg_fill_price;
        const double current = positions[i].current_price;
        if (positions[i].quantity <= 0 || avg_fill <= 0.0 || current <= 0.0) {
            continue;
        }
        const double pnl_pct = (current - avg_fill) / avg_fill * 100.0;
        if (first < 0 || pnl_pct < first_pnl) {
            second = first;
            second_pnl = first_pnl;
            first = (long)i;
            first_pnl = pnl_pct;
        } else if (second < 0 || pnl_pct < second_pnl) {
            second = (long)i;
            second_pnl = pnl_pct;
        }
    }

    size_t selected = 0;
    if (first >= 0) {
        out_indices[selected++] = (size_t)first;
    }
    if (second >= 0) {
        out_indices[selected++] = (size_t)second;
    }
    return selected;
}

bool alpha_is_daily_loss_blocked(double daily_realized_pnl_pct, const alpha_risk_config_t *config) {
    return daily_realized_pnl_pct <= -(double)config->daily_loss_limit_pct;
}
