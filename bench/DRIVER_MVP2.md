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
`--concurrency`, `--trial`, and a per-trial `--namespace`. It isolates mutable
keys/rows under that namespace, establishes/warm ups connections outside the
timed region, performs the fixture's repeated operations, and prints one JSON
object:

```json
{
  "elapsed_ms": 123.4,
  "fixture_sha256": "...",
  "completed_tokens": ["000000:r001", "000000:r002", "000000:r003"],
  "operation_latency_ns": [100, 101, 102],
  "terminal": [{"id": "r001", "stored": "..."}],
  "result_sha256": "canonical terminal/result checksum",
  "errors": 0,
  "dropped": 0,
  "configuration": {
    "concurrency": 8,
    "operation_count": 3000,
    "pipeline_depth": 1,
    "timeout_ms": 5000,
    "event_loop_mode": "asyncio|lws|tokio",
    "worker_count": 1,
    "queue_depth": 1,
    "connection_count": 1,
    "retry_policy": "none",
    "saturation_policy": "bounded_wait",
    "service_config_sha256": "...",
    "schema_sha256": "..."
  },
  "resources": {"peak_rss_bytes": 1, "cpu_time_ms": 1.0},
  "build": {"runtime": "...", "compiler": "...", "flags": "...",
            "dependencies": {"driver": "version", "server": "version"}}
}
```

Each case commits its canonical terminal object and checksum in the fixture.
The orchestrator rejects terminal/checksum, all 3,000 completion tokens,
per-operation latency, exact loop/configuration, dependency/service/schema,
error/drop, adapter-artifact attestation, and source-cleanliness failures. It
emits 18 files: two cases ×
three concurrencies × three variants, each with 30 raw order-rotated latency and
derived operation-throughput samples.
`evaluate_mvp2.py` uses deterministic paired bootstrap resampling and returns
`GO` only when every cell's 95% speedup interval is above 1.0 and candidate
CPU/RSS ratios stay within the supplied limits (default: no regression).

No adapter means no result: the harness never substitutes synthetic timings.
