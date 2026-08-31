# Redis key contract

Pinned reference: Python `3642cdc0e4026424ca9b6158125551eee1d42683`.

| Key | Value | Expiry | Owner |
|---|---|---|---|
| `redis:cache:latest_ticks:{ticker}` | canonical tick JSON | 60 seconds | retained Python collector / compatible processors |
| `krx:holidays:{year}` | sorted JSON array of `YYYY-MM-DD` | positive TTL required | retained Python holiday fetcher |
| `hard_stop:lockout:{scope}` | absolute Unix expiry as decimal | Redis `EXAT` at the same epoch | broker safety edge |

The hard-stop name matches `src/services/trading_mode.py`. Its absolute expiry is
a C-side safety extension because the Python daily-loss check is recomputed from
realized P&L rather than persisted. Readers fail closed when the value, TTL, or
calendar is missing, stale, malformed, or inconsistent. Only an explicit clear
or Redis expiry at the next covered KRX session may release it.
