# Requirements

This directory holds the normative requirements of EmbLinkRTOS, one file per identifier group. The architecture documents in `docs/architecture/` explain and motivate; the specifications in `docs/specs/` design; the files here state what must be true, in a form a test can check. When prose elsewhere and a requirement here disagree, the requirement is fixed or the prose is fixed in the same change (principle 10, 01 §3).

## Format

Every requirement is one block with a fixed shape so that `tools/reqs/` (to be written) can extract the traceability matrix without a hand-maintained table.

```markdown
### GROUP-NNN  Short title
**Statement.** One or more "shall" sentences. Testable. No rationale here.
**Rationale.** Why, in one to three sentences. May cite a design document or decision record.
**Status.** Proposed | Accepted | Implemented | Verified | Withdrawn
**Verification.** Inspection | Analysis | Test | Demonstration, then how: the test or check that covers it, by path or planned path.
**Trace.** Design and decision references (SPEC-001 §4, ADR-002), and parent requirements if any.
```

Rules:

- The heading is exactly `### `, the identifier, two spaces, the title. Identifiers are never reused or renumbered; a withdrawn requirement keeps its number with status Withdrawn and a one-line reason.
- "Shall" is normative. "Should" does not appear in a statement; a recommendation belongs in a specification.
- A statement names the configuration it applies to when it is not universal (`In isolated profiles, ...`).
- One requirement, one testable idea. A statement that needs two different tests is two requirements.
- Status moves forward only with evidence: Accepted on review, Implemented when code exists on `main`, Verified when the named verification runs in CI and passes.

## Verification methods

| Method | Meaning |
|---|---|
| Inspection | A reviewer reads the artifact against the statement (used for documentation and naming rules until an analysis exists) |
| Analysis | A tool checks it: static analysis, identifier scans, generated-size assertions, schema validation |
| Test | A test in `tests/` fails if the statement is false; the test names the identifier it covers |
| Demonstration | Observed on a target or in an emulator with recorded evidence (benchmarks, HIL) |

## Groups

| Group | Scope | File | Migration status |
|---|---|---|---|
| API | Public API conventions, status codes, time types, context classes | [`API.md`](API.md) | Complete; Accepted 2026-10-07 (work item 1) |
| KRN-THR | Threads | [`KRN-THR.md`](KRN-THR.md) | Complete; Accepted 2026-10-07 (work item 7). v0.1 §3.3 and 03 §1.1 identifiers restated |
| KRN-SCH | Scheduler | pending | 03 §2, v0.1 §4 |
| KRN-RQ, KRN-TCB | Ready queues and thread control block | pending | v0.1 §5 |
| KRN-IRQ | Interrupts, exceptions, critical sections, scheduler lock, preemption points | [`KRN-IRQ.md`](KRN-IRQ.md) | Complete; Accepted 2026-10-07 (work item 2). v0.1 identifiers restated |
| KRN-MM | Memory ordering | pending | 03 §1.3 |
| KRN-TIM | Kernel clock, clock modes, timeouts, sleep, software timers, cycle counter, wall clock | [`KRN-TIM.md`](KRN-TIM.md) | Complete; Accepted 2026-10-07 (work item 3). v0.1 and 03 §4 identifiers restated |
| KRN-WAIT | Wait and wake protocol | [`KRN-WAIT.md`](KRN-WAIT.md) | Complete; Accepted 2026-10-07 (work item 4). 03 §3 identifiers restated; reference model `tools/model/` |
| KRN-SYNC | Synchronization | [`KRN-SYNC.md`](KRN-SYNC.md) | Complete; Accepted 2026-10-07 (work item 5). v0.1 §8.6 and 03 §5.2 identifiers restated |
| KRN-NOTIF | Notifications and binding | [`KRN-NOTIF.md`](KRN-NOTIF.md) | Complete; Accepted 2026-10-07 (work item 6). 03 §6.1 identifiers restated |
| KRN-WQ | Work queues and delayed work | [`KRN-WQ.md`](KRN-WQ.md) | Complete; Accepted 2026-10-07 (work item 6). 03 §6.2 identifiers restated |
| KRN-IPC | Inter-thread communication | [`KRN-IPC.md`](KRN-IPC.md) | Complete; Accepted 2026-10-07 (work item 6b). v0.1 §9.4 and 03 §6.5, §6.6 identifiers restated |
| KRN-OBJ | Objects, lifecycle, storage | [`KRN-OBJ.md`](KRN-OBJ.md) | Complete; Accepted 2026-10-07 (work item 8). 03 §7.4 identifiers restated |
| KRN-CAP | Handles and capabilities | [`KRN-CAP.md`](KRN-CAP.md) | Complete; Accepted 2026-10-07 (work item 8). 03 §7.2 identifiers restated |
| KRN-PART | Partitions and the syscall boundary | [`KRN-PART.md`](KRN-PART.md) | Complete; Accepted 2026-10-07 (work item 9). 03 §8 identifiers restated |
| KRN-MEM | Memory | pending | 03 §9, v0.1 §11 |
| KRN-TP | Temporal protection | pending | 03 §2.3 |
| KRN-SMP, MC | Multicore | pending | v0.1 §13, 04 §8 |
| FLT | Fault management | pending | 03 §10 |
| HW | Hardware description | pending | 04 §1 (work item 13) |
| DRV | Device and driver model | pending | 04 §2, §3 |
| PWR | Power management | pending | 04 §4 |
| BOOT | Boot, images, update | pending | 04 §5 |
| SEC | Security | pending | 04 §6, v0.1 §17 |
| OBS | Observability | pending | 04 §7 (work item 14) |
| SIM | Simulation and the native port | [`SIM.md`](SIM.md) | Complete; Accepted 2026-10-07 (work item 12). 04 §9 identifiers restated |
| PORT | Compiler portability (PORT-ABI) and the architecture port contract (PORT) | [`PORT.md`](PORT.md) | Complete; Accepted 2026-10-07 (work item 10). v0.1 §21.5 identifiers restated |
| ARCH-AVR | ATmega328P port | [`ARCH-AVR.md`](ARCH-AVR.md) | Complete; Accepted 2026-10-07 (work item 11). New group |
| BLD | Configuration and build | pending | 05 §2 |
| TEST | Verification | pending | 05 §4, v0.1 §28 |
| REL, SUP | Release and support | pending | 05 §7, §8 |

Migration happens group by group as each specification work item lands (07 §3). Until a group's file exists, the architecture document named in the table is its home and its identifiers are already stable.

## Numbering

Identifiers from the v0.1 specification keep their numbers. v0.2 continued each existing group after its last v0.1 number and started new groups at 001. New requirements take the next free number in their group at the time they are written; gaps left by withdrawals are never filled.
