# 🏛️ architecture.md — Target architecture (C base + selected Rust adapters)

> Read this before implementing. It defines the build roots, the data flow, the
> module→language map, and the storage tiers. Behavior source of truth is the
> Python repo `../alpha-financial-pipeline`.

---

## System overview

`alpha-core` is the C comparison implementation for selected deterministic paths
of the operating Python KOSPI/KOSDAQ system. Python remains the pinned baseline;
this project measures whether equivalent C and selected Rust-edge implementations
improve latency, throughput, resource use, or operational characteristics.
Infrastructure, Infisical, LLM reasoning, RL training, and React UI are **out of
scope** (see `CLAUDE.md` and `docs/BENCHMARK_PLAN.md`).

---

## Two build roots

```
┌─────────────────────────────────────────────────────────────┐
│ libalpha_core  (C11 static lib)  — the mandatory base        │
│   domain · market_data · indicators · risk · portfolio ·     │
│   blending · cost_model · backtest · platform drivers        │
│   (nonblocking/pipelined libpq · async hiredis · curl-multi) │
│   Exposes a stable C ABI in core/include/alpha/*.h           │
└───────────────▲─────────────────────────────────────────────┘
                │ C ABI (FFI)
┌───────────────┴─────────────────────────────────────────────┐
│ edge/  — async/networking/data edges:                        │
│   (a) pure-C   : libwebsockets-owned loop + libcurl          │
│   (b) Rust     : selected datalake + optional network edges  │
│   modules: broker · api · orchestrator · datalake            │
└──────────────────────────────────────────────────────────────┘
```

The numeric/decision core is **single-sourced in C**. Whichever edge variant is
chosen calls into `libalpha_core` for risk/portfolio/blend/backtest decisions, so
behavior is identical across variants.

The existing Python implementation is not inside this build graph. It runs as an
independent baseline against the same fixtures and shared services. Parity gates
precede performance comparison.

CMake builds the C core and pure-C edges. Cargo builds the selected Rust datalake
and optional network challengers after verifying the C ABI version/hash. Rust links
the exact `libalpha_core` artifact from the same revision.

---

## Data flow

```
External sources
  ├─ EOD OHLCV (FinanceDataReader/yfinance)
  └─ KIS WebSocket (intraday ticks)
          │
          ▼
   existing Python collector (retained)
          │
          ▼
   C normalization  ──►  PostgreSQL: market_data / ohlcv_daily
                    Redis: latest_ticks:{ticker}
          │
          ▼
   indicators / ranking (non-RL features)
          │
          ▼
   strategy signals ──►  blending (N-way weighted score)  ──► combined signal
          │                                                        │
          │                          external RL signal (optional, replay only)
          ▼                                                        ▼
   portfolio_manager  ──►  RISK GATES (below signals; validated config only):
        L1 per-position stop/take · L2 portfolio drawdown ·
        L3 daily-loss circuit breaker (Redis lockout)
          │
          ▼
   broker order path (edge)  ──►  PostgreSQL: portfolio_positions, trade_history
          │
          ▼
   notifier (HTTP) · REST API (edge) for dashboards
```

Backtest engine is a parallel, offline consumer of the same core (domain,
cost_model, metrics).

---

## Module → language map

| Subsystem | Lang | Why |
|---|---|---|
| Domain types, enums (signal/side/market) | **C** | Shared foundation, exposed over C ABI |
| market_data normalization | **C** | Pure transform |
| Indicators / ranking (non-RL) | **C** | Numeric, deterministic |
| Risk rules / circuit breakers | **C** | Simple, auditable, no async |
| Portfolio sizing + blending + cost model | **C** | Pure math |
| Backtest engine + metrics | **C** | Self-contained — first slice |
| DB / Redis / HTTP drivers | **C** (nonblocking libpq/hiredis, libcurl multi) | Integrated into LWS loop |
| Telegram notifier | **C** (libcurl) | Trivial HTTP POST |
| Market-data collection | **Python retained** | FDR/yfinance/KIS remains authoritative |
| Optional WS replay comparison | **C** (libwebsockets) *or* Rust | Not the operating collector |
| Broker order execution | **C** (LWS+libcurl) *or* Rust (opt) | Safety-critical concurrency |
| Scheduler + orchestrator | **C** (LWS timers) *or* Rust (opt) | Event-loop supervision |
| REST API (replaces FastAPI) | **C** (libwebsockets HTTP) *or* Rust axum (opt) | Async HTTP server |
| Datalake | **Rust** (aws-sdk-s3 + Arrow/Parquet) | Self-hosted S3-compatible endpoint |

Edge rows: **C is the default**; the Rust column is an optional swap behind the
same C-ABI contract. See `docs/EDGE_OPTIONS.md`.

Risk/blend/sizing never have a second implementation. Broker adapters may coexist
only for paper/replay tests; a deployment enables exactly one real-order adapter,
and challengers cannot load real credentials.

---

## Storage tiers (mirror of the Python system)

These stores are shared experimental dependencies, not components to rewrite.
Python/C/Rust variants must use equivalent schema, data, pool size, timeout, and
service versions. Infisical resolves configuration outside timed regions.

| Tier | Store | Lifetime | Use |
|------|-------|----------|-----|
| Hot | Redis | ~24h | latest ticks, circuit-breaker lockout flags, OAuth tokens, heartbeats |
| Warm | PostgreSQL | ~90d | trades, predictions, positions, tournament/backtest results |
| Cold | S3/MinIO (Parquet) + PG archive | ∞ | raw OHLCV, annual performance, event logs |

Table catalog: `docs/DATABASE_TABLES.md`. Redis keys: source `docs/db/redis_keys.md`.

---

## Repo layout (created in Phase 0)

```
alpha-fincial-pipline-C-version/
├── CLAUDE.md · AGENTS.md · architecture.md · progress.md · MEMORY.md
├── .agent/{conventions.md, tech_stack.md, roadmap.md}
├── docs/{PORTING_MAP,EDGE_OPTIONS,MODULE_SPECS,DATABASE_TABLES,DEPENDENCY_MAP,BUILD_AND_TEST,BENCHMARK_PLAN}.md
│   └── plans/{README.md, phase-0-scaffold.md … phase-5-e2e.md}
├── core/                      # libalpha_core (C)
│   ├── include/alpha/*.h      # public C ABI
│   ├── src/{domain,market_data,indicators,risk,portfolio,backtest}/
│   ├── platform/{db,cache,http}/
│   ├── tests/                 # CMocka
│   └── CMakeLists.txt
├── edge/                      # async/data edges (C and optional Rust workspace)
│   ├── broker/ · api/ · orchestrator/ · ws-replay/ · datalake/
│   └── Cargo.toml               # datalake + optional Rust challengers
├── db/migrations/             # SQL mirrored from source schema
├── bindings/                  # cbindgen/bindgen headers (only if Rust variant)
├── bench/                     # immutable fixtures + raw benchmark results
└── CMakePresets.json
```
