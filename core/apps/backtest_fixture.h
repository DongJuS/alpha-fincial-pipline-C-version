#ifndef ALPHA_BACKTEST_FIXTURE_H
#define ALPHA_BACKTEST_FIXTURE_H

#include <stddef.h>
#include <stdint.h>

#include "alpha/backtest.h"

/* A loaded backtest fixture: config, aligned price/date bars, and the replay
 * signal table. Owns its heap buffers; release with alpha_fixture_free. */
typedef struct {
    alpha_backtest_config_t config;
    double *prices;
    int64_t *dates;
    size_t bar_count;
    int64_t *signal_days;
    alpha_signal_t *signal_values;
    size_t signal_count;
} alpha_fixture_t;

/* Parse a bench/fixtures JSON file. Returns ALPHA_OK on success. */
alpha_err_t alpha_fixture_load(const char *path, alpha_fixture_t *out);

void alpha_fixture_free(alpha_fixture_t *fixture);

/* Convenience: build a replay source view over the fixture's signal table. */
alpha_replay_source_t alpha_fixture_replay(const alpha_fixture_t *fixture);

#endif
