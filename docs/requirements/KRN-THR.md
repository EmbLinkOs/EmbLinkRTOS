# KRN-THR - Threads

Group `KRN-THR`. Design: `docs/specs/SPEC-008-thread-lifecycle.md`. Related groups: KRN-SCH (dispatch, priorities), KRN-WAIT (START, SUSPEND, JOIN, SLEEP, NOTIFY as wait reasons), KRN-SYNC (owner death, priority changes), KRN-MEM (stacks), KRN-OBJ (generations, destroy, freeze), KRN-PART (partition membership, restart), KRN-TP (budgets).

KRN-THR-001 to 008 originate in the v0.1 specification (§3.3); KRN-THR-009 to 014 in `docs/architecture/03-kernel-architecture.md` §1.1. All are restated here as the authoritative copy. New requirements start at 015.

---

### KRN-THR-001  One RUNNING thread per processor
**Statement.** At most one thread shall be in the RUNNING state on each online logical processor.
**Rationale.** Definition of the scheduler's invariant.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: model invariant (`current` per CPU); Test: SMP conformance `sched/one_running_per_cpu`.
**Trace.** SPEC-008 §1; 03 §1.1; v0.1 §3.3

### KRN-THR-002  READY is eligible
**Statement.** A READY thread shall be eligible for scheduling.
**Rationale.** State definition.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: model invariant 5 of SPEC-004 §5.4.
**Trace.** SPEC-008 §1

### KRN-THR-003  BLOCKED is ineligible
**Statement.** A BLOCKED thread shall not be eligible for scheduling.
**Rationale.** State definition.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: model invariant 5.
**Trace.** SPEC-008 §1

### KRN-THR-004  Independent context and stack
**Statement.** Each normal thread shall possess an independent execution context and stack.
**Rationale.** Preemptive multithreading.
**Status.** Accepted 2026-10-07
**Verification.** Test: `thr/stacks_independent` (fill pattern of one thread untouched by another).
**Trace.** SPEC-008 §4, §10

### KRN-THR-005  Entry return terminates
**Statement.** Returning from a thread entry function shall cause controlled thread termination through the kernel.
**Rationale.** No fall-off-the-end crash.
**Status.** Accepted 2026-10-07
**Verification.** Test: `thr/entry_return_exits_with_zero`.
**Trace.** SPEC-008 §4, §5

### KRN-THR-006  Execution and object lifetimes are independent
**Statement.** Thread execution lifetime and thread-object lifetime shall be independent.
**Rationale.** The exit code and the storage outlive execution; the object can exist before start.
**Status.** Accepted 2026-10-07
**Verification.** Test: `thr/join_after_exit_returns_code`, `thr/init_without_start_then_destroy`.
**Trace.** SPEC-008 §1, §5, §6, §8

### KRN-THR-007  Static threads
**Statement.** The kernel shall support statically allocated threads without requiring a dynamic allocator.
**Rationale.** KRN-MEM-001; the tiny profile.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `emb_thread_init` allocates nothing; `EMB_THREAD_DEFINE` is fully static.
**Trace.** SPEC-008 §2, §4

### KRN-THR-008  Stable identity
**Statement.** Thread identity shall remain stable for the lifetime of the thread object.
**Rationale.** Handles and debugger views.
**Status.** Accepted 2026-10-07
**Verification.** Test: `thr/index_stable_across_states`.
**Trace.** SPEC-008 §3, §8

### KRN-THR-009  Start, suspend, resume are wait reasons
**Statement.** Thread start, suspend, and resume shall be expressed through the common wait mechanism as wait reasons, not as additional scheduler states.
**Rationale.** One protocol (SPEC-004).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `emb_thread_start` is a wake with reason START; suspend is the overlay.
**Trace.** SPEC-008 §4, §7; SPEC-004 §6.5

### KRN-THR-010  Suspend keeps the wait
**Statement.** A suspended thread shall retain any pending object wait and its deadline; resume shall re-enter that wait without losing the thread's wait-queue position relative to later waiters.
**Rationale.** Removes the classic suspend-while-waiting ambiguity.
**Status.** Accepted 2026-10-07
**Verification.** Test: `wait/suspend_while_waiting`; model scenario `suspend_while_waiting`.
**Trace.** SPEC-008 §7; KRN-WAIT-014

### KRN-THR-011  Cooperative cancellation
**Statement.** Thread cancellation shall be cooperative: a cancel request is delivered as a wake result at the next blocking call or explicit cancellation point; threads are never asynchronously killed.
**Rationale.** Asynchronous kills leave locks and buffers in undefined states.
**Status.** Accepted 2026-10-07
**Verification.** Test: `thr/cancel_*`; model scenarios `cancel_*`.
**Trace.** SPEC-008 §5, §7; KRN-WAIT-015

### KRN-THR-012  Join and detach
**Statement.** Join shall be supported for joinable threads; a detached thread's object storage becomes reusable immediately on termination according to its storage policy.
**Rationale.** POSIX-shaped lifetime control without an allocator.
**Status.** Accepted 2026-10-07
**Verification.** Test: `thr/join_*`, `thr/detach_*`; model scenarios `join_*`.
**Trace.** SPEC-008 §6

### KRN-THR-013  TLS slots
**Statement.** Thread-local storage slots shall be available as a compile-time option with a fixed per-thread slot count.
**Rationale.** Per-thread state for libraries without compiler TLS.
**Status.** Accepted 2026-10-07
**Verification.** Test: `thr/tls_slots`.
**Trace.** SPEC-008 §9

### KRN-THR-014  No compiler TLS
**Statement.** The kernel, ports, and drivers shall not use compiler thread-local storage (`_Thread_local`, `__thread`); EmbCC compiles it to one shared instance on embedded targets (09 §3). Kernel TLS slots are the only per-thread storage mechanism.
**Rationale.** Toolchain constraint.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: CI grep for `_Thread_local` and `__thread` in kernel, arch, drivers.
**Trace.** SPEC-008 §9; 09 §3

### KRN-THR-015  Lifecycle states and reusability
**Statement.** A thread object shall pass through `INACTIVE` (initialized, not started), the scheduler states, and `TERMINATED`; its storage shall become reusable only when it is `INACTIVE`, or `TERMINATED` and either detached or joined, and the switch-out of its last execution has completed.
**Rationale.** A defined reuse point instead of idle-task reclamation (R-001 §1, FreeRTOS) or a spin on in-flight timeouts (Zephyr).
**Status.** Accepted 2026-10-07
**Verification.** Test: `thr/reuse_after_join`, `thr/reuse_detached`; SMP test `thr/destroy_waits_switch_out`.
**Trace.** SPEC-008 §5, §8

### KRN-THR-016  Start semantics
**Statement.** `start` shall be ISR-safe, legal before kernel start, shall make an `INACTIVE` thread READY behind its peers, and shall return `EMB_ESTATE` for any other state; statically defined threads shall autostart at kernel start unless marked otherwise.
**Rationale.** One way to begin execution; no implicit start at init.
**Status.** Accepted 2026-10-07
**Verification.** Test: `thr/start_before_kernel_start`, `thr/start_twice_estate`, `thr/define_autostart`.
**Trace.** SPEC-008 §4

### KRN-THR-017  Exit path
**Statement.** Termination shall release owned mutexes per `CONFIG_EMB_MUTEX_OWNER_DEATH`, finalize statistics, store the exit code, wake the joiner with the code handed over, discard the thread's timeout, notification bits, TLS slots and bindings, set `TERMINATED`, and switch away, in that order; a pending cancel request shall not be delivered during the exit path.
**Rationale.** Deterministic teardown order that other specifications can rely on.
**Status.** Accepted 2026-10-07
**Verification.** Test: `thr/exit_releases_mutex`, `thr/exit_wakes_joiner`; trace check of the event order.
**Trace.** SPEC-008 §5

### KRN-THR-018  Single joiner, exit-code hand-off
**Statement.** Exactly one thread may join a given thread (`EMB_EBUSY` for a second); the exit code shall be delivered by hand-off; joining oneself shall be `EMB_EDEADLK`, joining a detached thread `EMB_EINVAL`, joining a restarted thread's old generation `EMB_ESTALE`; join of a `TERMINATED` thread shall return at once.
**Rationale.** One-slot join is O(1) and unambiguous.
**Status.** Accepted 2026-10-07
**Verification.** Test: `thr/join_*`; model scenarios `join_before_exit`, `join_after_exit`, `join_timeout`, `join_second_ebusy`.
**Trace.** SPEC-008 §6

### KRN-THR-019  Detach
**Statement.** `detach` shall make a joinable thread detached, shall make an already terminated thread reusable at once, and shall be refused with `EMB_EBUSY` while a joiner is blocked.
**Rationale.** Lifetime control for fire-and-forget threads.
**Status.** Accepted 2026-10-07
**Verification.** Test: `thr/detach_*`.
**Trace.** SPEC-008 §6

### KRN-THR-020  Suspend and resume are idempotent and overlaid
**Statement.** `suspend` and `resume` shall be idempotent, shall implement the overlay of SPEC-004 §6.5, and shall be refused with `EMB_EPERM` on kernel threads.
**Rationale.** Counting suspends are a classic source of stuck threads.
**Status.** Accepted 2026-10-07
**Verification.** Test: `thr/suspend_twice_resume_once`, `misuse/suspend_idle`.
**Trace.** SPEC-008 §7, §11

### KRN-THR-021  Cancellation delivery and disable count
**Statement.** A cancel request shall wake a blocked cancelable wait at once when cancellation is enabled, otherwise be delivered at the next cancellation point; delivery shall clear the request; a per-thread disable count (bounded at 255) shall defer delivery while nonzero; `cancel_point()` shall be a cancellation point.
**Rationale.** Threads need regions where cancellation cannot interrupt a protocol.
**Status.** Accepted 2026-10-07
**Verification.** Test: `thr/cancel_disabled_deferred`, `thr/cancel_point`; model scenarios `cancel_*`.
**Trace.** SPEC-008 §7

### KRN-THR-022  Priority range and direction
**Statement.** Larger numeric values shall mean higher priority; `0` shall be the idle level and not assignable to application threads; application priorities shall be `1 .. CONFIG_EMB_PRIORITY_COUNT - 1`; `get_priority` shall return the base and `get_effective_priority` the effective priority.
**Rationale.** v0.1 §3.5; consistency with the interrupt numbering of SPEC-002 §8.
**Status.** Accepted 2026-10-07
**Verification.** Test: `misuse/thread_priority_zero`; Analysis: KRN-SCH-036 range.
**Trace.** SPEC-008 §1, §3; SPEC-005 §2.3

### KRN-THR-023  Stacks
**Statement.** Thread stacks shall be caller-provided, aligned to `EMB_STACK_ALIGN`, at least `EMB_THREAD_STACK_MIN` for the architecture; `EMB_THREAD_STACK` shall declare one; checked and statistics builds shall fill stacks at init and report the high-water mark; overflow shall be checked at every switch-out when `CONFIG_EMB_STACK_CHECK` is set and by hardware limits or MPU guards where available.
**Rationale.** KRN-MEM-009, 010, 011 applied to threads.
**Status.** Accepted 2026-10-07
**Verification.** Test: `thr/stack_high_water`, `thr/stack_overflow_detected` (fault injection).
**Trace.** SPEC-008 §10; KRN-MEM-009 to 011

### KRN-THR-024  Destroy
**Statement.** `destroy` shall require `INACTIVE`, or `TERMINATED` and reusable; it shall remove the thread from the all-threads list and bump the handle generation; anything else shall be misuse.
**Rationale.** KRN-OBJ-001 to 003 for threads.
**Status.** Accepted 2026-10-07
**Verification.** Test: `misuse/thread_destroy_running`, `thr/destroy_after_join`.
**Trace.** SPEC-008 §8

### KRN-THR-025  Introspection
**Statement.** The kernel shall report a thread's state and wait reason, name, stable index, base and effective priority, and (optionally) stack usage, and shall offer `emb_thread_foreach` when `CONFIG_EMB_THREAD_LIST` is set.
**Rationale.** Tooling and statistics.
**Status.** Accepted 2026-10-07
**Verification.** Test: `thr/state_queries`; debug descriptor consumer test.
**Trace.** SPEC-008 §3, §13

### KRN-THR-026  Kernel threads
**Statement.** The idle thread and work queue threads shall be kernel threads: not suspendable, cancelable, joinable, or re-prioritizable by the application (`EMB_EPERM`), invisible to `foreach` unless asked for.
**Rationale.** The kernel's own progress must not depend on application calls.
**Status.** Accepted 2026-10-07
**Verification.** Test: `misuse/kernel_thread_ops`.
**Trace.** SPEC-008 §11

### KRN-THR-027  Isolated profile rules
**Statement.** A thread shall belong to one partition; an unprivileged partition shall create threads only in itself, from storage in its own region, before freeze; cross-partition thread operations shall require the corresponding right on a thread capability.
**Rationale.** KRN-PART-001, ADR-033, SPEC-009.
**Status.** Accepted 2026-10-07
**Verification.** Isolated-profile tests `part/thread_create_*`, `part/thread_op_without_right`.
**Trace.** SPEC-008 §4; KRN-PART-001, 007; KRN-CAP-002

### KRN-THR-028  Trace and statistics
**Statement.** Init, start, exit with code, join, suspend, resume, cancel, priority change, and destroy shall be trace events; CPU time, switch count, blocked time, and stack high-water shall be optional per-thread statistics.
**Rationale.** 04 §7.5.
**Status.** Accepted 2026-10-07
**Verification.** Test: trace decoder; statistics API test.
**Trace.** SPEC-008 §13

### KRN-THR-029  Tiny profile
**Statement.** With the table scheduler a thread's slot shall be its unique priority, up to 16 threads including idle; `start`, `suspend`, `resume`, and `join` shall be bit operations; TLS and stack statistics shall default off; destroy shall be allowed only before freeze.
**Rationale.** ADR-036; targets T1 and T2.
**Status.** Accepted 2026-10-07
**Verification.** Footprint test of the tiny reference configuration; `thr/` conformance group on it.
**Trace.** SPEC-008 §14

### KRN-THR-030  Model coverage
**Statement.** The reference model shall include start of inactive threads, join with exit-code hand-off and the one-joiner rule, explicit exit codes, and shall be explored over join before and after exit, join with timeout, cancellation of a joiner, start by another thread, and exit while owning a mutex.
**Rationale.** Lifecycle races are protocol races.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `tools/model/tests` scenarios `JOIN`.
**Trace.** SPEC-008 §15; KRN-WAIT-007
