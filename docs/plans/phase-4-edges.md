# Phase 4 — Async edges (pure-C or Rust)

## Goal
Wire and measure the selected async/data edges. Pure-C API/broker/orchestrator use
the libwebsockets-owned loop. Rust comparisons use the locked Tokio stack, and the
Rust S3/Parquet datalake is a selected deliverable. The Python collector remains.

## Decision first
Record which optional Rust network comparisons are built. Add `bindings/` +
`core-sys`; the Rust datalake is already selected and does not require a new vote.

## Modules
- `edge/ws-replay` (optional comparison): libwebsockets vs tokio-tungstenite on
  recorded frames/reconnect/backpressure. It never becomes live KIS authority.
- `edge/broker` ← `src/brokers/{kis,paper,virtual_broker}.py`,
  `src/services/{account_state,paper_trading}.py`:
  **sole order authority.** Accept a risk-approved order, place (paper default /
  real behind flag+confirm), persist `broker_orders`/`trade_history`. Every order
  passes `libalpha_core` risk gates first.
- `edge/api` ← `src/api/*` routers. Endpoints mirror source `/api/v1/*`:
  benchmark only the representative set defined in `BENCHMARK_PLAN.md` (health,
  DB read, large JSON, JWT read, validated write). Port additional source routers
  only when a separate functional requirement or measured question justifies it.
- `edge/orchestrator` ← `src/agents/orchestrator.py`, `src/schedulers/*`:
  cron triggers (collect, blend, risk snapshot), heartbeat/health monitoring,
  task supervision/backoff. Replaces LangGraph with a native state machine.
- `edge/datalake` (Rust): `arrow`/`parquet` encode, then `aws-sdk-s3` upload to the
  configured self-hosted endpoint with path-style addressing. Exercise multipart,
  checksum, retry, and object round-trip against the actual server.

## Contract
`docs/EDGE_OPTIONS.md` per-module contract table. Order authority isolation and
paper-default are non-negotiable.

## Tests
- `ws-replay` (if built): recorded frame parse + reconnect/backpressure; no live KIS.
- `broker`: risk-gate-then-place flow (paper); rejects an order that fails a gate.
  C/Rust challengers have no real credentials and cannot place real orders.
- `api`: endpoint smoke tests vs. source `docs/api_spec.md` shapes; JWT required.
- `orchestrator`: scheduled trigger fires; dead task is restarted.
- `datalake`: Parquet schema/content parity with pyarrow and S3-compatible
  PUT/GET/list/delete integration against the self-hosted test server.
- Benchmark: recorded WS replay and representative API concurrency levels; compare
  Python/pure-C and any justified Rust edge using identical fixtures/settings.

## Dependencies
Phases 1–3.

## Done criteria
- E2E dry run: normalized collector input → signal → risk gate → paper order
  → API read, all green.
- Eligible raw edge results exist; any Rust addition has a recorded justification.
- Exactly one configured broker adapter can hold real-order authority; activation
  or replacement requires a separate safety approval outside benchmark automation.
- Chosen variant(s) recorded in `progress.md`; P4 done.

## Gotchas
- Keep decisions in the C core even in Rust network variants (no logic duplication).
- Backpressure on the WS feed: bound queues; drop-oldest for `latest_ticks` cache.
- `aws-sdk-s3` is used as an S3-protocol client, not as an AWS-cloud requirement;
  require configurable endpoint and path style.
