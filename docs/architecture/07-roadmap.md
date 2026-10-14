# 07 - Roadmap

**Status:** Delivery baseline revised 2026-10-14 (ADR-039). The comprehensive platform vision is retained, with independently validated delivery gates. Dates below describe recorded history, not delivery forecasts. [Project status](../STATUS.md) is the current implementation/evidence inventory.

## 1. First supported 1.0 boundary

The first supported release is a complete base-profile interrupt-driven controller on the NUCLEO-F446RE (STM32F446, Cortex-M4F), with native development. The STM32F407 Discovery is a follow-on STM32F4 family target, not the same SoC as the F446. A same-SoC data-only board demonstration must use a second F446 design chosen separately; physical availability must be established before HIL execution. The choice follows ADR-025 and does not imply either board is already running the project.

Required scope:

- Kernel: UP fixed priority, thread lifecycle, common waits, typed time, tested hardware clock modes, mutexes with bounded inheritance/ceiling, semaphores, notifications, event flags, condition variables, work queues/software timers and copy-by-value message queues. Each included API is implemented and tested before freeze. Barrier, pipe and pool APIs may remain experimental until their own evidence is complete.
- Ports: native/POSIX plus the selected Cortex-M4F board, with compiler/feature/version support declared explicitly. Existing AVR and RV32 emulated ports remain regression targets; physical validation and compiler qualification on them do not block the primary release and are not implicitly claimed.
- Application/platform: the controller in [Adoption](../adoption.md); minimal GPIO, pin/clock, timer and UART support; a small reproducible board/layout generation path; retained fault record, matching host decoder, useful trace and debugger metadata for implemented services. Diagnostics must identify an injected field failure from its artifacts.
- Evidence: all applicable requirements mapped to executed individual cases or analyses; checked/release conformance, model agreement with unresolved failures triaged, statement/branch coverage reported with gaps, selected static-analysis gates, physical timing and complete flash/RAM/stack accounting, adversarial workload/soak runs, pinned reproducible builds, SBOM, disclosure and explicit maintenance/support windows.

This release does not require isolation, INDEXED/TABLE handles, budgets/shares, Armv6-M/Armv8-M support, PMP, multicore, complete SVD/DTS importers, a broad driver catalog, power/update services, full diagnostic transports, middleware stacks, compiler qualification across all ports, LTS cadence, or certification packages. Those retain their designs and have subsequent gates below. No unsupported future API/schema or isolation ABI is frozen merely by releasing the base kernel.

Optional focused CMSIS-RTOS2/FreeRTOS compatibility previews may ship after underlying APIs exist and their implemented subsets pass migration checks. They do not alter native semantics or form part of a future qualification profile. Source/API stability and format versions apply only to the supported scope advertised by a release.

## 2. Delivery gates

### M0 - Architecture and specifications

The original SPEC-001 to SPEC-015 acceptance baseline was recorded on 2026-10-07. The reference model and research records exist. Acceptance is design status, not completion of every requirement. This documentation reconciliation folds M1 contract changes into the specifications and adds timing, isolation and evidence obligations. Remaining architecture groups need requirement migration when their owning subsystem is implemented.

### M1 - First execution (implemented subset)

Native and AVR execute the kernel; Cortex-M3/M4F and RV32IMAC ports now exist under QEMU (ADR-038). M1 includes threads, fixed priority, waits/timeouts, notifications, semaphores, mutexes, generated storage, build configuration and conformance. Native periodic/tickless operation is implemented; embedded tickless remains refused. AVR map-derived T1/T2 subtotals are recorded in the port page; no hardware latency measurement is complete. The full original M1 hardware exit is therefore outstanding, not silently waived.

Continue native and emulator regression testing. Preserve AVR as the tiny-profile constraint check. Concentrate new physical-board development on Cortex-M; keep each port's skips and supported features visible.

### M2 - Finish the blocking kernel and its evidence

The [ordered M2 checklist](../engineering/m2-checklist.md) sequences the known-failure investigations, contract/lifecycle decisions, primitives and evidence review into scoped implementation work. Checklist items are plans, not executed requirement evidence.

Open work: event flags, condition variables, work queues, software timers, message queues; defined binding teardown/lifetime; applicable object lifecycle/freeze behavior; misuse/property campaigns; generated traceability for the implemented core. Pipes, pools, barriers and INDEXED handles have separate feature gates if not needed by the reference application. TABLE handles belong to M3b.

Extend model scenarios and conformance with each primitive, including timeout/cancel/destroy races, ownership conservation and boundary resource counts. Diagnose/minimize the deep replay cases before declaring a campaign clean. Reconcile public annotations with actual complexity and implement the complete resource-bound inventory.

Exit: each included kernel API has explicit semantics, implementation, applicable individual evidence and documented limits; no unexplained required test failure; traceability/coverage reports identify uncovered and skipped cases instead of counting identifier presence as coverage.

### M3a - One physical Cortex-M controller and minimal hardware pipeline

Bring up the NUCLEO-F446RE, kernel-aware IRQ/timer behavior, minimal drivers and first HIL runner. Deliver the controller, retained crash/decoder demonstration, useful trace/debug metadata and adversarial timing/memory campaigns. Compare equivalent operations with pinned competitors through a fair same-board harness when their adapters exist; competitive rankings are optional evidence, not a substitute for the application's latency needs.

Start the hardware generator with one SoC and two boards, clear validation errors, generated device/layout data and deterministic output. Prove that the second supported-SoC board needs no peripheral C changes. SVD/DTS importing and broad clock/power automation are later additions. Publish migration examples and a focused compatibility preview after its underlying APIs work.

Exit: the application runs and handles overload/failure on physical hardware; full memory/stack and measured latency evidence is archived; the diagnostic record identifies an injected failure; the first 1.0 scope has its required release evidence. The same-SoC second-board demonstration is an independent generator acceptance gate, rather than a prerequisite for the primary-board kernel release. A 1.0 release may occur at this gate after API/support review, without waiting for the remaining platform vision.

### M3b - First isolated configuration (independent later release gate)

On one supported MPU board, implement a small static partition set, protected kstores, TABLE capabilities, authenticated blocking syscalls, safe-copy/buffer-lifetime validation, the supervisor and restart. Start with trusted DMA mediation. Implement incarnation persistence/exhaustion and freeze/regrant recovery. Expand from native validation to actual hardware privilege and DMA negative tests; record cross-partition interference and recovery evidence per SPEC-010 §17.

Budgets enter only after a separate model/analysis of lock owners, ISR/syscall accounting, depletion, replenishment overflow and restart progress. Reject unsupported combinations. Armv8-M stack limits and the RP2350 are follow-on targets, not prerequisites for demonstrating the first MPU boundary.

Exit: the defined attack/fault campaign passes, including active DMA and remote waits; a restart preserves stale rejection and unrelated workload requirements without reboot; no universal security or temporal guarantee is inferred from one test. Stabilize only the ABI/profile actually supported.

### M4 - Platform services and wider adoption

Extend drivers as demanded by real applications: SPI/I2C, DMA mediation, watchdog, flash, RTC and entropy. Add power management with measured residency/wake constraints, MCUboot-compatible updates with power-loss injection, richer observability and transports, full importers/CLI, expanded adapters and a second SoC family. Each service has an independent conformance, HIL and support gate.

Exit: an application validates each included service on hardware; update recovery and power behavior are measured; support matrix comes from archived outcomes; maintenance commitments match available resources.

### M5 - Additional protected ports and qualification scope

RISC-V PMP/Hazard3, additional Cortex-M variants, and EmbCC execution/qualification follow their own compiler/target evidence. Preserve existing QEMU coverage while adding HIL. Select and resource any safety/qualification scope explicitly; certification, tool qualification and LTS are not consequences of ordinary conformance alone.

Exit: each promoted port/profile/version has its declared evidence and no inherited support claim from another port. Release/maintenance/qualification artifacts identify their exact scope.

### M6+ - Advanced platform

SMP, AMP/Ports/leases, budget donation, DEADLINE/TIME_TABLE classes, middleware/network/USB/filesystems, secure-side integration/attestation, wider boards and EmbStudio integration retain their design direction. Validate hardware architecture-selection constraints before committing to heterogeneous mixed-core configurations. Each needs its own semantic model, interoperability/fault campaign and performance evidence.

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

The original specification work items were accepted on 2026-10-07. M1 implementation exists; the remaining gates above implement and validate the uncompleted portions. New proposed constraints are recorded with their own status and do not become verified through this documentation revision.

## 4. Risks and mitigations

| Risk | Mitigation |
|---|---|
| AVR-first biases layouts toward 8-bit | Native port in M1; Cortex-M/RV32 emulator ports now exist; primary-board HIL precedes broad platform work; all widths are configuration |
| Scope growth before the scheduler runs | Focused 1.0 boundary and independent gates in §1/§2; expand supported scope only with corresponding evidence and maintenance capacity |
| Hardware description schema churn | Versioned schema with migration tool from v1; importers reduce hand authoring |
| Isolation complexity delays M3 | Partitions scoped to static tables and MPU only; dynamic creation and MMU are FUTURE |
| Reference model drifts from kernel | Differential tests in CI; semantic changes must touch both in one change |
| Single-developer bandwidth | Specification order front-loads the parts that are expensive to change; tooling and tests are where external help is used, per v0.1 §1.2 |
| EmbCC RTOS integration/qualification: missing layout marker and AVR flash-macro wiring, target runtime/link setup, remaining TLS/CMSE/C++/debug limits, no current compiler CI leg | The ten legacy compiler gaps are implemented within the scopes in document 09 and the capability review. Preserve upstream-reported experiments separately from current RTOS evidence; pin sources and run applicable ABI/conformance checks. GCC/Clang remain the active matrix |
