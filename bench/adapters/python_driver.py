#!/usr/bin/env python3
"""Real asyncio/asyncpg/redis MVP-2 adapter for the pinned Python baseline."""
from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
import os
import platform
import resource
import sys
import time
from pathlib import Path


def canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def fixture_sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def distribute(total: int, workers: int) -> list[int]:
    quotient, remainder = divmod(total, workers)
    return [quotient + (index < remainder) for index in range(workers)]


def rss_bytes() -> int:
    maximum = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return int(maximum if sys.platform == "darwin" else maximum * 1024)


async def redis_case(case: dict, concurrency: int) -> tuple[list[dict], int, int]:
    try:
        import redis.asyncio as redis_async
    except ImportError as exc:
        raise RuntimeError("install pinned redis>=5,<6 baseline dependency") from exc

    connection_pool = redis_async.BlockingConnectionPool.from_url(
        os.getenv("ALPHA_REDIS_URL", "redis://127.0.0.1:56379/0"),
        max_connections=case["connection_count"],
        timeout=case["timeout_ms"] / 1000,
        decode_responses=True,
    )
    client = redis_async.Redis(connection_pool=connection_pool)
    tick_key = "redis:cache:latest_ticks:005930"
    breaker_key = "hard_stop:lockout:paper"
    await client.delete(tick_key, breaker_key)
    await client.ping()

    errors = 0

    async def worker(iterations: int) -> None:
        nonlocal errors
        for _ in range(iterations):
            try:
                for operation in case["operations"]:
                    if operation["op"] == "set_latest_tick":
                        await client.set(
                            f"redis:cache:latest_ticks:{operation['ticker']}",
                            operation["value"], ex=60,
                        )
                    elif operation["op"] == "get_latest_tick":
                        value = await client.get(
                            f"redis:cache:latest_ticks:{operation['ticker']}"
                        )
                        if value != operation["expected"]:
                            errors += 1
                    elif operation["op"] == "breaker_check":
                        locked = await client.exists(f"hard_stop:lockout:{operation['scope']}")
                        if bool(locked) != operation["expected"]:
                            errors += 1
            except Exception:
                errors += 1

    started = time.perf_counter_ns()
    await asyncio.gather(*(worker(count) for count in distribute(case["repeat"], concurrency)))
    elapsed_ns = time.perf_counter_ns() - started
    terminal = [
        {"id": "r001", "stored": await client.get(tick_key)},
        {"id": "r002", "ttl_positive": await client.ttl(tick_key) > 0},
        {"id": "r003", "locked": bool(await client.exists(breaker_key))},
    ]
    await client.aclose()
    return terminal, errors, elapsed_ns


async def db_case(case: dict, concurrency: int) -> tuple[list[dict], int, int]:
    try:
        import asyncpg
    except ImportError as exc:
        raise RuntimeError("install pinned asyncpg>=0.30,<1 baseline dependency") from exc

    pool = await asyncpg.create_pool(
        dsn=os.getenv(
            "ALPHA_POSTGRES_URL",
            "postgresql://alpha_test:alpha_test_only@127.0.0.1:55432/alpha_test",
        ),
        min_size=case["connection_count"],
        max_size=case["connection_count"],
        command_timeout=case["timeout_ms"] / 1000,
    )
    async with pool.acquire() as connection:
        await connection.execute("DELETE FROM portfolio_positions WHERE ticker=$1", "MVP2A")
        await connection.fetchval("SELECT 1")

    errors = 0
    upsert = """
        INSERT INTO portfolio_positions
          (ticker,name,quantity,avg_price,current_price,is_paper,account_scope,strategy_id,
           opened_at,updated_at)
        VALUES ($1,$2,$3,1000,$4,true,$5,$6,NOW(),NOW())
        ON CONFLICT (ticker,account_scope,COALESCE(strategy_id,'')) DO UPDATE SET
          quantity=EXCLUDED.quantity,current_price=EXCLUDED.current_price,updated_at=NOW()
    """

    async def worker(iterations: int) -> None:
        nonlocal errors
        for _ in range(iterations):
            try:
                async with pool.acquire() as connection:
                    for operation in case["operations"]:
                        if operation["op"] == "upsert_position":
                            await connection.execute(
                                upsert, operation["ticker"], "MVP-2 fixture",
                                operation["quantity"], operation["current_price"],
                                operation["account_scope"], operation["strategy_id"],
                            )
                        elif operation["op"] == "read_exposure":
                            row = await connection.fetchrow(
                                "SELECT COALESCE(SUM(quantity),0)::bigint AS quantity,"
                                "COALESCE(SUM(quantity*current_price),0)::bigint AS value "
                                "FROM portfolio_positions WHERE ticker=$1 AND quantity>0",
                                operation["ticker"],
                            )
                            expected = operation["expected"]
                            if row["quantity"] != expected["total_quantity"] or row["value"] != expected["total_market_value"]:
                                errors += 1
            except Exception:
                errors += 1

    started = time.perf_counter_ns()
    await asyncio.gather(*(worker(count) for count in distribute(case["repeat"], concurrency)))
    elapsed_ns = time.perf_counter_ns() - started
    async with pool.acquire() as connection:
        rows = await connection.fetch(
            "SELECT strategy_id,quantity,current_price FROM portfolio_positions "
            "WHERE ticker=$1 ORDER BY strategy_id", "MVP2A"
        )
        exposure = await connection.fetchrow(
            "SELECT COALESCE(SUM(quantity),0)::bigint AS quantity,"
            "COALESCE(SUM(quantity*current_price),0)::bigint AS value "
            "FROM portfolio_positions WHERE ticker=$1 AND quantity>0", "MVP2A"
        )
        await connection.execute("DELETE FROM portfolio_positions WHERE ticker=$1", "MVP2A")
    await pool.close()
    terminal = [
        {"id": "d001", "strategy_id": rows[0]["strategy_id"], "quantity": rows[0]["quantity"], "current_price": rows[0]["current_price"]},
        {"id": "d002", "strategy_id": rows[1]["strategy_id"], "quantity": rows[1]["quantity"], "current_price": rows[1]["current_price"]},
        {"id": "d003", "total_quantity": exposure["quantity"], "total_market_value": exposure["value"]},
    ]
    return terminal, errors, elapsed_ns


async def run(args: argparse.Namespace) -> dict:
    fixture = json.loads(args.fixture.read_text(encoding="utf-8"))
    case = fixture["cases"][args.case]
    cpu_before = time.process_time_ns()
    if args.case == "redis-hot-path":
        terminal, errors, elapsed_ns = await redis_case(case, args.concurrency)
    else:
        terminal, errors, elapsed_ns = await db_case(case, args.concurrency)
    cpu_ns = time.process_time_ns() - cpu_before
    return {
        "elapsed_ms": elapsed_ns / 1_000_000,
        "fixture_sha256": fixture_sha(args.fixture),
        "completed_ids": [operation["id"] for operation in case["operations"]],
        "result_sha256": hashlib.sha256(canonical(terminal)).hexdigest(),
        "errors": errors,
        "dropped": 0,
        "configuration": {
            "concurrency": args.concurrency,
            "operation_count": len(case["operations"]) * case["repeat"],
            "pipeline_depth": case["pipeline_depth"],
            "timeout_ms": case["timeout_ms"],
            "event_loop_mode": "asyncio",
            "worker_count": 1,
            "queue_depth": case["pipeline_depth"],
            "connection_count": case["connection_count"],
        },
        "resources": {"peak_rss_bytes": rss_bytes(), "cpu_time_ms": cpu_ns / 1_000_000},
        "build": {"runtime": platform.python_version(), "compiler": platform.python_compiler(), "flags": "pinned requirements workload"},
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--case", required=True, choices=("redis-hot-path", "db-read-write"))
    parser.add_argument("--concurrency", required=True, type=int, choices=(1, 8, 32))
    parser.add_argument("--trial", required=True, type=int)
    args = parser.parse_args()
    if args.trial < 0:
        parser.error("trial must be nonnegative")
    print(json.dumps(asyncio.run(run(args)), sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
