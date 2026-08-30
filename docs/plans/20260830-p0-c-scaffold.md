# C core scaffold (Phase 0)

## Goal

Create the C11 static-library scaffold, public domain/error/constants ABI, pinned
yyjson integration, and a Unity-based smoke test. Both sanitizer `dev` and optimized
`bench` presets must compile and pass `ctest` without fast-math.

## Files

- `CMakePresets.json`, `CMakeLists.txt`, `.clang-format`, `.clang-tidy`
- `core/CMakeLists.txt`, `core/include/alpha/*.h`, `core/src/alpha.c`
- required empty `core/src/`, `core/platform/`, and `edge/` placeholders
- `core/tests/test_scaffold.c`
- `third_party/yyjson/*`, `third_party/unity/*`, `third_party/README.md`
- `progress.md`

## Contract

Establishes the Phase 0 build layout and implements only the enums/constants in
`docs/MODULE_SPECS.md §0`. Constants mirror pinned `src/constants.py`. yyjson and
Unity are explicitly permitted by `.agent/tech_stack.md`.

## Tests

- Static `libalpha_core` builds with C11 warnings-as-errors.
- Test proves ABI version, representative constants/enums, `FE_TONEAREST`, and a
  yyjson parse/serialize round trip.
- Configure/build/ctest pass under both `dev` and `bench` presets.
- `compile_commands.json` proves `-ffp-contract=off` and no `-ffast-math` for bench.

## Risks / open questions

- CMocka is not installed on the benchmark host, so the allowed Unity alternative
  is vendored and pinned.
- This module does not claim Python/C parity; business logic begins in Phase 1.
