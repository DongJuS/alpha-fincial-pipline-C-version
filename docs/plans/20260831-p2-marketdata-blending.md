# P2 (part 1) — market_data normalization + N-way blending  (Phase 2)

## Goal
Port the two self-contained, directly parity-testable P2 decision helpers with no
I/O: `market_data` (instrument-id maps, `sanitize_change_pct`,
`compute_change_pct`) and `blending` (N-way weighted score + 2-way A/B wrapper).
Deliver golden parity against the pinned Python source, unit tests, and a
`blend-batch` benchmark. Risk gates + sizing + indicators follow in P2 part 2.

## Files
- `core/include/alpha/market_data.h`, `core/src/market_data/market_data.c`
- `core/include/alpha/blending.h`, `core/src/portfolio/blending.c`
- Fixtures: `bench/fixtures/blend-cases.json`, `bench/fixtures/market-data-cases.json`
- Golden generator: `tools/generate_golden_decisions.py` (reads committed
  fixtures, dumps Python outputs with pinned SHA + input checksum).
- Goldens: `core/tests/golden/{blend-cases,market-data-cases}.json`
- Tests: `core/tests/test_market_data.c`, `core/tests/test_blending.c` (unit +
  parity), extend the parity runner path in the loader if needed.
- Benchmark: `core/apps/blend_runner.c` + `bench/run_c_blend.py`.

## Contract
`docs/MODULE_SPECS.md §1 market_data, §4 blending`. Python source
`src/utils/market_data.py`, `src/agents/blending.py` @ `3642cdc…`.
- suffix maps verbatim; `from_instrument_id` splits on the **last** `.`.
- `sanitize`: non-finite→none, `|v|>999.999`→none, else round(v,3).
- blend: clamp conf[0,1], weight≥0; zero total weight→equal weights; ±0.15
  threshold; confidence 4dp, weighted_score 6dp; conflict = has_buy & has_sell.

## Tests
Unit + parity: empty→HOLD, conflict, zero-weight equal fallback, ±0.15 boundary,
2-way ratio clamp; instrument-id round-trip incl. dotted raw code; change_pct
clamp/round/none paths.

## Risks
- "optional double" modelled as `bool has_value` out-params (no NaN sentinel).
- Blend input signals arrive as strings in fixtures; C uses the enum, CLOSE/invalid
  → HOLD score, matching Python's `sig not in VALID_SIGNALS`.
