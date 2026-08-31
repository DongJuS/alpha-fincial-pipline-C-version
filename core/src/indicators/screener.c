#include "alpha/screener.h"

#include <math.h>

alpha_screener_score_t alpha_screener_score(const alpha_screener_bar_t *bars, size_t bar_count,
                                            double vol_threshold, double pct_threshold) {
    alpha_screener_score_t result = {false, 0.0};
    if (bar_count < ALPHA_SCREENER_MIN_DATA_DAYS) {
        return result;
    }
    const double today_volume = bars[0].volume;
    const double change_pct = bars[0].has_change_pct ? fabs(bars[0].change_pct) : 0.0;

    double past_sum = 0.0;
    const size_t past_count = bar_count - 1;
    for (size_t i = 1; i < bar_count; ++i) {
        past_sum += bars[i].volume;
    }
    const double avg_volume = past_count > 0 ? past_sum / (double)past_count : 0.0;
    const double volume_ratio = avg_volume > 0.0 ? today_volume / avg_volume : 0.0;

    result.passes = (bool)(volume_ratio >= vol_threshold || change_pct >= pct_threshold);
    result.score = (volume_ratio / vol_threshold) + (change_pct / pct_threshold);
    return result;
}

size_t alpha_screener_select(const alpha_screener_score_t *candidates, size_t count, size_t cap,
                             size_t *out_indices) {
    size_t selected = 0;
    while (selected < cap) {
        long best = -1;
        for (size_t i = 0; i < count; ++i) {
            if (!candidates[i].passes) {
                continue;
            }
            bool already = false;
            for (size_t k = 0; k < selected; ++k) {
                if (out_indices[k] == i) {
                    already = true;
                    break;
                }
            }
            if (already) {
                continue;
            }
            /* Strict '>' keeps the earliest index on equal scores, matching
             * Python's stable descending sort. */
            if (best < 0 || candidates[i].score > candidates[best].score) {
                best = (long)i;
            }
        }
        if (best < 0) {
            break;
        }
        out_indices[selected++] = (size_t)best;
    }
    return selected;
}
