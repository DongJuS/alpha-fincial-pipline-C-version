# BENCHMARK_PLAN.md — Correctness and performance comparison contract

This document is normative. The project compares a pinned Python baseline with a
C implementation and, for selected async edges, a Rust implementation. It does
not benchmark or reimplement infrastructure or Infisical.

## 1. Valid comparison gate

Record in `progress.md` before generating fixtures or results:

- Python repository commit SHA and dirty/clean state.
- C repository commit SHA and dirty/clean state (once Git is initialized).
- Rust commit is the same repository commit when a Rust edge exists.
- schema/migration version and fixture dataset version.
- host OS, kernel, CPU model/count, RAM, compiler/interpreter versions and flags.

A performance result is publishable only when the **same optimized artifact** (the
`bench` preset, `§5.2`) passes that case's parity test. Parity proven only on a
`dev`/`-O0` build does not qualify a `bench` timing, because FP contraction/FMA
appears only at `-O2/-O3`. Failed-parity timings may be retained for diagnosis but
must be labelled invalid and excluded from summaries.

## 2. Exclusions and shared environment

- PostgreSQL, Redis, S3/MinIO, Docker/cluster definitions, monitoring, and network
  infrastructure are shared dependencies, not port targets.
- Infisical and secret retrieval are excluded. Inject equivalent configuration
  before the timed region and never include secret lookup in latency results.
- The operating Python collector is retained. External KRX/Yahoo/KIS latency is
  outside candidate timings. Use a
  recorded response or local mock for timed runs; use live services only for
  compatibility smoke tests.
- RL training/model/evaluation, React UI, search/blog lanes remain out of scope.

## 3. Benchmark layers

| Layer | Representative workload | Compared variants | Primary metrics |
|---|---|---|---|
| Numeric | cost model, metrics, backtest over fixed bars | Python / C | wall time, CPU time, peak RSS |
| Decision | normalize, blend, risk, sizing, ranking batches | Python / C | ops/s, p50/p95/p99, peak RSS |
| Drivers | fixed DB/Redis operations | Python / C; selected Rust SQLx/fred | throughput, latency percentiles, connections, errors |
| Edges | WS ingest, paper broker, representative API, scheduler | Python / pure C / selected Rust | msg/s or req/s, p50/p95/p99, CPU, RSS, drops/errors |
| E2E | replayed tick/signal → risk → paper order → API read | eligible variants | cycle latency, throughput, CPU, RSS, correctness |

Do not port every endpoint merely to increase coverage. For the API benchmark use
at least: health, DB-backed read, large JSON response, JWT-protected read, and one
validated write. Expand only when a measured or operational question requires it.

## 4. Canonical workloads

Commit inputs under `bench/fixtures/` and never fetch mutable external data inside
a timed run.

- `backtest-small`: 1 ticker × 1,000 daily bars.
- `backtest-large`: 100 tickers × 10,000 bars, run independently.
- `blend-batch`: 1,000,000 fixed 2-way/N-way signal sets including conflicts.
- `risk-batch`: 100,000 portfolio snapshots including every boundary condition.
- `risk-configs` / `blend-configs`: pinned default, custom-valid, boundary, and
  invalid JSON fixtures. Python/C consume the identical normalized config bytes;
  record config SHA-256, source (`fixture` or `portfolio_config`), and validation result.
- `db-read-write`: fixed rows and indexed queries at concurrency 1/8/32.
- `redis-hot-path`: latest-tick set/get and breaker check at concurrency 1/8/32.
- `ws-replay` (optional): recorded frames at controlled rates, including reconnect;
  never a live collector test.
- `datalake`: fixed Arrow batches → Parquet → self-hosted S3-compatible round-trip.
- `api-read`: fixed response shapes at concurrency 1/8/32/128.
- `e2e-paper`: deterministic replay with no real broker order capability.

Fixture checksums and expected golden-output checksums belong in
`bench/fixtures/manifest.json`.

## 5. Run protocol

1. Use the same host and shared service instances for compared variants; run
   variants sequentially and rotate their order between trials.
2. Build C via the `bench` preset (`-O3 -DNDEBUG -ffp-contract=off`) and Rust with
   Cargo `--release`, unless an explicitly recorded alternative is used. Keep
   sanitizer runs as separate correctness jobs. Record all flags. The `bench`
   preset that produced a timing must be the same artifact that passed parity
   (`§1`). Standard eligible builds forbid `-ffast-math`; fast-math/FMA experiments
   are separately labelled challengers and never reported as Python-equivalent.
3. Pin Python version and dependencies. Disable debug/profiling hooks in timed runs.
4. Preload fixtures and establish pools/connections before timing unless startup
   is the metric. Keep pool sizes, payloads, timeouts, retries, and concurrency equal.
   Record event-loop mode, worker count, worker-queue depth, DB/Redis pipeline depth,
   and any saturation/backpressure policy; results with different modes are
   separate configurations, not interchangeable language samples.
5. Warm up until steady state, then run at least 10 measured trials. Each trial
   must be long enough to exceed timer noise (target ≥1 second for micro-batches).
6. Report median, p95, min/max, standard deviation, sample count, and raw samples.
   Report speedup as `python_median / candidate_median` with units and direction.
7. Abort or mark invalid on parity failure, dropped work, unexpected retry, service
   throttling, thermal throttling, or material background-load interference.
8. Never time Debug/ASan C against release Python and call it a language result.

## 6. Correctness rules

Golden generation is scripted and committed; ad-hoc untracked scripts are not
acceptable. Each golden records the pinned Python SHA, generator version, input
checksum, timezone (`Asia/Seoul` where market time is relevant), and output
checksum. Numeric tolerances remain defined in `docs/BUILD_AND_TEST.md`.

Parity C processes set and verify `FE_TONEAREST`. Python-compatible decimal
rounding uses ties-to-even semantics; do not substitute C `round()`. Pin handling
of `-0.0`, NaN, infinity, compiler, target architecture, and FP flags.

For date-sensitive cases, use a supplied trading calendar. Breaker expiry is an
absolute timestamp derived from the next configured trading session at 09:00 KST,
not a hardcoded 24-hour TTL.

## 7. Result format

Write machine-readable results to `bench/results/<date>/<case>-<variant>.json`:

```json
{
  "case": "backtest-small",
  "variant": "python|c|rust",
  "eligible": true,
  "parity": {"passed": true, "golden_sha256": "..."},
  "source": {"commit": "...", "dirty": false},
  "environment": {"host_id": "...", "cpu": "...", "os": "..."},
  "build": {"runtime": "...", "compiler": "...", "flags": "..."},
  "workload": {"fixture_sha256": "...", "concurrency": 1},
  "samples_ms": [],
  "summary": {"median_ms": 0, "p95_ms": 0, "stddev_ms": 0},
  "resources": {"peak_rss_bytes": 0, "cpu_time_ms": 0},
  "errors": {"count": 0, "dropped": 0}
}
```

Human-readable reports must include absolute numbers, not speedup alone, and must
separate CPU-bound, I/O-bound, startup, and E2E findings. Also record source lines,
binary/container size, build time, and notable safety/maintenance tradeoffs; these
are comparison evidence, not a combined performance score.

## 8. Decision rule for Rust

Implement the pure-C edge first. Add a Rust variant only when at least one is true:

- C measurements show an edge-specific bottleneck worth comparing.
- reconnect, backpressure, or concurrency safety is materially difficult in C.
- an operational question requires a three-way Python/C/Rust result.

Rust edge code calls the same `libalpha_core` C ABI. It must use the same fixtures,
service configuration, concurrency, and parity gates as the other variants.

## 9. Migration and production-safety rule

- A numeric-core speedup alone never justifies production migration or the
  permanent cost of a second language/toolchain. A migration candidate must show
  a material production-relevant benefit in E2E p95/p99 latency, sustained
  throughput, CPU/RSS, startup/recovery, or operational safety/complexity.
- Risk, blending, and sizing remain a single C implementation. No edge may clone
  those decisions.
- C and Rust broker adapters may coexist only in paper/replay comparison. Exactly
  one adapter may be enabled for real trading in a deployment; all challengers
  must be unable to load real credentials or place real orders.
- Activating or replacing the production broker is a separate safety approval,
  not an automatic consequence of a benchmark win.
