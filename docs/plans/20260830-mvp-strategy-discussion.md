# Discussion — MVP strategy to overcome Python, C, pro-C, and Rust

> Format: a working debate among three roles — **Dev** (ships pragmatically),
> **Eng** (systems/correctness/perf), **Lead** (decision/cost/safety). Goal: agree
> a set of MVPs whose end-state is measurably better than (a) the operating Python
> system, (b) the current comparison-only C plan, (c) a pure pro-C build, and
> (d) a Rust-everywhere build. Conclusion is folded into `MVP_ROADMAP.md`.

Date: 2026-08-30 · Topic: MVP sequencing + "overcome-all" end-state.

---

## Round 1 — What does "overcome" even mean here?

**Lead:** Our current docs are honest but they stop at *"compare, don't migrate."*
That measures value; it never captures it. If the whole project ends at a report,
we haven't overcome Python — we've described it. I want the MVP chain to end at a
committed, shadow-validated production path, not a PDF.

**Eng:** Agreed, but "overcome Python" has to be a *measured, parity-gated* claim
per path, not a vibe. Python here is async (asyncpg/uvicorn). On CPU-bound numeric
work C wins trivially and it proves little. The decision-relevant wins are E2E
p95/p99, throughput/core, CPU, RSS, and startup/recovery. So each MVP must name the
metric it beats Python on, and it's only valid on the `bench` artifact that passed
parity (`BENCHMARK_PLAN §1`).

**Dev:** Then the first MVP can't be the whole pipeline. If MVP-1 is "port
everything," we die in the libwebsockets + nonblocking-libpq integration before we
show a single number. Give me the smallest vertical that produces a defensible win.

---

## Round 2 — pure pro-C vs Rust vs hybrid

**Dev:** I like pure C — one toolchain, one binary, no FFI. But I've written the
LWS-owns-the-loop + async libpq pipeline + reconnect/backpressure code before. It's
weeks of fiddly, CVE-prone buffer management for the *async edges*. That's not where
I want to spend our risk budget.

**Eng:** Right. Split the problem by where each language actually wins:
- The **numeric/decision core** (backtest, blend, risk, sizing) is tight,
  branchy, allocation-light. C is unbeatable there on RSS/startup and ties or beats
  Rust — and it's the one thing that must be a *single* implementation for safety.
- The **async edges** (order path, API, WS replay, datalake) are where memory-safe
  concurrency and `Result`-typed error handling pay off. Hand-rolled C here is the
  liability Dev just described.

**Lead:** So pure pro-C loses on edge safety/maintenance, and Rust-everywhere loses
by paying FFI/toolchain weight and a fatter binary to reimplement the numeric core
it can't actually beat. The synthesis that beats *both* single-language framings is
a **hybrid**: C core + Rust edges, with Python retained only where porting isn't
worth it (collector, LLM reasoning, RL).

**Dev:** I can live with that — as long as we still *build the pure-C edge first*
per the existing rule and only reach for Rust when C measurably hurts. Otherwise
"hybrid" becomes "two toolchains on day one for no reason."

**Eng:** Deal. Keep the decision rule: pure-C edge first; Rust when reconnect/
backpressure/order-safety is hard *or* a measured bottleneck appears. The datalake
is the one pre-approved Rust piece because Parquet+SigV4 in C is pure downside.

---

## Round 3 — how many MVPs, and what's the cut line?

**Lead:** I want each MVP to be independently defensible — if we stop after any of
them, we've still produced a real result. And I want a go/no-go gate after the
cheap ones so we don't build the expensive edges on faith.

**Eng:** Five, vertical, each with a parity gate and a named "beats" claim:
- **MVP-0 Measurement rig** — pinned Python baseline, golden generator, `bench`
  preset, one baseline number. Beats nothing yet; it makes every later claim valid.
- **MVP-1 Numeric win** — C core (cost/backtest/metrics/blend/risk) beats Python on
  the numeric/decision batches at parity. First real win, zero I/O risk.
- **MVP-2 I/O truth** — thin driver slice: C nonblocking libpq + hiredis in the LWS
  loop vs Python asyncpg/redis vs Rust SQLx/fred, on `db-read-write` + `redis-hot-
  path`. This *cheaply* answers the only question that decides the rest.
- **MVP-3 Vertical E2E slice** — normalize → blend → risk → paper order → API read,
  end-to-end, beating Python on E2E p95 at parity. First "overcome Python" on a
  real path.
- **MVP-4 Shadow hybrid + cutover** — production-candidate: C core + chosen edge
  language, shadow-run against live Python, meet go-live criteria. The end-state.

**Dev:** MVP-2 is the linchpin. If C/Rust drivers don't beat async Python on I/O by
a margin that matters, we *stop*, and the honest answer is "keep Python for the I/O
paths, ship only the C numeric core as a library." That's still a win and we saved
MVP-3/4 of effort.

**Lead:** Yes — MVP-2 is the go/no-go. That's exactly the discipline I wanted.

---

## Round 4 — the safety objection (this is live-trading money)

**Lead:** Nothing we do is allowed to create two divergent implementations of the
risk gates or the order path. That's a financial-safety hazard, not a perf question.

**Eng:** Covered by the single-C-core rule: risk/blend/sizing exist *only* in
`libalpha_core`; every edge (C or Rust) calls it over the C ABI. MVP-4's cutover is
a *shadow* run — the challenger computes and logs, Python still trades — until a
separate safety approval flips exactly one real-order adapter. Speedup never
auto-authorizes the flip.

**Dev:** And challengers can't load real credentials, so a benchmark build
physically can't place a real order. Good.

---

## Conclusion (folded into `MVP_ROADMAP.md`)

We reject both single-language end-states and the "compare-only" end-state. The
target that **overcomes all four** is a **measured, parity-gated hybrid with a
committed shadow-cutover**:

- **Overcomes Python** — on each ported path, the `bench` artifact beats Python on a
  decision-relevant metric (E2E p95/p99, throughput/core, CPU, RSS, startup) at
  parity, at lower infra cost. Value is *captured* via cutover, not just reported.
- **Overcomes the current comparison-only C plan** — the chain doesn't stop at a
  report; MVP-4 commits to a shadow-validated production path with go-live criteria.
- **Overcomes pure pro-C** — keeps C for the numeric core (its strength) but refuses
  hand-rolled C for the async edges where it's a safety/maintenance liability.
- **Overcomes Rust-everywhere** — no FFI/toolchain tax on the numeric core; C stays
  smaller/faster to start with lower RSS, while Rust is used exactly where it wins.

Gate: **MVP-2 is go/no-go.** If drivers don't beat async Python by a margin that
matters, ship the C numeric core as a library and keep Python for I/O — and record
that as the honest result.

Execution decision (2026-08-30): "margin that matters" means any statistically
repeatable primary throughput/tail-latency improvement: at least 30 order-rotated
trials and a 95% bootstrap confidence interval excluding parity, with no
correctness, error, drop, resource, or safety regression. MVP-4 evidence uses an
order-disabled replay shadow; live shadowing and real-order activation remain
outside benchmark authorization and require separate approval.

Decisions to carry forward: hybrid end-state; five vertical MVPs; MVP-2 gate;
single C decision core; Rust pre-approved only for datalake, otherwise earned;
shadow-before-cutover with separate safety approval.
