# SPEC-006 - Notifications, Notification Binding, and Work Queues

**Status:** Accepted 2026-10-07 by the project owner ("do all"), with the defaults of §12. Specification work item 6 of the roadmap (07 §3). Reference model: `tools/model/` (§11).
**Requirements:** `docs/requirements/KRN-NOTIF.md` (KRN-NOTIF-001 to 006 restated; new from 007) and `docs/requirements/KRN-WQ.md` (KRN-WQ-001 to 004 restated; new from 005).
**Builds on:** SPEC-004 (the wait protocol; a notification wait is a wait with reason `NOTIFY` on the thread's own pseudo-queue), SPEC-003 §7 (software timers run their callbacks as work items; delayed work is a timer), SPEC-005 (semaphores and events are the first bound objects), SPEC-001 §5 (context classes), 03 §6.1, §6.2; ADR-019, ADR-027, ADR-029 (the `NOTIFY` budget policy uses a reserved bit).
**Research:** R-001 §4.3 and §6: per-thread bits are the cheapest wake in every kernel that has them (FreeRTOS direct-to-task notifications, uC/OS-III task semaphores, RIOT thread flags, Hubris notifications, ChibiOS events); FreeRTOS's stream buffers and Zephyr's triggered work show that higher mechanisms can be built on them. Work queue design follows Zephyr's v2 state bits (R-001 §6, `kernel/work.c`) without its single global lock.

---

## 1. Scope and terms

| Term | Meaning |
|---|---|
| **Notification bits** | A per-thread word of `CONFIG_EMB_NOTIFY_BITS` bits (8, 16, or 32; default 32, tiny 8). Any context may set bits; only the owning thread waits on them |
| **Application bits** | The low `EMB_NOTIFY_APP_BITS` bits, free for the application and for bindings |
| **Kernel bits** | The high `EMB_NOTIFY_KERNEL_BITS` bits, used by the kernel for work queues, supervisor fault delivery, and budget overrun notices |
| **Binding** | The link from a waitable object to one `(thread, application bit)` pair: when the object becomes ready for its bound operation, the kernel sets the bit (ADR-027) |
| **Work item** | An intrusive, caller-owned record with a handler, run in thread context by a work queue, in FIFO order |
| **Work queue** | A thread plus a FIFO of work items; the system work queue exists in `base` and above |
| **Delayed work** | A work item with an embedded software timer; the timer's expiry submits the item |

Numbers in this specification: `EMB_NOTIFY_KERNEL_BITS` is 8 on 32-bit notification words, 4 on 16-bit, 4 on 8-bit; `EMB_NOTIFY_APP_BITS` is therefore 24, 12, or 4 (KRN-NOTIF-006 asks for at least 16 in `base` and 4 in `tiny`).

## 2. Notifications

### 2.1 Interface

```c
typedef uint32_t emb_notify_bits_t;             /* width from CONFIG_EMB_NOTIFY_BITS */

#define EMB_NOTIFY_APP_MASK      ((emb_notify_bits_t)((1u << EMB_NOTIFY_APP_BITS) - 1u))
#define EMB_NOTIFY_K_WORK        (1u << (EMB_NOTIFY_BITS - 1))   /* work queue thread: items available */
#define EMB_NOTIFY_K_FAULT       (1u << (EMB_NOTIFY_BITS - 2))   /* supervisor: a partition fault is pending (03 §8) */
#define EMB_NOTIFY_K_BUDGET      (1u << (EMB_NOTIFY_BITS - 3))   /* NOTIFY overrun policy (ADR-029) */
/* remaining kernel bits reserved */

emb_status_t emb_notify_set(emb_thread_t t, emb_notify_bits_t bits);                 /* @ctx thread isr  @blocks no  @time O(1) */
emb_status_t emb_notify_wait(emb_notify_bits_t mask, uint8_t mode,                  /* EMB_NOTIFY_ANY | EMB_NOTIFY_ALL, optionally | EMB_NOTIFY_CLEAR */
                             emb_timeout_t timeout, emb_notify_bits_t *out_bits);    /* @ctx thread  @blocks timeout  @time O(1) */
emb_status_t emb_notify_wait_until(emb_notify_bits_t mask, uint8_t mode, emb_instant_t deadline, emb_notify_bits_t *out_bits);
emb_notify_bits_t emb_notify_get(void);                                               /* @ctx thread  own bits, snapshot */
emb_status_t emb_notify_clear(emb_notify_bits_t bits);                               /* @ctx thread  own bits */
```

There is no counting or value-passing notification: counting is a semaphore, values travel in a message queue (SPEC-007). The decision is §12.1.

### 2.2 Semantics

- `set` ORs `bits` into the target's word in one critical section. If the target is waiting with reason `NOTIFY` and the result satisfies its mask and mode, the kernel hands it the satisfied bits (`wake_data = bits & mask`), applies the waiter's `CLEAR` to those bits, and wakes it (SPEC-004 §5.2); otherwise the bits simply accumulate. Setting a kernel bit from application code is misuse (`EMB_EINVAL`); kernel subsystems set them through an internal entry.
- `wait` evaluates in section 1 of SPEC-004 §5.1: satisfied when `(word & mask) != 0` (`ANY`) or `(word & mask) == mask` (`ALL`); on satisfaction it writes `word & mask` to `out_bits`, clears those bits when `CLEAR` is set, and returns `EMB_OK` without blocking; else it blocks on the thread's own pseudo-queue with reason `NOTIFY`. A `mask` of zero is `EMB_EINVAL`. There is exactly one possible waiter, so the pseudo-queue is a one-slot structure and every operation is O(1) (KRN-NOTIF-003).
- Bits are level-sensitive: a bit set while nobody waits stays set until consumed by a `CLEAR` wait or `clear`. A waiter that does not pass `CLEAR` leaves the bits set and will be satisfied again immediately.
- `set` is the canonical ISR-to-thread completion path and the only wake mechanism the tiny profile needs besides timeouts (03 §6.1). It preserves the caller's mask (KRN-IRQ-033) and never blocks.
- Isolated profiles: `set` on a thread of another partition requires a thread capability with the `NOTIFY` right (SPEC-009); within a partition no right is needed. The kernel bits cannot be set across a partition boundary at all.
- Thread termination discards the word; a restart clears it (ADR-032).

### 2.3 Worked pattern: ISR completion

```c
/* ISR */
EMB_ISR(DMA1_Stream5) { ...; (void)emb_notify_set(g_rx_thread, RX_DONE); }

/* thread */
for (;;) {
    emb_notify_bits_t got;
    if (emb_notify_wait(RX_DONE | RX_ERROR, EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR, EMB_MS(100), &got) == EMB_ETIMEDOUT)
        recover();
    else if (got & RX_ERROR) ...
}
```

## 3. Notification binding (ADR-027)

### 3.1 Interface

Every waitable object type whose readiness is a state (semaphore count, event bits, queue occupancy, pipe fill, port request) offers:

```c
emb_status_t emb_<obj>_bind_notify(emb_<obj>_t obj, emb_thread_t t, uint8_t bit);   /* @ctx thread; bit < EMB_NOTIFY_APP_BITS */
emb_status_t emb_<obj>_unbind_notify(emb_<obj>_t obj);                             /* @ctx thread */
```

`bind` fails with `EMB_EEXIST` when the object is already bound (unbind first), `EMB_EINVAL` for a kernel bit, `EMB_EPERM` in isolated profiles when the caller lacks the object's `BIND` right or the thread's `NOTIFY` right. The binding is stored in the object: one thread reference and one bit index.

### 3.2 Semantics

- The object signals its binding on every transition to **ready for the bound operation** that is not consumed by a hand-off to a direct waiter (SPEC-004 §6.3, KRN-SYNC-035). Per object type: semaphore count 0 to 1; event bits changed and nonzero after direct waiters were served; message queue empty to non-empty (receive side); pipe fill crossing the read trigger level; port request arrival.
- Signalling is `emb_notify_set(binding.thread, 1 << binding.bit)` from inside the object's critical section: O(1), ISR-safe, no list.
- The bound thread waits on its mask, then performs the operation with `EMB_NO_WAIT` on each object whose bit is set and treats `EMB_ETIMEDOUT` as "another consumer was faster", clearing nothing it did not consume (the bit was already cleared by the `CLEAR` wait; the next transition sets it again). Edge semantics plus re-check give **no lost wakeups and at most one spurious pass per transition** (KRN-NOTIF-008).
- Direct waiters on the object are unaffected (KRN-NOTIF-005): they are served first, by hand-off, and such a transition sets no bit.
- One binding per object in 1.0 (§12.3); a second consumer of the same object uses a direct wait.
- Destroying a bound object does not touch the thread's bits; destroying the bound thread leaves a dangling binding, which the kernel detects by generation in profiles that check generations and ignores otherwise (the set targets a dead thread and is dropped). Rebinding after thread restart is the application's job.

### 3.3 Worked pattern: one thread, three sources

```c
emb_sem_bind_notify(g_adc_done, self, 0);
emb_msgq_bind_notify(g_cmd_queue, self, 1);
emb_event_bind_notify(g_errors, self, 2);
for (;;) {
    emb_notify_bits_t got;
    emb_notify_wait(0x7, EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR, EMB_WAIT_FOREVER, &got);
    if (got & 1) while (emb_sem_take(g_adc_done, EMB_NO_WAIT) == EMB_OK) handle_sample();
    if (got & 2) while (emb_msgq_receive(g_cmd_queue, &cmd, EMB_NO_WAIT) == EMB_OK) handle_cmd(&cmd);
    if (got & 4) { emb_event_wait(g_errors, ERR_ANY, EMB_EVENT_ANY | EMB_EVENT_CLEAR, EMB_NO_WAIT, &bits); handle_errors(bits); }
}
```

## 4. Work queues

### 4.1 Objects

```c
typedef void (*emb_work_fn_t)(emb_work_t *work);

typedef struct emb_work {                 /* intrusive, caller-owned, statically allocatable (KRN-WQ-001) */
    embk_list_node_t node;
    emb_work_fn_t    fn;
    emb_workq_t      queue;               /* the queue it is pending on or running on, else EMB_HANDLE_NULL */
    uint8_t          state;               /* EMB_WORK_IDLE | EMB_WORK_QUEUED | EMB_WORK_RUNNING, plus the RESUBMITTED flag */
} emb_work_t;

typedef struct emb_dwork {                /* delayed work: a work item plus an embedded timer (SPEC-003 §7) */
    emb_work_t            work;
    emb_timer_storage_t   timer_storage;
    emb_timer_t           timer;
} emb_dwork_t;

typedef struct emb_workq_attr {
    const char *name;
    uint8_t     priority;                 /* of the queue's thread */
    void       *stack;                    /* required: caller-provided stack (KRN-THR-007) */
    size_t      stack_size;
} emb_workq_attr_t;
```

A work queue is a thread (SPEC-008) plus an intrusive FIFO. The queue's thread waits on `EMB_NOTIFY_K_WORK` and drains the FIFO; submission is a list push plus a notification set, which is why it is O(1) and ISR-safe.

### 4.2 Interface

```c
emb_status_t emb_workq_init(emb_workq_storage_t *storage, const emb_workq_attr_t *attr, emb_workq_t *out);   /* @ctx prekernel thread */
emb_status_t emb_workq_destroy(emb_workq_t q);                                 /* @ctx thread; misuse while items are queued or running */
emb_workq_t  emb_workq_system(void);                                           /* the system work queue, CONFIG_EMB_SYSTEM_WORKQ */
emb_status_t emb_workq_flush(emb_workq_t q, emb_timeout_t timeout);            /* @ctx thread  @blocks timeout: until every item queued before the call has run */

void         emb_work_init(emb_work_t *work, emb_work_fn_t fn);                /* @ctx prekernel thread isr (plain initialization) */
emb_status_t emb_work_submit(emb_workq_t q, emb_work_t *work);                 /* @ctx thread isr  @blocks no  @time O(1) */
emb_status_t emb_work_cancel(emb_work_t *work, emb_work_state_t *out_state);   /* @ctx thread isr  @blocks no  @time O(1) */
emb_status_t emb_work_cancel_sync(emb_work_t *work, emb_timeout_t timeout);    /* @ctx thread  @blocks timeout */
emb_work_state_t emb_work_state(const emb_work_t *work);                       /* @ctx thread isr */

void         emb_dwork_init(emb_dwork_t *dw, emb_work_fn_t fn);
emb_status_t emb_dwork_schedule(emb_workq_t q, emb_dwork_t *dw, emb_duration_t delay);     /* @ctx thread isr  @blocks no  @time O(timeouts) */
emb_status_t emb_dwork_schedule_at(emb_workq_t q, emb_dwork_t *dw, emb_instant_t at);
emb_status_t emb_dwork_reschedule(emb_workq_t q, emb_dwork_t *dw, emb_duration_t delay);   /* replaces a pending delay */
emb_status_t emb_dwork_cancel(emb_dwork_t *dw, emb_work_state_t *out_state);
emb_status_t emb_dwork_cancel_sync(emb_dwork_t *dw, emb_timeout_t timeout);
```

### 4.3 Semantics

- **Submit.** In one critical section: if `state == IDLE`, append to the queue's FIFO, `state = QUEUED`, `queue = q`, set `EMB_NOTIFY_K_WORK` on the queue's thread, return `EMB_OK`. If `QUEUED`, return `EMB_EEXIST` (idempotent: not queued twice, 03 §6.2). If `RUNNING` on `q`, set the `RESUBMITTED` flag and return `EMB_OK`: the item is appended again when its handler returns, so an item never runs concurrently with itself and a submission during execution is never lost (Zephyr's rule, R-001 §6). If `RUNNING` on another queue, `EMB_EBUSY`: an item belongs to one queue at a time.
- **Run.** The queue's thread loop: wait on `EMB_NOTIFY_K_WORK` with `CLEAR`; then, repeatedly, pop the head under a critical section, set `RUNNING`, release the critical section, call `fn(work)`, re-enter the critical section, and set `IDLE` (or re-append and keep `QUEUED` if `RESUBMITTED`). Items run in FIFO order per queue, in thread context at the queue's priority (KRN-WQ-002). A handler may block; the documentation says that it delays every later item on that queue and that latency-sensitive work gets its own queue (03 §6.2).
- **Cancel.** `emb_work_cancel`: `QUEUED` → unlink, `state = IDLE`, report `PENDING`; `RUNNING` → clear `RESUBMITTED`, report `RUNNING` (the handler completes); `IDLE` → report `IDLE`. `cancel_sync` does the same and then, if it was `RUNNING`, waits on the queue's completion wait queue until that item's handler returns; calling it from the item's own handler is misuse (it would wait for itself). KRN-WQ-004.
- **Flush.** Submits an internal marker item and waits for it: everything queued before the call has run when it returns. Flushing a queue from one of its own handlers is misuse.
- **Delayed work.** `schedule` arms the embedded timer (SPEC-003 §7) whose callback is "submit the work item to `q`"; the timer itself runs in `ISR_CONTEXT` because its only action is the O(1) submit, so no second queue hop is paid. `schedule` on a pending delayed item (timer armed or work queued) returns `EMB_EEXIST` and changes nothing; `reschedule` replaces the delay (or, if the work is already queued, leaves it queued and returns `EMB_EEXIST`). `cancel` stops the timer if armed, else cancels the work; it reports `PENDING` for either pending form. Delayed work is the only timing mechanism work queues have (KRN-WQ-003).
- **System work queue.** Exists when `CONFIG_EMB_SYSTEM_WORKQ=y` (default in `base` and above, off in `tiny`); priority `CONFIG_EMB_SYSTEM_WORKQ_PRIO` (default: the second-highest priority, because software timer callbacks run on it, SPEC-003 §7.3), stack `CONFIG_EMB_SYSTEM_WORKQ_STACK`. Applications should not block on it for long; the kernel's own uses are timer callbacks, deferred logging flush, and driver completion bottom halves (04 §3).
- **Destroy.** A queue with queued or running items: misuse (`EMB_EBUSY`). The queue's thread is terminated as part of destroy.

### 4.4 Priorities and latency

Handler latency is the sum of the queue thread's dispatch latency at its priority and the handlers ahead of it in the FIFO. A system with two latency classes creates two queues. The trace events of §10 make both terms measurable; the optional statistics record the worst submit-to-start latency and the longest handler per queue.

## 5. Interaction with other mechanisms

- **Timers (SPEC-003 §7).** A timer's embedded work item is submitted at expiry; the idempotent submit is what coalesces expiries whose previous callback has not run, counted as overruns (SPEC-003 §7.4).
- **Temporal protection (ADR-029).** The `NOTIFY` overrun policy sets `EMB_NOTIFY_K_BUDGET` on the owner or supervisor thread; a work queue thread that overruns a budget is handled like any thread.
- **Partition faults (03 §8).** The supervisor receives `EMB_NOTIFY_K_FAULT`; the fault record carries the detail.
- **Cancellation (SPEC-004 §6.6).** A notification wait is cancelable; a work queue thread is never canceled by the application (its handle is kernel-owned).
- **Suspend.** A suspended thread's bits accumulate; its wait completes on resume per SPEC-004 §6.5.

## 6. Misuse and checked-build diagnostics

| Condition | Checked build | Release build |
|---|---|---|
| `wait` with a zero mask; `bind` to a kernel bit | fault `EMB_FAULT_API_ARGUMENT` | `EMB_EINVAL` |
| application `set` of a kernel bit | fault `EMB_FAULT_API_ARGUMENT` | `EMB_EINVAL` |
| `wait` from an ISR or under the scheduler lock | fault `EMB_FAULT_API_CONTEXT` | `EMB_EPERM` |
| `bind` on an already bound object | runtime condition | `EMB_EEXIST` |
| `submit` of a `RUNNING` item to another queue | runtime condition | `EMB_EBUSY` |
| `cancel_sync` or `flush` from the item's own handler or the queue's own thread | fault `EMB_FAULT_API_CONTEXT` | `EMB_EPERM` |
| `workq_destroy` with items queued or running | fault `EMB_FAULT_API_LIFECYCLE` | `EMB_EBUSY` |
| work item submitted while its storage is being reused (`state` not IDLE and `queue` mismatch) | fault `EMB_FAULT_KERNEL_INVARIANT` | undefined |
| handler returns with a mutex still owned | fault `EMB_FAULT_API_LIFECYCLE` (the next handler would inherit the lock) | the mutex stays owned by the queue thread; documented hazard |

## 7. Observability

Trace events: `notify_set(target, bits, woke)`, `notify_wait(thread, mask, mode, result)`, `bind_signal(object, thread, bit)`, `work_submit(queue, item, result)`, `work_start(queue, item, latency)`, `work_end(queue, item, duration)`, `work_cancel(item, state)`, `dwork_schedule(item, deadline)`. Statistics (optional): per queue, maximum FIFO length, worst submit-to-start latency, longest handler with its address; per thread, notification sets received. The debug descriptor (ADR-011) exports the work item layout so a debugger can list a queue's pending items.

## 8. Tiny profile

`CONFIG_EMB_NOTIFY_BITS=8` (4 application bits, 4 kernel bits of which `K_WORK` is used only when a work queue is configured). `CONFIG_EMB_SYSTEM_WORKQ=n` by default: timers then run in `ISR_CONTEXT` (SPEC-003 §7.3, ADR-019 consequence). Bindings are available (one thread reference and a bit index per object). A table-scheduler build stores a binding's thread as its table index.

## 9. Per-architecture summary

Nothing here is architecture-specific beyond the critical section of SPEC-002 §4. On SMP (FUTURE) `set` targets a thread on another CPU through the wake path's IPI (KRN-WAIT-018); the work FIFO is protected by the queue's spinlock.

## 10. Worked example: driver completion on the system queue

A UART driver's receive ISR fills a buffer and submits its bottom-half work item (`emb_work_submit(emb_workq_system(), &drv->rx_work)`), O(1) and idempotent; the handler, in thread context, parses frames and gives a semaphore the application thread is bound to; the application thread's single `emb_notify_wait` covers that semaphore, a command queue, and an error event group (§3.3). Interrupt-masked time per ISR is one list push and one bit set.

## 11. Reference model

`tools/model/` gains `NotifySet(tid, bits)` (thread and ISR) and `NotifyWait(mask, mode, clear, timeout)` over the same `_block_section`, with the one-slot pseudo-queue, hand-off of satisfied bits, and `CLEAR` applied at wake; the semaphore binding already present now signals through the same `set` path. New invariants: a thread blocked with reason `NOTIFY` has an unsatisfied mask; a `set` that satisfies a waiting owner always wakes it in the same step. New scenarios: set in the window, set before wait (level), `ALL` across two sets, and the multi-source pattern of §3.3 with two bound semaphores (property: every given unit is eventually taken by the bound thread or remains in the count; at most one spurious pass per transition). Work queues are a thread pattern over notifications and are not modeled separately; their FIFO and state machine are tested in conformance.

## 12. Decisions taken at acceptance (2026-10-07)

1. Notifications are bits only: no counting value and no overwrite value. Counting is a semaphore; values travel in queues.
2. Kernel bits are the high `EMB_NOTIFY_KERNEL_BITS` (8 of 32, 4 of 16, 4 of 8): `K_WORK`, `K_FAULT`, `K_BUDGET`, rest reserved. Application code cannot set them.
3. One binding per object; `bind` on a bound object is `EMB_EEXIST`; unbind first.
4. Work queues are built on the `K_WORK` notification bit; submit is one push plus one set.
5. `submit` of a queued item returns `EMB_EEXIST`; of a running item on the same queue, `EMB_OK` with re-append after the handler; of a running item on another queue, `EMB_EBUSY`.
6. Delayed work uses an `ISR_CONTEXT` timer whose only action is the O(1) submit, so one queue hop is paid, not two.
7. `dwork_schedule` on a pending item is `EMB_EEXIST`; `reschedule` replaces the delay.
8. The system work queue defaults to the second-highest priority and exists in `base` and above; `tiny` has no system queue and runs timers in interrupt context unless it configures one.
9. A handler that returns while owning a mutex is a checked-build fault.
10. Work queues are not modeled separately in the reference model; notifications and bindings are.
