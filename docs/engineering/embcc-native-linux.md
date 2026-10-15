# EmbCC kernel on the Linux native port

**Evidence date:** 2026-10-15. **Implementation:** `df32ec2`. **Compiler source:** EmbLinkOs/EmbCC `a3c3ef4c81f9de977b8741ed16072cadca921788`, version `1.0.0-m2.complete`. This follows the [storage-marker correction](embcc-layout-marker.md). The new experimental presets execute an EmbCC-built kernel with a GCC-built Linux host; they do not qualify an all-EmbCC application or an embedded port.

## Why two compilers

EmbCC's `x86_64-linux-gnu` runtime uses its own static syscall C library. Its headers do not provide pthreads. Installing/building that runtime does not by itself supply the native port's POSIX threading environment.

A small program compiled with EmbCC against the system glibc headers, then linked with GCC, successfully created/joined a thread, synchronized a mutex/condition variable and called `clock_gettime`. The experiment required explicit system include paths and GNU compatibility version selection. The complete native build nevertheless failed in glibc's `sys/types.h`: EmbCC rejected `register_t`'s `__attribute__((__mode__(__word__)))`. The experiment demonstrates a narrow working subset, not general glibc header compatibility. Neither pthread declarations nor system headers were patched.

[ADR-041](../architecture/06-decision-records.md#adr-041-experimental-embcc-kernel-with-a-gcc-linux-host) therefore makes the compiler boundary explicit:

| Component | Compiler/runtime |
|---|---|
| `emb_kernel`, storage probe and kernel ABI probe | EmbCC, `--target=x86_64-elf`, freestanding headers |
| POSIX architecture port, native board, samples, tests, differential harness and host ABI probe | Host GCC with the real Linux headers |
| Archive and executable link | Host GNU tools/GCC, glibc and pthreads, `-no-pie` |

EmbCC objects follow the host's x86-64 ABI; the non-PIE link accommodates their relocations. This path does not use EmbCC's Linux CRT, libc or linker. Kernel/port algorithms and public API contracts are unchanged. RTOS build integration owns this boundary; complete EmbCC compilation of the POSIX host still needs upstream compatibility work.

## Reproduce the experimental build

Use Linux x86-64, host GCC, CMake/Ninja, Python/PyYAML and an installed EmbCC. The reviewed compiler was bootstrapped as described in the [capability review](embcc-capability-review.md); its bootstrap is not a warning-clean qualification. Select its executable explicitly:

```sh
cmake --preset native-embcc -DEMB_KERNEL_COMPILER=/path/to/EmbCC/embcc
cmake --build --preset native-embcc
ctest --preset native-embcc --output-on-failure --no-tests=error
```

The matching configure/build/test presets `native-embcc-release`, `native-embcc-tiny` and `native-embcc-periodic` exercise the other native configurations. Pass the compiler path on each first configure, or install it on `PATH`. A non-EmbCC executable is rejected; compilation errors never fall back to GCC. The older `embcc-native.cmake` remains the separate all-EmbCC experiment, with its POSIX limitations.

CMake identifies the **host** compiler as GNU. Its configure message separately prints the kernel compiler. `EMB_KERNEL_COMPILER`, `EMB_KERNEL_COMPILER_VERSION` and `EMB_KERNEL_COMPILER_SHA256` in `CMakeCache.txt` identify the kernel toolchain. Differential failure captures retain these values and fingerprints of the launcher/layout inventory. Ninja's actual commands include the explicit launcher; `compile_commands.json` alone describes CMake's host compiler command and does not establish the kernel compiler.

## ABI guard and execution evidence

Both compilers compile `tools/toolchain/native_abi.c` with the resolved configuration. Marker extraction compares sizes, alignments, member widths and offsets for the maintained inventory of control blocks, object/queue/timeout/list/CPU records, architecture context, public attributes/handles/time wrappers, fault information and trace events. The build compares `generated/abi/host.h` and `kernel.h` before building the kernel archive. Probe objects contain marker bytes and are never linked or executed. New boundary types/members must extend the inventory.

A deliberately packed host probe failed this gate; selecting GCC as the kernel compiler failed configuration. Launcher tests check exact argument/exit-status preservation and missing-compiler failure. The layout inventory is a guard, not a proof of every calling convention: the executable conformance/bridge checks additionally exercise handles, attributes, time wrappers, callbacks, context switching and synchronization across the compiler boundary.

| Preset | CTest targets passed / failed | Model executions, seeds 3 and 5 | Divergences |
|---|---|---|---|
| Checked base | 12 / 0 | 2,800 | 0 |
| Release base | 12 / 0 | 2,800 | 0 |
| Tiny | 12 / 0 | 2,800 | 0 |
| Periodic base | 12 / 0 | 2,800 | 0 |

Each unfiltered campaign used 40 schedules per applicable scenario: 1,400 executions, one unsupported scenario skipped. Totals are 48 passing CTest results and 11,200 agreeing campaign executions; target counts are separate from individual C cases. The model/tooling suite passed 68 tests, launcher checks passed two, and the storage contract passed one. Ordinary native GCC also passed all twelve CTest targets. These bounded results neither recover the historical seed-5 input nor close that investigation.

Repeat a campaign with the selected build's runner:

```sh
python3 -I tools/model/bridge.py \
  --runner build/native-embcc/tests/differential/diff_runner \
  --runs 40 --seed 3 --verbose
```

The [evidence archive](https://github.com/EmbLinkOs/EmbLinkRTOS/tree/main/tools/toolchain/evidence/embcc-native-2026-10-15) retains actual compile commands, resolved configurations, layouts, runner/source/toolchain identities, raw and readable logs, and initial POSIX/negative-check inputs. Its manifest records hashes and commands. Readable logs strip trailing whitespace; `raw-logs.json` retains original output. Compiler warnings for unsupported diagnostic options remain visible; no warning suppression was added.

## Remaining scope

CI runs the launcher/storage contracts and existing GCC/Clang/embedded matrix. The actual EmbCC executions above are local evidence, not a pinned EmbCC CI leg. Other hosts, compiler revisions, PIE/LTO, sanitizing the EmbCC kernel, embedded images and physical timing remain unqualified. AVR flash/registration integration, RTOS hardware-import adaptation, embedded C++ checks and complete compiler/port conformance remain separate tasks. A subsequent all-EmbCC host build must resolve the actual POSIX header/runtime/link boundaries rather than inheriting the mixed build's result.
