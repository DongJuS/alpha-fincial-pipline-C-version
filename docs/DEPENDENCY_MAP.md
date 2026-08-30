# DEPENDENCY_MAP.md — Python lib → C lib / Rust crate

From `../alpha-financial-pipeline/requirements.txt`. Only in-scope deps are mapped;
RL/UI/blog deps are intentionally omitted.

| Python | C (base) | Rust (edges) | Notes |
|--------|----------|--------------|-------|
| asyncpg | **libpq nonblocking + pipeline** | **SQLx** | fixed pool/depth; PostgreSQL Warm tier |
| redis | **hiredis async** | **fred** | bounded pipeline/pub-sub; Redis Hot tier |
| httpx / anthropic / openai / google-generativeai | **libcurl multi + yyjson** | reqwest + serde_json | client seam only |
| websockets | libwebsockets (optional replay) | **tokio-tungstenite** (optional replay) | operating collector stays Python |
| boto3 (S3) | not the default path | **aws-sdk-s3** | custom self-hosted endpoint + path style |
| pyarrow (Parquet) | not the default path | **arrow + parquet crates** | Rust datalake encoding |
| fastapi / uvicorn | *(libwebsockets HTTP)* | axum + tower | REST API server |
| pydantic / pydantic-settings | hand-written validators + `getenv` | serde + hand-written validation | request/config validation |
| PyJWT | **libjwt** | jsonwebtoken | dashboard auth |
| apscheduler | **libwebsockets timers** | tokio timers | scheduling |
| langgraph | native state machine (C) | native state machine (Rust) | orchestrator; no LangGraph |
| python-telegram-bot | **libcurl** (Bot API POST) | reqwest | notifier only |
| finance-datareader / yfinance | **Python retained** | **Python retained** | collection is outside port boundary |
| pytest / pytest-asyncio | **CMocka** | cargo test | tests |

## Boundary and native data path

1. **FinanceDataReader / yfinance / KIS ingest** remain in the operating Python
   collector. C begins at normalization. Recorded HTTP/WS payloads may be parsed in
   an optional benchmark, but there is no C/Rust production collector deliverable.

2. **Parquet + S3** use Rust `arrow`/`parquet` and `aws-sdk-s3`. The SDK is configured
   with the self-hosted S3-compatible `endpoint_url` and `force_path_style(true)`;
   AWS cloud is not required. Test the actual server for multipart/checksum/API
   compatibility. Handwritten C SigV4 is not the default implementation. See the
   [AWS SDK for Rust custom endpoint documentation](https://docs.aws.amazon.com/sdk-for-rust/latest/dg/endpoints.html).

## System packages (dev environment)
`libpq-dev`, `libhiredis-dev`, `libcurl4-openssl-dev`, `libssl-dev`, `libjwt-dev`,
`libwebsockets-dev`, `libcmocka-dev`; yyjson is pinned/built by CMake. Rust:
`rustup` + the crates above.
