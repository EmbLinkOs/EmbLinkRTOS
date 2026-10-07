# EmbLinkRTOS Documentation

| Directory | Contents | Read when |
|---|---|---|
| [`architecture/`](architecture/README.md) | The v0.2 architecture baseline: vision, system and kernel architecture, platform, engineering system, decision records, roadmap, open questions, toolchain profile | You want to understand or change *what* EmbLinkRTOS is and *why* |
| [`specs/`](specs/) | Detailed specifications, one per roadmap work item (`SPEC-NNN-*.md`): the design that implements a part of the architecture, with examples | You are about to implement or review a subsystem |
| [`requirements/`](requirements/README.md) | Normative requirements, one file per identifier group, in a fixed format that tooling extracts into the traceability matrix | You are writing a test, checking coverage, or deciding whether a behavior is promised |

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
| [SPEC-003](specs/SPEC-003-time-timeouts-and-timers.md) | Time source, clock modes (periodic tick and tickless), timeout structure, sleep, software timers, time slicing hook, cycle counter, wall clock, per-architecture timer sources | 07 §3 item 3 | Draft for review |

Upcoming, in roadmap order: wait and wake protocol; synchronization; notifications and work queues; thread lifecycle; objects and capabilities; partitions; architecture-port contract; ATmega328P port; native port; hardware description schema; observability formats.
