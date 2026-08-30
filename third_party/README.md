# Vendored test/build dependencies

- `yyjson/`: yyjson 0.10.0, upstream commit
  `9ddba001a4ea88e93b46932e5c5b87b222e19a5f`, MIT license.
- `unity/`: Unity 2.6.1, upstream commit
  `cbcd08fa7de711053a3deec6339ee89cad5d2697`, MIT license.

Only the upstream source/header/license files needed by the CMake build are
vendored. Checksums are reviewable with `shasum -a 256 third_party/*/*`.
