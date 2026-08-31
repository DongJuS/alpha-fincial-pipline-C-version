#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/resource.h>

#include "alpha/risk.h"

typedef struct {
    long take_profit;
    long stop_loss;
    long daily_blocked;
    long buy_allowed;
} risk_counts_t;

static double monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

static long peak_rss_bytes(void) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    return usage.ru_maxrss;
#else
    return usage.ru_maxrss * 1024L;
#endif
}

static risk_counts_t run_batch(long count) {
    const alpha_risk_config_t config = alpha_risk_config_default();
    risk_counts_t result = {0, 0, 0, 0};
    for (long i = 0; i < count; ++i) {
        alpha_position_t position = {{0}, 1, 100.0, 90.0 + (double)(i % 21)};
        const alpha_exit_kind_t exit = alpha_check_rule_based_exit(&position, &config, NULL);
        result.take_profit += exit == ALPHA_EXIT_TAKE_PROFIT;
        result.stop_loss += exit == ALPHA_EXIT_STOP_LOSS;
        result.daily_blocked +=
            alpha_is_daily_loss_blocked((double)((i % 9) - 5), &config) ? 1L : 0L;

        const double existing = (double)(i % 25) * 100.0;
        const double intended = (double)(((i * 7) % 20) + 1) * 100.0;
        const double total = 10000.0 + (double)(i % 100) * 100.0;
        const double denominator =
            alpha_max_position_denominator(i % 2 == 0, total, 10000.0, intended);
        result.buy_allowed +=
            alpha_max_position_allows_buy(&config, existing, intended, denominator, NULL) ? 1L : 0L;
    }
    return result;
}

static unsigned long checksum(risk_counts_t value) {
    return (unsigned long)value.take_profit * 1000000009UL +
           (unsigned long)value.stop_loss * 1000003UL +
           (unsigned long)value.daily_blocked * 1009UL + (unsigned long)value.buy_allowed;
}

static void print_counts(risk_counts_t value) {
    printf("\"take_profit\":%ld,\"stop_loss\":%ld,\"daily_blocked\":%ld,"
           "\"buy_allowed\":%ld",
           value.take_profit, value.stop_loss, value.daily_blocked, value.buy_allowed);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <run|bench> <count>\n", argv[0]);
        return 2;
    }
    const long count = strtol(argv[2], NULL, 10);
    if (count <= 0) {
        return 2;
    }
    const risk_counts_t counts = run_batch(count);
    if (strcmp(argv[1], "run") == 0) {
        printf("{\"count\":%ld,\"counts\":{", count);
        print_counts(counts);
        printf("},\"checksum\":%lu}\n", checksum(counts));
        return 0;
    }
    if (strcmp(argv[1], "bench") != 0) {
        return 2;
    }
    const int trials = 10;
    double samples[10];
    unsigned long sink = 0;
    const clock_t cpu_start = clock();
    for (int trial = 0; trial < trials; ++trial) {
        const double start = monotonic_ms();
        sink += checksum(run_batch(count));
        samples[trial] = monotonic_ms() - start;
    }
    const double cpu_ms = (double)(clock() - cpu_start) / CLOCKS_PER_SEC * 1000.0 / trials;
    printf("{\"count\":%ld,\"counts\":{", count);
    print_counts(counts);
    printf("},\"checksum\":%lu,\"samples_ms\":[", checksum(counts));
    for (int trial = 0; trial < trials; ++trial) {
        printf("%s%.10g", trial ? "," : "", samples[trial]);
    }
    printf("],\"peak_rss_bytes\":%ld,\"cpu_time_ms\":%.10g,\"sink\":%lu}\n", peak_rss_bytes(),
           cpu_ms, sink);
    return 0;
}
