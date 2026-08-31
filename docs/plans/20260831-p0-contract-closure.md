# P0 reproducibility contract closure

## Goal

Reopen P0 and close three audit findings before any P3 work: provide the selected
Rust datalake workspace boundary, build the exact libwebsockets source revision
with alternate event loops disabled, and execute plus assert every migration in
the locked Python schema manifest.

## Work streams

- Rust: minimal `edge/datalake` workspace, pinned toolchain/direct dependencies,
  lockfile, feature-boundary tests, fmt/clippy/test/build CI.
- Event loop: source pin and checksum, reproducible static build, backend/link
  verification, and use of the same staged artifact in CI and the spike.
- Schema: AST-extract and apply both locked migrations, idempotence/catalog
  assertions, and CI evidence against pinned PostgreSQL.

## Verification

Run local tests available on this host; validate workflow syntax and scripts;
push the isolated branch only after review. Hosted CI is the authoritative Linux,
PostgreSQL, and Rust-toolchain check.

## Stop condition

Do not resume P2/P3 until all three streams are evidenced. This closes P0 only;
it does not implement the P4 datalake adapter or production P3 drivers.
