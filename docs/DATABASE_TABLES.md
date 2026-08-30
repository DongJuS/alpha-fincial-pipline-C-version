# DATABASE_TABLES.md — Schema catalog (mirrors source `docs/db/`)

The target reuses the **same PostgreSQL schema** as the Python system. Per-table
column details live in the source repo `../alpha-financial-pipeline/docs/db/pg_*.md`
— **read those for exact columns/constraints** and translate into
`db/migrations/*.sql` in Phase 0. This file lists the **in-scope** tables, who
writes them, and which target module owns them. Redis keys: source
`docs/db/redis_keys.md`. Datalake layout: source `docs/db/minio_alpha_lake.md`.

## In-scope tables

| Table | Writer (target module) | Source doc | Tier |
|-------|------------------------|-----------|------|
| `market_data` | retained Python collector / C normalization | `pg_market_data.md` | Warm |
| `ohlcv_daily` (+partitions) | retained Python collector | `pg_ohlcv_daily.md` | Warm |
| `ohlcv_minute` (+partitions) | aggregation cron | `pg_ohlcv_minute.md` | Warm |
| `tick_data` (+partitions) | retained Python collector | `pg_tick_data.md` | Hot→Warm |
| `instruments` | master loader | `pg_instruments.md` | ref |
| `markets` | master loader | `pg_markets.md` | ref |
| `ticker_master` | master loader | `pg_ticker_master.md` | ref |
| `krx_stock_master` | master collector | `pg_krx_stock_master.md` | ref |
| `macro_indicators` | macro collector | `pg_macro_indicators.md` | Warm |
| `predictions` | external signal input | `pg_predictions.md` | Warm |
| `predictor_tournament_scores` | orchestrator | `pg_predictor_tournament_scores.md` | Warm |
| `portfolio_positions` | broker | `pg_portfolio_positions.md` | Warm |
| `trade_history` | broker | `pg_trade_history.md` | Warm |
| `broker_orders` | broker | `pg_broker_orders.md` | Warm |
| `trading_accounts` | broker/session | `pg_trading_accounts.md` | Warm |
| `account_snapshots` | account_state | `pg_account_snapshots.md` | Warm |
| `portfolio_config` | api (config) | `pg_portfolio_config.md` | ref |
| `model_role_configs` | api (config) | `pg_model_role_configs.md` | ref |
| `aggregate_risk_snapshots` | risk | (used by `aggregate_risk.py`) | Warm |
| `backtest_runs` | backtest repo | `pg_backtest_runs.md` | Warm |
| `backtest_daily` | backtest repo | `pg_backtest_daily.md` | Warm |
| `daily_rankings` | ranking | `pg_daily_rankings.md` | Warm |
| `theme_stocks` | screener | `pg_theme_stocks.md` | Warm |
| `watchlist` | api/screener | `pg_watchlist.md` | ref |
| `paper_trading_runs` | paper broker | `pg_paper_trading_runs.md` | Warm |
| `strategy_promotions` | orchestrator | `pg_strategy_promotions.md` | Warm |
| `agent_heartbeats` | all edge tasks | `pg_agent_heartbeats.md` | Warm(7d) |
| `collector_errors` | collector | `pg_collector_errors.md` | Warm |
| `operational_audits` | api/orchestrator | `pg_operational_audits.md` | Warm |
| `real_trading_audit` | broker (real mode) | `pg_real_trading_audit.md` | Warm |
| `notification_history` | notifier | `pg_notification_history.md` | Warm |
| `users` | api auth | `pg_users.md` | ref |

`trading_universe`, `position_state`, `prediction_schedule` also appear in source
migrations (`scripts/db/`) — mirror if the collector/orchestrator needs them.

## Out-of-scope tables (do not port)

`rl_experiments`, `rl_policies`, `rl_targets`, `rl_training_jobs` (RL);
`debate_transcripts`, `research_outputs`, `search_queries`, `search_results`,
`page_extractions` (LLM/search lanes); `agent_registry`, `marketplace_*` unless a
ported edge needs them.

## Key aggregate query (risk) — reproduce as-is
`aggregate_risk.py` aggregates `portfolio_positions` where `quantity > 0`:
`total_aum = Σ(quantity*current_price)`; per-ticker exposure and per-strategy
allocation via `GROUP BY`. See `docs/MODULE_SPECS.md §6` for the exact SQL to port
into `core/platform/db/`.
