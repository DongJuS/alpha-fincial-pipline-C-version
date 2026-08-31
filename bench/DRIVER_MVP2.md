# MVP-2 driver benchmark adapter contract

`run_driver_mvp2.py` is an orchestrator, not a workload simulator. Supply three
real adapters, sharing the same PostgreSQL/Redis instances and fixture:

```sh
python3 bench/run_driver_mvp2.py \
  --adapter 'python=python3 path/to/python_adapter.py' \
  --adapter 'c=build/bench/core/c_driver_adapter' \
  --adapter 'rust=edge/target/release/rust_driver_adapter' \
  --source python=../alpha-financial-pipeline \
  --source c=. --source rust=. \
  --output-dir bench/results/YYYYMMDD/mvp2
```

Source worktrees must be clean. Each command receives `--fixture`, `--case`,
`--concurrency`, and `--trial`, establishes/warm ups its connections outside the
timed region, performs the fixture's repeated operations, and prints exactly one
JSON object:

```json
{
  "elapsed_ms": 123.4,
  "fixture_sha256": "...",
  "completed_ids": ["r001", "r002", "r003"],
  "result_sha256": "canonical terminal/result checksum",
  "errors": 0,
  "dropped": 0,
  "configuration": {
    "concurrency": 8,
    "operation_count": 3000,
    "pipeline_depth": 32,
    "timeout_ms": 5000,
    "event_loop_mode": "asyncio|lws|tokio",
    "worker_count": 1,
    "queue_depth": 32,
    "connection_count": 1
  },
  "resources": {"peak_rss_bytes": 1, "cpu_time_ms": 1.0},
  "build": {"runtime": "...", "compiler": "...", "flags": "..."}
}
```

The orchestrator rejects checksum, completion-order, parity, configuration,
error, drop, and source-cleanliness failures. It emits 18 files: two cases ×
three concurrencies × three variants, each with 30 raw order-rotated latency and
derived operation-throughput samples.
`evaluate_mvp2.py` uses deterministic paired bootstrap resampling and returns
`GO` only when every cell's 95% speedup interval is above 1.0 and candidate
CPU/RSS ratios stay within the supplied limits (default: no regression).

No adapter means no result: the harness never substitutes synthetic timings.
