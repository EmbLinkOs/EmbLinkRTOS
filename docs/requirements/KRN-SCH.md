# KRN-SCH - Scheduler

Group `KRN-SCH`. Design: `docs/architecture/03-kernel-architecture.md` §2, `docs/specs/SPEC-002-interrupts-and-critical-sections.md` §5, §6, `docs/specs/SPEC-003-time-timeouts-and-timers.md` §6, §8. Related groups: KRN-IRQ (preemption points and the scheduler lock at the API level), KRN-THR (thread states and priorities), KRN-SYNC (effective priority), KRN-TIM (the quantum as a deadline).

KRN-SCH-036 to 043 originate in 03 §2 and are restated verbatim. KRN-SCH-001 to 035 originate in v0.1 §4, whose text is not in the repository. The identifiers that code, tests or specifications cite are restated below from the in-repository text that fixes their meaning, named in each Trace; the others (005, 006, 010 to 015, 022 to 025, 027 to 034, 035) stay reserved and are not cited until a source restates them. New requirements start at 044.

---

## Requirements

### KRN-SCH-001  Highest effective priority runs
**Statement.** On each CPU the scheduler shall run the READY thread of highest effective priority.
**Rationale.** Fixed-priority preemptive scheduling is the locked base class (03 §2.1).
**Status.** Accepted 2026-10-31 (restated); implemented.
**Verification.** Test: `sched_higher_priority_runs_first`, `first_execution_sequence_repeats_reliably`; model and differential bridge.
**Trace.** 03 §2.1 ("highest effective priority wins"); v0.1 §4

### KRN-SCH-002  Preemption at the next legal point
**Statement.** A thread that becomes READY with an effective priority above the running thread's, or whose effective priority rises above it, shall preempt it at the next preemption point of SPEC-002 §6.1.
**Rationale.** 03 §2.1 ("preemption at the next legal point"); a raise must preempt as a wake does.
**Status.** Accepted 2026-10-31 (restated); implemented.
**Verification.** Test: `sched_higher_priority_runs_first`, `sched_raising_a_ready_thread_above_the_caller_preempts`; differential bridge.
**Trace.** 03 §2.1; SPEC-002 §6.1; v0.1 §4

### KRN-SCH-003  FIFO among equals
**Statement.** Threads of equal effective priority shall run in the order in which they became READY.
**Rationale.** 03 §2.1 ("FIFO among equals").
**Status.** Accepted 2026-10-31 (restated); implemented.
**Verification.** Test: `sched_fifo_within_level_and_yield_rotates`.
**Trace.** 03 §2.1; v0.1 §4

### KRN-SCH-004  No switch without a change
**Statement.** A preemption point shall switch threads only when a change of some thread's schedulability changed the scheduling decision; a tick that changes no thread's state shall cause no switch.
**Rationale.** SPEC-002 §6.1 names this the only reason a point may decline to switch.
**Status.** Accepted 2026-10-31 (restated); implemented.
**Verification.** Test: `first_execution_sequence_repeats_reliably`; Analysis: the tick path in `kernel/sched/sched.c`.
**Trace.** SPEC-002 §6.1; v0.1 §4

### KRN-SCH-007  Optional round robin
**Statement.** Round-robin time slicing among threads of equal effective priority shall be optional, selected by `CONFIG_EMB_TIME_SLICING`.
**Rationale.** 03 §2.1 ("optional round-robin"); KRN-SCH-037 restricts where it applies.
**Status.** Accepted 2026-10-31 (restated); not implemented.
**Verification.** Test: planned round-robin rotation and quantum cases.
**Trace.** 03 §2.1; SPEC-003 §8; KRN-TIM-029; v0.1 §4

### KRN-SCH-008  Yield never to lower priority
**Statement.** `emb_thread_yield()` shall move the caller behind its equal-priority READY peers and shall never let a lower-priority thread run; with no such peer it shall return without a switch.
**Rationale.** SPEC-003 §6 (a zero sleep is a yield); 03 §2.1.
**Status.** Accepted 2026-10-31 (restated); implemented.
**Verification.** Test: `sched_yield_never_to_lower_priority`, `sched_fifo_within_level_and_yield_rotates`.
**Trace.** SPEC-003 §6; v0.1 §4

### KRN-SCH-009  Decisions use effective priority
**Statement.** Every scheduling decision and every priority-ordered wait queue shall use a thread's effective priority: its base priority raised by inheritance and ceilings.
**Rationale.** 03 §2.1 ("highest effective priority wins"); SPEC-005 §2.1.
**Status.** Accepted 2026-10-31 (restated); implemented.
**Verification.** Test: `sched_thread_self_and_priorities`, `thread_priority_change_requeues_waiter`; model.
**Trace.** 03 §2.1; SPEC-005 §2.1; v0.1 §4

### KRN-SCH-016  The scheduler lock defers preemption
**Statement.** While the scheduler is locked, a thread made READY with a higher effective priority shall not run; the kernel shall record that a reschedule is pending.
**Rationale.** SPEC-002 §5; KRN-IRQ restates it at the API level.
**Status.** Accepted 2026-10-31 (restated); implemented.
**Verification.** Test: `sched_lock_defers_preemption_until_unlock`, `irq_scheduler_lock_defers_the_switch_not_the_handler`.
**Trace.** SPEC-002 §5; v0.1 §4.5

### KRN-SCH-017  The scheduler lock nests
**Statement.** The scheduler lock shall nest by a depth counter.
**Rationale.** SPEC-002 §5.
**Status.** Accepted 2026-10-31 (restated); implemented.
**Verification.** Test: `sched_lock_defers_preemption_until_unlock`.
**Trace.** SPEC-002 §5; v0.1 §4.5

### KRN-SCH-018  Unlock reschedules at depth zero
**Statement.** The unlock that brings the scheduler lock's depth to zero shall perform a pending reschedule immediately, in the unlocking thread's context.
**Rationale.** SPEC-002 §5.
**Status.** Accepted 2026-10-31 (restated); implemented.
**Verification.** Test: `sched_lock_defers_preemption_until_unlock`, `irq_scheduler_lock_defers_the_switch_not_the_handler`.
**Trace.** SPEC-002 §5; v0.1 §4.5

### KRN-SCH-019  The scheduler lock does not mask interrupts
**Statement.** Locking the scheduler shall not mask interrupts: kernel-aware interrupts still run and may make threads READY.
**Rationale.** SPEC-002 §5; it is not a synchronization primitive against interrupts.
**Status.** Accepted 2026-10-31 (restated); implemented.
**Verification.** Test: `irq_scheduler_lock_defers_the_switch_not_the_handler`, `sched_blocking_under_scheduler_lock_is_misuse`.
**Trace.** SPEC-002 §5; v0.1 §4.5

### KRN-SCH-020  Interrupts schedule only through the pending flag
**Statement.** An interrupt shall influence scheduling only by making threads READY or otherwise changing the decision and marking a reschedule as pending; the switch shall happen at the outermost interrupt exit.
**Rationale.** SPEC-002 §6.2; the single mechanism by which interrupts drive scheduling (KRN-IRQ-003).
**Status.** Accepted 2026-10-31 (restated); implemented.
**Verification.** Test: `irq_isr_wake_switches_at_outermost_exit`, `sem_give_from_isr_wakes_at_exit`.
**Trace.** SPEC-002 §6.2; v0.1 §3.7

### KRN-SCH-021  No switch inside a nested interrupt
**Statement.** No thread switch shall occur inside a nested interrupt; the decision shall be taken only at the outermost exit.
**Rationale.** SPEC-002 §6.1; KRN-IRQ-013.
**Status.** Accepted 2026-10-31 (restated); implemented.
**Verification.** Test: `irq_isr_wake_switches_at_outermost_exit`.
**Trace.** SPEC-002 §6.1; KRN-IRQ-013; v0.1 §6.6

### KRN-SCH-026  Quantum expiry rotates only among peers
**Statement.** When a running thread's quantum expires, it shall move behind its equal-priority peers only if one is READY; otherwise its quantum shall be renewed.
**Rationale.** SPEC-003 §8 (v0.1 §5.8).
**Status.** Accepted 2026-10-31 (restated); not implemented.
**Verification.** Test: planned quantum expiry cases with and without a peer.
**Trace.** SPEC-003 §8; KRN-TIM-029; v0.1 §5.8

### KRN-SCH-036  Configurable priority count
**Statement.** Priority count shall be configurable from 2 to 256; the ready bitmap shall be one word for up to the word width and two-level above that.
**Rationale.** 03 §2.1; the lower bound was 4 until A62 (two levels, idle and one application level, is a valid tiny configuration).
**Status.** Accepted (03 §2.1, amended by A62); implemented with `CONFIG_EMB_PRIORITY_COUNT`.
**Verification.** Analysis: the Kconfig range and `kernel/sched/sched.c`; Test: the conformance suites at 8 (tiny) and 32 (base) levels.
**Trace.** 03 §2.1

### KRN-SCH-037  Time slicing below a threshold
**Statement.** Time slicing shall apply only to threads at or below a configured threshold priority level (`CONFIG_EMB_TIMESLICE_PRIORITY`), with one configurable quantum, so that round robin can be enabled for background levels only without a per-level table.
**Rationale.** Revised per R-003 §3.1; answers 08 Q8.
**Status.** Accepted (03 §2.1); not implemented.
**Verification.** Test: planned quantum cases above and below the threshold.
**Trace.** 03 §2.1; R-003 §3.1

### KRN-SCH-038  Class interface for the ready structure
**Statement.** The ready structure shall be reached through a class interface (`enqueue`, `dequeue`, `pick`, `requeue_on_priority_change`) so that additional classes do not modify fixed-priority code.
**Rationale.** 03 §2.2 scheduling classes.
**Status.** Proposed (03 §2.2); the fixed-priority class is the only one.
**Verification.** Inspection: `kernel/include/embk/sched.h` once a second class exists.
**Trace.** 03 §2.2

### KRN-SCH-039  One thread model across classes
**Statement.** The four-state thread model shall be identical across classes.
**Rationale.** 03 §2.2.
**Status.** Proposed (03 §2.2).
**Verification.** Inspection when a second class exists.
**Trace.** 03 §2.2; KRN-THR

### KRN-SCH-040  Zero-overhead single class
**Statement.** When no optional class is configured, the class dispatch shall compile to a direct call into the fixed-priority implementation with zero overhead.
**Rationale.** 03 §2.2.
**Status.** Proposed (03 §2.2); trivially true while only one class exists.
**Verification.** Analysis: disassembly once a class interface exists.
**Trace.** 03 §2.2

### KRN-SCH-041  Preempted threads keep their place
**Statement.** A thread preempted by a higher-priority thread shall be placed ahead of its equal-priority peers when it becomes eligible again, because its quantum is unspent; a thread that yields or exhausts its quantum shall be placed behind them.
**Rationale.** 03 §2.1.
**Status.** Accepted (03 §2.1); implemented.
**Verification.** Test: `sched_preempted_thread_goes_ahead_of_its_peers`; model.
**Trace.** 03 §2.1

### KRN-SCH-042  Tiny thread table
**Statement.** The tiny profile may implement the fixed-priority class as a priority-indexed thread table with unique priorities and bitmap wait sets (ADR-036); the public API and the conformance suite are unchanged within the tiny profile's documented restrictions.
**Rationale.** Footprint targets T1 and T2.
**Status.** Accepted (03 §2.1); implemented.
**Verification.** Test: the conformance suites on native-tiny, cm4-an386-tiny, rv32-virt-tiny and the AVR presets.
**Trace.** 03 §2.1; ADR-036

### KRN-SCH-043  Optional idle thread
**Statement.** The idle thread shall be optional; when absent, the scheduler shall idle inline with interrupts enabled at the preemption points of SPEC-002.
**Rationale.** 03 §2.1; the tiny profile's RAM budget (ADR-050).
**Status.** Accepted (03 §2.1); implemented (`CONFIG_EMB_IDLE_THREAD`).
**Verification.** Test: `sched_idle_runs_when_nothing_is_ready`.
**Trace.** 03 §2.1; SPEC-002 §6; ADR-050
