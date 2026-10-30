# M2 exit review

**Date:** 2026-10-30. **Delivered as:** the stacked pull requests #21 to #39 (one logical change each, every one with its own validation), reviewed and merged by the project owner; the stack reaches `main` through #40. **Not done:** anything that needs the NUCLEO-F446RE board.

## Checklist items

| Item | Delivered | Pull requests | Evidence |
|---|---|---|---|
| M2-01 known failures | Seed 5 closed as not reproducible (maintainer decision 2026-10-27), with reopen triggers | #23 | [seed-5 triage](m2-01-seed5-triage.md), [timing and evidence](timing-and-evidence.md) |
| M2-02 contracts and lifecycle | Live-storage refusal for every object type (ADR-045), contract corrections A29 to A33, tiny RAM headroom (slim idle ADR-050, thread table and fault-hook savings), clean IDE diagnostics | #21, #22, #24, #25, #26, #27 | [object lifecycle](m2-02-object-lifecycle.md); T1 39 bytes of RAM, TCB 32 |
| M2-03 binding lifetime | Unbind, a bound thread outlives its bindings (ADR-046) | #28 | KRN-NOTIF-013, -014; BINDING model group |
| M2-04 evidence | Individual case evidence and the requirement matrix; the resource-bound inventory and its check | #29, #30 | [case evidence](case-evidence.md), [resource bounds](resource-bounds.md) (16 bounds, 59 functions) |
| M2-05 event flags | ANY/ALL/CLEAR, deterministic consumption, binding | #31 | `test_event`, EVENT group |
| M2-06 condition variables | Atomic release, mandatory non-cancelable relock | #32 | `test_cond`, COND group |
| M2-07 message queues | Model first (#33), then copy-by-value queues with hand-off both ways | #33, #34 | `test_msgq`, MSGQ group |
| M2-08 work queues | Kernel-thread queues, system queue linked only when used (ADR-048) | #35 | `test_workq`, WORKQ group |
| M2-09 software timers | Own deadline list (ADR-047), drift-free periodic, closed-form catch-up, delayed work | #36, #37 | `test_timer`, TIMER and DWORK groups |
| M2-10 integrated campaign | Combined scenarios, 158,800-run campaign, sanitizers, coverage | #38 | [M2-10 campaign](m2-10-campaign.md) |
| M2-11 exit review | This review, STATUS, roadmap, configuration guide | #39 | this page |

## Reconciliation

- **Public API.** Every function in `include/emb/` has the complete annotation set (the docs annotation audit passes) and a registered API ID (142 IDs; `tools/fault/generate.py --check`). Each new header is reachable from `<emb/emb.h>` under its `CONFIG_` symbol.
- **Complexity.** Every non-constant `@time` annotation maps to the resource-bound inventory (`tools/evidence/check_bounds.py`: 16 bounds cover 59 functions).
- **Configuration.** Each primitive has a Kconfig symbol, a documented default per profile ([configuration](../user/configuration.md)) and, where it is off on tiny, a dependency or a `footprint-t1.conf` line. The T1 reference image stays at 4,056 bytes of kernel text, 39 bytes of kernel RAM and a 32-byte control block on AVR.
- **Model.** Every delivered primitive has a model group and is followed by the differential bridge: 122 model tests, the catalogue on five native runners.
- **Requirements.** The cards of the delivered requirements say "implemented" with their executed cases; the two implemented ones without a case (KRN-IPC-022, KRN-WQ-011) say what verifies them instead.
- **Contract changes.** Amendments A29 to A61 ([M2 amendments](../specs/AMENDMENTS-M2.md)) and ADR-045 to ADR-048 and ADR-050 record every deviation from the accepted specifications. The project owner reviewed and accepted all of them, as written, on 2026-10-31.

## Defects found during M2

| Defect | Found by | Fixed in |
|---|---|---|
| A work queue flush computed its deadline after a tick could advance the clock in its first section | seeded bridge campaign | #35, plus a timer regression |
| The EmbCC native ABI probe lacked `<embk/ipc.h>` | inspection while adding work queues | #34 |
| A C function named `reset` could take the RISC-V reset vector's place | a conformance hang on rv32-virt | #36 |
| Raising a ready thread above the caller did not preempt | the M2-10 combined campaign | #38 |
| AddressSanitizer suites could not run on macOS | the M2-10 sanitizer leg | #38 |

## CI

At this review, #21 to #39 pass every required check, including the new `native-coverage` job. Three jobs that had hung in their tool installation until GitHub's six-hour limit (on #32, #34 and #35) were re-run and passed. No required check fails unexplained.

## Limits carried out of M2

- **Hardware.** No physical-board run, HIL, masked-time, jitter or latency measurement; arming-overhead compensation is unmeasured.
- **Tiny profile.** No work queues, timers, delayed work or condition variables (maintainer decision for timers, 2026-10-27).
- **Not built.** No pipes, buffer pools, ownership queues, barriers, statistics, system freeze, or INDEXED or TABLE handles.
- **Toolchains.** The EmbCC native flow is not built in CI. The local docs build uses a Doxygen version shim: CI's pinned Doxygen is authoritative.

## Remaining M3a work

1. Bring up the NUCLEO-F446RE: run the existing suites on the board, enable kernel-aware IRQ and timer behavior on real hardware, and record the first HIL runner.
2. Measure the masked copy time of a maximum-size message, timer arming overhead and jitter, and work queue latency (KRN-IPC-011, KRN-TIM-006, KRN-WQ-011).
3. Retained crash records and their decoder, trace and debug metadata (SPEC-015).
4. The hardware description generator with one SoC and two boards.
