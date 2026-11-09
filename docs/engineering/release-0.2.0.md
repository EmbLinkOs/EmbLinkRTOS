# EmbLinkRTOS 0.2.0 (2026-11-09)

The first release of EmbLinkRTOS: a preview. The kernel is implemented, tested on four physical boards and emulated on nine more, and its claims come with the evidence behind them. It is not the first *supported* release: that is 1.0, whose evidence bar ([roadmap §1](../architecture/07-roadmap.md)) is not complete yet (see [Known limits](#known-limits)). Until then, the API and the formats may change, and every change will be in the changelog.

**Source:** tag `v0.2.0`, signed. **Changes:** [CHANGELOG.md](https://github.com/EmbLinkOs/EmbLinkRTOS/blob/main/CHANGELOG.md), section 0.2.0. **Licence:** Apache-2.0.

## What is in it

| | |
|---|---|
| **Threads** | Fixed-priority preemptive scheduling (uniprocessor), optional round-robin time slicing, sleep and drift-free periodic wake-ups, suspend and resume, join |
| **Synchronization** | Semaphores; mutexes with priority inheritance, priority ceiling, recursion and owner-death policies; event flags; condition variables |
| **Communication** | Copy-by-value message queues, direct-to-thread notifications and bindings |
| **Time** | Software timers, deadlines, a timeout on every blocking call; periodic and tickless clock modes on the native port |
| **Deferred work** | Work queues, delayed work, a system work queue |
| **Diagnostics** | Checked builds that fault on API misuse, stack guards, an interrupt stack with a bound and a check, a crash record that survives the reset (format version 1), a debug descriptor for GDB (version 1), a build identity in every image |
| **Profiles** | *base*, and *tiny* for 8-bit parts |
| **Platform** | Board and SoC descriptions in YAML with a generator, importers for SVD, Zephyr DeviceTree and STM32 pin data, applications outside the tree through `find_package(EmbLinkRTOS)` |

The [project status](../STATUS.md) lists exactly what is implemented and what is only specified.

## Boards

A board's tier comes from archived runs ([support matrix](support-matrix.md)):

| Tier | Boards |
|---|---|
| **Tested on hardware** | Arduino Uno (ATmega328P); NUCLEO-F446RE (STM32F446, Cortex-M4F); Raspberry Pi Pico 2 W (RP2350, Cortex-M33); ESP32 DevKitC (Xtensa LX6) |
| **Tested in CI on an emulator** | BBC micro:bit (Cortex-M0); Arm MPS2 AN385, AN386, AN500, AN505 (Cortex-M3, M4F, M7, M33); RISC-V `virt` (RV32IMAC); ESP32-C3 DevKitM-1 (EmbSIM); NUCLEO-F446ZE (EmbSIM); native (Linux, macOS) |

## Toolchains

Built in CI: GCC (Arm GNU, avr-gcc, RISC-V, Espressif's) and Clang with LLD, on every port that has them; EmbCC for the native kernel (experimental). Espressif's Clang for the ESP32. Link-time optimization is an option (`EMB_LTO`, off by default) for GCC and Clang.

## The evidence

- **The reference controller** (`examples/controller`) on the NUCLEO-F446RE, checked and release, built with GCC, GCC with LTO, and Clang: 9 of 9 phases on each, with overload, an injected fault decoded from its crash record, and a watchdog reset; the checked build also ran a 10-hour soak with nothing missed. Interrupt to control thread at work, measured with a logic analyzer: a median of 7.33 µs for GCC release, 5.92 µs with LTO ([checked](controller-nucleo-2026-11-07.md), [release](controller-release-2026-11-08.md), [soak](controller-soak-2026-11-08.md)).
- **Memory:** every byte of the controller's flash and RAM accounted for, with a static stack bound per thread that CI checks ([memory](controller-memory-2026-11-07.md)).
- **The conformance suites** on every board above, checked and release, with misuse checks run on the boards by reset and replay ([hardware runs](hil.md)).
- **An executable reference model** the kernel is checked against in CI ([differential evidence](differential-evidence.md)).
- **Comparison with FreeRTOS** on four boards, as measured, including where EmbLinkRTOS is slower ([README](https://github.com/EmbLinkOs/EmbLinkRTOS#performance-against-freertos)).
- **Gates on every change:** clang-tidy, cppcheck and its MISRA addon against a baseline, the requirement evidence baseline, kernel coverage floors (92.5% of lines, 70.7% of branches), byte-identical double builds, SPDX SBOMs ([static analysis](static-analysis.md), [case evidence](case-evidence.md), [reproducible builds](reproducible-builds.md), [SBOM](sbom.md)).

## Known limits

- **A preview, not 1.0.** 145 of the 395 requirements have executed cases; the rest are analyses still to write, cases still to add, or features not built. Kernel coverage is not complete. The MISRA findings are a baseline that shrinks, not zero.
- **Not built:** barriers, pipes, buffer pools, isolation and partitions, indexed handles, budgets, multicore; drivers beyond the boards' basics; power management and updates.
- **Tickless operation** runs on the native port only; the embedded ports use the periodic tick.
- **Scope accepted for this version** (M3 review): the Cortex-M33 runs in the Secure state with TrustZone unused; the Cortex-M0 has no zero-latency interrupts; the Cortex-M7's caches stay off.
- **Hardware evidence comes from one bench:** one board of each kind, run by the maintainer.
- **No safety claim** ([functional safety readiness](safety-readiness.md)).

## Support

Security fixes for 0.2.x until the next minor release, by one maintainer, best effort, with no long-term support. Report vulnerabilities privately, as [SECURITY.md](https://github.com/EmbLinkOs/EmbLinkRTOS/blob/main/SECURITY.md) describes.

## Getting started

The [README's quick start](https://github.com/EmbLinkOs/EmbLinkRTOS#quick-start) builds the kernel and runs its tests in five commands; the [user guide](../user/index.md) covers boards and your own application.

## The release's files

Attached to the GitHub release, with `SHA256SUMS` and its signature:
- the source archive of the tag;
- the reference controller's images for the NUCLEO-F446RE (GCC, release) and their SPDX SBOMs.

Verify the tag with `git verify-tag v0.2.0`, and the checksums with `ssh-keygen -Y verify` against the maintainer's key in `allowed_signers` ([releases](releases.md)).
