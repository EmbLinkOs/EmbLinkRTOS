# KRN-WAIT - Wait and Wake Protocol

Group `KRN-WAIT`. Design: `docs/specs/SPEC-004-wait-and-wake-protocol.md`. Related groups: KRN-IRQ (critical sections, preemption points), KRN-TIM (timeout as a wake source), KRN-THR (suspend, cancel, join), KRN-SYNC and KRN-IPC (the primitives built on this protocol), KRN-NOTIF (binding hook), KRN-OBJ (destroy with waiters, generations).

KRN-WAIT-001 to 009 originate in `docs/architecture/03-kernel-architecture.md` §3 (008 and 009 added by ADR-026). All are restated here as the authoritative copy. New requirements start at 010.

---

### KRN-WAIT-001  Default wake order
**Statement.** Default wake order shall be highest effective priority first, FIFO among equal priority.
**Rationale.** Priority order is what every real-time analysis assumes; FIFO among equals gives bounded waiting. ThreadX's FIFO default is a documented source of unexpected inversion (R-001 §4.1).
**Status.** Proposed 2026-10-07
**Verification.** Test: conformance `wait/order_priority_fifo` blocks threads of mixed priorities in several arrival orders and checks wake order against the reference model.
**Trace.** SPEC-004 §3; 03 §3.3; R-001 §4.1

### KRN-WAIT-002  FIFO option
**Statement.** Objects may be created with pure FIFO wake order where the application requires fairness over priority.
**Rationale.** Barriers and some streams need arrival order.
**Status.** Proposed 2026-10-07
**Verification.** Test: conformance `wait/order_fifo`.
**Trace.** SPEC-004 §3; 03 §3.3

### KRN-WAIT-003  Repositioning on priority change
**Statement.** A waiter whose effective priority changes while blocked shall be repositioned in a PRIORITY_FIFO wait queue.
**Rationale.** Inheritance and explicit priority changes are meaningless if the queue keeps the old order.
**Status.** Proposed 2026-10-07
**Verification.** Test: conformance `wait/requeue_on_priority_change` raises and lowers a blocked thread's priority and checks `expected_order`; model property "queue order maintained".
**Trace.** SPEC-004 §6.4; 03 §3.3; KRN-SYNC-009

### KRN-WAIT-004  Exactly one winner
**Statement.** Exactly one wake source shall win for a given blocked thread; the losers shall observe that the thread is no longer waiting and shall not modify it.
**Rationale.** Two sources acting on one wait corrupt queues and deliver two results.
**Status.** Proposed 2026-10-07
**Verification.** Analysis: exhaustive exploration of the reference model (SPEC-004 §13) proves it for the bounded configuration. Test: conformance `wait/race_signal_timeout` and `wait/race_cancel_signal` with the deadline interrupt forced into the window on the native port.
**Trace.** SPEC-004 §5; 03 §3.4; ADR-026

### KRN-WAIT-005  Result before READY
**Statement.** The winning wake source shall set the wake result and the hand-off word before making the thread READY.
**Rationale.** A woken thread may run on another CPU or at the next instruction; it must find its result.
**Status.** Proposed 2026-10-07
**Verification.** Analysis: model property; review of `embk_wait_wake`.
**Trace.** SPEC-004 §5.2; 03 §3.4

### KRN-WAIT-006  Atomic removal from queue and timeout structure
**Statement.** Removal from the wait queue and from the timeout structure shall be atomic with respect to each other, as seen by any other wake source.
**Rationale.** A node left in one structure after being removed from the other produces a stale wake.
**Status.** Proposed 2026-10-07
**Verification.** Analysis: model property "no timeout acts after its generation changed"; Test: `wait/race_signal_timeout`.
**Trace.** SPEC-004 §5.2, §5.3, §8; 03 §3.4

### KRN-WAIT-007  Modeled and explored before implementation
**Statement.** The protocol shall be expressed in the executable reference model and verified by randomized and, where practical, exhaustive state exploration before implementation.
**Rationale.** Race rules cannot be reviewed by reading; they are checked by exploring every interleaving.
**Status.** Proposed 2026-10-07
**Verification.** Analysis: the model and its exploration log are committed before the first kernel source file of M2; CI runs the exhaustive exploration on every change to the model.
**Trace.** SPEC-004 §13; 03 §3.4; ADR-013; 05 §4

### KRN-WAIT-008  Three-state wait flag, no lock across the block window
**Statement.** The wait state shall be a three-state flag (READY, INTEND_TO_BLOCK, BLOCKED) changed by compare-and-set on cores that provide it and by a bounded critical section otherwise; no lock or masked section shall be held from the decision to block until the switch.
**Rationale.** This is what bounds the kernel's longest masked section by one list operation instead of by the whole block path (R-001 §4.2; R-003 target T7).
**Status.** Proposed 2026-10-07
**Verification.** Analysis: model invariant 6 of SPEC-004 §5.4. Test: the critical-section monitor (ADR-034) on the conformance run reports every masked section of the wait path within the T7 budget.
**Trace.** SPEC-004 §5.1, §5.4; ADR-026

### KRN-WAIT-009  Wait generation and superseded timeouts
**Statement.** Every wait shall carry a generation; a timeout, cancel, or destroy that observes a different generation shall do nothing. On SMP a timeout whose handler is running on another CPU shall be marked superseded and its cancellation retried.
**Rationale.** Generations make late wake sources harmless without holding locks across them; the superseded bit closes the SMP window between popping a node and acting on it.
**Status.** Proposed 2026-10-07
**Verification.** Analysis: model property "no timeout acts after its generation changed"; the SMP model exercises the superseded path. Test: `wait/stale_timeout_counted` checks the statistics counter.
**Trace.** SPEC-004 §5.3, §8; ADR-026; KRN-TIM-026

### KRN-WAIT-010  One wait at a time, node in the thread control block
**Statement.** A thread shall wait on at most one object at a time; its wait node and its timeout node shall be embedded in its thread control block, and the kernel shall allocate nothing to block a thread.
**Rationale.** Static memory, O(1) wake, and a single place for a debugger to look.
**Status.** Proposed 2026-10-07
**Verification.** Analysis: structure review; the debug descriptor exports the node offsets. Test: footprint test shows no allocation on the wait path.
**Trace.** SPEC-004 §2.1; KRN-MEM

### KRN-WAIT-011  Three bounded sections
**Statement.** A blocking operation shall consist of at most three lock-domain sections (condition check and enqueue; timeout arm; commit and switch), each bounded by one list operation, with the lock domain released between them.
**Rationale.** Makes the T7 masked-time bound a structural property rather than a measurement.
**Status.** Proposed 2026-10-07
**Verification.** Analysis: review of `embk_block_on`, `embk_wait_prepare`, `embk_wait_commit`. Test: monitor of ADR-034 on the conformance run.
**Trace.** SPEC-004 §5.1; R-003 §4 T7

### KRN-WAIT-012  Hand-off with the result
**Statement.** A wake source that satisfies a wait shall deliver the satisfaction (ownership, unit, message, bits, exit code) with the result in one step, and the woken thread shall not re-check the object's condition.
**Rationale.** Prevents barging, gives wait-order fairness, and removes the retry loops that other kernels need (R-001 §4.2, FreeRTOS "loop back to the top").
**Status.** Proposed 2026-10-07
**Verification.** Test: conformance `wait/no_barging` has a higher-priority thread attempt the operation between the give and the waiter's run and checks the waiter still gets the unit.
**Trace.** SPEC-004 §6.1; SPEC-005; SPEC-007

### KRN-WAIT-013  Wake-all order and single reschedule
**Statement.** Waking all waiters of a queue shall deliver results in queue order, make every thread ready, and request one reschedule at the end.
**Rationale.** Deterministic order and no switch storm.
**Status.** Proposed 2026-10-07
**Verification.** Test: `wait/wake_all_order` with a trace check of a single switch after the operation.
**Trace.** SPEC-004 §6.7, §7

### KRN-WAIT-014  Suspend is an overlay
**Statement.** Suspending a blocked thread shall leave its wait-queue position and its deadline untouched; a wake that arrives while suspended shall complete the wait and take effect when the thread is resumed.
**Rationale.** Defines the classic "suspend while waiting" ambiguity once, from the protocol's side (KRN-THR-010).
**Status.** Proposed 2026-10-07
**Verification.** Test: `wait/suspend_while_waiting` in three variants (wake, timeout, resume first); model property "a suspended thread keeps its queue position and deadline".
**Trace.** SPEC-004 §6.5; 03 §1.1; KRN-THR-010

### KRN-WAIT-015  Cancellation as a wake source
**Statement.** A thread cancellation request shall wake a thread blocked with reason SLEEP, OBJECT, JOIN, or NOTIFY with result CANCELED; a thread blocked with reason START or SUSPEND, or not blocked, shall receive the request at its next blocking call, which returns CANCELED before enqueuing. There shall be no other way for one thread to end another's wait.
**Rationale.** One mechanism for cooperative cancellation (KRN-THR-011); no "wait abort" API with ambiguous ownership.
**Status.** Proposed 2026-10-07
**Verification.** Test: `wait/cancel_blocked`, `wait/cancel_pending_delivered`, `wait/cancel_start_not_woken`.
**Trace.** SPEC-004 §6.6; KRN-THR-011

### KRN-WAIT-016  Flush on destroy and on stale peers
**Statement.** Destroying an object created with `ABORT_WAITERS` shall wake every waiter with DESTROYED in queue order and then bump the object's generation; a thread or partition restart shall wake waiters on its join queues and ports with STALE and bump the handle generation.
**Rationale.** Restates KRN-OBJ-001, 002, and 005 from the protocol's side so that one flush primitive serves both.
**Status.** Proposed 2026-10-07
**Verification.** Test: `wait/destroy_abort_waiters`, `wait/destroy_refused_with_waiters`, `wait/stale_on_restart` (isolated profile).
**Trace.** SPEC-004 §6.7; ADR-020; ADR-032; KRN-OBJ-001, 002, 005

### KRN-WAIT-017  Lock domains and order
**Statement.** Wait queues shall be protected by the lock domain of the object that embeds them. On SMP the order shall be object, then scheduler, then timeout, never reversed; the timeout expiry path shall call the wake with no lock held.
**Rationale.** A fixed order is the only deadlock-free discipline; the expiry rule is what makes the order hold for timeouts.
**Status.** Proposed 2026-10-07
**Verification.** Analysis: the spinlock validator (SMP checked builds) records lock order and faults on inversion; the SMP reference model encodes the order.
**Trace.** SPEC-004 §8; ADR-012; KRN-SMP

### KRN-WAIT-018  Cross-CPU wake
**Statement.** A wake that readies a thread which should preempt the current thread of another CPU shall send that CPU the reschedule interrupt; a wake on a thread in INTEND_TO_BLOCK shall not touch any ready structure.
**Rationale.** Preemption latency on SMP must not depend on the woken CPU's next tick; the INTEND_TO_BLOCK rule keeps the common wake lock-free.
**Status.** Proposed 2026-10-07
**Verification.** Test: SMP conformance `wait/cross_cpu_preempt` measures wake-to-run on the other CPU; model property on the SMP model.
**Trace.** SPEC-004 §5.2, §8; KRN-IRQ-024

### KRN-WAIT-019  Tiny profile bitmap wait sets
**Statement.** With the table scheduler, a wait queue shall be one word with one bit per thread; prepare sets the bit, wake-first is the highest set bit, wake clears it; the state machine, generations, results, and the suspend overlay shall be identical to the list form.
**Rationale.** Saves two pointers per thread and makes wake-highest O(1) on the smallest targets (ADR-036).
**Status.** Proposed 2026-10-07
**Verification.** Test: the whole `wait/` conformance group runs on the tiny reference configuration unchanged, within its documented restrictions (unique priorities, no FIFO policy).
**Trace.** SPEC-004 §11; ADR-036; KRN-SCH-042

### KRN-WAIT-020  Trace and statistics
**Statement.** The protocol shall emit trace events for wait begin, wait end with result and blocked duration, wake with source, and requeue; and, as an option, per-object contention count and maximum queue length, per-thread blocked time, and per-CPU stale-timeout count.
**Rationale.** "Blocked on what, behind whom, for how long" is the first question in every scheduling investigation.
**Status.** Proposed 2026-10-07
**Verification.** Test: the trace decoder reconstructs every wait of the conformance run; OBS tests check compile-out.
**Trace.** SPEC-004 §10; OBS-005, OBS-008

### KRN-WAIT-021  Checked-build diagnostics
**Statement.** Checked builds shall fault on: blocking with a nonzero timeout from an ISR, under the scheduler lock, or inside the caller's critical section; blocking while the wait state is not READY; a wake asserted on a thread that does not wait on the given queue; a queue order violation after insert; destroy with waiters without `ABORT_WAITERS`. Release builds shall return `EMB_EINVAL`, false, or `EMB_EBUSY` as SPEC-004 §9 lists.
**Rationale.** SPEC-001 §5.3 rule applied to this protocol.
**Status.** Proposed 2026-10-07
**Verification.** Test: `misuse/wait_*` in checked and release configurations (TEST-010).
**Trace.** SPEC-004 §9; SPEC-001 §5.3; KRN-IRQ-001, 021, 035; ADR-035

### KRN-WAIT-022  Exploration coverage
**Statement.** The reference model shall be explored exhaustively for every interleaving at section granularity of up to 3 threads, 2 objects, and one armed timeout per thread over the operations block, wake, expire, cancel, suspend, resume, flush, set-priority, interrupt entry and exit, scheduler lock and unlock, and yield; and by randomized stateful testing beyond those bounds with a committed seed corpus.
**Rationale.** Fixes what "explored" means so that KRN-WAIT-007 is checkable.
**Status.** Proposed 2026-10-07
**Verification.** Analysis: the exploration tool reports the state count and the bounds reached; CI fails if the bounds are lowered.
**Trace.** SPEC-004 §13; KRN-WAIT-007; TEST-008
