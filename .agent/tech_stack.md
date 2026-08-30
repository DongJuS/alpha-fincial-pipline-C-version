# tech_stack.md — Allowed technology stack

> Only the libraries below may be used. Adding anything new requires updating this
> file in the same change, with a one-line rationale.

## C base — core library & drivers

| Library | Purpose |
|--------|---------|
| C11 + CMake (≥3.20) | Language + build |
| **libpq** (nonblocking + pipeline mode) | PostgreSQL client; bounded batch pipelines |
| **hiredis** async API | Redis client (Hot tier, pub/sub); attach to LWS loop |
| **libcurl** multi API | Required outbound HTTP (broker/LLM/notifier); no EOD collector |
| **yyjson** | JSON parse/serialize |
| **libjwt** | JWT verify for the REST API (if C API variant) |
| **OpenSSL** | TLS/crypto required by C networking/auth paths |
| **CMocka** (or Unity) | Unit tests |

## C pro-C edges (async variant)

| Library | Purpose |
|--------|---------|
| **libwebsockets** | Sole C event loop; REST API, timers, socket integration, optional WS replay |

## Rust async/data adapters

| Crate | Purpose |
|------|---------|
| tokio | Async runtime |
| axum + tower | REST API server (replaces FastAPI) |
| tokio-tungstenite | Optional recorded WebSocket replay comparison |
| reqwest | HTTP client (broker REST, LLM/notifier seams) |
| serde / serde_json | (De)serialization |
| thiserror | Typed edge/FFI errors without panics |
| SQLx | PostgreSQL client + pool + checked queries |
| fred | Redis/Valkey async client, pool, pipeline, pub/sub |
| jsonwebtoken | JWT |
| bindgen | `core-sys` FFI to `libalpha_core` |
| aws-config + aws-sdk-s3 | Self-hosted S3-compatible datalake via custom endpoint |
| arrow + parquet | Rust Arrow/Parquet datalake encoding |

## Infrastructure

| Component | Standard |
|-----------|----------|
| Database | PostgreSQL 15+ |
| Cache / Bus | Redis 7+ |
| Object store | S3-compatible (Cloudflare R2 / MinIO) |
| Local/dev orchestration | Docker Compose (Postgres + Redis for integration tests) |

Infrastructure and Infisical are not port targets. The entries above are clients
or shared test services needed to compare application implementations.

## Forbidden / avoid

| Item | Status | Reason |
|------|--------|--------|
| Reimplementing risk/blend logic in Rust | Forbidden | Must call the single-source C core over FFI |
| Broker calls outside the broker edge module | Forbidden | Order authority isolation |
| Adding a package not listed here | Forbidden | Update this file first with rationale |
| Porting RL model/training/testing | Out of scope | See `CLAUDE.md` |
| Porting the Python collector | Out of scope | C starts at normalization |
| A second C event loop (libuv/libevent) | Forbidden | libwebsockets owns the C loop; bounded non-I/O workers are allowed |
| Handwritten C SigV4 as the default | Avoid | Rust SDK targets custom S3-compatible endpoint |
| A GC'd or scripting runtime in the core | Avoid | Defeats the C-base performance/embeddability goal |
