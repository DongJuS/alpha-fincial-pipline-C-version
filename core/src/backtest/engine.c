#include "alpha/backtest.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Mutable simulation state (BacktestEngine instance fields). */
typedef struct {
    double cash;
    int64_t position_qty;
    double avg_buy_price;
    double prev_portfolio_value;
} engine_state_t;

static void copy_ticker(char *dst, const char *src) {
    strncpy(dst, src, ALPHA_TICKER_CAP - 1);
    dst[ALPHA_TICKER_CAP - 1] = '\0';
}

/* _open_position: all-cash integer-share buy with the rounding guard.
 * Returns 1 and fills *trade on success, 0 when no trade is taken. */
static int open_position(engine_state_t *state, const alpha_cost_model_t *cost,
                         const alpha_backtest_config_t *config, int64_t date, double price,
                         alpha_trade_record_t *trade) {
    const alpha_trade_cost_t unit = alpha_cost_calculate(cost, ALPHA_SIDE_BUY, price, 1);
    const double effective_price = price + unit.total;
    if (effective_price <= 0.0) {
        return 0;
    }
    int64_t quantity = (int64_t)floor(state->cash / effective_price);
    if (quantity <= 0) {
        return 0;
    }
    alpha_trade_cost_t c = alpha_cost_calculate(cost, ALPHA_SIDE_BUY, price, quantity);
    double total_outlay = price * (double)quantity + c.total;
    if (total_outlay > state->cash) {
        quantity -= 1;
        if (quantity <= 0) {
            return 0;
        }
        c = alpha_cost_calculate(cost, ALPHA_SIDE_BUY, price, quantity);
    }

    state->cash -= price * (double)quantity + c.total;
    state->avg_buy_price = price;
    state->position_qty = quantity;

    trade->date = date;
    trade->side = ALPHA_SIDE_BUY;
    copy_ticker(trade->ticker, config->ticker);
    trade->price = price;
    trade->quantity = quantity;
    trade->commission = c.commission;
    trade->tax = c.tax;
    trade->slippage_cost = c.slippage_cost;
    trade->total_cost = c.total;
    trade->pnl = 0.0;
    return 1;
}

/* _close_position: full-quantity sell, realized pnl. */
static void close_position(engine_state_t *state, const alpha_cost_model_t *cost,
                           const alpha_backtest_config_t *config, int64_t date, double price,
                           alpha_trade_record_t *trade) {
    const int64_t qty = state->position_qty;
    const alpha_trade_cost_t c = alpha_cost_calculate(cost, ALPHA_SIDE_SELL, price, qty);
    const double pnl = (price - state->avg_buy_price) * (double)qty - c.total;

    state->cash += price * (double)qty - c.total;
    state->position_qty = 0;
    state->avg_buy_price = 0.0;

    trade->date = date;
    trade->side = ALPHA_SIDE_SELL;
    copy_ticker(trade->ticker, config->ticker);
    trade->price = price;
    trade->quantity = qty;
    trade->commission = c.commission;
    trade->tax = c.tax;
    trade->slippage_cost = c.slippage_cost;
    trade->total_cost = c.total;
    trade->pnl = pnl;
}

static alpha_daily_snapshot_t take_snapshot(engine_state_t *state, int64_t date,
                                            double close_price) {
    const double position_value = (double)state->position_qty * close_price;
    const double portfolio_value = state->cash + position_value;
    const double daily_return_pct =
        state->prev_portfolio_value > 0.0
            ? (portfolio_value - state->prev_portfolio_value) / state->prev_portfolio_value * 100.0
            : 0.0;
    state->prev_portfolio_value = portfolio_value;

    alpha_daily_snapshot_t snapshot;
    snapshot.date = date;
    snapshot.close_price = close_price;
    snapshot.cash = state->cash;
    snapshot.position_qty = state->position_qty;
    snapshot.position_value = position_value;
    snapshot.portfolio_value = portfolio_value;
    snapshot.daily_return_pct = daily_return_pct;
    return snapshot;
}

alpha_err_t alpha_backtest_run(const alpha_backtest_config_t *config, const double *prices,
                               const int64_t *dates, size_t n, alpha_signal_fn signal_fn,
                               void *signal_ctx, const alpha_cost_model_t *cost,
                               alpha_backtest_result_t *out) {
    if (config == NULL || prices == NULL || dates == NULL || signal_fn == NULL || cost == NULL ||
        out == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    if (config->train_end >= config->test_start) {
        return ALPHA_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->config = *config;

    alpha_trade_record_t *trades = NULL;
    alpha_daily_snapshot_t *snapshots = NULL;
    if (n > 0) {
        /* At most one trade per bar, exactly one snapshot per bar. */
        trades = (alpha_trade_record_t *)malloc(n * sizeof(alpha_trade_record_t));
        snapshots = (alpha_daily_snapshot_t *)malloc(n * sizeof(alpha_daily_snapshot_t));
        if (trades == NULL || snapshots == NULL) {
            free(trades);
            free(snapshots);
            return ALPHA_ERR_IO;
        }
    }

    engine_state_t state;
    state.cash = (double)config->initial_capital;
    state.position_qty = 0;
    state.avg_buy_price = 0.0;
    state.prev_portfolio_value = (double)config->initial_capital;

    size_t trade_count = 0;
    for (size_t i = 0; i < n; ++i) {
        const int64_t date = dates[i];
        const double close_price = prices[i];
        /* price_history grows one bar at a time and is passed to the seam. */
        const alpha_signal_t signal =
            signal_fn(signal_ctx, date, prices, i + 1, state.position_qty);

        if (signal == ALPHA_SIGNAL_BUY && state.position_qty == 0) {
            if (open_position(&state, cost, config, date, close_price, &trades[trade_count])) {
                trade_count += 1;
            }
        } else if ((signal == ALPHA_SIGNAL_SELL || signal == ALPHA_SIGNAL_CLOSE) &&
                   state.position_qty > 0) {
            close_position(&state, cost, config, date, close_price, &trades[trade_count]);
            trade_count += 1;
        }

        snapshots[i] = take_snapshot(&state, date, close_price);
    }

    out->trades = trades;
    out->trade_count = trade_count;
    out->snapshots = snapshots;
    out->snapshot_count = n;
    out->metrics =
        alpha_compute_metrics(snapshots, n, trades, trade_count, config->initial_capital);
    return ALPHA_OK;
}

void alpha_backtest_result_free(alpha_backtest_result_t *result) {
    if (result == NULL) {
        return;
    }
    free(result->trades);
    free(result->snapshots);
    result->trades = NULL;
    result->snapshots = NULL;
    result->trade_count = 0;
    result->snapshot_count = 0;
}
