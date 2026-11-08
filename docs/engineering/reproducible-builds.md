# Reproducible builds

BLD-004 requires reproducible builds given the pinned toolchain, verified in CI by a double build. REL-001 makes reproducibility a condition of every release. This page says what the build does for it, how CI checks it, and its limits.

## What makes an image reproducible

An image built from the same commit, with the same configuration and the same toolchain, has the same bytes wherever the source tree and the build directory are.

- **Code and data** already were. The build identity (A72) is computed from the commit, a dirty flag and the configuration hash, so it names no path and no time. Nothing in the build reads the clock.
- **Debug information** was not. `.debug_line` and `.debug_str` held the absolute directories of the tree and the build: 115 occurrences in the reference controller's ELF. Since 2026-11-08, `cmake/emb_flags.cmake` passes `-ffile-prefix-map` to GCC and Clang, which also covers `__FILE__`. The directories are named as follows:

  | Directory | Name in the image |
  |---|---|
  | the build directory, which is also the compilation directory | `.` |
  | the RTOS's tree, when the application is in it (the RTOS's own build, `examples/`) | `.`: files read `kernel/sched/sched.c`, `examples/controller/main.c` |
  | an application outside the RTOS's tree | `.`: files read `main.c` |
  | the RTOS, used by an application outside it | `emblinkrtos`: files read `emblinkrtos/kernel/sched/sched.c` |

  A debugger or `addr2line` started at the source root finds the files: the RTOS's root, or an outside application's. From elsewhere, use GDB's `set substitute-path`. Generated files read `generated/...`, relative to the build directory.

The toolchain's own paths stay in the debug information, for example the C library's headers under its install directory. They are part of the pinned toolchain: the same toolchain at the same place gives the same bytes. That is the scope BLD-004 sets.

## The double build

`tools/repro/double_build.py` builds the same commit in two places and compares the results:
- The first place is the tree it runs from.
- The second is a fresh checkout of the same commit at another absolute path, made with `git clone --shared`.

Each target is configured and built from scratch in both. Then every image is compared byte by byte: `.elf`, `.hex`, `.bin`, `.uf2` and native programs. For an ELF that differs, the tool names the sections that differ.

```sh
python3 tools/repro/double_build.py --preset cm4-an386 --app examples/controller:nucleo_f446re
```

The tree must be clean: the second checkout holds only the commit, and the build identity includes a dirty flag.

**Exit status:** 0 when every image is identical, 1 when one differs or a target produced none, 2 on a build or git error.

CI's `reproducibility` job runs it on every pull request, and the `CI merge gate` requires it. The job covers:
- the native programs, built by GCC and by Clang;
- Cortex-M (MPS2-AN386), built by GCC and by Clang;
- RV32;
- AVR;
- the Pico 2 W, with its UF2 images;
- the reference controller for the NUCLEO-F446RE.

## The first run (2026-11-08)

Before the prefix maps, the reference controller's `.hex` was already identical from two checkouts at different paths, and its build identity matched. Its ELF differed only in `.debug_line` and `.debug_str`.

With the prefix maps, 293 images from six embedded targets were identical on macOS, built by Arm GNU 14.2, Homebrew Clang, the RISC-V and AVR GCCs. The targets:
- 58 Cortex-M test images from GCC and 58 from Clang;
- 54 RV32 images;
- 40 AVR images;
- 81 Pico 2 W images;
- the controller's ELF and hex.

## Limits

- **Native programs on macOS are not reproducible.** Each Mach-O program differs in its `LC_UUID` from one checkout to the other, even with `ZERO_AR_DATE=1`; the cause is not established. CI checks native programs on Linux, which is where the double build is required. On macOS, the double build is meaningful for the embedded images only.
- **Two places on one machine, not two machines.** The double build changes the path, the build directory and the time of the build. It does not change the machine or the user. CI's runner is a fresh machine each time, but both builds of one run share it.
- **The toolchain is not built from source.** CI installs Ubuntu 24.04's packages, which the apt cache pins between runs but Ubuntu updates. A release records the exact versions (the release checklist's build manifest); an image is reproducible with those versions.
- **EmbCC, Espressif's toolchains, LTO builds and IAR or Keil** are not in the double build yet.
