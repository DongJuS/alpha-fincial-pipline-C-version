#ifndef ALPHA_DATE_H
#define ALPHA_DATE_H

#include <stddef.h>
#include <stdint.h>

#include "alpha/errors.h"

/* Calendar date represented as a proleptic-Gregorian day number.
 *
 * The day number is days-from-civil relative to 1970-01-01 (Hinnant's
 * algorithm). Only differences and equality are relied upon, so any fixed
 * epoch reproduces Python `(date_b - date_a).days` exactly. */

/* Parse an ISO "YYYY-MM-DD" string into an epoch-day number.
 * Returns ALPHA_ERR_INVALID_ARG on malformed input. */
alpha_err_t alpha_date_parse(const char *iso, int64_t *out_epoch_day);

/* Convert an epoch-day number back to "YYYY-MM-DD" (needs >= 11 bytes). */
alpha_err_t alpha_date_format(int64_t epoch_day, char *out, size_t n);

/* Whole-day difference b - a, matching Python `(b - a).days`. */
static inline int64_t alpha_date_diff_days(int64_t a, int64_t b) { return b - a; }

#endif
