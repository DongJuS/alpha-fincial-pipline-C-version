#ifndef ALPHA_BACKTEST_H
#define ALPHA_BACKTEST_H

#include <stddef.h>
#include <stdint.h>

#include "alpha/cost_model.h"
#include "alpha/domain.h"
#include "alpha/errors.h"

/* Backtest data models (src/backtest/models.py). Dates are epoch-day numbers
 * (see alpha/date.h). Strings are fixed inline buffers to keep results POD. */

#define ALPHA_TICKER_CAP 32
#define ALPHA_STRATEGY_CAP 16

typedef struct {
    char ticker[ALPHA_TICKER_CAP];
    char strategy[ALPHA_STRATEGY_CAP];
    int64_t train_start;
    int64_t train_end;
    int64_t test_start;
    int64_t test_end;
    int64_t initial_capital;
    double commission_rate_pct;
    double tax_rate_pct;
    int slippage_bps;
} alpha_backtest_config_t;

typedef struct {
    int64_t date;
    alpha_side_t side; /* ALPHA_SIDE_BUY (open) or ALPHA_SIDE_SELL (close) */
    char ticker[ALPHA_TICKER_CAP];
    double price;
    int64_t quantity;
    double commission;
    double tax;
    double slippage_cost;
    double total_cost;
    double pnl;
} alpha_trade_record_t;

typedef struct {
    int64_t date;
    double close_price;
    double cash;
    int64_t position_qty;
    double position_value;
    double portfolio_value;
    double daily_return_pct;
} alpha_daily_snapshot_t;

typedef struct {
    double total_return_pct;
    double annual_return_pct;
    double sharpe_ratio;
    double max_drawdown_pct;
    double win_rate;
    int64_t total_trades;
    double avg_holding_days;
    double baseline_return_pct;
    double excess_return_pct;
} alpha_backtest_metrics_t;

typedef struct {
    alpha_backtest_config_t config;
    alpha_backtest_metrics_t metrics;
    alpha_trade_record_t *trades;
    size_t trade_count;
    alpha_daily_snapshot_t *snapshots;
    size_t snapshot_count;
} alpha_backtest_result_t;

/* SignalSource seam (MODULE_SPECS §3a). An external RL policy can be replayed
 * later by supplying a different function; only ReplaySignalSource ships now. */
typedef alpha_signal_t (*alpha_signal_fn)(void *ctx, int64_t epoch_day, const double *prices,
                                          size_t n, int64_t position);

/* ReplaySignalSource: {date -> signal}; missing date -> HOLD.
 * `days` must be a lookup table the signal fn can scan. */
typedef struct {
    const int64_t *days;
    const alpha_signal_t *signals;
    size_t count;
} alpha_replay_source_t;

alpha_signal_t alpha_replay_signal(void *ctx, int64_t epoch_day, const double *prices, size_t n,
                                   int64_t position);

/* Metrics over snapshots+trades (compute_backtest_metrics). Empty -> all zero. */
alpha_backtest_metrics_t alpha_compute_metrics(const alpha_daily_snapshot_t *snapshots, size_t ns,
                                               const alpha_trade_record_t *trades, size_t nt,
                                               int64_t initial_capital);

/* Run the backtest engine (BacktestEngine.run). Allocates result buffers; free
 * with alpha_backtest_result_free. Returns ALPHA_ERR_INVALID_ARG on
 * train_end >= test_start or NULL args, ALPHA_ERR_IO on allocation failure. */
alpha_err_t alpha_backtest_run(const alpha_backtest_config_t *config, const double *prices,
                               const int64_t *dates, size_t n, alpha_signal_fn signal_fn,
                               void *signal_ctx, const alpha_cost_model_t *cost,
                               alpha_backtest_result_t *out);

void alpha_backtest_result_free(alpha_backtest_result_t *result);

#endif
