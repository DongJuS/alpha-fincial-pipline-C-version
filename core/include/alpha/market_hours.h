#ifndef ALPHA_MARKET_HOURS_H
#define ALPHA_MARKET_HOURS_H

#include <stddef.h>
#include <stdint.h>

/* KRX trading-calendar helpers (src/utils/market_hours.py). Korea Standard Time
 * is a fixed UTC+9 with no DST, so all computation is exact integer epoch
 * arithmetic. Used by the L3 breaker lockout to compute an absolute expiry. */

#define ALPHA_KST_OFFSET_SECONDS 32400 /* +09:00 */

/* Next trading session start (09:00 KST) strictly after `from_epoch` (UTC
 * seconds). Weekend-only, exact parity with Python next_trading_day_start:
 * advance one day, then skip Saturday/Sunday. Returns the session start in UTC
 * epoch seconds (== that date 00:00 UTC == 09:00 KST). */
int64_t alpha_next_trading_session_start(int64_t from_epoch);

/* Fail-closed sentinel for the holiday-aware variant. */
#define ALPHA_SESSION_CALENDAR_INSUFFICIENT INT64_C(-1)

/* Holiday-aware safety variant: like the above but also skips any day whose KST
 * epoch-day number appears in `holiday_days` (sorted ascending, `count`
 * entries). `calendar_max_day` is the last KST epoch-day the calendar is known
 * to cover; if skipping would need a day beyond it, returns
 * ALPHA_SESSION_CALENDAR_INSUFFICIENT so the caller keeps the lockout (never
 * releases a breaker early on a missing/stale calendar). */
int64_t alpha_next_trading_session_start_holiday_aware(int64_t from_epoch,
                                                       const int64_t *holiday_days, size_t count,
                                                       int64_t calendar_max_day);

#endif
