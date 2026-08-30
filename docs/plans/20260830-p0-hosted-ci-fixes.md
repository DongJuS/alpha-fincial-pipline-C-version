# Hosted CI portability fixes (Phase 0)

## Goal

Fix the first hosted Ubuntu CI run without weakening any gate, then require all
jobs to pass on the pushed main commit.

## Files

- `core/CMakeLists.txt`
- `.github/workflows/ci.yml`
- `tests/test_ci_workflow.py`
- `progress.md`

## Contract

Satisfies the Phase 0 hosted CI exit gate. Linux must link the C floating-point
environment dependency explicitly, shared-service schema tests must check out the
pinned Python source, and clang-tidy must keep the selected checks while excluding
the platform-inapplicable Annex K warning.

## Tests

- Re-run dev and bench C build/test/flag gates locally.
- Assert CI contains the pinned reference checkout in every job that consumes it.
- Push the independent fix commit, merge to main, and verify every hosted job is
  green before closing Phase 0.

## Risks / open questions

- Ubuntu's packaged libwebsockets installs optional evlib DSOs; the evidence gate
  still rejects an executable that dynamically links libuv/libevent.
