# Research records

Research records answer "what do other systems do, what does the outside world require, and what will EmbLinkRTOS do differently" with sources. They are not normative. Decisions they motivate are recorded as ADRs in `../architecture/06-decision-records.md`; requirements they motivate live in the architecture documents until the owning specification work item moves them into `../requirements/`.

| Id | Title | Status |
|---|---|---|
| [R-001](R-001-rtos-mechanism-comparison.md) | RTOS mechanism comparison across FreeRTOS, ThreadX, Zephyr, RTEMS, NuttX, ChibiOS RT and NIL, uC/OS-III, Hubris, Tock, Embassy, RIOT: scheduler, context switch and interrupts, time, wait and wake, inheritance, IPC, isolation, observability, configuration and quality, scorecard of gaps | Record, 2026-10-07 |
| [R-002](R-002-market-certification-and-positioning.md) | Who uses what, measured performance in the public record, safety certification landscape, regulation (EU CRA, memory-safety roadmaps), scheduling ideas from QNX, seL4 MCS, RTIC, ThreadX | Record, 2026-10-07 |
| [R-003](R-003-differentiation.md) | Differentiation: the nine claims, complaint checklist, adopt/avoid/beat decisions, measurable targets T1 to T14, the cross-RTOS benchmark harness, candidate ADR-026 to ADR-037, questions for the owner | Proposed, awaiting acceptance |

## Per-kernel source notes

Read-only analyses at pinned commits, one fixed question list each, with file and symbol citations. Line numbers drift; paths and symbols are the durable reference.

| Note | Kernels | Commit |
|---|---|---|
| [`notes/freertos.md`](notes/freertos.md) | FreeRTOS kernel V11.1+ | `8be86d4a24fd` |
| [`notes/threadx.md`](notes/threadx.md) | Eclipse ThreadX 6.5.2 | `93387b0a6038` |
| [`notes/zephyr.md`](notes/zephyr.md) | Zephyr v4.5.0-rc1 | `d175d3bfb6c2` |
| [`notes/rtems.md`](notes/rtems.md) | RTEMS SuperCore | `b3a3b372fa8d` |
| [`notes/chibios-nuttx.md`](notes/chibios-nuttx.md) | ChibiOS RT 8.0.0 and NIL 4.2.0; Apache NuttX | `fd2e59c34878`; `b463a4bf7a48` |
| [`notes/ucos3.md`](notes/ucos3.md) | Micrium uC/OS-III 3.08.02 | `9a3fc5f45d75` |
| [`notes/hubris-tock.md`](notes/hubris-tock.md) | Hubris; Tock | `446dfcd5019a`; `40b9e1378345` |
| [`notes/embassy-riot.md`](notes/embassy-riot.md) | Embassy; RIOT | `b3e27baf6c20`; `f9e38576567b` |

## Method

Each kernel was cloned at the pinned commit and read, not executed. A fixed question list (scheduler, context switch and interrupts, time, blocking and waiting, synchronization and inheritance, IPC and memory, isolation, observability, configuration and quality) was answered per kernel with citations. The notes end with "ideas to borrow" and "weaknesses". R-001 synthesizes across kernels; R-003 turns the synthesis into decisions with measurable targets. Web sources in R-002 that could not be fetched directly are marked (summary) and listed in R-002 §6 for re-verification before external use.
