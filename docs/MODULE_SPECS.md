# MODULE_SPECS.md — C core module contracts & exact algorithms

> The implementing agent must reproduce these behaviors **exactly** (numeric
> outputs match Python within `docs/BUILD_AND_TEST.md` tolerance). Each section
> cites the Python source of truth in `../alpha-financial-pipeline`.
> Signatures below are the intended **C ABI** in `core/include/alpha/`. Rust
> network/data adapters consume these contracts via FFI — they do not reimplement
> deterministic decisions.

---

## 0. domain — types, enums, constants
Source: `src/db/models.py`, `src/constants.py`

```c
typedef enum { ALPHA_SIGNAL_BUY, ALPHA_SIGNAL_SELL, ALPHA_SIGNAL_HOLD, ALPHA_SIGNAL_CLOSE } alpha_signal_t;
typedef enum { ALPHA_SIDE_BUY, ALPHA_SIDE_SELL } alpha_side_t;
typedef enum { ALPHA_MARKET_KOSPI, ALPHA_MARKET_KOSDAQ, ALPHA_MARKET_NYSE, ALPHA_MARKET_NASDAQ } alpha_market_t;
typedef enum { ALPHA_OK = 0, ALPHA_ERR_INVALID_ARG, ALPHA_ERR_RANGE, ALPHA_ERR_IO, ALPHA_ERR_DB, ALPHA_ERR_HTTP } alpha_err_t;
```

Constants (mirror `src/constants.py` verbatim):
```
PAPER_TRADING_INITIAL_CAPITAL     = 10 000 000   (int64, KRW)
BACKTEST_COMMISSION_RATE_PCT      = 0.015         (percent, both sides)
BACKTEST_TAX_RATE_PCT             = 0.18          (percent, SELL only)
BACKTEST_SLIPPAGE_BPS             = 3             (basis points)
MAX_TICKERS_PER_WS                = 40
MAX_SCREENED_TICKERS              = 10
DEFAULT_COLLECTOR_DAILY_LIMIT     = 100
SCREENER_VOLUME_SURGE_RATIO       = 2.0
SCREENER_CHANGE_PCT_THRESHOLD     = 3.0
```
`PredictionSignal` fields to carry (from `models.py:86`): agent_id, llm_model,
strategy ("A"/"B"/"RL"/"S"), ticker, signal, confidence (0..1), trading_date.

---

## 1. market_data — normalization
Source: `src/utils/market_data.py`

Instrument-id mapping (port the maps **verbatim**):
- market→suffix: KOSPI→`KS`, KOSDAQ→`KQ`, NYSE→`US`, NASDAQ→`US`.
- suffix→market: `KS`→KOSPI, `KQ`→KOSDAQ, `US`→NYSE. Default suffix `KS`, default market KOSPI.

```c
alpha_err_t alpha_to_instrument_id(const char *ticker, alpha_market_t m, char *out, size_t n);
// "005930" + KOSPI -> "005930.KS"
alpha_err_t alpha_from_instrument_id(const char *iid, char *raw_out, size_t rn, alpha_market_t *m_out);
// "005930.KS" -> ("005930", KOSPI); split on LAST '.'
```

`sanitize_change_pct(value) -> optional double`:
- non-finite → none; `|value| > 999.999` (`MAX_ABS_CHANGE_PCT`) → none; else `round(value, 3)`.

`compute_change_pct(current, previous) -> optional double`:
- previous none/≤0 or non-finite → none; else `sanitize_change_pct((current-previous)/previous*100)`.

Represent "optional double" as `alpha_err_t` + `bool has_value` out-params (NaN is NOT used as sentinel).

---

## 2. cost_model — Korean trading costs
Source: `src/backtest/cost_model.py`

Rates: commission_rate = commission_pct/100; tax_rate = tax_pct/100;
slippage_rate = slippage_bps/10000. Defaults from constants above.

```c
typedef struct { double commission, tax, slippage_cost, total; } alpha_trade_cost_t;
alpha_trade_cost_t alpha_cost_calculate(const alpha_cost_model_t *m, alpha_side_t side, double price, int64_t qty);
```
Algorithm (exact):
```
notional      = price * qty
commission    = notional * commission_rate
tax           = (side == SELL) ? notional * tax_rate : 0
slippage_cost = notional * slippage_rate
total         = commission + tax + slippage_cost
```

---

## 3. backtest — engine, metrics, signal source
Source: `src/backtest/{engine,metrics,models,signal_source}.py`

### 3a. SignalSource interface (RL model out of scope; keep the seam)
```c
typedef alpha_signal_t (*alpha_signal_fn)(void *ctx, /*date*/ int64_t epoch_day,
                                          const double *prices, size_t n, int64_t position);
```
Provide **ReplaySignalSource** only: a `{date → signal}` map; missing date → HOLD
(`ReplaySignalSource`, `signal_source.py:157`). The V1/V2 `_state_key` RL logic and
neural policies are **out of scope** — the seam accepts an externally-supplied
`alpha_signal_fn` so an RL signal can be replayed later.

### 3b. Engine (`engine.py`, port `_execute_trade`/`_open_position`/`_close_position` exactly)
State: `cash (double)`, `position_qty (int64)`, `avg_buy_price (double)`,
`prev_portfolio_value (double)`.
Validation: `train_end < test_start` else error; `len(prices)==len(dates)` else error.
Init: `cash = initial_capital`, `prev_portfolio_value = initial_capital`.

Per bar `(dt, close)`:
1. append close to history; `signal = signal_fn(dt, history, position_qty)`.
2. `_execute_trade`:
   - BUY and `position_qty==0` → **open**.
   - SELL/CLOSE and `position_qty>0` → **close**.
   - else no trade.
3. take snapshot; append.

**Open (all-cash integer-share buy):**
```
unit_cost      = cost_calculate(BUY, price, 1).total
effective      = price + unit_cost;  if effective <= 0 -> no trade
qty            = floor(cash / effective);  if qty <= 0 -> no trade
cost           = cost_calculate(BUY, price, qty)
total_outlay   = price*qty + cost.total
if total_outlay > cash:            # rounding guard
    qty -= 1;  if qty <= 0 -> no trade
    cost = cost_calculate(BUY, price, qty)
cash          -= price*qty + cost.total
avg_buy_price  = price
position_qty   = qty
record BUY trade (commission, tax, slippage_cost, total_cost, pnl=0)
```
**Close (full-qty sell, realized pnl):**
```
qty   = position_qty
cost  = cost_calculate(SELL, price, qty)
pnl   = (price - avg_buy_price)*qty - cost.total
cash += price*qty - cost.total
position_qty = 0; avg_buy_price = 0
record SELL trade with pnl
```
**Snapshot:**
```
position_value  = position_qty * close
portfolio_value = cash + position_value
daily_return_pct= prev>0 ? (portfolio_value-prev)/prev*100 : 0
prev            = portfolio_value
```

### 3c. Metrics (`metrics.py`) — port exactly, round as noted
Empty snapshots → all-zero metrics.
```
initial = initial_capital;  final = last.portfolio_value
total_return     = (final-initial)/initial;  total_return_pct = *100
n = len(snapshots)
annual_return_pct= (n>1 && total_return>-1) ? ((1+total_return)^(252/n)-1)*100 : 0
sharpe           = _sharpe(daily_returns/100)      # below
mdd              = _mdd(snapshots)                 # peak-to-trough %, ≤0
win_rate         = sell_trades ? wins/sells*100 : 0    (win = pnl>0)
avg_holding_days = FIFO-match BUY→SELL, mean of max(0, (sell.date-buy.date).days)
baseline_return  = first_price>0 ? (last_price/first_price-1)*100 : 0
excess_return    = total_return_pct - baseline_return
```
Rounding: total/annual/sharpe/mdd/win_rate/baseline/excess → **4 dp**;
avg_holding_days → **1 dp**; total_trades = len(trades) (int).

`_sharpe(returns)`: if <2 → 0; `mean/ std * sqrt(252)`, sample variance
(divide by n-1); std≤0 → 0; risk-free = 0. `TRADING_DAYS_PER_YEAR = 252`.
`_mdd`: running peak of portfolio_value; `dd = (v-peak)/peak*100`; track min (≤0).

---

## 4. blending — N-way weighted score
Source: `src/agents/blending.py`

`SIGNAL_SCORE = {BUY:+1, HOLD:0, SELL:-1}`. Threshold constant **0.15**.
Input per strategy: `{strategy, signal, confidence(0..1), weight(≥0)}`.
```
for each input: sig = upper(signal) if valid else HOLD; conf = clamp(conf,0,1); w = max(0,weight)
total_weight = Σ w
norm_factor  = total_weight>0 ? total_weight : 1
equal_w      = total_weight<=0 ? 1/max(1,count) : 0
for each: w' = total_weight>0 ? w/norm_factor : equal_w
          weighted_score      += SIGNAL_SCORE[sig] * w' * conf
          weighted_confidence += conf * w'
conflict = has_buy && has_sell
signal   = weighted_score >  0.15 ? BUY
         : weighted_score < -0.15 ? SELL : HOLD
confidence = clamp(weighted_confidence, 0, 1)   # round 4dp
weighted_score round 6dp
```
2-way A/B wrapper (`blend_strategy_signals`): A weight `= 1 - clamp(blend_ratio,0,1)`,
B weight `= clamp(blend_ratio,0,1)`; empty → HOLD/0.

---

## 5. risk — enforced gates below the signal layer
Source: `src/agents/portfolio_manager.py` (`process_signal`, `_is_daily_loss_blocked`,
`_check_rule_based_exits`, `_check_portfolio_drawdown`, `_hard_stop_scan`)

Config keys with Python-compatible **defaults** (ints, percent):
- `max_position_pct = 20` — single-position weight cap.
- `daily_loss_limit_pct = 3` — circuit breaker.
- `individual_stop_loss_pct = 7` — per-position stop (threshold `-7`).
- `take_profit_pct = 5` — per-position take (threshold `+5`).
- `portfolio_drawdown_limit_pct = 8` — L2 portfolio drawdown.

These values are configurable in the Python source and remain configurable for
behavioral parity. The safety invariant is that only validated portfolio config
may supply them; a signal, strategy, API payload, or RL seam cannot override them.
Reject non-finite, negative, or out-of-policy values before calling the gates.
Parity tests load identical pinned JSON/`portfolio_config` fixtures, normalize
them once, and assert the config checksum before comparing decisions.

**Max-position gate (BUY sizing, `process_signal`):**
```
next_value    = existing_position_value + intended_buy_value
denominator   = portfolio_total_value (incl. the intended buy)
next_weight_pct = next_value / denominator * 100
if next_weight_pct > max_position_pct:  SKIP BUY   # blocked, no order
```

**Layered exits (evaluate independently):**
- **L1 per-position** (`_check_rule_based_exits`): for each held position,
  `pnl_pct = (current-avg_fill)/avg_fill*100`;
  `pnl_pct >= +take_profit_pct` → SELL (take profit);
  `pnl_pct <= -individual_stop_loss_pct` → SELL (stop loss).
- **L2 portfolio drawdown** (`_check_portfolio_drawdown`):
  `dd_pct = (current_equity-baseline_equity)/baseline_equity*100`;
  if `dd_pct <= -portfolio_drawdown_limit_pct` → sell the **two weakest** positions
  (lowest pnl_pct).
- **L3 daily-loss circuit breaker** (`_hard_stop_scan` / `_is_daily_loss_blocked`):
  `daily_realized_pnl_pct <= -daily_loss_limit_pct` → block all BUY, publish a
  circuit-breaker event, and set a **Redis lockout flag** persisting until the next
  KRX trading session at 09:00 `Asia/Seoul` (persistent lockout checked before any
  BUY). Calendar input is Redis `krx:holidays:{year}`, populated by source
  `scripts/fetch_krx_holidays.py`. Store absolute `expires_at` plus Redis TTL; if
  the calendar is missing/stale, fail closed and never release BUY early. If this
  differs from the pinned Python behavior, record it as an intentional safety
  deviation rather than silently claiming exact parity.

Fail-safe: missing price/data → treat as HOLD/skip, never error into an order.

---

## 6. aggregate_risk — cross-strategy exposure (read/report)
Source: `src/utils/aggregate_risk.py`

Pure-SQL aggregation over `portfolio_positions` (see `docs/DATABASE_TABLES.md`):
- `check_total_exposure(ticker)`: sum qty & market_value across strategies;
  `exposure_pct = total_value/total_aum*100`; `over_limit = pct > max_single_stock_pct`.
- `check_strategy_correlation()`: tickers held by >1 distinct strategy_id.
- `get_risk_summary()`: total_aum, per-strategy allocations, top-10 exposures,
  overlap warnings; `record_risk_snapshot()` writes JSON to `aggregate_risk_snapshots`.
Settings: `max_single_stock_exposure_pct`, `max_strategy_overlap_count` (from config).

---

## 7. indicators / ranking (non-RL)
Source: `src/agents/ranking_calculator.py`, `src/agents/screener.py`
Screener constants: volume-surge ratio 2.0× (vs 20-day avg), change-pct ±3.0%,
top `MAX_SCREENED_TICKERS = 10`. Port the deterministic filters; exclude any
RL-feature builders (`rl_intraday_features`, `rl_dataset_builder*`).

---

## Coverage checklist (every in-scope module has a contract above)
domain ✔ · market_data ✔ · cost_model ✔ · backtest{engine,metrics,models,signal_source} ✔ ·
blending ✔ · risk ✔ · aggregate_risk ✔ · indicators/ranking ✔.
Driver modules (db/cache/http) are specified in `docs/plans/phase-3-drivers.md`;
edges in `docs/EDGE_OPTIONS.md`.
