### Changed (the build identity includes the toolchain, 2026-11-08)

- The build identity (A72), which the crash decoder uses to refuse a record from another image, also digests the toolchain: the compiler's identity and version and whether LTO is on. Before, GCC, Clang and LTO images of one commit and configuration shared an id, so a crash record could be decoded against the wrong image. The generated `build_id.c` names the toolchain. Every image's id changes once; the double build still gives one id ([page](docs/engineering/crash-record.md)).
