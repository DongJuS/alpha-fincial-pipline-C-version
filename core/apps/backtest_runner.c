/* backtest_runner — loads a bench fixture and either prints the result summary
 * or benchmarks the engine. Timing statistics are computed by the Python
 * wrapper (bench/run_c_backtest.py) so all variants share one methodology.
 * _DEFAULT_SOURCE (set by CMake) exposes clock_gettime/getrusage under -std=c11
 * on glibc; macOS ignores it and exposes them by default. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/resource.h>

#include "alpha/alpha.h"
#include "alpha/backtest.h"
#include "backtest_fixture.h"

static double monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

static long peak_rss_bytes(void) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    return usage.ru_maxrss; /* bytes on macOS */
#else
    return usage.ru_maxrss * 1024L; /* kilobytes on Linux */
#endif
}

/* One backtest run; returns 0 on success. */
static int run_once(const alpha_fixture_t *fixture, alpha_backtest_result_t *result) {
    alpha_replay_source_t replay = alpha_fixture_replay(fixture);
    const alpha_cost_model_t cost =
        alpha_cost_model_make(fixture->config.commission_rate_pct, fixture->config.tax_rate_pct,
                              fixture->config.slippage_bps);
    return alpha_backtest_run(&fixture->config, fixture->prices, fixture->dates, fixture->bar_count,
                              alpha_replay_signal, &replay, &cost, result) == ALPHA_OK
               ? 0
               : 1;
}

static int cmd_run(const alpha_fixture_t *fixture) {
    alpha_backtest_result_t result;
    if (run_once(fixture, &result) != 0) {
        fprintf(stderr, "backtest run failed\n");
        return 1;
    }
    const alpha_backtest_metrics_t *m = &result.metrics;
    printf("{\"bars\":%zu,\"trades\":%zu,\"final_portfolio_value\":%.10g,"
           "\"total_return_pct\":%.4f,\"annual_return_pct\":%.4f,\"sharpe_ratio\":%.4f,"
           "\"max_drawdown_pct\":%.4f,\"win_rate\":%.4f,\"avg_holding_days\":%.1f,"
           "\"baseline_return_pct\":%.4f,\"excess_return_pct\":%.4f}\n",
           result.snapshot_count, result.trade_count,
           result.snapshot_count ? result.snapshots[result.snapshot_count - 1].portfolio_value
                                 : 0.0,
           m->total_return_pct, m->annual_return_pct, m->sharpe_ratio, m->max_drawdown_pct,
           m->win_rate, m->avg_holding_days, m->baseline_return_pct, m->excess_return_pct);
    alpha_backtest_result_free(&result);
    return 0;
}

static int cmd_bench(const alpha_fixture_t *fixture) {
    const int trials = 10;
    const double target_ms = 1000.0;

    /* Auto-scale iterations until a trial exceeds the timer-noise target. */
    long iterations = 1;
    for (;;) {
        const double start = monotonic_ms();
        for (long i = 0; i < iterations; ++i) {
            alpha_backtest_result_t result;
            if (run_once(fixture, &result) != 0) {
                fprintf(stderr, "backtest run failed\n");
                return 1;
            }
            alpha_backtest_result_free(&result);
        }
        if (monotonic_ms() - start >= target_ms) {
            break;
        }
        iterations *= 2;
    }

    double *samples = (double *)malloc((size_t)trials * sizeof(double));
    double *totals = (double *)malloc((size_t)trials * sizeof(double));
    if (samples == NULL || totals == NULL) {
        free(samples);
        free(totals);
        return 1;
    }

    const clock_t cpu_start = clock();
    for (int t = 0; t < trials; ++t) {
        const double start = monotonic_ms();
        for (long i = 0; i < iterations; ++i) {
            alpha_backtest_result_t result;
            if (run_once(fixture, &result) != 0) {
                free(samples);
                free(totals);
                return 1;
            }
            alpha_backtest_result_free(&result);
        }
        const double elapsed = monotonic_ms() - start;
        totals[t] = elapsed;
        samples[t] = elapsed / (double)iterations;
    }
    const double cpu_ms = (double)(clock() - cpu_start) / (double)CLOCKS_PER_SEC * 1000.0;

    printf("{\"iterations_per_trial\":%ld,\"samples_ms\":[", iterations);
    for (int t = 0; t < trials; ++t) {
        printf("%s%.10g", t ? "," : "", samples[t]);
    }
    printf("],\"trial_totals_ms\":[");
    for (int t = 0; t < trials; ++t) {
        printf("%s%.10g", t ? "," : "", totals[t]);
    }
    printf("],\"peak_rss_bytes\":%ld,\"cpu_time_ms\":%.10g}\n", peak_rss_bytes(),
           cpu_ms / ((double)trials * (double)iterations));

    free(samples);
    free(totals);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <run|bench> <fixture.json>\n", argv[0]);
        return 2;
    }
    if (alpha_initialize() != ALPHA_OK) {
        fprintf(stderr, "alpha_initialize failed\n");
        return 1;
    }

    alpha_fixture_t fixture;
    if (alpha_fixture_load(argv[2], &fixture) != ALPHA_OK) {
        fprintf(stderr, "failed to load fixture: %s\n", argv[2]);
        return 1;
    }

    int rc;
    if (strcmp(argv[1], "run") == 0) {
        rc = cmd_run(&fixture);
    } else if (strcmp(argv[1], "bench") == 0) {
        rc = cmd_bench(&fixture);
    } else {
        fprintf(stderr, "unknown command: %s\n", argv[1]);
        rc = 2;
    }
    alpha_fixture_free(&fixture);
    return rc;
}
