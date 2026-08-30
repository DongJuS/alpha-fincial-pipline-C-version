# docs/plans/ — Plan discipline

This repo carries the source project's rule: **write a plan md before coding, and
update `progress.md` after.**

## Two kinds of files here

1. **Phase guides** (`phase-0-scaffold.md` … `phase-5-e2e.md`) — the fixed,
   pre-written build guides. Read the current one before starting a phase. Do not
   rewrite them; they are the handoff spec.

2. **Task plans** (`YYYYMMDD-topic-slug.md`) — you create one per task/PR before
   implementing. One topic per file. Suggested skeleton:

```markdown
# <topic>  (Phase N)

## Goal
What this task delivers and its done-criteria.

## Files
New/changed files (target paths).

## Contract
Which `docs/MODULE_SPECS.md` section(s) this satisfies; Python source to match.

## Tests
Unit + parity cases to add (`docs/BUILD_AND_TEST.md`).

## Risks / open questions
```

## Workflow
1. Read `progress.md` → pick the current phase guide.
2. Write a task plan here.
3. Implement against `docs/MODULE_SPECS.md`; add tests.
4. Update `progress.md`; record durable decisions in `MEMORY.md`.
5. When a task plan's conclusions are folded into permanent docs, the task plan
   may be deleted (keep the phase guides).
