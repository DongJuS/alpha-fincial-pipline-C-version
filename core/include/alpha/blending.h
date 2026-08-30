#ifndef ALPHA_BLENDING_H
#define ALPHA_BLENDING_H

#include <stdbool.h>
#include <stddef.h>

#include "alpha/domain.h"

/* N-way signal blending (src/agents/blending.py). Score map BUY=+1, HOLD=0,
 * SELL=-1; decision threshold 0.15. */
#define ALPHA_BLEND_THRESHOLD 0.15

typedef struct {
    alpha_signal_t signal; /* BUY/SELL/HOLD; CLOSE or unknown scores as HOLD */
    double confidence;     /* clamped to [0,1] */
    double weight;         /* clamped to >= 0 */
} alpha_blend_input_t;

typedef struct {
    alpha_signal_t signal; /* BUY / SELL / HOLD */
    double confidence;     /* weighted mean, clamped [0,1], 4 dp */
    double weighted_score; /* weighted score, 6 dp */
    bool conflict;         /* has_buy && has_sell */
} alpha_blend_result_t;

/* Blend N strategy inputs. Empty -> HOLD/0. Weights auto-normalize; zero total
 * weight -> equal weights. */
alpha_blend_result_t alpha_blend_signals(const alpha_blend_input_t *inputs, size_t count);

/* 2-way A/B wrapper (blend_strategy_signals). Absent legs are excluded; A weight
 * = 1 - clamp(ratio,0,1), B weight = clamp(ratio,0,1). Empty -> HOLD/0.
 * weighted_score is populated but the Python 2-way result exposes only
 * signal/confidence/conflict. */
alpha_blend_result_t alpha_blend_ab(bool has_a, alpha_signal_t a_signal, double a_confidence,
                                    bool has_b, alpha_signal_t b_signal, double b_confidence,
                                    double blend_ratio);

#endif
