# 07 - Roadmap

**Status:** PROPOSED. Replaces v0.1 §40 and §41 with a 1.0 boundary and a revised milestone order. The principle is unchanged: vertical slices, no subsystem before the scheduler runs.

---

## 1. What 1.0 is

**1.0 contains**

- Kernel: threads, fixed-priority class, unified wait protocol, 64-bit and 32-bit time, tickless, software timers on work queues, mutex with priority inheritance and optional ceiling, semaphores, event flags, condition variables, notifications, work queues, message queues, pipes, buffer pools, capabilities (pointer, tagged, and validated representations), partitions on MPU and PMP hardware with supervisor, budgets with all four overrun policies, crash records, software watchdog.
- Ports: `native`, `avr`, `cortex_m` (Armv6-M, Armv7-M with FPU, Armv8-M with MPU and TrustZone non-secure), `riscv` (RV32 with PMP).
- Platform: hardware description schema v1 with SVD and DTS importers and generator; device model; driver classes GPIO, pinctrl, interrupt controller, clock and reset, timer, UART, SPI, I2C, DMA, watchdog, flash, RTC, entropy; power management core with system and device states; MCUboot-compatible images and A/B update service; deferred-format logging; CTF tracing; debug descriptor; flight recorder.
- Engineering: Kconfig and CMake; EmbCC, GCC, Clang qualified on stated versions; conformance suite; reference model; traceability matrix; HIL on at least one board per architecture; Renode CI; benchmark publication; SBOM; reproducible builds; SECURITY.md; support matrix; LTS policy.
- Profiles: tiny, base, isolated, native fully supported; multicore experimental.

**1.0 explicitly excludes**

SMP scheduling (experimental only), DEADLINE and TIME_TABLE classes, dynamic partitions, networking and USB stacks (contracts defined, integrations experimental), filesystems beyond flash and block class APIs, CMSIS-RTOS2 and POSIX layers, safety evidence packages, hardware ports beyond the four named.

## 2. Milestones

### M0 - Architecture baseline (complete 2026-10-07)

Deliverables: these documents accepted; the specification work in §3 completed (SPEC-001 to SPEC-015); requirement files created under `docs/requirements/` for every group the specifications cover; API conventions written (SPEC-001); hardware description schema v0 specified (SPEC-014); the reference model written and explored for the wait protocol, mutexes with inheritance, notifications, and thread lifecycle (`tools/model/`, 48 scenarios, 44 tests); the research records R-001 to R-003 and ADR-026 to ADR-037 accepted.

Exit: met for the kernel groups (KRN-*), PORT, ARCH-AVR, SIM, HW, OBS. The coding standard (05 §1.6) is written as the first M1 deliverable: `docs/CODING-STANDARD.md` with `.clang-format`, `.clang-tidy`, and `.editorconfig`. Still pending from the original exit criterion: the remaining architecture groups without their own specification (KRN-MEM, KRN-TP beyond ADR-029, KRN-SMP/MC, FLT, DRV, PWR, BOOT, SEC, BLD, TEST, REL), whose identifiers stay in documents 03 to 05 until their milestones (M3 to M5) specify them.

Status of the §3 work order: 1 to 14 and 6b accepted; see each item's line below.

### M1 - First execution on two ports (native + AVR)

**Status 2026-10-07: delivered, except the hardware measurement.** `arch/native` and `arch/avr` with the `arduino_uno` board; kernel init, idle, threads, the bitmap and table schedulers, the wait protocol, time with periodic and tickless modes, notifications, semaphores, mutexes with inheritance and ceilings (the M2 blocking kernel brought forward, since the reference model already existed); nine conformance suites (70 tests) passing on the native port under GCC, Clang, both profiles, both build kinds, and three sanitizers, and on the ATmega328P under QEMU (SIM-002) with the fork-based fault checks skipped there; the v0.1 §41.3 sequence running for two million switches on native and continuously under QEMU; CI matrix with GCC, Clang and avr-gcc (EmbCC when available); footprint tracking with a per-commit gate. Not met: the measured switch latency on AVR hardware (no board in the loop yet; `tests/benchmarks/bench_switch` is ready and emulator numbers are never published, SIM-003), The R-003 footprint targets T1 and T2 were met by the footprint pass that followed (`docs/ports/avr.md`: release kernel text 3805 bytes against 4096, static RAM 63 against 64, control block 32 against 32, gated per commit on the `avr-uno-t1` preset). Deviations found during implementation: `docs/specs/AMENDMENTS-M1.md`.

Deliverables: `arch/native` and `arch/avr`; kernel init; idle; thread create and start; fixed-priority scheduler with FIFO; context switch; interrupt entry and exit with reschedule-on-exit; tick and tickless timer hook; scheduler lock; conformance suite skeleton running on both; CI with a build matrix of EmbCC, GCC, and Clang on both ports (EmbCC already targets the ATmega328P and the x86-64 host, see 09); footprint tracking.

Exit: the v0.1 §41.3 sequence runs for millions of switches on ATmega328P and on the native port; both pass the same tests; measured switch latency on AVR published with metadata.

### M2 - Blocking kernel

**Status 2026-10-07: in progress.** Delivered with M1: the wait protocol, timeouts and wraparound, sleep and `sleep_until`, mutexes with inheritance and ceilings, semaphores, notifications, the object storage generation, the pointer capability form, the checked-build fault paths. Delivered since: the differential bridge (`tools/model/bridge.py` with `tests/differential/diff_runner`), which replays the kernel's trace through the reference model for every scenario of the catalogue under seeded schedules of interrupts and ticks, in CI on every native preset (TEST-008); its first campaign found and fixed three kernel defects (`tools/model/README.md`). Open: event flags, condition variables, work queues, software timers, message queues, the tagged capability form, property-based misuse tests.

Deliverables: wait protocol; timeouts and wraparound; sleep and `sleep_until`; mutex with inheritance; semaphores; event flags; condition variables; notifications; work queues; software timers; message queues; object storage generation; capability layer in pointer and tagged forms; differential tests against the reference model; property-based misuse tests; checked-build fault paths.

Exit: all kernel requirement groups covered in the traceability matrix; differential test campaign clean; priority inheritance nested and contention tests pass on both ports.

### M3 - Cortex-M, isolation, and the hardware pipeline

Deliverables: `arch/cortex_m` for Armv6-M, Armv7-M (lazy FPU stacking), Armv8-M (MPU, stack limits, TrustZone non-secure); partitions with supervisor on MPU targets; syscall boundary and validated capabilities; budgets; hardware description schema v1, SVD importer, generator, first SoC family STM32F4 with the STM32F407 Discovery and NUCLEO-F446RE boards, then the RP2350 (Raspberry Pi Pico 2) as the Armv8-M isolation board (ADR-025); device model; drivers GPIO, pinctrl, clock, timer, UART, DMA; deferred-format logging with decoder; CTF trace with generated metadata; debug descriptor; crash record; Renode in CI; first HIL board; first run of the cross-RTOS benchmark harness against FreeRTOS, Zephyr, and ThreadX on the STM32F4 boards (R-003 §6, TEST-012).

Exit: conformance suite unchanged and green on native, AVR, three Cortex-M variants; a partition fault is contained and restarted on the RP2350; a second board on the same SoC is added with no C changes; harness results for R-003 targets T1 to T9 published with raw data.

### M4 - Platform maturity

Deliverables: power management core and device power states; SPI, I2C, watchdog, flash, RTC, entropy drivers; MCUboot-compatible images and update service; software watchdog; flight recorder; `emb` CLI; CMSIS-RTOS2 and FreeRTOS-API adapters (ADR-037); EmbDebug descriptor consumer; benchmark publication pipeline; SBOM and reproducible build verification; DTS importer; second SoC family, an industrial Armv8-M part in the STM32U5 or STM32H5 class with CAN-FD and Ethernet.

Exit: a reference application updates itself A/B with power-loss injection on HIL; measured idle power states on a reference board; support matrix generated from CI.

### M5 - RISC-V, qualification, and 1.0

Deliverables: `arch/riscv` with PMP partitions, first on the RP2350's Hazard3 cores and on an emulated RV32 target in Renode; EmbCC qualified for all ports; compiler version matrix; API freeze review; documentation complete per 05 §9; SECURITY.md and disclosure process; LTS policy; release engineering; coverage targets met on kernel.

Exit: 1.0 criteria in §1 met; 1.0 released.

### M6+ - Advanced platform

SMP on the RP2350 (global structure first, ADR-012); AMP ports, with the RP2350 in mixed Arm and RISC-V configuration and an STM32H7 dual-core part as candidates; DEADLINE and TIME_TABLE classes; networking and USB integrations; filesystems; CMSIS-RTOS2 and POSIX layers; PSA secure partition integration; attestation; safety evidence packages; broad board catalog; EmbStudio integration; first LTS line.

## 3. Specification work order (before M1 code)

Refines v0.1 §47 with the v0.2 additions. Each produces a requirement file and, where applicable, reference-model code.

1. API conventions, status codes, time types, context classes (05 §1). **Accepted 2026-10-07:** `docs/specs/SPEC-001-api-conventions.md` and `docs/requirements/API.md` (API-001 to API-032).
2. Interrupt, exception, and critical-section specification, including the kernel-independent interrupt class. **Accepted 2026-10-07:** `docs/specs/SPEC-002-interrupts-and-critical-sections.md` and `docs/requirements/KRN-IRQ.md`.
3. Time source, timeout, and software timer specification (03 §4). **Accepted 2026-10-07:** `docs/specs/SPEC-003-time-timeouts-and-timers.md` and `docs/requirements/KRN-TIM.md`.

   Between items 3 and 4 the research records `docs/research/R-001` (mechanism comparison of eleven kernels), `R-002` (market, performance, certification, regulation), and `R-003` (differentiation, ADR-026 to ADR-037, measurable targets T1 to T14, the benchmark harness) were produced. Items 4 to 9 implement the proposed decisions where they apply: item 4 ADR-026 and 027; item 5 ADR-028 and 030; item 6 ADR-027; item 7 ADR-032; item 8 ADR-032 and 033; item 9 ADR-029 and 033; item 14 ADR-034.

4. Wait and wake protocol, including the wake race, modeled and explored exhaustively (03 §3). **Accepted 2026-10-07:** `docs/specs/SPEC-004-wait-and-wake-protocol.md`, `docs/requirements/KRN-WAIT.md`, and the first module of the reference model in `tools/model/` (SPEC-004 §13), explored exhaustively over the scenario catalogue.
5. Mutex, semaphore, event, condition variable semantics and the inheritance algorithm (03 §5). **Accepted 2026-10-07:** `docs/specs/SPEC-005-synchronization.md`, `docs/requirements/KRN-SYNC.md`; the reference model has the mutex with inheritance, explored exhaustively.
6. Notifications and work queues (03 §6.1, §6.2). **Accepted 2026-10-07:** `docs/specs/SPEC-006-notifications-and-work-queues.md`, `docs/requirements/KRN-NOTIF.md`, `docs/requirements/KRN-WQ.md`; notifications and binding in the reference model.

   6b. Inter-thread communication: message queues, pipes and streams, buffer pools with ownership transfer, ports outline (03 §6.3 to §6.6). Added because M2 delivers message queues and the original list had no item for them. **Accepted 2026-10-07:** `docs/specs/SPEC-007-inter-thread-communication.md`, `docs/requirements/KRN-IPC.md`.
7. Thread lifecycle: start, suspend, join, detach, cancel, destroy (03 §1). **Accepted 2026-10-07:** `docs/specs/SPEC-008-thread-lifecycle.md`, `docs/requirements/KRN-THR.md`; start, join, and exit codes in the reference model.
8. Object, capability, and storage generation (03 §7). **Accepted 2026-10-07:** `docs/specs/SPEC-009-objects-capabilities-and-storage.md`, `docs/requirements/KRN-OBJ.md`, `docs/requirements/KRN-CAP.md`.
9. Partition model and syscall boundary, scoped to what M3 implements (03 §8). **Accepted 2026-10-07:** `docs/specs/SPEC-010-partitions-and-syscall-boundary.md`, `docs/requirements/KRN-PART.md`.
10. Architecture-port contract in enough detail to implement native and AVR (02, 04 §9). **Accepted 2026-10-07:** `docs/specs/SPEC-011-architecture-port-contract.md`, `docs/requirements/PORT.md`.
11. ATmega328P startup, interrupt, timer, and context-frame specification. **Accepted 2026-10-07:** `docs/specs/SPEC-012-atmega328p-port.md`, `docs/requirements/ARCH-AVR.md`.
12. Native port design: host gate, virtual time, interrupt injection. **Accepted 2026-10-07:** `docs/specs/SPEC-013-native-port.md`, `docs/requirements/SIM.md`.
13. Hardware description schema v0 and generator outputs (04 §1). **Accepted 2026-10-07:** `docs/specs/SPEC-014-hardware-description-schema.md`, `docs/requirements/HW.md`.
14. Observability formats: log record, trace event set, crash record, debug descriptor (04 §7). **Accepted 2026-10-07:** `docs/specs/SPEC-015-observability-formats.md`, `docs/requirements/OBS.md`.

All items are specified and accepted as of 2026-10-07; M1 implementation begins with the native and AVR ports and the kernel modules in dependency order, with the reference model (`tools/model/`) as the oracle and the specifications' decision sections as the contract.

## 4. Risks and mitigations

| Risk | Mitigation |
|---|---|
| AVR-first biases layouts toward 8-bit | Native port in M1; Cortex-M in M3; all widths are configuration |
| Scope growth before the scheduler runs | 1.0 boundary in §1; FUTURE items need an ADR to move earlier |
| Hardware description schema churn | Versioned schema with migration tool from v1; importers reduce hand authoring |
| Isolation complexity delays M3 | Partitions scoped to static tables and MPU only; dynamic creation and MMU are FUTURE |
| Reference model drifts from kernel | Differential tests in CI; semantic changes must touch both in one change |
| Single-developer bandwidth | Specification order front-loads the parts that are expensive to change; tooling and tests are where external help is used, per v0.1 §1.2 |
| EmbCC gaps on embedded targets: no C++, single-instance thread-local storage, no 64-bit atomics on 32-bit targets, no linker scripts on AVR, `const` data in RAM on AVR, Armv8-M stack-limit and TrustZone support unverified or absent | Document 09 lists each gap with the kernel rule that avoids it and the EmbCC issue that would close it; GCC and Clang stay in the matrix so no gap blocks a milestone |
