#include "alpha/market_data.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "alpha/round.h"

static const char *market_suffix(alpha_market_t market) {
    switch (market) {
    case ALPHA_MARKET_KOSPI:
        return "KS";
    case ALPHA_MARKET_KOSDAQ:
        return "KQ";
    case ALPHA_MARKET_NYSE:
    case ALPHA_MARKET_NASDAQ:
        return "US";
    default:
        return "KS";
    }
}

static alpha_market_t suffix_market(const char *suffix) {
    if (strcmp(suffix, "KS") == 0) {
        return ALPHA_MARKET_KOSPI;
    }
    if (strcmp(suffix, "KQ") == 0) {
        return ALPHA_MARKET_KOSDAQ;
    }
    if (strcmp(suffix, "US") == 0) {
        return ALPHA_MARKET_NYSE;
    }
    return ALPHA_MARKET_KOSPI;
}

alpha_err_t alpha_to_instrument_id(const char *ticker, alpha_market_t market, char *out, size_t n) {
    if (ticker == NULL || out == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    const char *suffix = market_suffix(market);
    const int written = snprintf(out, n, "%s.%s", ticker, suffix);
    if (written < 0 || (size_t)written >= n) {
        return ALPHA_ERR_RANGE;
    }
    return ALPHA_OK;
}

alpha_err_t alpha_from_instrument_id(const char *instrument_id, char *raw_out, size_t raw_n,
                                     alpha_market_t *market_out) {
    if (instrument_id == NULL || raw_out == NULL || market_out == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    /* Split on the last '.', matching Python rsplit(".", 1). */
    const char *dot = strrchr(instrument_id, '.');
    const char *suffix = "KS";
    size_t raw_len = strlen(instrument_id);
    if (dot != NULL) {
        raw_len = (size_t)(dot - instrument_id);
        suffix = dot + 1;
    }
    if (raw_len >= raw_n) {
        return ALPHA_ERR_RANGE;
    }
    memcpy(raw_out, instrument_id, raw_len);
    raw_out[raw_len] = '\0';
    *market_out = suffix_market(suffix);
    return ALPHA_OK;
}

alpha_err_t alpha_sanitize_change_pct(double value, bool *has_value, double *out) {
    if (has_value == NULL || out == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    if (!isfinite(value) || fabs(value) > ALPHA_MAX_ABS_CHANGE_PCT) {
        *has_value = false;
        return ALPHA_OK;
    }
    *has_value = true;
    *out = alpha_round_dp(value, 3);
    return ALPHA_OK;
}

alpha_err_t alpha_compute_change_pct(double current, bool has_previous, double previous,
                                     bool *has_value, double *out) {
    if (has_value == NULL || out == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    if (!has_previous || previous <= 0.0 || !isfinite(current) || !isfinite(previous)) {
        *has_value = false;
        return ALPHA_OK;
    }
    return alpha_sanitize_change_pct(((current - previous) / previous) * 100.0, has_value, out);
}
