<!-- SPDX-License-Identifier: Apache-2.0 -->
# KRN-TSK - Tasks: Threads Written as State Machines

Group `KRN-TSK`. Design: `docs/specs/SPEC-020-tasks.md`. Related groups: KRN-THR (a task is a thread), KRN-IPC (its event queue is a message queue), KRN-TIM (a state's timeout, a periodic task's schedule).

The group was accepted with SPEC-020 on 2026-11-13.

---

## Requirements

### KRN-TSK-001  One handler at a time, no event lost
**Statement.** A task shall run one state handler at a time, each to completion, and shall deliver every event a post accepted, in the order accepted.
**Rationale.** A state machine is reasoned about one transition at a time; an event the poster was told was taken must reach a handler.
**Status.** Accepted 2026-11-13
**Verification.** Test: events posted while a handler runs are all handled afterwards, in order.
**Trace.** SPEC-020 §4, §5

### KRN-TSK-002  Exit, entry, then the timeout
**Statement.** When a handler returns another state, the task shall run the old state's exit handler, then the new state's entry handler, then arm the new state's timeout; a task shall start by running its initial state's entry handler.
**Rationale.** A fixed order lets a state set up and tear down what only it uses.
**Status.** Accepted 2026-11-13
**Verification.** Test: the order of entry, exit and handler calls across transitions and at the start.
**Trace.** SPEC-020 §4

### KRN-TSK-003  A full queue is an error, counted
**Statement.** A post to a task whose queue is full shall return an error without waiting, unless the poster asked to wait, and shall count a drop.
**Rationale.** An interrupt cannot wait; a dropped event must be visible.
**Status.** Accepted 2026-11-13
**Verification.** Test: a post to a full queue from a thread and from an interrupt; the drop count.
**Trace.** SPEC-020 §5

### KRN-TSK-004  A drift-free period, overruns counted
**Statement.** A periodic task's periods shall start at whole multiples of its period from its start; a period its handler overran shall be counted and not run late.
**Rationale.** A control loop's rate must not drift with its handler's time.
**Status.** Accepted 2026-11-13
**Verification.** Test: period start times against the schedule; an overrun counted, the next period on the schedule.
**Trace.** SPEC-020 §5
