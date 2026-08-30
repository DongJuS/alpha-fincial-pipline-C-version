#ifndef ALPHA_MARKET_DATA_H
#define ALPHA_MARKET_DATA_H

#include <stdbool.h>
#include <stddef.h>

#include "alpha/domain.h"
#include "alpha/errors.h"

/* Market-data normalization (src/utils/market_data.py). "Optional double" is
 * modelled as an ALPHA_OK result plus a `has_value` out-flag; NaN is never used
 * as a sentinel. */

#define ALPHA_MAX_ABS_CHANGE_PCT 999.999

/* raw code + market -> instrument id, e.g. "005930" + KOSPI -> "005930.KS".
 * market->suffix: KOSPI=KS, KOSDAQ=KQ, NYSE=US, NASDAQ=US. */
alpha_err_t alpha_to_instrument_id(const char *ticker, alpha_market_t market, char *out, size_t n);

/* instrument id -> (raw code, market); splits on the LAST '.'. Missing suffix
 * defaults to KS; suffix->market: KS=KOSPI, KQ=KOSDAQ, US=NYSE, else KOSPI. */
alpha_err_t alpha_from_instrument_id(const char *instrument_id, char *raw_out, size_t raw_n,
                                     alpha_market_t *market_out);

/* sanitize_change_pct: non-finite or |value|>999.999 -> has_value=false;
 * otherwise *out = round(value, 3). */
alpha_err_t alpha_sanitize_change_pct(double value, bool *has_value, double *out);

/* compute_change_pct: previous absent/<=0 or non-finite -> has_value=false;
 * otherwise sanitize((current-previous)/previous*100). */
alpha_err_t alpha_compute_change_pct(double current, bool has_previous, double previous,
                                     bool *has_value, double *out);

#endif
