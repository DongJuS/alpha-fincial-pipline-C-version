# MVP roadmap review and decision lock

## Goal

Review the existing MVP overlay against the normative P0–P5, parity, benchmark,
and broker-safety rules; commit it as an auditable decision document.

## Files

- `docs/plans/MVP_ROADMAP.md`
- `docs/plans/20260830-mvp-strategy-discussion.md`
- `MEMORY.md`, `progress.md`

## Contract

The phase guides and `docs/BENCHMARK_PLAN.md` remain normative. This overlay may
sequence work and define a stop gate, but cannot weaken parity, safety, or scope.

## Tests

- Cross-reference every MVP to its phase and benchmark gate.
- Confirm challengers are paper/replay-only and real-order activation remains a
  separate approval.
- Confirm the MVP-2 gate has a deterministic statistical decision rule.

## Risks / open questions

- A NO-GO intentionally ends I/O migration after publishing the verified numeric
  library outcome; it is not permission to claim P3–P5 completion.
