#ifndef ALPHA_RISK_H
#define ALPHA_RISK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "alpha/backtest.h" /* ALPHA_TICKER_CAP */

/* Risk gates enforced below the signal layer (src/agents/portfolio_manager.py).
 * All functions are pure: they take validated config plus already-resolved
 * position/price/equity inputs. A signal or strategy payload can never supply
 * these config values. Redis lockout persistence and DB reads are wired in P3;
 * only the decision arithmetic lives here. */

/* Validated portfolio risk config (percent ints). Defaults match Python. */
typedef struct {
    int max_position_pct;             /* 20 */
    int daily_loss_limit_pct;         /* 3  */
    int individual_stop_loss_pct;     /* 7  */
    int take_profit_pct;              /* 5  */
    int portfolio_drawdown_limit_pct; /* 8  */
} alpha_risk_config_t;

alpha_risk_config_t alpha_risk_config_default(void);

/* A held position resolved to decision inputs (avg_fill_price already reflects
 * the broker-wavg-or-fallback rule from the caller). */
typedef struct {
    char ticker[ALPHA_TICKER_CAP];
    int64_t quantity;
    double avg_fill_price;
    double current_price;
} alpha_position_t;

typedef enum {
    ALPHA_EXIT_NONE,
    ALPHA_EXIT_TAKE_PROFIT,
    ALPHA_EXIT_STOP_LOSS,
} alpha_exit_kind_t;

/* L1 per-position exit. Returns the exit kind and, when not NONE, the pnl_pct.
 * qty<=0 or non-positive avg_fill/current price -> NONE. */
alpha_exit_kind_t alpha_check_rule_based_exit(const alpha_position_t *position,
                                              const alpha_risk_config_t *config,
                                              double *pnl_pct_out);

/* L1 scan over positions. Writes the index and kind of each SELL into the caller
 * arrays (capacity >= count) and returns how many fired, in input order. */
size_t alpha_check_rule_based_exits(const alpha_position_t *positions, size_t count,
                                    const alpha_risk_config_t *config, size_t *out_indices,
                                    alpha_exit_kind_t *out_kinds);

/* L2 portfolio drawdown. dd_pct = (current-baseline)/baseline*100. If equities
 * are non-positive or dd_pct > -limit, returns 0. Otherwise writes up to two
 * indices (the weakest by pnl_pct, stable on ties) and returns that count.
 * dd_pct_out is set whenever both equities are positive. */
size_t alpha_check_portfolio_drawdown(const alpha_position_t *positions, size_t count,
                                      double current_equity, double baseline_equity,
                                      const alpha_risk_config_t *config, size_t *out_indices,
                                      double *dd_pct_out);

/* L3 daily-loss circuit breaker decision: blocked when the day's realized pnl
 * pct <= -daily_loss_limit_pct. */
bool alpha_is_daily_loss_blocked(double daily_realized_pnl_pct, const alpha_risk_config_t *config);

/* Max-position denominator: paper = max(total, seed, 1); real = max(total+buy, 1). */
double alpha_max_position_denominator(bool is_paper, double total_value, double paper_seed_capital,
                                      double intended_buy_value);

/* Max-position BUY gate. next_weight = (existing+intended)/denominator*100.
 * Returns true if the BUY is allowed (next_weight <= max_position_pct). */
bool alpha_max_position_allows_buy(const alpha_risk_config_t *config,
                                   double existing_position_value, double intended_buy_value,
                                   double denominator, double *next_weight_pct_out);

#endif
