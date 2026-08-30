/* blend_runner — benchmarks alpha_blend_signals over a deterministic synthetic
 * batch. The batch formula is mirrored byte-for-byte in bench/run_python_blend.py
 * so the emitted BUY-count checksum cross-checks that both variants processed the
 * identical workload. Timing statistics are computed by the Python wrapper. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/resource.h>

#include "alpha/alpha.h"
#include "alpha/blending.h"

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

static const alpha_signal_t SLOT0[3] = {ALPHA_SIGNAL_BUY, ALPHA_SIGNAL_SELL, ALPHA_SIGNAL_HOLD};
static const alpha_signal_t SLOT1[3] = {ALPHA_SIGNAL_HOLD, ALPHA_SIGNAL_BUY, ALPHA_SIGNAL_SELL};
static const alpha_signal_t SLOT2[3] = {ALPHA_SIGNAL_SELL, ALPHA_SIGNAL_HOLD, ALPHA_SIGNAL_BUY};

/* Build the i-th deterministic 3-way set and blend it; returns the decision. */
static alpha_signal_t blend_case(long i) {
    alpha_blend_input_t inputs[3];
    inputs[0].signal = SLOT0[i % 3];
    inputs[1].signal = SLOT1[(i / 3) % 3];
    inputs[2].signal = SLOT2[(i / 9) % 3];
    for (int j = 0; j < 3; ++j) {
        inputs[j].confidence = (double)((i * (j + 1)) % 100) / 100.0;
        inputs[j].weight = (double)((i + j) % 5 + 1);
    }
    return alpha_blend_signals(inputs, 3).signal;
}

/* Run the batch once; returns the count of BUY decisions (the checksum). */
static long run_batch(long count) {
    long buys = 0;
    for (long i = 0; i < count; ++i) {
        if (blend_case(i) == ALPHA_SIGNAL_BUY) {
            buys += 1;
        }
    }
    return buys;
}

static int cmd_run(long count) {
    printf("{\"count\":%ld,\"buy_checksum\":%ld}\n", count, run_batch(count));
    return 0;
}

static int cmd_bench(long count) {
    const int trials = 10;
    const long checksum = run_batch(count); /* warm up + capture checksum */

    double *samples = (double *)malloc((size_t)trials * sizeof(double));
    if (samples == NULL) {
        return 1;
    }
    unsigned long sink = 0; /* accumulate so the optimizer cannot elide the work */
    const clock_t cpu_start = clock();
    for (int t = 0; t < trials; ++t) {
        const double start = monotonic_ms();
        sink += (unsigned long)run_batch(count);
        samples[t] = monotonic_ms() - start;
    }
    const double cpu_ms = (double)(clock() - cpu_start) / (double)CLOCKS_PER_SEC * 1000.0;

    printf("{\"count\":%ld,\"buy_checksum\":%ld,\"samples_ms\":[", count, checksum);
    for (int t = 0; t < trials; ++t) {
        printf("%s%.10g", t ? "," : "", samples[t]);
    }
    printf("],\"peak_rss_bytes\":%ld,\"cpu_time_ms\":%.10g,\"sink\":%lu}\n", peak_rss_bytes(),
           cpu_ms / (double)trials, sink);
    free(samples);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <run|bench> <count>\n", argv[0]);
        return 2;
    }
    if (alpha_initialize() != ALPHA_OK) {
        return 1;
    }
    const long count = strtol(argv[2], NULL, 10);
    if (count <= 0) {
        fprintf(stderr, "count must be positive\n");
        return 2;
    }
    if (strcmp(argv[1], "run") == 0) {
        return cmd_run(count);
    }
    if (strcmp(argv[1], "bench") == 0) {
        return cmd_bench(count);
    }
    fprintf(stderr, "unknown command: %s\n", argv[1]);
    return 2;
}
