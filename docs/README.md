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
| [SPEC-005](specs/SPEC-005-synchronization.md) | Synchronization: effective priority and the inheritance walk, mutex (inherit, ceiling, none, recursive, deadlock detection, owner death), semaphores, event flags, condition variables, barriers, spinlocks, atomics, priority changes, tiny profile, model extension | 07 §3 item 5 | Proposed 2026-10-07, awaiting acceptance |

Upcoming, in roadmap order: notifications and work queues; thread lifecycle; objects and capabilities; partitions; architecture-port contract; ATmega328P port; native port; hardware description schema; observability formats.

## Research records

| Id | Title | Status |
|---|---|---|
| [R-001](research/R-001-rtos-mechanism-comparison.md) | RTOS mechanism comparison: scheduler, interrupts, time, waiting, inheritance, IPC, isolation, observability, quality across FreeRTOS, ThreadX, Zephyr, RTEMS, NuttX, ChibiOS, uC/OS-III, Hubris, Tock, Embassy, RIOT | Record, 2026-10-07 |
| [R-002](research/R-002-market-certification-and-positioning.md) | Market share, measured performance in the public record, safety certification landscape, regulation (CRA, memory safety), scheduling ideas from QNX, seL4, RTIC, ThreadX | Record, 2026-10-07 |
| [R-003](research/R-003-differentiation.md) | Differentiation: nine claims, complaint checklist, adopt/avoid/beat decisions, measurable targets T1 to T14, the benchmark harness, ADR-026 to ADR-037 | Record; decisions accepted 2026-10-07 |
