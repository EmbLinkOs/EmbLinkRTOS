# Configuration and testing

The configuration pipeline resolves a port manifest, board defaults, a profile, optional fragments and explicit overrides. CMake produces generated configuration and storage headers; do not edit those outputs. Invalid combinations should fail configuration rather than silently change semantics.

| Native preset | Purpose |
|---|---|
| `native-gcc` | GCC, checked base configuration |
| `native-release` | GCC, release checks/behavior |
| `native-tiny` | Tiny thread-table configuration and narrower time/notification choices |
| `native-clang` | Clang compiler coverage; requires Clang |
| `native-asan` / `native-tsan` | Sanitizer configurations; require supported compiler runtimes |

Use the same preset name for configure, build and CTest. A tiny result does not verify base-only features; a release result does not exercise checked-build faults. Embedded presets additionally require their cross-compiler, libc and emulator; do not interpret a build with no registered tests as conformance.

Onboarding validated the first three GCC presets in this cloud machine. Clang/sanitizer and embedded toolchains are not installed here. CI definitions describe a broader matrix, whose successful outcomes must be recorded separately.

For configuration overrides and generated files, read `tools/kconfig/README.md` in the repository. For implementation and platform limits, read [Project status](../STATUS.md). For declaring a timing or footprint result, use [Timing and evidence](../engineering/timing-and-evidence.md).
