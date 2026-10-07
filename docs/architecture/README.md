# EmbLinkRTOS Architecture, v0.2

This directory is the architecture baseline for EmbLinkRTOS. It supersedes the v0.1 PDF specification where it disagrees and otherwise builds on it.

## Reading order

| # | Document | What it answers |
|---|---|---|
| 00 | [Review of v0.1](00-review-of-v0.1.md) | What v0.1 got right, what was missing, what v0.2 changes and why |
| 01 | [Vision and principles](01-vision-and-principles.md) | Mission, what "modern" means here, principles, non-goals, positioning, definition of success |
| 02 | [System architecture](02-system-architecture.md) | Layers, responsibilities, concept vocabulary, dependency rules, execution contexts, profiles, native port, how a board arrives |
| 03 | [Kernel architecture](03-kernel-architecture.md) | Execution model, scheduling classes and budgets, wait protocol, time, synchronization, IPC, objects and capabilities, partitions, memory, faults |
| 04 | [Platform architecture](04-platform-architecture.md) | Hardware description pipeline, device and driver model, power, boot and update, security, observability, multicore, simulation, middleware |
| 05 | [Engineering system](05-engineering-system.md) | API conventions, configuration and build, repository layout, verification, quality gates, traceability, release, support, documentation |
| 06 | [Decision records](06-decision-records.md) | ADR-001 to ADR-037 with alternatives and consequences; ADR-004, ADR-024, and ADR-025 are accepted; ADR-026 to ADR-037 come from the research records in `../research/` (R-003); the rest await review |
| 07 | [Roadmap](07-roadmap.md) | 1.0 boundary, milestones M0 to M6+, specification work order, risks |
| 08 | [Open questions](08-open-questions.md) | Decisions awaiting the project owner, each with a recommendation |
| 09 | [EmbCC toolchain profile](09-embcc-toolchain-profile.md) | Verified EmbCC capabilities and limits per target, and the kernel rules that follow from them |

## Status markers

- **LOCKED** - established in v0.1 or confirmed here; changed only through a new ADR.
- **PROPOSED** - new in v0.2; recommended, becomes LOCKED on review.
- **PLANNED** - required capability; detailed design still to be written.
- **FUTURE** - must not be precluded; not in 1.0.

## Requirement identifiers

v0.1 groups continue their numbering (for example `KRN-SCH-036` follows v0.1's `KRN-SCH-035`). New groups introduced in v0.2:

```
KRN-MM    memory ordering          KRN-TP    temporal protection
KRN-WAIT  wait and wake protocol   KRN-NOTIF notifications
KRN-WQ    work queues              KRN-CAP   capabilities
KRN-OBJ   object lifecycle         KRN-PART  partitions
FLT  faults      HW   hardware description   DRV  drivers      PWR  power
BOOT boot/update SEC  security (continues)   OBS  observability MC  multicore
SIM  simulation  API  API conventions        BLD  build        TEST verification (continues)
REL  release     SUP  support levels
```

Requirements move into `docs/requirements/` as structured files group by group, as each specification work item lands; the format guide and group index are in [`docs/requirements/README.md`](../requirements/README.md), and the first migrated group is `API`. Detailed designs live in `docs/specs/`. The architecture documents cite requirements rather than restate them once a group has moved.

## Phase

Architecture only. No kernel code exists yet by design; see roadmap §3 for the specification work that precedes the first implementation milestone.
