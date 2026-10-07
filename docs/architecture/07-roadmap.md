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

### M0 - Architecture baseline (now)

Deliverables: these documents accepted; the specification work in §3 completed; requirement files created under `docs/requirements/`; API conventions and coding standard written; hardware description schema v0 drafted; reference model skeleton written against the wait and scheduler requirements.

Exit: every LOCKED and PROPOSED item in 02 and 03 has requirement identifiers, and the reference model executes the scheduler transition reference (v0.1 Appendix A) as tests.

### M1 - First execution on two ports (native + AVR)

Deliverables: `arch/native` and `arch/avr`; kernel init; idle; thread create and start; fixed-priority scheduler with FIFO; context switch; interrupt entry and exit with reschedule-on-exit; tick and tickless timer hook; scheduler lock; conformance suite skeleton running on both; CI with a build matrix of EmbCC, GCC, and Clang on both ports (EmbCC already targets the ATmega328P and the x86-64 host, see 09); footprint tracking.

Exit: the v0.1 §41.3 sequence runs for millions of switches on ATmega328P and on the native port; both pass the same tests; measured switch latency on AVR published with metadata.

### M2 - Blocking kernel

Deliverables: wait protocol; timeouts and wraparound; sleep and `sleep_until`; mutex with inheritance; semaphores; event flags; condition variables; notifications; work queues; software timers; message queues; object storage generation; capability layer in pointer and tagged forms; differential tests against the reference model; property-based misuse tests; checked-build fault paths.

Exit: all kernel requirement groups covered in the traceability matrix; differential test campaign clean; priority inheritance nested and contention tests pass on both ports.

### M3 - Cortex-M, isolation, and the hardware pipeline

Deliverables: `arch/cortex_m` for Armv6-M, Armv7-M (lazy FPU stacking), Armv8-M (MPU, stack limits, TrustZone non-secure); partitions with supervisor on MPU targets; syscall boundary and validated capabilities; budgets; hardware description schema v1, SVD importer, generator, first SoC family STM32F4 with the STM32F407 Discovery and NUCLEO-F446RE boards, then the RP2350 (Raspberry Pi Pico 2) as the Armv8-M isolation board (ADR-025); device model; drivers GPIO, pinctrl, clock, timer, UART, DMA; deferred-format logging with decoder; CTF trace with generated metadata; debug descriptor; crash record; Renode in CI; first HIL board.

Exit: conformance suite unchanged and green on native, AVR, three Cortex-M variants; a partition fault is contained and restarted on the RP2350; a second board on the same SoC is added with no C changes.

### M4 - Platform maturity

Deliverables: power management core and device power states; SPI, I2C, watchdog, flash, RTC, entropy drivers; MCUboot-compatible images and update service; software watchdog; flight recorder; `emb` CLI; EmbDebug descriptor consumer; benchmark publication pipeline; SBOM and reproducible build verification; DTS importer; second SoC family, an industrial Armv8-M part in the STM32U5 or STM32H5 class with CAN-FD and Ethernet.

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
4. Wait and wake protocol, including the wake race, modeled and explored exhaustively (03 §3).
5. Mutex, semaphore, event, condition variable semantics and the inheritance algorithm (03 §5).
6. Notifications and work queues (03 §6.1, §6.2).
7. Thread lifecycle: start, suspend, join, detach, cancel, destroy (03 §1).
8. Object, capability, and storage generation (03 §7).
9. Partition model and syscall boundary, scoped to what M3 implements (03 §8).
10. Architecture-port contract in enough detail to implement native and AVR (02, 04 §9).
11. ATmega328P startup, interrupt, timer, and context-frame specification.
12. Native port design: host gate, virtual time, interrupt injection.
13. Hardware description schema v0 and generator outputs (04 §1).
14. Observability formats: log record, trace event set, crash record, debug descriptor (04 §7).

Only then does M1 implementation begin.

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
