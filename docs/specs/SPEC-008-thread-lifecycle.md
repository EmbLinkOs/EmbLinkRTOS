# SPEC-008 - Thread Lifecycle

**Status:** Accepted 2026-10-07 by the project owner ("do all"), with the defaults of §16. Specification work item 7 of the roadmap (07 §3). Reference model: `tools/model/` (§15).
**Requirements:** `docs/requirements/KRN-THR.md` (KRN-THR-001 to 008 from v0.1 §3.3 and 009 to 014 from 03 §1.1 restated; new from 015).
**Builds on:** 03 §1 (states and wait reasons), §2.1 (priorities), §9.3 (stacks); SPEC-004 (START, SUSPEND, JOIN, SLEEP, NOTIFY are wait reasons over one protocol; suspend overlay §6.5; cancellation §6.6); SPEC-005 §2, §10 (base and effective priority; owner death); SPEC-001 §6, §12 (attributes, storage, entry points); SPEC-002 §7 (interrupt stacks); ADR-029 (`CRITICAL` attribute, budgets), ADR-032 (handle generations), ADR-033 (partition-local storage), ADR-036 (table scheduler).
**Research:** R-001 §1 and §7: Hubris's fixed task set with generation-tagged ids and restart, FreeRTOS's deferred reclamation of self-deleted tasks by the idle task, Zephyr's abort that must wait for in-flight timeouts before storage reuse, ThreadX's completion and terminate hooks. The design below keeps lifetime of execution and lifetime of the object separate (KRN-THR-006) and makes storage reuse a defined point, not an idle-task side effect.

---

## 1. Scope and terms

| Term | Meaning |
|---|---|
| **Thread object** | The thread control block in caller-provided storage, from `init` to `destroy` (or storage reuse) |
| **Execution** | From `start` to termination; strictly inside the object's lifetime (KRN-THR-006) |
| **Joinable** | The default: termination keeps the object until one `join` has collected the exit code |
| **Detached** | Termination makes the object's storage reusable at once; no exit code is kept |
| **Cancellation point** | A blocking call, `emb_thread_sleep`, or `emb_thread_cancel_point()`: where a pending cancel request is delivered (KRN-THR-011) |
| **Exit code** | An `int` passed to `emb_thread_exit` or `0` when the entry function returns; delivered to the joiner by hand-off |

Priorities: larger numeric values mean higher priority; `0` is the idle level and is not assignable to application threads; application priorities are `1 .. CONFIG_EMB_PRIORITY_COUNT - 1` (v0.1 §3.5, KRN-SCH-036).

## 2. Object and attributes

```c
typedef void (*emb_thread_entry_t)(void *arg);              /* SPEC-001 §12; returning terminates the thread */

typedef struct emb_thread_attr {
    const char       *name;          /* kept by pointer; NULL allowed */
    uint8_t           priority;      /* 1 .. CONFIG_EMB_PRIORITY_COUNT - 1 */
    uint8_t           flags;         /* EMB_THREAD_DETACHED | EMB_THREAD_CRITICAL (ADR-029) | EMB_THREAD_CANCEL_DISABLED */
    void             *stack;         /* required; aligned to EMB_STACK_ALIGN */
    size_t            stack_size;    /* >= EMB_THREAD_STACK_MIN (per architecture) */
    emb_partition_t   partition;     /* isolated profiles; EMB_HANDLE_NULL = the caller's partition */
    const emb_budget_t *budget;      /* optional thread budget (ADR-029); NULL = none */
    uint8_t           cpu_affinity;  /* SMP (FUTURE): bit mask; 0 = any */
} emb_thread_attr_t;

#define EMB_THREAD_STACK(name, size)          /* declares an aligned stack array of at least `size` bytes */
#define EMB_THREAD_DEFINE(name, attr..., entry, arg)   /* storage + handle + stack + init-table registration (SPEC-001 §6.4) */
```

## 3. Interface

| Function | `@ctx` | `@blocks` | `@time` | Notes |
|---|---|---|---|---|
| `emb_thread_init(storage, attr, entry, arg, out)` | prekernel thread | no | O(1) + stack fill | the object exists, execution has not begun: state `INACTIVE` (BLOCKED with reason `START`) |
| `emb_thread_start(t)` | prekernel thread isr | no | O(1) | makes it READY; before `emb_kernel_start()` it is queued and runs at start |
| `emb_thread_self()` | thread | no | O(1) | |
| `emb_thread_exit(code)` | thread | never returns | O(owned) | self only; §5 |
| `emb_thread_join(t, timeout, out_code)` | thread | timeout | O(1) | one joiner; §6 |
| `emb_thread_detach(t)` | thread isr | no | O(1) | §6 |
| `emb_thread_suspend(t)` | thread | no | O(1) | overlay, SPEC-004 §6.5; idempotent |
| `emb_thread_resume(t)` | thread isr | no | O(1) | idempotent |
| `emb_thread_cancel(t)` | thread isr | no | O(1) | §7 |
| `emb_thread_cancel_point()` | thread | may return at once | O(1) | returns `EMB_ECANCELED` when a request was pending, else `EMB_OK` |
| `emb_thread_cancel_disable()` / `_enable()` | thread | no | O(1) | counting; §7 |
| `emb_thread_set_priority(t, prio)` | thread | no | O(owned + depth) | SPEC-005 §10 |
| `emb_thread_get_priority(t)`, `emb_thread_get_effective_priority(t)` | thread isr | no | O(1) | base, effective |
| `emb_thread_yield()` | thread | no | O(1) | SPEC-003 §6 |
| `emb_thread_sleep(d)`, `emb_thread_sleep_until(t)` | thread | timeout | | SPEC-003 §6 |
| `emb_thread_state(t, out_state, out_reason)` | thread isr | no | O(1) | `INACTIVE`, `READY`, `RUNNING`, `BLOCKED`, `TERMINATED`; reason when BLOCKED |
| `emb_thread_name(t)`, `emb_thread_index(t)` | thread isr | no | O(1) | the index is the small stable integer of the debug descriptor |
| `emb_thread_stack_info(t, out_size, out_high_water)` | thread | no | O(stack) scan | fill-pattern scan when `CONFIG_EMB_STACK_STATS` |
| `emb_tls_set(slot, p)`, `emb_tls_get(slot)` | thread | no | O(1) | `CONFIG_EMB_TLS_SLOTS`; §9 |
| `emb_thread_foreach(cb, arg)` | thread | no | O(threads) | `CONFIG_EMB_THREAD_LIST`; runs under the scheduler lock |
| `emb_thread_destroy(t)` | thread | no | O(1) | only when `TERMINATED` and joined or detached, or `INACTIVE`; §8 |

Every function that takes `t` validates the handle per SPEC-001 §5.3 and, in isolated profiles, the right the operation needs (SPEC-009): `START`, `SUSPEND`, `CANCEL`, `PRIORITY`, `JOIN`, `NOTIFY`, `DESTROY`.

## 4. Creation and start

- `init` fills in the control block, builds the initial context frame through the architecture contract (`emb_arch_context_init`, SPEC-011) so that the first dispatch enters `entry(arg)` with a return address that leads to `emb_thread_exit(0)` (KRN-THR-005), fills the stack with `EMB_STACK_FILL` when `CONFIG_EMB_STACK_STATS` or a checked build is configured, assigns the stable index, sets `base_prio = eff_prio = priority`, and leaves the thread BLOCKED with reason `START` (KRN-THR-009). Nothing is allocated (KRN-THR-007).
- `start` is a wake with reason `START` (SPEC-004 §5.2): the thread becomes READY behind its peers; before `emb_kernel_start()` the wake is recorded and performed at P3. `start` of a thread that is not `INACTIVE` is `EMB_ESTATE`.
- `EMB_THREAD_DEFINE` objects are initialized from the generated init table before `main()` and started at `emb_kernel_start()` unless defined with `EMB_THREAD_NO_AUTOSTART`.
- Isolated profiles: a thread belongs to `attr.partition` or the caller's partition; an unprivileged partition may create threads only in itself, only from storage inside its own region (ADR-033), and only before `emb_system_freeze()` (KRN-OBJ-004).

## 5. Termination

Only the thread itself ends its execution: by returning from `entry`, by `emb_thread_exit(code)`, or by observing `EMB_ECANCELED` and exiting. There is no `terminate(t)`: threads are never killed asynchronously (KRN-THR-011). The one forced path is partition restart (SPEC-010), which stops the partition's threads at the kernel boundary and re-initializes them.

The exit path, in thread context with interrupts enabled except where noted:

1. `cancel_disable` is implied: a cancel request can no longer be delivered.
2. Owned mutexes are released per `CONFIG_EMB_MUTEX_OWNER_DEATH` (SPEC-005 §3.6); in checked builds, owned pool blocks are a lifecycle fault (SPEC-007 §4.3).
3. Statistics are finalized (CPU time, switches, stack high-water) and the `thread_exit(code)` trace event is emitted.
4. In one critical section: the exit code is stored; the joiner, if blocked, is woken with the code handed over (SPEC-004 §6.1); the thread's own pending timeout, notification bits, TLS slots, and binding references are discarded; the state becomes `TERMINATED`; the thread is removed from the ready structure; `embk_sched_reschedule_if_needed()` performs the final switch (P2).
5. The object becomes **reusable** when the architecture confirms the switch-out has completed (`emb_arch_switch_out_done`, SPEC-011): on uniprocessor this is the moment the next thread runs; on SMP the reusability flag is set by the switching code after the old stack is no longer in use, and `join`/`destroy` on another CPU wait for it. A joinable object is reusable after `join` returns; a detached one at step 5. Reusable means the owner may `destroy` it or re-`init` the storage; the kernel frees nothing (KRN-OBJ-003).

The exiting thread's stack is in use until step 5. Interrupts taken between steps 4 and 5 run on the interrupt stack where one exists (SPEC-002 §7) and on the dying stack otherwise, which is why reusability is published only after the switch.

## 6. Join and detach

- `join(t, timeout, out_code)`: if `t` is `TERMINATED`, returns its code at once and marks the object reusable. Otherwise the caller blocks with reason `JOIN` on `t`'s join slot (one slot: a second joiner gets `EMB_EBUSY`). The hand-off delivers the exit code. Joining a detached thread is `EMB_EINVAL`; joining oneself is `EMB_EDEADLK`; joining an `INACTIVE` thread blocks until it has run and exited (it may never start, which the timeout covers). A thread whose generation changed because its partition restarted answers `EMB_ESTALE` (KRN-OBJ-005).
- `detach(t)`: a joinable thread becomes detached; if it is already `TERMINATED` the object becomes reusable now. Detaching with a joiner blocked is `EMB_EBUSY`.
- The join slot is a one-entry wait queue (the same shape as the notification pseudo-queue), so join costs nothing per object beyond one pointer.

## 7. Suspend, resume, cancel

- `suspend` and `resume` are the overlay of SPEC-004 §6.5 and KRN-THR-010: a suspended thread keeps its wait-queue position and its deadline; a wake that arrives while suspended takes effect at resume. Both are idempotent (`EMB_OK` when already in the requested state). A thread may suspend itself; the kernel's own threads (work queue threads, idle) refuse `suspend` with `EMB_EPERM`.
- `cancel(t)` sets the request. If `t` is blocked with a cancelable reason (`SLEEP`, `OBJECT`, `JOIN`, `NOTIFY`) and cancellation is enabled for it, it is woken with `CANCELED` at once (SPEC-004 §6.6); otherwise the request stays pending and is delivered at the next cancellation point. Delivery clears the request: a thread that keeps running after `EMB_ECANCELED` is not canceled again unless asked again. `cancel` of a `TERMINATED` or `INACTIVE` thread is `EMB_ESTATE`.
- `cancel_disable()` increments a per-thread count; while nonzero, no cancellation point delivers and no wake with `CANCELED` happens (the request waits). `cancel_enable()` decrements; reaching zero with a request pending does not deliver by itself: the next cancellation point does. `EMB_THREAD_CANCEL_DISABLED` starts the thread with count 1. The count is bounded (255); overflow is misuse.
- The recommended shape of a cancelable thread: a loop whose blocking calls check for `EMB_ECANCELED`, release resources, and return from `entry`.

## 8. Destroy

`destroy(t)` requires `INACTIVE` (never started), or `TERMINATED` and reusable (joined or detached, switch-out complete). Anything else is misuse (`EMB_EBUSY`). Destroy removes the thread from the all-threads list, bumps the handle generation (KRN-OBJ-002), and returns the storage to the owner. A thread that is still a mutex owner cannot be `TERMINATED` (step 2 of §5 released them), so no ownership survives destroy. After destroy or re-init, stale handles fail with `EMB_ESTALE` in profiles that check generations and are undefined in the tiny profile, as for every object (SPEC-009).

## 9. Thread-local storage slots

`CONFIG_EMB_TLS_SLOTS` (default 0) adds an array of that many `void *` to each control block; `emb_tls_set` and `emb_tls_get` access the calling thread's slots in O(1) with no locking. No destructors exist in 1.0: a thread that owns slot contents cleans them up before exiting. Kernel code never uses the slots (KRN-THR-013, KRN-THR-014: this is the only per-thread storage mechanism, since compiler TLS is banned).

## 10. Stacks

- Caller-provided, aligned to `EMB_STACK_ALIGN` (8 on Cortex-M and RISC-V, 1 on AVR), at least `EMB_THREAD_STACK_MIN` for the architecture (SPEC-011 states each value); `EMB_THREAD_STACK(name, size)` declares a correctly aligned array. The size includes the context frame the port saves at switch and the worst-case kernel call depth from a thread, both documented per port; interrupt handling uses the interrupt stack where one exists (KRN-MEM-011).
- Fill pattern `EMB_STACK_FILL` (0xEB) written at `init` in checked and statistics builds; `emb_thread_stack_info` scans for the high-water mark (KRN-MEM-009).
- Overflow detection (KRN-MEM-010): at every switch-out the port checks the guard words at the stack limit (all profiles with `CONFIG_EMB_STACK_CHECK`, default on in checked builds); hardware stack limits (`PSPLIM` on Armv8-M, PMP on RISC-V) where available; MPU guard regions in isolated profiles. A detected overflow is a kernel fault for privileged threads and a partition fault for unprivileged ones (03 §10.1).
- The pre-kernel stack (the one `main()` runs on) is reused after `emb_kernel_start()`: as the interrupt stack on architectures with one (Cortex-M `MSP`), or as the idle thread's stack where there is none (AVR), per SPEC-011. `main()` never returns.

## 11. Idle and kernel threads

- The idle thread exists per CPU unless the tiny profile removes it (KRN-SCH-043, ADR-036); its priority is `0`, it is not visible as an application thread, cannot be suspended, canceled, joined, or re-prioritized (`EMB_EPERM`), and it runs the power core's idle hook (04 §4).
- Work queue threads (SPEC-006) are kernel threads: created by `emb_workq_init`, destroyed with the queue, same restrictions as idle except priority, which is the queue's.
- The supervisor partition's thread (03 §8) is an ordinary thread of a privileged partition.

## 12. Misuse and checked-build diagnostics

| Condition | Checked build | Release build |
|---|---|---|
| `init` with priority 0 or out of range, misaligned or undersized stack, NULL entry | fault `EMB_FAULT_API_ARGUMENT` | `EMB_EINVAL` |
| `start` when not `INACTIVE`; `cancel` of `INACTIVE` or `TERMINATED`; `detach` with a joiner | runtime condition | `EMB_ESTATE` / `EMB_EBUSY` |
| `join` on self; `join` on a detached thread; second joiner | fault `EMB_FAULT_API_OWNER` for self-join | `EMB_EDEADLK`, `EMB_EINVAL`, `EMB_EBUSY` |
| `destroy` of a running, blocked, or not-yet-reusable thread | fault `EMB_FAULT_API_LIFECYCLE` | `EMB_EBUSY` |
| `suspend`, `cancel`, `set_priority`, `join` on a kernel thread | fault `EMB_FAULT_API_OWNER` | `EMB_EPERM` |
| `exit` or entry return with owned mutexes or (checked) owned pool blocks | per SPEC-005 §3.6 / fault `EMB_FAULT_API_LIFECYCLE` | mutexes released per configuration |
| `cancel_disable` count overflow; `cancel_enable` at zero | fault `EMB_FAULT_API_ARGUMENT` | `EMB_EOVERFLOW` / `EMB_ESTATE` |
| stack overflow detected at switch | kernel or partition fault per 03 §10.1 | same |
| storage re-initialized before reusable (SMP: before switch-out completed) | fault `EMB_FAULT_KERNEL_INVARIANT` when detectable | undefined |

## 13. Observability

Trace events: `thread_init(t, prio, partition)`, `thread_start`, `thread_exit(t, code)`, `thread_join(joiner, t, result)`, `thread_suspend`, `thread_resume`, `thread_cancel(t, delivered_now)`, `thread_priority(t, base, eff)`, `thread_destroy`, plus the switch events of the scheduler. Statistics (04 §7.5, optional): per thread CPU time, switch count, blocked time, stack high-water, deadline misses, budget overruns. The debug descriptor exports the control block layout, the all-threads list, and the index so a debugger enumerates threads without symbols (ADR-011).

## 14. Tiny profile

With the table scheduler (ADR-036), `init` binds the thread to the table slot equal to its priority (unique priorities, up to 16 including idle); `start`, `suspend`, `resume`, `join` are bit operations; no all-threads list is needed (the table is the list); `CONFIG_EMB_TLS_SLOTS=0`, `CONFIG_EMB_STACK_STATS` off by default, `destroy` is allowed only before freeze. Everything else is identical.

## 15. Reference model

`tools/model/` gains `Start(tid)` with threads created `INACTIVE` when a scenario says so, `Join(tid, timeout)` with the exit-code hand-off and the one-joiner rule, and `ExitCode(code)` as the explicit exit op. New invariants: a `TERMINATED` thread is in no queue and owns nothing; at most one joiner per thread; a joiner of a terminated thread is never left blocked. Scenarios: join before and after exit, join with timeout, cancel of a joiner, start of an inactive thread by another, exit of a thread that owns a mutex (owner-death policy already modeled).

## 16. Decisions taken at acceptance (2026-10-07)

1. Threads are joinable by default; `EMB_THREAD_DETACHED` opts out. One joiner per thread; a second joiner is `EMB_EBUSY`.
2. No `terminate(t)` API; the only forced stop is partition restart (SPEC-010). Cancellation is cooperative with a per-thread disable count.
3. The exit code is an `int`; entry return means code 0.
4. Object reusability is published only after the switch-out completes; on SMP `join` and `destroy` wait for that point. No idle-task reclamation.
5. `start` is ISR-safe and legal before kernel start; `EMB_THREAD_DEFINE` threads autostart unless marked otherwise.
6. `suspend` and `resume` are idempotent; kernel threads (idle, work queues) refuse suspend, cancel, join, and application priority changes.
7. Priority numbering: larger is higher; `0` is idle and not assignable (restates v0.1 §3.5).
8. TLS slots have no destructors in 1.0.
9. The pre-kernel stack becomes the interrupt stack or the idle stack per architecture; `main()` never returns.
10. The model gains start, join, and explicit exit codes.
