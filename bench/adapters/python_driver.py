#!/usr/bin/env python3
"""Real asyncio/asyncpg/redis MVP-2 adapter for the pinned Python baseline."""
from __future__ import annotations

import argparse
import asyncio
import hashlib
import importlib.metadata
import json
import os
import platform
import resource
import sys
import time
from pathlib import Path
from urllib.parse import urlparse


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


def service_config_sha() -> str:
    redis_url = urlparse(os.getenv("ALPHA_REDIS_URL", "redis://127.0.0.1:56379/0"))
    postgres_url = urlparse(os.getenv(
        "ALPHA_POSTGRES_URL",
        "postgresql://alpha_test:alpha_test_only@127.0.0.1:55432/alpha_test",
    ))
    config = {
        "postgres": {"database": postgres_url.path.lstrip("/"),
                     "host": postgres_url.hostname, "port": postgres_url.port},
        "redis": {"database": int(redis_url.path.lstrip("/") or "0"),
                  "host": redis_url.hostname, "port": redis_url.port},
    }
    return hashlib.sha256(canonical(config)).hexdigest()


async def redis_case(case: dict, concurrency: int, namespace: str) -> tuple[list[dict], list[str], list[int], int, int, int, dict]:
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
    suffix = hashlib.sha256(namespace.encode()).hexdigest()[:8]
    tick_key = f"redis:cache:latest_ticks:M2{suffix}"
    breaker_key = f"hard_stop:lockout:mvp2-{suffix}"
    await client.delete(tick_key, breaker_key)
    await client.ping()
    server_info = await client.info("server")

    errors = 0
    completions: list[tuple[str, int]] = []

    async def worker(first: int, iterations: int) -> None:
        nonlocal errors
        for iteration in range(first, first + iterations):
            try:
                for operation in case["operations"]:
                    operation_started = time.perf_counter_ns()
                    if operation["op"] == "set_latest_tick":
                        await client.set(
                            tick_key,
                            operation["value"], ex=60,
                        )
                    elif operation["op"] == "get_latest_tick":
                        value = await client.get(
                            tick_key
                        )
                        if value != operation["expected"]:
                            errors += 1
                    elif operation["op"] == "breaker_check":
                        locked = await client.exists(breaker_key)
                        if bool(locked) != operation["expected"]:
                            errors += 1
                    completions.append((f"{iteration:06d}:{operation['id']}", time.perf_counter_ns() - operation_started))
            except Exception:
                errors += 1

    counts = distribute(case["repeat"], concurrency)
    starts = [sum(counts[:index]) for index in range(concurrency)]
    cpu_started = time.process_time_ns()
    started = time.perf_counter_ns()
    await asyncio.gather(*(worker(first, count) for first, count in zip(starts, counts)))
    elapsed_ns = time.perf_counter_ns() - started
    cpu_ns = time.process_time_ns() - cpu_started
    terminal = [
        {"id": "r001", "stored": await client.get(tick_key)},
        {"id": "r002", "ttl_positive": await client.ttl(tick_key) > 0},
        {"id": "r003", "locked": bool(await client.exists(breaker_key))},
    ]
    await client.aclose()
    completions.sort()
    return terminal, [item[0] for item in completions], [item[1] for item in completions], errors, elapsed_ns, cpu_ns, {"redis": importlib.metadata.version("redis"), "redis_server": server_info["redis_version"]}


async def db_case(case: dict, concurrency: int, namespace: str) -> tuple[list[dict], list[str], list[int], int, int, int, dict]:
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
    ticker = "M2" + hashlib.sha256(namespace.encode()).hexdigest()[:8]
    async with pool.acquire() as connection:
        await connection.execute("DELETE FROM portfolio_positions WHERE ticker=$1", ticker)
        await connection.fetchval("SELECT 1")

    errors = 0
    completions: list[tuple[str, int]] = []
    upsert = """
        INSERT INTO portfolio_positions
          (ticker,name,quantity,avg_price,current_price,is_paper,account_scope,strategy_id,
           opened_at,updated_at)
        VALUES ($1,$2,$3,1000,$4,true,$5,$6,NOW(),NOW())
        ON CONFLICT (ticker,account_scope,COALESCE(strategy_id,'')) DO UPDATE SET
          quantity=EXCLUDED.quantity,current_price=EXCLUDED.current_price,updated_at=NOW()
    """

    async def worker(first: int, iterations: int) -> None:
        nonlocal errors
        for iteration in range(first, first + iterations):
            try:
                async with pool.acquire() as connection:
                    for operation in case["operations"]:
                        operation_started = time.perf_counter_ns()
                        if operation["op"] == "upsert_position":
                            await connection.execute(
                                upsert, ticker, "MVP-2 fixture",
                                operation["quantity"], operation["current_price"],
                                operation["account_scope"], operation["strategy_id"],
                            )
                        elif operation["op"] == "read_exposure":
                            row = await connection.fetchrow(
                                "SELECT COALESCE(SUM(quantity),0)::bigint AS quantity,"
                                "COALESCE(SUM(quantity*current_price),0)::bigint AS value "
                                "FROM portfolio_positions WHERE ticker=$1 AND quantity>0",
                                ticker,
                            )
                            expected = operation["expected"]
                            if row["quantity"] != expected["total_quantity"] or row["value"] != expected["total_market_value"]:
                                errors += 1
                        completions.append((f"{iteration:06d}:{operation['id']}", time.perf_counter_ns() - operation_started))
            except Exception:
                errors += 1

    counts = distribute(case["repeat"], concurrency)
    starts = [sum(counts[:index]) for index in range(concurrency)]
    cpu_started = time.process_time_ns()
    started = time.perf_counter_ns()
    await asyncio.gather(*(worker(first, count) for first, count in zip(starts, counts)))
    elapsed_ns = time.perf_counter_ns() - started
    cpu_ns = time.process_time_ns() - cpu_started
    async with pool.acquire() as connection:
        rows = await connection.fetch(
            "SELECT strategy_id,quantity,current_price FROM portfolio_positions "
            "WHERE ticker=$1 ORDER BY strategy_id", ticker
        )
        exposure = await connection.fetchrow(
            "SELECT COALESCE(SUM(quantity),0)::bigint AS quantity,"
            "COALESCE(SUM(quantity*current_price),0)::bigint AS value "
            "FROM portfolio_positions WHERE ticker=$1 AND quantity>0", ticker
        )
        server_version = connection.get_server_version()
        await connection.execute("DELETE FROM portfolio_positions WHERE ticker=$1", ticker)
    await pool.close()
    terminal = [
        {"id": "d001", "strategy_id": rows[0]["strategy_id"], "quantity": rows[0]["quantity"], "current_price": rows[0]["current_price"]},
        {"id": "d002", "strategy_id": rows[1]["strategy_id"], "quantity": rows[1]["quantity"], "current_price": rows[1]["current_price"]},
        {"id": "d003", "total_quantity": exposure["quantity"], "total_market_value": exposure["value"]},
    ]
    completions.sort()
    return terminal, [item[0] for item in completions], [item[1] for item in completions], errors, elapsed_ns, cpu_ns, {"asyncpg": importlib.metadata.version("asyncpg"), "postgresql": ".".join(map(str, server_version[:2]))}


async def run(args: argparse.Namespace) -> dict:
    fixture = json.loads(args.fixture.read_text(encoding="utf-8"))
    case = fixture["cases"][args.case]
    if args.case == "redis-hot-path":
        terminal, completed_tokens, latency_ns, errors, elapsed_ns, cpu_ns, dependencies = await redis_case(case, args.concurrency, args.namespace)
    else:
        terminal, completed_tokens, latency_ns, errors, elapsed_ns, cpu_ns, dependencies = await db_case(case, args.concurrency, args.namespace)
    contract = fixture["contract"]
    return {
        "elapsed_ms": elapsed_ns / 1_000_000,
        "fixture_sha256": fixture_sha(args.fixture),
        "completed_tokens": completed_tokens,
        "operation_latency_ns": latency_ns,
        "terminal": terminal,
        "result_sha256": hashlib.sha256(canonical(terminal)).hexdigest(),
        "errors": errors,
        "dropped": 0,
        "configuration": {
            "concurrency": args.concurrency,
            "operation_count": len(case["operations"]) * case["repeat"],
            "pipeline_depth": contract["pipeline_depth"],
            "timeout_ms": case["timeout_ms"],
            "event_loop_mode": "asyncio",
            "worker_count": 1,
            "queue_depth": contract["queue_depth"],
            "connection_count": case["connection_count"],
            "retry_policy": contract["retry_policy"],
            "saturation_policy": contract["saturation_policy"],
            "service_config_sha256": service_config_sha(),
            "schema_sha256": contract["schema_sha256"],
        },
        "resources": {"peak_rss_bytes": rss_bytes(), "cpu_time_ms": cpu_ns / 1_000_000},
        "build": {"runtime": platform.python_version(), "compiler": platform.python_compiler(), "flags": "pinned requirements workload", "dependencies": dependencies},
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--case", required=True, choices=("redis-hot-path", "db-read-write"))
    parser.add_argument("--concurrency", required=True, type=int, choices=(1, 8, 32))
    parser.add_argument("--trial", required=True, type=int)
    parser.add_argument("--namespace", required=True)
    args = parser.parse_args()
    if args.trial < 0:
        parser.error("trial must be nonnegative")
    print(json.dumps(asyncio.run(run(args)), sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
