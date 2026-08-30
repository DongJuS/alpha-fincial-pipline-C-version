# Single-libwebsockets-loop feasibility spike (Phase 0)

## Goal

Prove that nonblocking libpq, hiredis async, and libcurl multi operations can be
driven by one libwebsockets-owned service loop with bounded queues and explicit
request/result ownership, cancellation, recovery, backpressure, and shutdown.

## Files

- `spike/event_loop/CMakeLists.txt`
- `spike/event_loop/main.c`, `spike/event_loop/README.md`
- `spike/event_loop/run.sh`, `spike/event_loop/verify_result.py`
- `tests/test_event_loop_spike.py`
- `.github/workflows/ci.yml`, `progress.md`, `MEMORY.md`

## Contract

Satisfies the integration-spike exit gate in `docs/plans/phase-0-scaffold.md` and
the ownership/backpressure rules in `.agent/conventions.md`. The spike is not a
production driver implementation and contains no risk or broker-order logic.

## Tests

- One LWS service thread completes PostgreSQL and Redis round trips plus a curl-
  multi request without blocking calls on that thread.
- PostgreSQL pipeline abort/sync recovery and Redis disconnect are exercised.
- Cancellation, bounded queue saturation/backpressure, request/result matching,
  and clean shutdown emit machine-verifiable evidence.
- Link/build evidence shows no libuv/libevent ownership path is enabled.

## Risks / open questions

- This validates feasibility and failure semantics, not Phase 3 throughput.
- Local dependencies must use pinned LWS built without alternate event-loop
  backends; CI reproduces the same configuration from its pinned source revision.
