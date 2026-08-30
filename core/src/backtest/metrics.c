#include "alpha/backtest.h"

#include <math.h>
#include <stdlib.h>

#include "alpha/round.h"

#define TRADING_DAYS_PER_YEAR 252.0

/* Sharpe ratio: mean / sample-std * sqrt(252), risk-free 0 (metrics._compute_sharpe).
 * Fewer than two returns, or non-positive std, yields 0. */
static double compute_sharpe(const double *returns, size_t n) {
    if (n < 2) {
        return 0.0;
    }
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += returns[i];
    }
    const double mean = sum / (double)n;
    double sq = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double d = returns[i] - mean;
        sq += d * d;
    }
    const double variance = sq / (double)(n - 1);
    const double std = sqrt(variance);
    if (std <= 0.0) {
        return 0.0;
    }
    return (mean / std) * sqrt(TRADING_DAYS_PER_YEAR);
}

/* Max drawdown: peak-to-trough percent, <= 0 (metrics._compute_mdd). */
static double compute_mdd(const alpha_daily_snapshot_t *snapshots, size_t n) {
    double peak = snapshots[0].portfolio_value;
    double mdd = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double value = snapshots[i].portfolio_value;
        if (value > peak) {
            peak = value;
        }
        const double dd = ((value - peak) / peak) * 100.0;
        if (dd < mdd) {
            mdd = dd;
        }
    }
    return mdd;
}

/* FIFO BUY->SELL matching, mean of max(0, holding days) (metrics._compute_avg_holding_days).
 * `buy_stack` provides scratch space of at least nt entries. */
static double compute_avg_holding_days(const alpha_trade_record_t *trades, size_t nt,
                                       int64_t *buy_queue) {
    size_t head = 0;
    size_t tail = 0;
    int64_t total_holding = 0;
    int64_t matched = 0;
    for (size_t i = 0; i < nt; ++i) {
        if (trades[i].side == ALPHA_SIDE_BUY) {
            buy_queue[tail++] = trades[i].date;
        } else if (head < tail) {
            const int64_t holding = trades[i].date - buy_queue[head++];
            total_holding += holding > 0 ? holding : 0;
            matched += 1;
        }
    }
    return matched ? (double)total_holding / (double)matched : 0.0;
}

alpha_backtest_metrics_t alpha_compute_metrics(const alpha_daily_snapshot_t *snapshots, size_t ns,
                                               const alpha_trade_record_t *trades, size_t nt,
                                               int64_t initial_capital) {
    alpha_backtest_metrics_t metrics = {0};
    if (ns == 0) {
        return metrics;
    }

    const double initial = (double)initial_capital;
    const double final_value = snapshots[ns - 1].portfolio_value;

    const double total_return = (final_value - initial) / initial;
    const double total_return_pct = total_return * 100.0;

    double annual_return_pct = 0.0;
    if (ns > 1 && total_return > -1.0) {
        annual_return_pct =
            (pow(1.0 + total_return, TRADING_DAYS_PER_YEAR / (double)ns) - 1.0) * 100.0;
    }

    /* Sharpe over daily returns expressed as fractions. */
    double sharpe = 0.0;
    {
        /* daily_returns are read once; reuse a stack buffer capped by ns. */
        double stack_returns[512];
        double *returns = stack_returns;
        double *heap = NULL;
        if (ns > sizeof(stack_returns) / sizeof(stack_returns[0])) {
            heap = (double *)malloc(ns * sizeof(double));
            returns = heap;
        }
        if (returns != NULL) {
            for (size_t i = 0; i < ns; ++i) {
                returns[i] = snapshots[i].daily_return_pct / 100.0;
            }
            sharpe = compute_sharpe(returns, ns);
        }
        free(heap);
    }

    const double mdd = compute_mdd(snapshots, ns);

    double win_rate = 0.0;
    {
        int64_t sells = 0;
        int64_t wins = 0;
        for (size_t i = 0; i < nt; ++i) {
            if (trades[i].side == ALPHA_SIDE_SELL) {
                sells += 1;
                if (trades[i].pnl > 0.0) {
                    wins += 1;
                }
            }
        }
        if (sells > 0) {
            win_rate = ((double)wins / (double)sells) * 100.0;
        }
    }

    double avg_holding_days = 0.0;
    if (nt > 0) {
        int64_t stack_queue[512];
        int64_t *queue = stack_queue;
        int64_t *heap = NULL;
        if (nt > sizeof(stack_queue) / sizeof(stack_queue[0])) {
            heap = (int64_t *)malloc(nt * sizeof(int64_t));
            queue = heap;
        }
        if (queue != NULL) {
            avg_holding_days = compute_avg_holding_days(trades, nt, queue);
        }
        free(heap);
    }

    const double first_price = snapshots[0].close_price;
    const double last_price = snapshots[ns - 1].close_price;
    const double baseline_return_pct =
        first_price > 0.0 ? ((last_price / first_price) - 1.0) * 100.0 : 0.0;
    const double excess_return_pct = total_return_pct - baseline_return_pct;

    metrics.total_return_pct = alpha_round_dp(total_return_pct, 4);
    metrics.annual_return_pct = alpha_round_dp(annual_return_pct, 4);
    metrics.sharpe_ratio = alpha_round_dp(sharpe, 4);
    metrics.max_drawdown_pct = alpha_round_dp(mdd, 4);
    metrics.win_rate = alpha_round_dp(win_rate, 4);
    metrics.total_trades = (int64_t)nt;
    metrics.avg_holding_days = alpha_round_dp(avg_holding_days, 1);
    metrics.baseline_return_pct = alpha_round_dp(baseline_return_pct, 4);
    metrics.excess_return_pct = alpha_round_dp(excess_return_pct, 4);
    return metrics;
}
