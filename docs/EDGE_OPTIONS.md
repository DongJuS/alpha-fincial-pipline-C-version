# EDGE_OPTIONS.md — Async edge: pure-C vs Rust

The async/networking modules are implemented in **pure C** first (default,
"pro-C") with **libwebsockets owning the event loop**. Selected Rust comparisons
use Tokio. The datalake is a selected Rust adapter. All variants satisfy C-owned
normalization/decision contracts.
Network variants call `libalpha_core` for every risk/blend/portfolio/backtest
decision; the numeric core is never reimplemented in an edge.

Record the reason from `BENCHMARK_PLAN.md §8` before creating an optional Rust
network variant. The Rust datalake is already selected.

## Edge modules & their contracts

| Module | Responsibility | Contract (both variants) |
|--------|----------------|--------------------------|
| `ws-replay` (optional comparison) | Replay recorded WS frames | Bounded input/reconnect test; no live KIS authority and no production collector replacement. |
| `broker` | **Sole order authority** | Accept a risk-approved order, place it (paper/real per config), persist to `broker_orders`/`trade_history`. Never bypass `libalpha_core` risk gates. Paper default. |
| `api` | REST server (replaces FastAPI) | Serve `/api/v1/*` (see `DATABASE_TABLES.md` + source `docs/api_spec.md`), JWT-verified, read-mostly; config writes go through validated handlers. |
| `orchestrator` | Scheduler + supervision | Cron-like triggers (collect, blend, risk snapshot), health/heartbeat monitoring, restart/backoff of edge tasks. |
| `datalake` (Rust selected) | Parquet + self-hosted object storage | Arrow/Parquet encode; aws-sdk-s3 custom endpoint/path-style; verify multipart/checksums. |

## Variant A — pure C (default, "pro-C")

- **Event loop / concurrency / timers:** `libwebsockets` owns the sole loop.
- **WebSocket + HTTP server:** `libwebsockets` (optional replay client, API server).
- **Outbound HTTP:** `libcurl` (broker REST, LLM/notifier seams).
- **JSON:** `yyjson`. **JWT:** `libjwt`. **TLS/crypto:** OpenSSL.
- **Pros:** one language/toolchain, one binary, direct `libalpha_core` linkage (no
  FFI shim), smallest deployment. **Cons:** more manual buffer/lifetime management;
  reconnect/backpressure logic is hand-written and needs careful ASan/UBSan testing.

## Variant B — Rust (optional)

- **Runtime:** `tokio`. **WS:** `tokio-tungstenite`. **HTTP client:** `reqwest`.
  **API server:** `axum` + `tower`. **JWT:** `jsonwebtoken`. **JSON:** `serde`.
- **PostgreSQL:** SQLx. **Redis:** fred. **Datalake:** aws-sdk-s3 with custom
  endpoint/path-style plus Arrow/Parquet crates.
- **Core access:** `core-sys` crate (bindgen) → calls `libalpha_core` over the C ABI.
- **Pros:** memory-safe concurrency, ergonomic async/reconnect/backpressure, strong
  error types on the order path. **Cons:** adds a second toolchain + an FFI boundary
  to maintain (`bindings/`); larger build.

## Comparison sequence

- **Implement and measure pure C first** for API/broker/orchestrator, with
  libwebsockets owning networking and timers.
- **Consider a Rust network comparison** when reconnect/backpressure, order-path
  safety, or measured performance justifies it. `ws-replay` uses recorded frames;
  the live Python collector remains authoritative.
- **Implement the selected Rust datalake** independently of that decision, using
  the self-hosted S3-compatible endpoint and Arrow/Parquet.

Record every Rust comparison decision in `progress.md`, use the same fixture and
service settings, and keep the `libalpha_core` decision calls unchanged.
