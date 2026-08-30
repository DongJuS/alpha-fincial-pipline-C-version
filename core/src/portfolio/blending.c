#include "alpha/blending.h"

#include "alpha/round.h"

static double clamp01(double value) {
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

static double signal_score(alpha_signal_t signal) {
    if (signal == ALPHA_SIGNAL_BUY) {
        return 1.0;
    }
    if (signal == ALPHA_SIGNAL_SELL) {
        return -1.0;
    }
    return 0.0; /* HOLD and any other (e.g. CLOSE) score 0 */
}

alpha_blend_result_t alpha_blend_signals(const alpha_blend_input_t *inputs, size_t count) {
    alpha_blend_result_t result = {ALPHA_SIGNAL_HOLD, 0.0, 0.0, false};
    if (inputs == NULL || count == 0) {
        return result;
    }

    /* Pass 1: clamp and total the weights (weight >= 0). */
    double total_weight = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const double weight = inputs[i].weight > 0.0 ? inputs[i].weight : 0.0;
        total_weight += weight;
    }

    const double norm_factor = total_weight > 0.0 ? total_weight : 1.0;
    const double equal_weight = total_weight <= 0.0 ? 1.0 / (double)count : 0.0;

    double weighted_score = 0.0;
    double weighted_confidence = 0.0;
    bool has_buy = false;
    bool has_sell = false;

    for (size_t i = 0; i < count; ++i) {
        const alpha_signal_t signal = inputs[i].signal;
        const double confidence = clamp01(inputs[i].confidence);
        const double raw_weight = inputs[i].weight > 0.0 ? inputs[i].weight : 0.0;
        const double weight = total_weight > 0.0 ? raw_weight / norm_factor : equal_weight;

        weighted_score += signal_score(signal) * weight * confidence;
        weighted_confidence += confidence * weight;
        if (signal == ALPHA_SIGNAL_BUY) {
            has_buy = true;
        } else if (signal == ALPHA_SIGNAL_SELL) {
            has_sell = true;
        }
    }

    if (weighted_score > ALPHA_BLEND_THRESHOLD) {
        result.signal = ALPHA_SIGNAL_BUY;
    } else if (weighted_score < -ALPHA_BLEND_THRESHOLD) {
        result.signal = ALPHA_SIGNAL_SELL;
    } else {
        result.signal = ALPHA_SIGNAL_HOLD;
    }
    result.confidence = alpha_round_dp(clamp01(weighted_confidence), 4);
    result.weighted_score = alpha_round_dp(weighted_score, 6);
    result.conflict = (bool)(has_buy && has_sell);
    return result;
}

alpha_blend_result_t alpha_blend_ab(bool has_a, alpha_signal_t a_signal, double a_confidence,
                                    bool has_b, alpha_signal_t b_signal, double b_confidence,
                                    double blend_ratio) {
    const double ratio = clamp01(blend_ratio);
    alpha_blend_input_t inputs[2];
    size_t count = 0;
    if (has_a) {
        inputs[count].signal = a_signal;
        inputs[count].confidence = a_confidence;
        inputs[count].weight = 1.0 - ratio;
        count += 1;
    }
    if (has_b) {
        inputs[count].signal = b_signal;
        inputs[count].confidence = b_confidence;
        inputs[count].weight = ratio;
        count += 1;
    }
    return alpha_blend_signals(inputs, count);
}
