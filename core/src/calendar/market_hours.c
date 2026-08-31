#include "alpha/market_hours.h"

#define SECONDS_PER_DAY INT64_C(86400)

/* Floor division that is correct for negative numerators (unlike C's trunc). */
static int64_t floor_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        q -= 1;
    }
    return q;
}

/* Python date.weekday(): Mon=0 .. Sun=6. Epoch day 0 (1970-01-01) is Thursday
 * (weekday 3). Weekend is 5 (Sat) or 6 (Sun). */
static int is_weekend(int64_t epoch_day) {
    const int64_t weekday = ((epoch_day + 3) % 7 + 7) % 7;
    return weekday >= 5;
}

/* KST epoch-day of the calendar day containing `from_epoch`. */
static int64_t kst_day_of(int64_t from_epoch) {
    return floor_div(from_epoch + ALPHA_KST_OFFSET_SECONDS, SECONDS_PER_DAY);
}

/* UTC epoch seconds for 09:00 KST on the given KST epoch-day (== day 00:00 UTC). */
static int64_t session_start_epoch(int64_t kst_day) {
    return kst_day * SECONDS_PER_DAY + INT64_C(9) * 3600 - ALPHA_KST_OFFSET_SECONDS;
}

int64_t alpha_next_trading_session_start(int64_t from_epoch) {
    int64_t day = kst_day_of(from_epoch) + 1;
    while (is_weekend(day)) {
        day += 1;
    }
    return session_start_epoch(day);
}

static int is_holiday(int64_t epoch_day, const int64_t *holiday_days, size_t count) {
    /* Sorted ascending; small calendars, linear scan is fine. */
    for (size_t i = 0; i < count; ++i) {
        if (holiday_days[i] == epoch_day) {
            return 1;
        }
        if (holiday_days[i] > epoch_day) {
            break;
        }
    }
    return 0;
}

int64_t alpha_next_trading_session_start_holiday_aware(int64_t from_epoch,
                                                       const int64_t *holiday_days, size_t count,
                                                       int64_t calendar_max_day) {
    int64_t day = kst_day_of(from_epoch) + 1;
    while (is_weekend(day) || is_holiday(day, holiday_days, count)) {
        if (day > calendar_max_day) {
            /* Beyond known coverage: fail closed, keep the lockout. */
            return ALPHA_SESSION_CALENDAR_INSUFFICIENT;
        }
        day += 1;
    }
    /* The chosen trading day itself must be within calendar coverage. */
    if (day > calendar_max_day) {
        return ALPHA_SESSION_CALENDAR_INSUFFICIENT;
    }
    return session_start_epoch(day);
}
