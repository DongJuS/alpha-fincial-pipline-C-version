# P0 single-loop feasibility spike

This executable is disposable integration evidence, not a production driver. A
single libwebsockets service thread schedules readiness probes with zero timeout;
all libpq, hiredis, and curl operations remain nonblocking. There is no worker
thread and no libuv/libevent adapter.

`run.sh` verifies and incrementally builds the exact revision and options in
`third_party/libwebsockets.lock.json`.
Set `ALPHA_LWS_PREFIX` only to reuse a stage produced by
`tools/build_pinned_libwebsockets.sh`; CMake intentionally rejects an unpinned
system installation. libuv, libevent, GLib, and event-library plugins are all
disabled, and the verifier rejects shared libwebsockets output.

The run proves a PostgreSQL round trip, pipeline abort/sync/reuse, a Redis round
trip followed by disconnect/reconnect, and a curl-multi request. It also exercises
a four-entry caller-owned queue, deterministic saturation rejection, cancellation,
request/result IDs, and clean shutdown. `verify_result.py` validates the emitted
JSON and the binary's dynamic link graph.

With the pinned Compose services running:

```sh
docker compose up -d --wait
spike/event_loop/run.sh
```

The 1 ms readiness probe is intentionally conservative. Phase 3 must use native
LWS foreign-fd integration or another measured LWS-owned mechanism and repeat the
failure tests before it can make throughput claims. This spike makes no speedup
claim and contains no decision or order logic.
