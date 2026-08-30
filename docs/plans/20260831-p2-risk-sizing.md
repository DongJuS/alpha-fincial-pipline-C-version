# P2 (part 2) — risk gates + max-position sizing  (Phase 2)

## Goal
Port the safety-critical decision gates as pure, no-I/O C functions taking
validated config + resolved position/price inputs: L1 per-position stop/take,
L2 portfolio-drawdown weakest-two, L3 daily-loss breaker decision, and the
max-position BUY gate. Deliver golden parity + unit tests. (Redis lockout
persistence and DB wiring are P3; indicators/ranking are a later P2 note.)

## Files
- `core/include/alpha/risk.h`, `core/src/risk/gates.c` (L1/L2/L3 + config).
- `core/src/portfolio/sizing.c` (max-position gate + denominator helper) — same
  header `risk.h`.
- Fixture `bench/fixtures/risk-cases.json` (config + gate input cases).
- Golden: `core/tests/golden/risk-cases.json` via `generate_golden_decisions.py`.
- Test `core/tests/test_risk.c` (unit + parity).

## Contract
`docs/MODULE_SPECS.md §5`, Python `src/agents/portfolio_manager.py` @ `3642cdc…`:
- L1 `_check_rule_based_exits` (lines ~431-460): pnl_pct=(cur-avg_fill)/avg_fill*100;
  `>= +take_profit` → SELL(take); `<= -stop_loss` → SELL(stop); skip qty<=0 or
  non-positive prices.
- L2 `_check_portfolio_drawdown` (~517-576): dd_pct=(cur_eq-base_eq)/base_eq*100;
  if `dd_pct > -dd_limit` or equities<=0 → none; else sort candidates ascending by
  pnl_pct (stable) and SELL the weakest two.
- L3 `_is_daily_loss_blocked` (~100-103): blocked = daily_pnl_pct <= -daily_limit.
- Max-position `process_signal` (~278-320): next_value = existing_pos_value +
  intended_buy_value; denominator paper=max(total,seed,1), real=max(total+buy,1);
  next_weight=next_value/denominator*100; block if `> max_position_pct`.

## Parity note
The live Python methods are async and DB-coupled (positions/avg-fill/snapshots
from Postgres), so they are not directly callable for goldens. The golden
generator uses a Python reference that **transcribes the pure decision
arithmetic line-for-line** from the pinned source (documented, reviewable). C is
compared against that transcription under dev+bench. Config is a checksummed
fixture input; the golden records its SHA-256. This transcription dependency is
recorded in `MEMORY.md`.

## Tests
Boundary cases: take exactly +5 vs +4.99, stop exactly -7 vs -6.99, L2 dd exactly
-8 vs -7.99 with weakest-two selection incl. ties, L3 -3 boundary, max-position
just over/under 20% for paper and real denominators.
