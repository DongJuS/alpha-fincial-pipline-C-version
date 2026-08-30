# MVP_ROADMAP.md — Vertical MVPs to an "overcome-all" end-state

> Source of this plan: `docs/plans/20260830-mvp-strategy-discussion.md`.
> End-state thesis: a **measured, parity-gated hybrid with a committed shadow
> cutover** that overcomes the Python system, the comparison-only C plan, a pure
> pro-C build, and a Rust-everywhere build. This roadmap is an *overlay* on the
> P0–P5 phase guides — MVPs are vertical slices; phases are the horizontal layers
> they cut through.

## Rules that make every MVP claim valid
- A "beats Python" claim is only valid on the **`bench` artifact that passed
  parity** (`BENCHMARK_PLAN §1`, `BUILD_AND_TEST §parity`).
- Risk/blend/sizing stay a **single C implementation**; every edge calls it via the
  C ABI. No cloned decisions.
- Report **absolute numbers**, CPU-bound/I-O-bound/startup/E2E separated; speedup
  alone never authorizes migration (`BENCHMARK_PLAN §9`).
- Challengers are paper/replay-only and cannot load real credentials.

## MVP ladder (each is independently defensible)

### MVP-0 — Measurement rig
- **Deliver:** pinned Python SHA + fixtures/goldens + `bench` preset + one recorded
  Python baseline (`backtest-small`). LWS↔libpq/hiredis feasibility spike.
- **Beats:** nothing yet — it makes every later claim *valid* and kills the biggest
  technical risk (the single-loop integration) before we build on it.
- **Exit / gate:** golden generator committed; `dev`+`bench` build & `ctest` green;
  spike proves no hidden loop / no unbounded queue. Maps to phase-0.

### MVP-1 — Numeric win (C core, no I/O)
- **Deliver:** `domain`, `cost_model`, `backtest` (engine/metrics), `blend`, `risk`
  gates, sizing — parity to Python.
- **Beats Python on:** numeric/decision batch throughput + peak RSS + startup, at
  parity (`backtest-small/large`, `blend-batch`, `risk-batch`).
- **Also settles C-vs-Rust for the core:** measure the same core called from a tiny
  Rust harness; expect C ≤ Rust on RSS/startup with no FFI tax → justifies "C owns
  the core." Maps to phases 1–2.
- **Exit:** 7 priority parity cases pass on `bench`; numeric results published.

### MVP-2 — I/O truth  ★ GO/NO-GO GATE
- **Deliver:** thin driver slice only — `db-read-write` + `redis-hot-path` — as
  three variants: Python (asyncpg/redis), C (nonblocking libpq + hiredis in the LWS
  loop), Rust (SQLx/fred). Identical pool/timeout/concurrency/service.
- **Beats Python on:** driver throughput + p95/p99 + connections/CPU at 1/8/32
  concurrency — *or it does not*.
- **Decision:**
  - **If C/Rust drivers show any statistically repeatable primary-metric win** →
    proceed to MVP-3/4 with the winning edge language. The gate uses at least 30
    order-rotated trials; a 95% bootstrap confidence interval for a throughput or
    tail-latency improvement must exclude parity, with no correctness, error,
    drop, resource, or safety regression.
  - **If not** → **stop**. Ship the C numeric core as an embeddable library, keep
    Python for I/O paths, and record that as the honest outcome. (Still overcomes
    nothing operationally, but saves MVP-3/4 and is a truthful result.)
- **Exit:** driver results published; go/no-go recorded in `progress.md`. Maps to a
  reduced slice of phase-3 (drivers only, no full API/broker yet).

### MVP-3 — Vertical E2E slice beats Python
- **Deliver:** one real path end-to-end — normalized input → blend → risk gate →
  paper order → API read — on the C core + the MVP-2-winning edge (pure-C first;
  Rust only if MVP-2/earned per `BENCHMARK_PLAN §8`). Rust datalake slotted in
  (pre-approved).
- **Beats Python on:** **E2E p95/p99 cycle latency + throughput/core** at parity,
  where E2E parity = **terminal persisted-state equality** (final
  `portfolio_positions` / `trade_history` / breaker flag), decoupled from latency.
- **Exit:** `e2e-paper` parity green; E2E numbers published. Maps to phases 3–4.

### MVP-4 — Replay-shadow hybrid + cutover-ready evidence (the end-state)
- **Deliver:** production-candidate hybrid — C decision core + chosen edge language
  + Rust datalake + retained Python where not ported. Run it in an order-disabled,
  deterministic **replay shadow** against captured Python inputs. Define go-live criteria:
  sustained E2E/throughput/CPU/RSS/startup wins at parity **plus** an operational
  safety review.
- **Beats all four framings** — see the thesis table below.
- **Exit:** replay-shadow run meets criteria and produces cutover-ready evidence;
  a **separate safety approval** may authorize live shadowing or enable
  exactly one real-order adapter. Speedup never auto-flips it. Maps to phase-5+.

## How the end-state overcomes each baseline

| Baseline | How MVP-4 overcomes it |
|---|---|
| **Python** | Measured win on E2E p95/p99, throughput/core, CPU, RSS, startup at parity; lower infra cost; replay-shadow evidence makes a separately approved cutover actionable. |
| **Current comparison-only C plan** | Doesn't stop at a report — commits to a shadow-validated production path with explicit go-live + safety gates. |
| **Pure pro-C** | Keeps C for the numeric core (its strength) but refuses hand-rolled C for async edges where reconnect/backpressure/order-safety is a liability. |
| **Rust-everywhere** | No FFI/toolchain tax on the numeric core; C stays smaller/faster-start/lower-RSS, while Rust is used exactly where it wins (edges, datalake). |

## Effort & sequencing note
Front-load MVP-0→2 (cheap, high-signal). **Do not build the full API/broker/
orchestrator until MVP-2 says the I/O win is real.** This aligns spend with the
benchmark-driven philosophy: measure the deciding question before paying for the
expensive edges.

## Per-MVP doc convention
Each MVP, when started, gets a task plan `docs/plans/YYYYMMDD-mvp-N-slug.md`
(skeleton in `docs/plans/README.md`) and updates `progress.md` on completion.
