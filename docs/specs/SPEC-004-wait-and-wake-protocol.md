# SPEC-004 - Wait and Wake Protocol

**Status:** Accepted 2026-10-07 by the project owner, with the defaults of §16. Specification work item 4 of the roadmap (07 §3). Reference model: `tools/model/` (SPEC-004 §13).
**Requirements:** `docs/requirements/KRN-WAIT.md` (KRN-WAIT-001 to 009 restated; new from 010).
**Builds on:** 03 §1.1 (thread states and wait reasons), 03 §3; SPEC-001 §4 (status codes), §5 (contexts and misuse); SPEC-002 §2 (per-CPU state), §4 (critical sections), §5 (scheduler lock), §6 (preemption points); SPEC-003 §5 (timeouts); ADR-013 (reference model), ADR-020 (destroy with waiters), ADR-026 (this protocol), ADR-027 (notification binding), ADR-036 (tiny profile scheduler).
**Research:** R-001 §4 compares how eleven kernels handle the same problem; this protocol takes RTEMS's three-state wait flag, ThreadX's suspension sequence number, Zephyr's superseded in-flight timeout and swap-data hand-off, and uC/OS-III's single pend path under every primitive.
**Toolchain constraints applied:** 09 §6 (no 64-bit atomics on 32-bit targets; byte atomics only on AVR; Armv6-M has no exclusive load and store, so no compare-and-swap).

---

## 1. Scope and terms

Every blocking primitive of the kernel (mutex, semaphore, event flags, condition variable, notification wait, message queue, pipe, join, sleep, thread start and suspend) is a thin layer over one protocol. This specification defines that protocol: its data, its state machine, its locking, its race rules, and its observable results. It does not define any primitive's own semantics (SPEC-005 onward) and does not define the notification API (SPEC-006); it defines the hook they use.

| Term | Meaning |
|---|---|
| **Wait queue** | The ordered set of threads blocked on one condition of one object (`embk_wait_queue_t`). An object may have several (a message queue has one for senders and one for receivers) |
| **Wait node** | The intrusive link that places a thread in a wait queue; one per thread, embedded in the thread control block |
| **Wait reason** | Why the thread is blocked: `START`, `SUSPEND`, `SLEEP`, `OBJECT`, `JOIN`, `NOTIFY` (03 §1.1) |
| **Wait state** | The protocol's own flag: `READY`, `INTEND_TO_BLOCK`, `BLOCKED`. Distinct from the thread state of 03 §1.1, which it drives |
| **Wait generation** | A counter in the thread control block incremented at every completed wait; stale wake sources compare it and do nothing |
| **Wake source** | Whatever ends a wait: a signal on the object, the timeout, thread cancellation, object destruction, a peer's restart (stale generation) |
| **Wake result** | `emb_wait_result_t`, written by the winning wake source before the thread becomes ready |
| **Hand-off word** | One `uintptr_t` the waker passes with the result (a message pointer, a count, satisfied event bits); the waiter never re-checks the object after a hand-off |
| **Object lock domain** | The lock that protects an object's state and its wait queues: a critical section on uniprocessor, the object's spinlock on SMP |
| **Scheduler lock domain** | Protects ready structures and the `BLOCKED` to `READY` transitions: a critical section on uniprocessor, the scheduler spinlock on SMP |
| **Timeout lock domain** | Protects the timeout structure of SPEC-003 §5: a critical section on uniprocessor, the timeout spinlock on SMP |
| **Section** | A stretch of code executed inside one lock domain without releasing it; the protocol's masked time is the longest section |

## 2. Data

### 2.1 Per-thread fields

```
thread:
  wait_state        READY | INTEND_TO_BLOCK | BLOCKED          one byte
  wait_generation   embk_wait_gen_t                            uint8_t on tiny, uint16_t otherwise
  wait_reason       START | SUSPEND | SLEEP | OBJECT | JOIN | NOTIFY
  wait_object       pointer to the object, or NULL for SLEEP and NOTIFY
  wait_queue        pointer to the wait queue the node is in, or NULL
  wait_node         intrusive doubly linked node (prev, next); absent in the table scheduler (§11)
  timeout_node      SPEC-003 §5.1, shared with sleep
  wake_result       emb_wait_result_t
  wake_data         uintptr_t hand-off word
  suspended         bool: a SUSPEND overlay is active (§6.5)
  cancel_pending    bool: thread cancellation requested (§6.6)
```

The thread state of 03 §1.1 is derived: `BLOCKED` when `wait_state == BLOCKED` or `suspended`, else `READY` or `RUNNING` by the scheduler.

### 2.2 Wait queue

```c
typedef struct embk_wait_queue {
    embk_list_t   waiters;     /* intrusive dlist of wait nodes, ordered by policy */
    uint8_t       policy;      /* EMBK_WAIT_PRIORITY_FIFO (default) | EMBK_WAIT_FIFO */
} embk_wait_queue_t;
```

The queue carries no lock of its own: it is protected by the lock domain of the object that embeds it. A mutex's owner pointer, needed for inheritance, lives in the mutex (SPEC-005), not in the queue. In the tiny profile's table scheduler the queue is one bitmap word (§11).

### 2.3 Wake results

```c
typedef enum emb_wait_result {
    EMB_WAIT_SATISFIED = 0,   /* the condition was met; wake_data carries the hand-off */
    EMB_WAIT_TIMEOUT,         /* deadline passed, including the EMB_NO_WAIT path */
    EMB_WAIT_CANCELED,        /* thread cancellation (KRN-THR-011) */
    EMB_WAIT_DESTROYED,       /* object destroyed with ABORT_WAITERS (ADR-020) */
    EMB_WAIT_STALE,           /* the peer handle's generation changed (join or port on a restarted thread, KRN-OBJ-005) */
    EMB_WAIT_INTERRUPTED,     /* reserved */
    EMB_WAIT_BUDGET           /* reserved (ADR-029 SUSPEND policy wakes through replenishment, not through this) */
} emb_wait_result_t;
```

Mapping to public status codes (SPEC-001 §4): `SATISFIED` is `EMB_OK` or the primitive's own value; `TIMEOUT` is `EMB_ETIMEDOUT`; `CANCELED` is `EMB_ECANCELED`; `DESTROYED` is `EMB_EDESTROYED`; `STALE` is `EMB_ESTALE`. A mutex whose previous owner died returns `SATISFIED` with a flag in `wake_data`, which the mutex maps to `EMB_EOWNERDEAD` (SPEC-005). The results are mutually exclusive for one wait by construction (§5).

## 3. Ordering policy

- `PRIORITY_FIFO` (default, KRN-WAIT-001): the queue is ordered by effective priority, highest first; among equal priority, insertion order. Insertion walks from the tail, because a new waiter is most often lower than or equal to the existing ones; the cost is O(waiters on this object).
- `FIFO` (KRN-WAIT-002): append at the tail, O(1); for fairness over priority (barrier, some pipes).
- Repositioning (KRN-WAIT-003): when a blocked thread's effective priority changes (explicit set, inheritance, ceiling, budget demotion), the kernel removes and reinserts its node under the object lock. Inheritance (SPEC-005) does this as part of its chain walk; the hook is `embk_wait_requeue()`.
- The first node is always the thread a single wake serves. `embk_wake_all` serves them in queue order.

## 4. Internal interface

All functions are kernel-internal (`embk_`, SPEC-001 §2). Context classes follow SPEC-001 §5.

```c
void  embk_wait_queue_init(embk_wait_queue_t *q, uint8_t policy);                      /* prekernel thread */

/* Phase 1: called by the primitive with the object lock held, after it has checked the
 * condition and found it unsatisfied. Enqueues self and marks INTEND_TO_BLOCK. */
void  embk_wait_prepare(embk_wait_queue_t *q, uint8_t reason, const void *object);      /* thread, object lock held */

/* Phases 2 and 3: called after the primitive released the object lock. Arms the timeout,
 * commits to BLOCKED or observes an early wake, switches, and returns the result. */
emb_wait_result_t embk_wait_commit(emb_timeout_t timeout, uintptr_t *data_out);         /* thread, no lock held */

/* Convenience for the common shape: releases @key, runs prepare and commit. */
emb_wait_result_t embk_block_on(embk_wait_queue_t *q, uint8_t reason, const void *object,
                                emb_timeout_t timeout, emb_irq_key_t key, uintptr_t *data_out);

/* Wakers: called with the object lock held. */
embk_thread_t *embk_wait_first(const embk_wait_queue_t *q);                            /* thread isr */
bool     embk_wait_wake(embk_thread_t *t, emb_wait_result_t result, uintptr_t data);    /* thread isr; false if t no longer waits here */
unsigned embk_wait_wake_all(embk_wait_queue_t *q, emb_wait_result_t result, uintptr_t data);
void     embk_wait_requeue(embk_thread_t *t);                                           /* after an effective-priority change */

/* Timeout path, called by SPEC-003 §5.3 expiry with no lock held on SMP, inside the
 * critical section on uniprocessor. Named embk_wait_wake(thread, TIMEOUT) in SPEC-003; this is it. */
void  embk_wait_wake_timeout(embk_thread_t *t, embk_wait_gen_t generation);

/* Cancellation and destruction, §6.6 and §6.7. */
bool  embk_wait_cancel(embk_thread_t *t);                                               /* thread; takes the object lock itself */
unsigned embk_wait_flush(embk_wait_queue_t *q, emb_wait_result_t result);               /* object lock held; DESTROYED or STALE */
```

`embk_wait_prepare` and `embk_wait_commit` are split so that a primitive can release its own lock between them; the window this opens is exactly what the state machine of §5 makes safe. A primitive that holds nothing but the critical section uses `embk_block_on`.

## 5. The protocol

### 5.1 Blocking, step by step

```
block_on(object, reason, timeout):                       -- thread context, no lock held
  key = object_lock()                                                   -- section 1
    if condition satisfied:        object_unlock(key); return SATISFIED (with hand-off if any)
    if timeout == EMB_NO_WAIT:     object_unlock(key); return TIMEOUT
    if misuse (ISR, scheduler locked, critical section held by caller): fault / EMB_EINVAL   (SPEC-001 §5.3)
    gen = thread.wait_generation
    thread.wait_reason = reason; thread.wait_object = object; thread.wait_queue = q
    enqueue wait_node by policy                                         -- O(waiters)
    thread.wait_state = INTEND_TO_BLOCK
  object_unlock(key)                                       -- WINDOW: interrupts on, wakers may act

  if timeout is finite:
    key = timeout_lock()                                                -- section 2
      embk_timeout_arm(&thread.timeout_node, deadline, gen)             -- O(armed timeouts)
    timeout_unlock(key)

  key = sched_lock_domain()                                             -- section 3
    if compare_and_set(thread.wait_state, INTEND_TO_BLOCK -> BLOCKED):
        remove thread from the ready structure
        embk_sched_reschedule_if_needed()                               -- P2 switch (SPEC-002 §6.3)
        sched_unlock_domain(key)                                        -- resumes here after the wake
    else:                                                               -- a waker won in the window
        sched_unlock_domain(key)
        key = timeout_lock(); embk_timeout_disarm(&thread.timeout_node); timeout_unlock(key)

  -- after wake: wait_state is READY, wake_result and wake_data are set, generation was bumped by the waker
  *data_out = thread.wake_data
  return thread.wake_result
```

Each numbered section is bounded by one list operation; nothing is held across the window. On uniprocessor every lock domain is the same critical section, so the sections are three short masked stretches with interrupts enabled in between (KRN-WAIT-008, KRN-WAIT-011).

### 5.2 Waking

```
wake(t, result, data):                                   -- object lock held; thread or ISR context
  if t.wait_queue != this object's queue:  return false  -- t is no longer waiting here (checked builds: fault if the caller asserts it was)
  dequeue t.wait_node; t.wait_queue = NULL
  t.wake_result = result; t.wake_data = data             -- result before READY (KRN-WAIT-005)
  t.wait_generation += 1                                 -- every later timeout, cancel, or flush for the old wait is stale
  if compare_and_set(t.wait_state, INTEND_TO_BLOCK -> READY):
      return true                                        -- t is still running on its own CPU and will see READY at commit
  -- else t.wait_state == BLOCKED
  key = sched_lock_domain()
    t.wait_state = READY
    if t.timeout_node armed: disarm it under the timeout lock (order: scheduler -> timeout)
    if not t.suspended:  make t ready; set reschedule_pending if t should preempt (SPEC-002 §6.2); IPI the owning CPU on SMP
  sched_unlock_domain(key)
  return true
```

The waker never spins and never waits. A wake that finds `INTEND_TO_BLOCK` costs a dequeue and one compare-and-set; one that finds `BLOCKED` adds the ready-structure insert.

### 5.3 Timeout

```
wake_timeout(t, gen):                                    -- from SPEC-003 §5.3 expiry
  obj = t.wait_object; q = t.wait_queue                  -- read without a lock; validated below
  key = object_lock(obj)                                 -- uniprocessor: already inside the critical section
    if t.wait_generation != gen or t.wait_queue != q:    -- stale: the wait completed or t waits elsewhere
        object_unlock(key); return
    wake(t, TIMEOUT, 0)                                  -- §5.2, with the lock held
  object_unlock(key)
```

Sleep (`SLEEP`, no object) and notification wait (`NOTIFY`) use a per-thread pseudo-queue so that the same path applies; their lock domain is the scheduler lock domain.

### 5.4 The state machine

| Current | Event | Next | Action |
|---|---|---|---|
| READY | prepare | INTEND_TO_BLOCK | enqueue node (section 1) |
| INTEND_TO_BLOCK | wake (any source) | READY | dequeue, result, generation bump; no ready-structure change |
| INTEND_TO_BLOCK | commit succeeds | BLOCKED | remove from ready structure, switch (section 3) |
| INTEND_TO_BLOCK | commit fails (already READY) | READY | disarm timeout, return result |
| BLOCKED | wake (any source) | READY | dequeue, result, generation bump, disarm timeout, make ready unless suspended |
| BLOCKED | suspend | BLOCKED | set `suspended`; node and timeout untouched (§6.5) |
| READY (woken while suspended) | resume | READY | make ready with the recorded result |
| any | stale timeout, cancel, or flush (generation mismatch) | unchanged | nothing |

Invariants, checked by the reference model and by checked builds:

1. `wait_state != READY` implies the node is in exactly one wait queue (or one bit is set in the tiny profile) and `wait_queue` names it.
2. `wake_result` is written before `wait_state` becomes READY, and read only after.
3. For one wait (one generation value), at most one wake source performs the dequeue.
4. `wait_generation` increases by exactly one per completed wait.
5. A thread is in the ready structure if and only if `wait_state == READY` and not `suspended` and not TERMINATED.
6. No lock domain is held while `wait_state == INTEND_TO_BLOCK` outside sections 1, 2, and 3.

## 6. Wake sources

### 6.1 Signal with hand-off

A primitive that satisfies a waiter passes the satisfaction in `wake_data`: the mutex passes ownership (the waiter owns it when it resumes, SPEC-005); the semaphore passes the unit (the count is not incremented and the waiter does not decrement it); the message queue passes the message slot or copies into the waiter's buffer before waking; event flags pass the satisfied bits; join passes the exit code; a port passes the request handle. The waiter never loops back to re-check the condition (default decision §16.4). This gives wait-order fairness and no barging: a thread that arrives while a waiter is being readied cannot steal the unit. The cost is that a woken thread that is preempted before running holds the unit meanwhile; this is accepted and documented per primitive.

### 6.2 Timeout

§5.3. The deadline is armed after the node is enqueued, so a timeout can only fire for an enqueued thread. On the 32-bit profile the arming path clamps and rejects per SPEC-003 §5.2. `EMB_NO_WAIT` never enqueues, never arms, never yields (API-024).

### 6.3 Notification binding (ADR-027, hook only)

An object that becomes ready for its bound operation, and whose transition was **not** consumed by a hand-off to a direct waiter, calls `embk_binding_signal(&obj->binding)`, which performs `emb_notify_set(binding.thread, binding.bit)` when a binding exists. A hand-off consumes the transition: a semaphore given to a blocked waiter never had a count of one, so no bit is set; a message delivered to a blocked receiver never sat in the ring. The bound thread, when it runs, performs the operation with `EMB_NO_WAIT` and treats `EMB_ETIMEDOUT` as "someone else was faster" (SPEC-001 §4: no would-block code exists). The binding's storage, API, and the notification wait itself are SPEC-006.

### 6.4 Priority change

`embk_wait_requeue(t)` under the object lock removes and reinserts the node by the new effective priority (`PRIORITY_FIFO` only; `FIFO` queues ignore it). A thread in `INTEND_TO_BLOCK` is enqueued and is repositioned like any other.

### 6.5 Suspend and resume (overlay)

`emb_thread_suspend(t)` sets `t.suspended`; if `t` was READY it is removed from the ready structure; if it was waiting, nothing else changes: the node stays in place and the timeout keeps running (KRN-THR-010, KRN-WAIT-014). A wake that arrives while suspended completes the wait (dequeue, result, generation) but does not make the thread ready. `emb_thread_resume(t)` clears the flag and makes the thread ready if `wait_state == READY`. A thread suspended while in `INTEND_TO_BLOCK` commits to BLOCKED normally and stays blocked until both the wait completes and it is resumed. Suspend of self is `block_on(self, SUSPEND, EMB_WAIT_FOREVER)` on the thread's pseudo-queue.

### 6.6 Thread cancellation

`emb_thread_cancel(t)` sets `cancel_pending`. If `t` is waiting with a cancelable reason (`SLEEP`, `OBJECT`, `JOIN`, `NOTIFY`), `embk_wait_cancel(t)` reads `t.wait_object`, takes its lock, re-validates that `t` still waits there with the same generation, and calls `wake(t, CANCELED, 0)`. `START` and `SUSPEND` are not cancelable by wake; the request is delivered at the thread's next blocking call, where `block_on` returns `CANCELED` before enqueuing when `cancel_pending` is set (KRN-THR-011). There is no separate "abort another thread's wait" API: cancellation is that API, and its scope is the whole thread, not one wait.

### 6.7 Destruction and stale peers

Destroying an object with waiters is a fault in checked builds and `EMB_EBUSY` in release builds unless it was created with `ABORT_WAITERS`, in which case `embk_wait_flush(q, DESTROYED)` wakes every waiter in queue order with `DESTROYED`, then the object's generation is bumped (ADR-020, KRN-OBJ-001, 002). A partition or thread restart flushes the waiters of its threads' join queues and ports with `STALE` and bumps the handle generation (ADR-032, KRN-OBJ-005). Flush is `wake_all` with a fixed result and a single reschedule at the end.

## 7. Scheduling interaction

- A wake that makes a higher-priority thread ready sets `reschedule_pending`; the switch happens at P1 (ISR exit) or P2 (end of the thread-context operation), never inside the wake (SPEC-002 §6).
- `embk_wait_wake_all` and `embk_wait_flush` ready every thread first and request one reschedule.
- Under the scheduler lock, wakes proceed normally (the ready structure stays current, SPEC-002 §5); only the switch is deferred. Blocking with a nonzero timeout under the scheduler lock is misuse.
- A woken thread that was preempted (not yet run) keeps its hand-off; nothing in the protocol depends on it running promptly.

## 8. Lock domains and SMP

Uniprocessor: the three domains are the critical section of SPEC-002 §4; the protocol's contribution is the split into bounded sections.

SMP (FUTURE, interface fixed now):

| Domain | Lock | Protects |
|---|---|---|
| object | per-object spinlock | object state, its wait queues, `wait_queue` and `wait_node` of its waiters |
| scheduler | scheduler spinlock (ADR-012) | ready structures, `BLOCKED` to `READY`, `suspended` |
| timeout | per-CPU timeout spinlock | the timeout structure |

Order: object, then scheduler, then timeout; never the reverse. Consequences: the expiry path of SPEC-003 §5.3 pops a node under the timeout lock, marks it in flight, releases the timeout lock, and only then calls `embk_wait_wake_timeout`, which takes the object lock; a wake that must disarm a timeout holds the scheduler lock and takes the timeout lock, which is in order; a cancellation of a timeout whose handler is in flight on another CPU sets the node's superseded bit and the handler checks it after acquiring the object lock (KRN-WAIT-009). Compare-and-set uses the architecture's exclusive or atomic instructions where present; `embk_wait_wake` on a thread in `INTEND_TO_BLOCK` therefore needs no scheduler lock, because that thread is still running on its own CPU and is in no ready structure. A wake that readies a thread whose CPU is not the current one sends the reschedule IPI of SPEC-002 (KRN-WAIT-018).

## 9. Misuse and checked-build diagnostics

| Condition | Checked build | Release build |
|---|---|---|
| `block_on` with nonzero timeout from ISR, with the scheduler locked, or inside a caller's critical section | fault `EMB_FAULT_API_CONTEXT` (SPEC-001 §5.3, KRN-IRQ-001, 021) | `EMB_EINVAL` |
| `block_on` while `wait_state != READY` | fault `EMB_FAULT_KERNEL_INVARIANT` | undefined (cannot happen without corruption) |
| `embk_wait_wake` asserting a thread that does not wait on the given queue | fault `EMB_FAULT_KERNEL_INVARIANT` | returns false |
| queue order violated after insert (`prev.prio < node.prio` in `PRIORITY_FIFO`) | fault | not checked |
| timeout armed with a generation that does not match at expiry | counted in statistics, no fault | same |
| destroy with waiters without `ABORT_WAITERS` | fault `EMB_FAULT_API_LIFECYCLE` | `EMB_EBUSY` |

The transition checker of ADR-035 covers the context conditions; the invariants of §5.4 are the kernel-invariant checks. `EMB_FAULT_KERNEL_INVARIANT` is introduced here as the fault identifier of the kernel fault class of 03 §10.1 (the `EMB_FAULT_API_*` identifiers of SPEC-001 §5.3 cover misuse by the caller; this one covers a broken kernel invariant and is always fatal).

## 10. Observability

Trace events (04 §7.2, CTF): `wait_begin(thread, object, reason, deadline)`, `wait_end(thread, result, cycles_blocked)`, `wake(source: signal | timeout | cancel | flush, waker, thread, result)`, `wait_requeue(thread, old_prio, new_prio)`. Statistics (04 §7.5, optional): per object, contention count (waits that blocked) and maximum queue length; per thread, blocked time; per CPU, stale-timeout count. The debug descriptor (ADR-011) exports the offsets of `wait_state`, `wait_object`, `wait_queue`, `wake_result`, and the queue list head so that a debugger can show "blocked on X behind Y".

## 11. Tiny profile variant (ADR-036)

With `CONFIG_EMB_SCHED_TABLE`, threads are indexed by unique priority and a wait queue is one word: bit *i* set means thread *i* waits. `embk_wait_prepare` sets the bit; `embk_wait_first` is count-leading-zeros; `embk_wait_wake` clears the bit; `embk_wait_requeue` is a no-op (priorities are unique and fixed); `FIFO` policy is not offered. There is no `wait_node`; `wait_queue` is the address of the word. The state machine, generations, results, and the suspend overlay are unchanged. The compare-and-set is a short critical section (AVR and Armv6-M have no atomic compare-and-swap).

## 12. Per-architecture summary

| | AVR | Cortex-M0+ (Armv6-M) | Cortex-M3/M4/M33 | RISC-V (A extension) | native |
|---|---|---|---|---|---|
| compare-and-set | critical section | critical section | `LDREX/STREX` | `LR/SC` or `AMOSWAP` loop | C11 atomics |
| lock domains | one critical section | one critical section | one critical section (UP) | one critical section (UP); spinlocks on SMP | host mutex per domain in the simulator |
| wait generation | `uint8_t` | `uint16_t` | `uint16_t` | `uint16_t` | `uint16_t` |
| wait queue | bitmap (table scheduler) or dlist | dlist | dlist | dlist | dlist |

## 13. Reference model (ADR-013, KRN-WAIT-007)

The model is the first executable artifact of the project and is written in **Python 3** (answers 08 Q15, §16.6): fastest to write and to read in review, with `hypothesis` for property-based exploration; the differential bridge to the kernel is a trace replay, so the model's language is independent of the kernel's. It lives in `tools/model/` (05 §3) and is described in `tools/model/README.md`.

**Structure.** `model/kernel.py` holds `Cpu` (SPEC-002 §2 state machine), `Thread` (§2.1 fields), `WaitQueue` (§2.2, both the list and the bitmap form), `TimeoutList` (SPEC-003 §5), and `Kernel` with operations `block_on`, `wake`, `expire(now)`, `cancel`, `suspend`, `resume`, `flush`, `set_priority`, `irq_enter`, `irq_exit`, `sched_lock`, `sched_unlock`, `yield`. Each operation is written as the sequence of sections of §5, and the explorer may interleave other operations (including interrupts and, in the SMP model, other CPUs) only at section boundaries, which is exactly the real system's atomicity.

**Exploration.** Exhaustive depth-first search over all interleavings for up to 3 threads, 2 objects, one armed timeout per thread, and the operation set above; every reachable state is checked against the invariants of §5.4 and the properties below. Randomized exploration (`hypothesis` stateful testing) beyond that, with a fixed seed corpus committed.

**Properties.** Exactly one wake source dequeues per generation; `wake_result` is set before READY; no thread stays BLOCKED after a wake was delivered to it; no timeout acts after its generation changed; queue order is maintained after every insert and requeue; a suspended thread keeps its queue position and its deadline; cancellation of a blocked thread completes within one operation; `EMB_NO_WAIT` never changes queue or timeout state; the set of ready threads equals the set with `wait_state == READY` and not suspended.

**Oracle for the kernel.** The model exposes `expected_result(thread)` and `expected_order(queue)`; the differential test feeds the kernel's trace events (§10) into the model as operations and compares outcomes. Divergence is a test failure in the kernel, never a reason to edit the model without a specification change.

## 14. Requirements

Restated: KRN-WAIT-001 to 009 (03 §3). New: KRN-WAIT-010 to 022, in `docs/requirements/KRN-WAIT.md`.

## 15. Worked example

Semaphore take with a 10 ms timeout, counting semaphore at zero, one other waiter of lower priority, giver in an ISR after 3 ms:

1. T1 calls `emb_sem_take(s, EMB_MS(10))`. Section 1: count is zero, T1 enqueues ahead of the lower-priority waiter, `INTEND_TO_BLOCK`. Window.
2. Section 2: deadline `now + 10 ms` armed with generation 7.
3. Section 3: compare-and-set succeeds, T1 is removed from the ready structure, switch to the next thread.
4. 3 ms later an ISR calls `emb_sem_give(s)`. The give finds a waiter: `embk_wait_first` is T1; `embk_wait_wake(T1, SATISFIED, 1)` dequeues T1, sets the result and the unit, bumps the generation to 8, finds BLOCKED, disarms the timeout, makes T1 ready, sets `reschedule_pending`. The count stays zero (hand-off, §6.1). No binding bit is set (the transition was consumed).
5. At the outermost ISR exit (P1) T1 runs, reads `wake_result == SATISFIED`, returns `EMB_OK`.
6. If instead the deadline had passed first: expiry pops T1's node with generation 7, `embk_wait_wake_timeout(T1, 7)` finds the generation unchanged and the queue matching, wakes with `TIMEOUT`; `emb_sem_take` returns `EMB_ETIMEDOUT`; the later give finds the lower-priority waiter first instead.

## 16. Decisions taken at acceptance (2026-10-07)

1. Wait queues are protected by the lock domain of the object that embeds them; they carry no lock of their own. On uniprocessor all three domains are the single critical section, and the protocol's guarantee is the split into three bounded sections.
2. The wait state transitions are compare-and-set operations; architectures without an atomic compare-and-swap (AVR, Armv6-M) perform them in a short critical section with identical semantics.
3. Thread cancellation is the only way another thread ends a wait; there is no separate wait-abort API. `START` and `SUSPEND` waits are not cancelable by wake.
4. Every primitive hands the satisfaction over in `wake_data`; a woken waiter never re-checks the condition. No barging.
5. Suspend is an overlay that keeps the wait-queue position and the running deadline; a wake while suspended completes the wait and takes effect at resume.
6. The reference model is written in Python 3 with `hypothesis`; exhaustive exploration up to 3 threads and 2 objects, randomized beyond. This answers 08 Q15.
7. `emb_wait_result_t` is an internal type; public functions return status codes. `STALE` is added to the result set for restarted peers (ADR-032); `INTERRUPTED` and `BUDGET` stay reserved.
8. One notification binding per object (ADR-027); a transition consumed by a hand-off to a direct waiter sets no bit.
9. The wait generation is 8 bits on the tiny profile and 16 bits elsewhere; wrap is harmless because a stale source can only hold a value from a wait that completed at most one generation ago on tiny targets with their bounded timeout count, and the checked build asserts that no two armed timeouts of one thread exist.
