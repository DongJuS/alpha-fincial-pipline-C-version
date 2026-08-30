#include "alpha/date.h"

#include <stdio.h>
#include <string.h>

/* Howard Hinnant's days-from-civil: days since 1970-01-01 for a proleptic
 * Gregorian (y, m, d). Exact for the full range we care about. */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= (m <= 2);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153U * (m + (m > 2 ? -3U : 9U)) + 2U) / 5U + d - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int64_t *y, unsigned *m, unsigned *d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    const int64_t yy = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    const unsigned mp = (5U * doy + 2U) / 153U;
    *d = doy - (153U * mp + 2U) / 5U + 1U;
    *m = mp + (mp < 10U ? 3U : -9U);
    *y = yy + (*m <= 2);
}

/* Parse `len` ASCII digits into a non-negative int; returns -1 on any non-digit. */
static int parse_digits(const char *s, int len, int *out) {
    int value = 0;
    for (int i = 0; i < len; ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        value = value * 10 + (s[i] - '0');
    }
    *out = value;
    return 0;
}

alpha_err_t alpha_date_parse(const char *iso, int64_t *out_epoch_day) {
    if (iso == NULL || out_epoch_day == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    /* Require exactly "YYYY-MM-DD". */
    if (strlen(iso) != 10 || iso[4] != '-' || iso[7] != '-') {
        return ALPHA_ERR_INVALID_ARG;
    }
    int year = 0;
    int month = 0;
    int day = 0;
    if (parse_digits(iso, 4, &year) != 0 || parse_digits(iso + 5, 2, &month) != 0 ||
        parse_digits(iso + 8, 2, &day) != 0) {
        return ALPHA_ERR_INVALID_ARG;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return ALPHA_ERR_INVALID_ARG;
    }
    *out_epoch_day = days_from_civil(year, (unsigned)month, (unsigned)day);
    return ALPHA_OK;
}

alpha_err_t alpha_date_format(int64_t epoch_day, char *out, size_t n) {
    if (out == NULL || n < 11) {
        return ALPHA_ERR_INVALID_ARG;
    }
    int64_t year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civil_from_days(epoch_day, &year, &month, &day);
    snprintf(out, n, "%04lld-%02u-%02u", (long long)year, month, day);
    return ALPHA_OK;
}
