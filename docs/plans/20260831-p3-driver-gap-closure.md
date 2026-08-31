# P3 driver gap closure and MVP-2 evidence

## Goal

Correct the audited Redis compatibility/expiry defects and complete the required
nonblocking Redis, PostgreSQL, and HTTP paths on the libwebsockets-owned loop.
Produce parity-gated driver benchmarks and an evidence-based MVP-2 GO/NO-GO.

## Work streams

1. Redis: restore Python `hard_stop:lockout:{scope}` compatibility, use absolute
   server expiry, validate stored values, read the locked KRX holiday calendar,
   and test restart/weekend/holiday/stale-calendar/release behavior.
2. PostgreSQL: nonblocking libpq connection plus bounded pipeline/result FIFO,
   typed round-trip and aggregate-risk parity against the pinned Python contract.
3. HTTP: libcurl-multi Telegram/optional LLM POST seam against deterministic
   local replay; no collector or secret retrieval in the timed region.
4. Integrate readiness with the single pinned LWS loop; no hidden loops or
   blocking calls on its service thread. Lint every new production source.
5. Benchmark `redis-hot-path` and `db-read-write` at concurrency 1/8/32 under
   identical Python/C service/config conditions. Run at least 30 order-rotated
   trials, bootstrap the primary-metric delta, and commit raw/result evidence.

## Verification

- Unit and Docker integration tests, exact Python key/query parity, restart and
  failure/recovery tests, dev/bench builds, sanitizers, format and clang-tidy.
- A workload is ineligible for a speed claim until parity passes.
- Update `progress.md` and durable decisions only after evidence exists; keep
  both under the repository line limits.

## Stop gate

GO requires a 95% bootstrap interval excluding parity across 30+ order-rotated
trials with no correctness, error/drop, resource, recovery, or safety regression.
NO-GO keeps Python I/O and stops P4/P5. Do not infer GO from unit tests or a
single-host point estimate.
