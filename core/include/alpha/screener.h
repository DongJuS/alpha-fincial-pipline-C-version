#ifndef ALPHA_SCREENER_H
#define ALPHA_SCREENER_H

#include <stdbool.h>
#include <stddef.h>

/* Non-RL daily screener (src/agents/screener.py). Pure decision logic only: the
 * DB fetch and async gather live in the collector/driver layer. Constants come
 * from constants.h (volume-surge 2.0x, change-pct 3.0%, top 10). */

#define ALPHA_SCREENER_MIN_DATA_DAYS 5

/* One daily bar as consumed by the screener. change_pct is optional (a missing
 * value counts as 0), mirroring `today.get("change_pct") or 0.0`. */
typedef struct {
    double volume;
    double change_pct;
    bool has_change_pct;
} alpha_screener_bar_t;

typedef struct {
    bool passes;
    double score;
} alpha_screener_score_t;

/* Combined _evaluate/_score_ticker: bars[0] is today, bars[1..] the past window.
 * Fewer than ALPHA_SCREENER_MIN_DATA_DAYS bars -> {false, 0}. Otherwise
 * volume_ratio = today/avg(past) (0 if avg<=0); change = abs(today change or 0);
 * passes = ratio>=vol_threshold OR change>=pct_threshold;
 * score  = ratio/vol_threshold + change/pct_threshold. */
alpha_screener_score_t alpha_screener_score(const alpha_screener_bar_t *bars, size_t bar_count,
                                            double vol_threshold, double pct_threshold);

/* Selection: from `count` scored candidates keep the passing ones, order by
 * score descending (stable on ties = input order), and write up to `cap` indices
 * into out_indices (capacity >= min(cap, count)). Returns the number written. */
size_t alpha_screener_select(const alpha_screener_score_t *candidates, size_t count, size_t cap,
                             size_t *out_indices);

#endif
