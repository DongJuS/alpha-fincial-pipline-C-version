# P3 single-LWS driver runtime

## Goal

Replace the driver tests' `poll()` stand-ins with a reusable runtime owned by the
exact pinned libwebsockets loop. Drive hiredis async, nonblocking libpq, and
libcurl multi together without a hidden loop or blocking work on the service
thread.

## Contract

- Register every driver socket as a native LWS foreign descriptor and translate
  LWS readiness into the existing driver service APIs.
- Use LWS sorted-usec timers for hiredis and curl deadlines; do not busy-probe.
- Bound descriptor/request ownership and reject saturation deterministically.
- Preserve request-to-result identity through PostgreSQL pipeline errors, Redis
  disconnect/reconnect, HTTP timeout/cancellation, and orderly shutdown.
- Link only the source-pinned static LWS build; reject alternate event-loop
  backends as in the P0 verifier.

## Verification

- Docker integration test runs all three drivers concurrently on one service
  thread and records thread count, queue depth, completion IDs, and shutdown.
- Inject PostgreSQL pipeline abort/sync recovery, Redis disconnect/reconnect,
  HTTP timeout/non-2xx, cancellation, and descriptor saturation.
- Run sanitizer dev build, format, clang-tidy, and hosted shared-services CI.

## Exit

This unit closes only P3 loop ownership and recovery evidence. Python/Rust golden
parity and the 30-trial MVP-2 benchmark remain separate stop-gate units.
