# EmbLinkRTOS Documentation

| Directory | Contents | Read when |
|---|---|---|
| [`architecture/`](architecture/README.md) | The v0.2 architecture baseline: vision, system and kernel architecture, platform, engineering system, decision records, roadmap, open questions, toolchain profile | You want to understand or change *what* EmbLinkRTOS is and *why* |
| [`specs/`](specs/) | Detailed specifications, one per roadmap work item (`SPEC-NNN-*.md`): the design that implements a part of the architecture, with examples | You are about to implement or review a subsystem |
| [`requirements/`](requirements/README.md) | Normative requirements, one file per identifier group, in a fixed format that tooling extracts into the traceability matrix | You are writing a test, checking coverage, or deciding whether a behavior is promised |
| [`research/`](research/README.md) | Research records: mechanism comparison of eleven kernels, market and certification facts, differentiation decisions with measurable targets, per-kernel source notes | You want to know why a design choice was made relative to other systems, or what EmbLinkRTOS promises to beat and how that is measured |

## How the pieces relate

```
architecture (why, what)  ->  decision records (chosen among alternatives)
        |
        v
specifications (how, in detail, with examples)
        |
        v
requirements (shall statements, each with a verification method)
        |
        v
tests and analyses (evidence)  ->  traceability matrix (generated)
```

A change that alters behavior touches all four in one pull request (05 §5).

## Specifications

| Id | Title | Roadmap item | Status |
|---|---|---|---|
| [SPEC-001](specs/SPEC-001-api-conventions.md) | Public API conventions: namespaces, status codes, time types, context classes, handles, headers, annotations | 07 §3 item 1 | Accepted 2026-10-07 |
| [SPEC-002](specs/SPEC-002-interrupts-and-critical-sections.md) | Interrupt, exception, and critical-section model: contexts, interrupt classes, preemption points, reschedule on exit, critical sections, scheduler lock, nesting and stacks, IRQ management, faults, latency metrics, per-architecture mapping | 07 §3 item 2 | Accepted 2026-10-07 |
| [SPEC-003](specs/SPEC-003-time-timeouts-and-timers.md) | Time source, clock modes (periodic tick and tickless), timeout structure, sleep, software timers, time slicing hook, cycle counter, wall clock, per-architecture timer sources | 07 §3 item 3 | Accepted 2026-10-07 |
| [SPEC-004](specs/SPEC-004-wait-and-wake-protocol.md) | Wait and wake protocol: wait queues and policies, three-state wait flag, generations, the block and wake sequences, wake sources (signal with hand-off, timeout, cancellation, destroy, stale peers), suspend overlay, lock domains and SMP order, tiny profile bitmap variant, reference model plan | 07 §3 item 4 | Accepted 2026-10-07; reference model in `tools/model/` |
| [SPEC-005](specs/SPEC-005-synchronization.md) | Synchronization: effective priority and the inheritance walk, mutex (inherit, ceiling, none, recursive, deadlock detection, owner death), semaphores, event flags, condition variables, barriers, spinlocks, atomics, priority changes, tiny profile, model extension | 07 §3 item 5 | Accepted 2026-10-07; mutex in the reference model |
| [SPEC-006](specs/SPEC-006-notifications-and-work-queues.md) | Notifications (per-thread bits, kernel bits), notification binding of objects (ADR-027), work queues and delayed work, system work queue, misuse, observability, tiny profile, model extension | 07 §3 item 6 | Accepted 2026-10-07; notifications and binding in the reference model |
| [SPEC-007](specs/SPEC-007-inter-thread-communication.md) | Inter-thread communication: message queues (copy by value, direct copy and slot hand-off, send to front, ownership queues), pipes (chunked and SPSC lock-free paths, min_len reads, close and reset), buffer pools (O(1), checked-build ownership tracking), ports outline | 07 §3 item 6b | Accepted 2026-10-07 |
| [SPEC-008](specs/SPEC-008-thread-lifecycle.md) | Thread lifecycle: attributes, init and start, exit path and reusability point, join and detach, suspend, cancellation with disable count, priorities, TLS slots, stacks, kernel threads, destroy, misuse, observability, tiny profile, model extension | 07 §3 item 7 | Accepted 2026-10-07; start, join, exit codes in the reference model |
| [SPEC-009](specs/SPEC-009-objects-capabilities-and-storage.md) | Kernel objects, capabilities, and storage: object header, three handle models (POINTER, INDEXED, TABLE), capability entries and validation, rights per type, grant, derive, delete, revoke, renew, generated storage types from a target layout probe, init table, lifecycle and freeze, names and registry | 07 §3 item 8 | Accepted 2026-10-07 |
| [SPEC-010](specs/SPEC-010-partitions-and-syscall-boundary.md) | Partitions and the syscall boundary: static partition description, memory layout with the kstore, protection hardware mapping and generator checks, cross-partition switch, trap stubs and generated marshallers with argument validation, fault capture and supervisor delivery, restart procedure, device capabilities with interrupt delivery, shared regions, accounting, M3 scope | 07 §3 item 9 | Accepted 2026-10-07 |
| [SPEC-011](specs/SPEC-011-architecture-port-contract.md) | Architecture port contract: port manifest, TCB contribution, startup and kernel start, context init and switch, interrupts and critical sections, timer and cycle counter, atomics, idle and power, stacks, faults, protection and trap hooks, SMP names, debug hooks, toolchain rules, port deliverables and conformance, per-architecture summary | 07 §3 item 10 | Accepted 2026-10-07 |
| [SPEC-012](specs/SPEC-012-atmega328p-port.md) | ATmega328P port and the `arduino_uno` board: manifest, startup, the single 37-byte frame, switch and interrupt epilogue, Timer1 modes, memory budget and `const`-in-RAM handling, robustness without fault hardware, debug and emulation path, board description, footprint and latency targets | 07 §3 item 11 | Accepted 2026-10-07 |
| [SPEC-013](specs/SPEC-013-native-port.md) | Native port: host abstraction, the gate, deterministic and asynchronous interrupt delivery, virtual time, fault injection and software region checks, differential bridge to the reference model, replay, multi-CPU reservation, CI legs | 07 §3 item 12 | Accepted 2026-10-07 |
| [SPEC-014](specs/SPEC-014-hardware-description-schema.md) | Hardware description schema v0 and generator: SoC, board, and system descriptions, conventions, generated outputs, semantic validation, SVD and DeviceTree importers, determinism, versioning | 07 §3 item 13 | Accepted 2026-10-07 |
| [SPEC-015](specs/SPEC-015-observability-formats.md) | Observability formats: deferred log records, CTF trace catalogue and rings, crash record v1 and v2, debug descriptor, monitors and statistics API, framing and memory channel, host tools, profile defaults | 07 §3 item 14 | Accepted 2026-10-07 |

All fourteen specification work items of the roadmap (plus 6b) are written and accepted as of 2026-10-07. M1 implementation (07 §2) follows, in dependency order: the coding standard (`CODING-STANDARD.md` in this directory, with the formatter and analyzer configuration at the repository root), the configuration and build system, the public headers, the kernel modules (SPEC-002 to 008), the native and AVR ports (SPEC-011 to 013), and the conformance suite, with the reference model as oracle.

## Ports

| Page | Content |
|---|---|
| `ports/native.md` | The native port as built in M1: gate, deterministic interrupts, virtual time, verification legs |
| `ports/cortex_m.md` | The Cortex-M port: PendSV switching, BASEPRI critical sections, lazy FPU context, SysTick, faults, the MPS2 boards under QEMU |
| `ports/avr.md` | The ATmega328P port as built in M1: frame layout, switch and epilogue, timer, linking, measured footprint, skipped tests |
| `specs/AMENDMENTS-M1.md` | Where the M1 code departs from or sharpens an accepted specification, to be folded into the specifications |

## Tools

| Tool | Purpose |
|---|---|
| `tools/kconfig/embconfig.py` | Configuration: Kconfig subset plus port manifests, emits `emb/config.h`, `config.cmake`, `config.json` (05 §2.1) |
| `tools/storage/gen_storage.py` | Generates `emb/storage.h` from the layout probe (ADR-006) |
| `tools/footprint/footprint.py` | Footprint report and regression gate from a link map (TEST-011) |
| `tools/qemu/run_qemu.py` | Runs a test image under the board's QEMU machine (AVR, Cortex-M, RISC-V) and reads its verdict from the console (SIM-002) |
| `tools/model/` | The executable reference model (ADR-013) and `bridge.py`, the differential bridge that replays the kernel's trace through it (TEST-008; the runner is `tests/differential/`) |
| `scripts/check-banned.py` | The tree-wide check for constructs the coding standard bans |

## Research records

| Id | Title | Status |
|---|---|---|
| [R-001](research/R-001-rtos-mechanism-comparison.md) | RTOS mechanism comparison: scheduler, interrupts, time, waiting, inheritance, IPC, isolation, observability, quality across FreeRTOS, ThreadX, Zephyr, RTEMS, NuttX, ChibiOS, uC/OS-III, Hubris, Tock, Embassy, RIOT | Record, 2026-10-07 |
| [R-002](research/R-002-market-certification-and-positioning.md) | Market share, measured performance in the public record, safety certification landscape, regulation (CRA, memory safety), scheduling ideas from QNX, seL4, RTIC, ThreadX | Record, 2026-10-07 |
| [R-003](research/R-003-differentiation.md) | Differentiation: nine claims, complaint checklist, adopt/avoid/beat decisions, measurable targets T1 to T14, the benchmark harness, ADR-026 to ADR-037 | Record; decisions accepted 2026-10-07 |
