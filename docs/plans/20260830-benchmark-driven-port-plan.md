# Benchmark-driven port plan

## Goal
Reframe this handoff around its actual purpose: preserve the operating Python
system as the reference, port selected application paths to a C base with optional
Rust async edges, and compare correctness, performance, resource use, and operating
complexity. Infrastructure and Infisical are shared/excluded, not reimplemented.

## Files
- Update project intent and scope in `CLAUDE.md`, `AGENTS.md`, `architecture.md`.
- Add `docs/BENCHMARK_PLAN.md` as the normative experiment contract.
- Align `docs/BUILD_AND_TEST.md`, `.agent/roadmap.md`, phase guides, `progress.md`,
  and `MEMORY.md` with benchmark-first vertical slices.
- Link the benchmark document from the existing maps where relevant.

## Contract
- The Python implementation remains the behavioral and performance baseline.
- Correctness parity is a prerequisite for publishing performance comparisons.
- C owns the deterministic decision core; Rust may be compared only at selected
  async edges and must call the same C ABI.
- PostgreSQL, Redis, S3/MinIO, deployment infrastructure, and Infisical are not
  language-port targets. All variants use equivalent shared services/config.

## Tests
- Verify all Markdown references point to repository files or explicitly named
  files in the Python reference repository.
- Check AI-read documents remain under approximately 200 lines.
- Review phase exits to ensure a measurable result is produced incrementally.

## Risks / open questions
- The exact Python reference commit must be recorded before golden generation.
- Final benchmark hardware and representative workloads remain to be selected.
- The final set of Rust comparison edges should follow measured C results rather
  than be decided before Phase 4.
