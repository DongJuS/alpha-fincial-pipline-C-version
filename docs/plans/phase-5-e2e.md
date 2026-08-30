# Phase 5 — E2E + consolidated comparison

## Goal
Prove the selected processor paths run end-to-end, match the pinned Python system,
and produce a reproducible comparison rather than assuming a replacement decision.

## Work
- **E2E scenario** against Docker Postgres+Redis (+ MinIO if datalake built):
  1. Seed Python-collector fixture/raw OHLCV; begin the candidate path at normalization.
  2. Feed strategy signals (from a fixture / replay of `predictions`).
  3. Blend → risk gates → paper order → persist → read back via the API.
  4. Trigger the daily-loss breaker path and confirm BUY lockout + Redis flag.
- **Parity suite (consolidate P1–P2 goldens + add cross-module):**
  full backtest run, blend outcomes, all risk-gate decisions, `compute_change_pct`,
  aggregate-risk totals. Tolerances per `docs/BUILD_AND_TEST.md`.
- **Ops:** smoke test script (health of DB/Redis, one collect cycle), and a
  `README` quickstart. Collector health belongs to the retained Python system;
  candidate smoke tests start at the normalization input contract.
- **Benchmark report:** consolidate absolute results and speedups for Python/C and
  selected Rust edges; separate CPU-bound, I/O-bound, startup, and E2E findings.
  Include CPU/RSS, binary/container size, build time, code size, error/drop rates,
  and operational/safety tradeoffs. Preserve raw JSON inputs and results.
  Numeric speedup alone is explicitly insufficient for migration; require a
  material E2E tail-latency, throughput, resource, recovery, or safety benefit.

## Contract
No new module contracts — this validates the sum of Phases 1–4 against Python.

## Done criteria
- E2E scenario green in CI (integration job).
- Parity suite passes; any intentional deviation documented in `MEMORY.md`.
- Report contains only parity-eligible results and does not time infrastructure or
  Infisical. It states which paths should remain Python and which merit migration.
- Report preserves C risk/blend/sizing as the single decision implementation and
  never enables multiple production broker adapters. Broker activation is a
  separate safety approval, not a benchmark side effect.
- `progress.md` and `MEMORY.md` finalized; all phase trackers ✅.

## Stretch (optional, not required for handoff completion)
- Add a Rust network-edge comparison if pure C showed measured bottlenecks or
  reconnect/backpressure/order-safety complexity. Do not replace the live Python
  collector as part of the stretch.
