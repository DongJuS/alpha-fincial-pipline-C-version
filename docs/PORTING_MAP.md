# PORTING_MAP.md — Python → target module map

Source root: `../alpha-financial-pipeline/`. Target root: this repo. **C is the
default language**; async edges have both a pure-C and a Rust option (see
`docs/EDGE_OPTIONS.md`). Pin the source commit in `progress.md` before generating
goldens. Every source path below was verified to exist at handoff time.

This is a comparison scope, not a requirement to replace every Python module.
Representative edge/API paths may be benchmarked without porting their entire
surface. Infrastructure definitions and Infisical integration are excluded.

## Core (C — mandatory)

| Python source | Target (C) | Notes |
|---|---|---|
| `src/constants.py` | `core/include/alpha/constants.h` | verbatim constants |
| `src/db/models.py` | `core/include/alpha/domain.h` | DTO structs + enums |
| `src/utils/market_data.py` | `core/src/market_data/` | instrument-id maps, change_pct |
| `src/backtest/cost_model.py` | `core/src/backtest/cost_model.c` | KR cost model |
| `src/backtest/models.py` | `core/include/alpha/backtest.h` | config/trade/snapshot/metrics structs |
| `src/backtest/metrics.py` | `core/src/backtest/metrics.c` | sharpe/mdd/win/holding |
| `src/backtest/signal_source.py` | `core/src/backtest/signal_source.c` | interface + **ReplaySignalSource only** (RL sources excluded) |
| `src/backtest/engine.py` | `core/src/backtest/engine.c` | simulation loop |
| `src/backtest/{cli.py,__main__.py}` | `core/src/backtest/cli.c` | thin CLI over engine |
| `src/backtest/repository.py` | `core/platform/db/backtest_repo.c` | persists runs (P3) |
| `src/backtest/optimizer.py` | `core/src/backtest/optimizer.c` | param sweep (deterministic) |
| `src/agents/blending.py` | `core/src/portfolio/blending.c` | N-way blend |
| `src/agents/portfolio_manager.py` (risk/sizing core) | `core/src/risk/` + `core/src/portfolio/sizing.c` | gates + sizing; order side-effects live in edge/broker |
| `src/utils/aggregate_risk.py` | `core/src/risk/aggregate_risk.c` | SQL exposure (needs P3 db) |
| `src/utils/risk_validation.py` | `core/tests/test_risk.c` | source file is a *test harness*; port as tests |
| `src/agents/ranking_calculator.py` | `core/src/indicators/ranking.c` | non-RL ranking |
| `src/agents/screener.py` | `core/src/indicators/screener.c` | daily filters |

## Platform drivers (C — real drivers, Phase 3)

| Python source | Target (C) | Library |
|---|---|---|
| `src/utils/db_client.py`, `src/db/queries.py` | `core/platform/db/` | libpq |
| `src/utils/redis_client.py` | `core/platform/cache/` | hiredis |
| `src/utils/s3_client.py`, `src/services/datalake.py` | `edge/datalake/` (Rust) | aws-sdk-s3 custom endpoint + Arrow/Parquet |
| `src/llm/*` | `core/platform/http/llm.c` | libcurl multi + yyjson |
| EOD/live collector (`FinanceDataReader`, yfinance, KRX/KIS paths) | **Python retained** | feeds normalized/replayed boundary; no production port |
| `src/agents/notifier.py` (Telegram send) | `core/platform/http/telegram.c` | libcurl |

## Async edges (C default; Rust optional — Phase 4)

| Python source | Target module | C variant | Rust variant |
|---|---|---|---|
| recorded WS replay (comparison only; source collector retained) | `edge/ws-replay` | libwebsockets | tokio-tungstenite |
| `src/brokers/{kis,paper,virtual_broker}.py`, `src/services/{account_state,paper_trading}.py` | `edge/broker` | libwebsockets + libcurl | reqwest + tokio |
| `src/api/*` (FastAPI routers) | `edge/api` | libwebsockets HTTP + libjwt | axum + tower + jsonwebtoken |
| `src/agents/orchestrator.py`, `src/schedulers/*` | `edge/orchestrator` | libwebsockets timers | tokio timers |

## Excluded (do NOT port)

| Python source | Reason |
|---|---|
| `src/agents/rl_*.py`, `rl_environment.py`, `rl_dataset_builder*.py`, `rl_trading*.py`, `rl_walk_forward.py`, `rl_hyperopt.py`, `rl_dreamer.py`, `rl_*` | RL model/training/testing out of scope |
| `src/api/routers/rl.py` | RL API surface (out of scope) |
| `ui/**` | React UI |
| `src/utils/blog_client.py`, `discussion_renderer.py` | blog auto-posting |
| infrastructure/deployment repositories and Infisical integration | shared/excluded from language comparison |
| `src/agents/predictor.py`, `strategy_*_consensus/tournament`, `src/llm/*` reasoning bodies | LLM reasoning — replaced by external signal input; only the HTTP client seam is ported |
| `src/agents/search_*`, `src/utils/searxng_client.py` | search/scraping research lane |
| `src/agents/collector/**`, FDR/yfinance/KRX/KIS collection | Python collector retained as input boundary |

> When a target module needs behavior not fully captured here, read the cited
> Python file directly and match it; update `MODULE_SPECS.md` if a new contract
> detail is discovered.
