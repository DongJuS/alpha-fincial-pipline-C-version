#include "alpha/backtest.h"

/* ReplaySignalSource.get_signal: return the mapped signal for the date, or
 * HOLD when the date is absent. prices/n/position are part of the SignalSource
 * seam but unused by replay (an RL policy would consume them). */
alpha_signal_t alpha_replay_signal(void *ctx, int64_t epoch_day, const double *prices, size_t n,
                                   int64_t position) {
    (void)prices;
    (void)n;
    (void)position;
    const alpha_replay_source_t *source = (const alpha_replay_source_t *)ctx;
    for (size_t i = 0; i < source->count; ++i) {
        if (source->days[i] == epoch_day) {
            return source->signals[i];
        }
    }
    return ALPHA_SIGNAL_HOLD;
}
