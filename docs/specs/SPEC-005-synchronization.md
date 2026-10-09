# SPEC-005 - Synchronization: Mutexes, Semaphores, Event Flags, Condition Variables, Barriers, Spinlocks

**Status:** Accepted 2026-10-07 by the project owner, with the defaults of §17. Specification work item 5 of the roadmap (07 §3). Reference model: `tools/model/` (SPEC-005 §15).
**Requirements:** `docs/requirements/KRN-SYNC.md` (KRN-SYNC-001 to 017 restated; new from 018).
**Builds on:** SPEC-004 (every blocking primitive here is a thin layer over the wait protocol; hand-off, generations, cancellation, destroy, suspend overlay are inherited and not restated); SPEC-001 §4 to §6 (status codes, contexts, handles, storage, attributes); SPEC-002 §4 (critical sections, spinlocks), §5 (scheduler lock); SPEC-003 (timeouts); 03 §5; v0.1 §8; ADR-027 (notification binding), ADR-028 (inheritance algorithm), ADR-030 (compile-time ceilings), ADR-036 (tiny profile).
**Research:** R-001 §5 is the column-by-column comparison this design is measured against: transitive inheritance, correct restore over several mutexes, disinheritance on timeout, owner-death handling, ceiling protocol, deadlock detection, with bounded masked sections. No small kernel in the comparison has all of them.
**Toolchain constraints applied:** 09 §6 (byte atomics only on AVR, no 64-bit atomics anywhere on 32-bit targets), 09 §7 (enums are `int`-sized; attribute fields are fixed-width).

---

## 1. Scope and terms

| Term | Meaning |
|---|---|
| **Base priority** | The priority a thread was created with or last set to with `emb_thread_set_priority()` |
| **Effective priority** | The priority the scheduler and the wait queues use: the maximum of the base priority and every contribution the thread currently receives (§2) |
| **Contribution** | A priority a thread receives from a mutex it owns: the effective priority of that mutex's highest waiter (inheritance) or the mutex's ceiling (ceiling protocol) |
| **Owned list** | Per thread, the intrusive list of mutexes it owns, in acquisition order (KRN-SYNC-014) |
| **Hand-off** | Transfer of a mutex, a semaphore unit, or event bits to a waiter in the same step as its wake (SPEC-004 §6.1); the waiter never re-checks |
| **Walk** | The transitive propagation of a contribution change along the chain waiter, owner, the mutex that owner waits on, its owner, and so on (ADR-028) |

The primitives, with their 1.0 status and context classes (SPEC-001 §5.2):

| Primitive | 1.0 | Blocking operations (`thread`) | Signal operations (`thread isr`) | Section |
|---|---|---|---|---|
| Mutex | yes | `lock`, `lock_until` | none | §3 |
| Semaphore (binary and counting) | yes | `take`, `take_until` | `give` | §4 |
| Event flags | yes | `wait`, `wait_until` | `set`, `clear` | §5 |
| Condition variable | yes | `wait`, `wait_until` | none (`signal`, `broadcast` are `thread`) | §6 |
| Barrier | `CONFIG_EMB_BARRIER` | `wait`, `wait_until` | none | §7 |
| Spinlock | SMP builds | none | `lock`, `unlock` (never block) | §8 |
| Atomics | yes | none | all | §9 |

Every object follows SPEC-001 §6: a handle type, a generated storage type, an attribute struct with `_attr_default()`, `init` (`prekernel thread`) and `destroy` (`thread`, KRN-OBJ-001), and a `_DEFINE` macro. Every blocking operation has an absolute `_until` form (API-002) and accepts `EMB_NO_WAIT` as its non-blocking form (SPEC-001 §4: an unsatisfied `EMB_NO_WAIT` is `EMB_ETIMEDOUT`).

## 2. Effective priority

### 2.1 Definition

```
effective(t) = max( base(t),
                    max over m in owned(t) with protocol INHERIT:  effective(head waiter of m), if any,
                    max over m in owned(t) with protocol CEILING:  ceiling(m) )
```

The recomputation `embk_prio_recompute(t)` evaluates this in O(owned mutexes); each term is O(1) because a wait queue's head is its highest waiter (KRN-WAIT-001). When the result differs from the stored effective priority the kernel applies it (KRN-SYNC-018):

- `t` READY: remove and reinsert in the ready structure at the new level, behind its new peers.
- `t` RUNNING: update; if a READY thread now has a higher effective priority, set `reschedule_pending` (P2 at the end of the operation).
- `t` BLOCKED on a `PRIORITY_FIFO` queue: `embk_wait_requeue(t)` (KRN-WAIT-003). If that queue belongs to an `INHERIT` mutex, the mutex's owner receives a changed contribution, so the walk continues with the owner (§2.2).
- `t` BLOCKED on a `FIFO` queue, sleeping, or suspended: update only.

### 2.2 The walk

`embk_prio_propagate(t)`: repeat `embk_prio_recompute` along the chain `t`, owner of the mutex `t` waits on, owner of the mutex *that* thread waits on, until a recomputation changes nothing, the chain ends, or `CONFIG_EMB_PI_MAX_DEPTH` (default 8) hops were taken. Both raises and lowerings propagate; a lowering stops as soon as a hop's effective priority does not change (another contribution holds it up). The walk is one section per hop on uniprocessor (the critical section is released and re-taken between hops when `CONFIG_EMB_PI_HOP_SECTIONS=y`, the default on Cortex-M; a single section on AVR where the hop count is tiny); R-003 target T9 bounds its cost.

Exceeding the depth: the walk stops, the lock proceeds with inheritance truncated at that depth, a `pi_depth_exceeded` trace event is recorded; in checked builds it is a kernel fault (KRN-SYNC-009). The depth is a system design parameter; exceeding it means the lock graph is deeper than the designer declared.

### 2.3 Where priorities live

```
thread:
  base_prio        uint8_t
  eff_prio         uint8_t          the one every queue and the scheduler read
  owned            intrusive list head (mutexes)
  sched_base       uint8_t          temporal protection may lower this below base_prio (DEMOTE); effective() uses it in place of base
mutex:
  owner            thread pointer or NULL
  owned_node       intrusive list node
  waiters          embk_wait_queue_t, policy PRIORITY_FIFO | FIFO, with the EMBK_WAIT_INHERIT flag when protocol is INHERIT
  protocol         INHERIT | CEILING | NONE
  ceiling          uint8_t
  count            recursion count, uint8_t (0 when free)
  flags            RECURSIVE | INCONSISTENT | ABORT_WAITERS
```

Temporal protection (ADR-029) lowers `sched_base`, never `eff_prio` directly: a demoted thread that owns a contended mutex is still lifted by inheritance, because otherwise it could not release the mutex and the waiter would inherit the demotion. `emb_thread_get_priority()` returns `base_prio`; `emb_thread_get_effective_priority()` returns `eff_prio`.

## 3. Mutex

### 3.1 Interface

```c
typedef struct emb_mutex_attr {
    const char *name;
    uint8_t     protocol;   /* EMB_MUTEX_INHERIT (default) | EMB_MUTEX_CEILING | EMB_MUTEX_NONE */
    uint8_t     ceiling;    /* EMB_MUTEX_CEILING only; 0 = taken from the generated system description (ADR-030) */
    uint8_t     flags;      /* EMB_MUTEX_RECURSIVE | EMB_OBJ_ABORT_WAITERS */
} emb_mutex_attr_t;

void         emb_mutex_attr_default(emb_mutex_attr_t *out_attr);
emb_status_t emb_mutex_init(emb_mutex_storage_t *storage, const emb_mutex_attr_t *attr, emb_mutex_t *out);  /* @ctx prekernel thread */
emb_status_t emb_mutex_destroy(emb_mutex_t m);                                   /* @ctx thread */
emb_status_t emb_mutex_lock(emb_mutex_t m, emb_timeout_t timeout);              /* @ctx thread  @blocks timeout  @time O(1) uncontended; O(waiters + depth) contended */
emb_status_t emb_mutex_lock_until(emb_mutex_t m, emb_instant_t deadline);       /* @ctx thread  @blocks deadline */
emb_status_t emb_mutex_unlock(emb_mutex_t m);                                   /* @ctx thread  @blocks no  @time O(owned) */
bool         emb_mutex_is_owner(emb_mutex_t m);                                 /* @ctx thread isr  @blocks no  O(1) */
emb_status_t emb_mutex_mark_consistent(emb_mutex_t m);                          /* @ctx thread; §3.6 */
```

Returns of `lock`: `EMB_OK`; `EMB_ETIMEDOUT` (including `EMB_NO_WAIT`); `EMB_ECANCELED`; `EMB_EDESTROYED`; `EMB_EOWNERDEAD` (§3.6, the caller owns the mutex); `EMB_EDEADLK` (§3.5, the caller does not own it); `EMB_EOVERFLOW` (recursion count at its maximum); `EMB_EPERM` (ceiling violation, wrong context, release builds); `EMB_EINVAL`, `EMB_ESTALE` (handle). `EMB_EDEADLK` is the first code of the kernel-reserved range (SPEC-001 §4.2): value -32, name added to `enum emb_status_code` by this specification.

A mutex has no ISR-safe operation (R-001 §5: FreeRTOS's ISR give with no holder is the source of its missing inheritance). An ISR that must signal a thread uses a semaphore or a notification.

### 3.2 Lock

```
lock(m, timeout):                                        -- thread context
  key = object_lock(m)                                   -- section 1 of SPEC-004 §5.1
    if m.owner == NULL:
        m.owner = self; m.count = 1; append m to self.owned
        if m.protocol == CEILING: check ceiling (§3.4); eff raise to ceiling
        result = m.flags.INCONSISTENT ? EMB_EOWNERDEAD : EMB_OK          -- §3.6
        object_unlock(key); return result
    if m.owner == self:
        if m.flags.RECURSIVE: if m.count == 255: unlock; return EMB_EOVERFLOW
                              m.count += 1; unlock; return EMB_OK
        else: unlock; return EMB_EDEADLK                                  -- self-deadlock, checked builds fault
    if timeout == EMB_NO_WAIT: unlock; return EMB_ETIMEDOUT
    if m.protocol == INHERIT and the walk from m.owner reaches self: unlock; return EMB_EDEADLK   -- §3.5
    embk_wait_prepare(&m.waiters, OBJECT, m)             -- enqueue by effective priority, INTEND_TO_BLOCK
    if m.protocol == INHERIT: embk_prio_propagate(m.owner)              -- raise the owner and its chain
  object_unlock(key)
  result = embk_wait_commit(timeout, &data)              -- sections 2 and 3
  if result == SATISFIED:  return (data & OWNERDEAD) ? EMB_EOWNERDEAD : EMB_OK     -- self owns m by hand-off
  else:                    return map(result)            -- the owner's priority was already recomputed by the wake path (§3.3)
```

The walk in section 1 is what makes the lock's contended cost O(waiters + depth). The deadlock check and the propagation share one traversal of the owner chain.

### 3.3 Unlock and waiter removal

```
unlock(m):
  key = object_lock(m)
    if m.owner != self: unlock; return EMB_EPERM                           -- KRN-SYNC-008, checked builds fault
    if m.count > 1: m.count -= 1; unlock; return EMB_OK                    -- recursive
    remove m from self.owned
    w = embk_wait_first(&m.waiters)
    if w != NULL:
        m.owner = w; m.count = 1; append m to w.owned                      -- hand-off: no barging (KRN-WAIT-012)
        embk_wait_wake(w, SATISFIED, m.flags.INCONSISTENT ? OWNERDEAD : 0)
        embk_prio_propagate(w)                                             -- w may inherit from the remaining waiters
    else:
        m.owner = NULL; m.count = 0
    m.flags.INCONSISTENT = 0                                               -- §3.6: cleared by the unlock of the recovering owner
    embk_prio_propagate(self)                                              -- KRN-SYNC-010, over the still-owned list
  object_unlock(key)
  reschedule_if_needed()                                                   -- P2
```

A waiter that leaves the queue for any other reason (timeout, cancellation, destroy with `ABORT_WAITERS`, a priority change that reorders the queue) changes the owner's contribution. The mutex's wait queue carries the `EMBK_WAIT_INHERIT` flag; SPEC-004's `wake()` and `requeue()` call `embk_mutex_on_waiters_changed(m)` after the dequeue, which runs `embk_prio_propagate(m.owner)` in the same section (KRN-SYNC-014). Nothing is deferred to the waiter's resumption; this is the behavior Zephyr documents as a limitation and FreeRTOS gets right only for one held mutex (R-001 §5).

### 3.4 Priority ceiling (ADR-030)

- **Static ceiling.** A mutex declared in the system description with `protocol: ceiling` and a `users:` list gets `ceiling = max base priority of its users`, emitted by the generator into the storage initializer. Locking raises `eff_prio` to the ceiling immediately. In checked builds a locker that is not a declared user, or whose base priority exceeds the ceiling, faults (`EMB_FAULT_API_OWNER`); release builds return `EMB_EPERM`. With every user declared the mutex can never have a waiter, so `CONFIG_EMB_MUTEX_CEILING_STATIC_ONLY=y` (default on the tiny profile) builds ceiling mutexes without a wait queue.
- **Dynamic ceiling.** `attr.ceiling != 0` on a mutex created at run time; same locking rule, checked against the caller's base priority only. Such a mutex keeps a wait queue because an undeclared caller of lower priority may contend.
- A ceiling mutex never inherits; a thread may own ceiling and inheritance mutexes at once, and §2.1 combines them.
- Thread-level only: sharing data with an ISR uses a critical section (SPEC-002 §4) or the zero-latency class; RTIC-style interrupt ceilings are out of scope.

### 3.5 Deadlock detection

During the walk of §3.2, reaching `self` means the caller already blocks the chain it wants to join: `EMB_EDEADLK` in release builds, kernel fault in checked builds (KRN-SYNC-015). Detection costs nothing extra because the walk visits the chain anyway. It covers `INHERIT` mutexes; a cycle through `NONE` mutexes is not detected (there is no owner chain to walk), which the documentation states. Timeouts do not disable detection: a cycle is an error whether or not it would time out.

### 3.6 Owner termination

`CONFIG_EMB_MUTEX_OWNER_DEATH` selects, for the whole build (KRN-SYNC-011, answers 08 Q5):

| Value | Thread exits or is terminated while `owned` is not empty |
|---|---|
| `FAULT` (default) | kernel fault `EMB_FAULT_API_LIFECYCLE` in checked builds; in release builds the mutexes are released as below but the fault is still recorded in the crash record as a non-fatal event |
| `RELEASE` | each owned mutex, in acquisition order: if it has a waiter, hand it over with the `OWNERDEAD` flag (the waiter's `lock` returns `EMB_EOWNERDEAD` and owns the mutex); if not, set `INCONSISTENT`, so the next `lock` returns `EMB_EOWNERDEAD` |

A thread whose `lock` returned `EMB_EOWNERDEAD` owns the mutex and must repair the protected state. The `INCONSISTENT` flag is cleared by that thread's `unlock`, or earlier by `emb_mutex_mark_consistent()`. Unlike POSIX robust mutexes there is no `ENOTRECOVERABLE` state: the kernel does not know whether the state was repaired, and a permanently dead mutex helps nobody on an MCU; the POSIX layer may emulate the stricter rule.

### 3.7 Destroy

KRN-OBJ-001 applies. A mutex that is owned cannot be destroyed by anyone but its owner (`EMB_EBUSY`, misuse); with `ABORT_WAITERS` the waiters wake with `EMB_EDESTROYED` and the owner's contribution is recomputed.

## 4. Semaphore

### 4.1 Interface

```c
typedef struct emb_sem_attr {
    const char       *name;
    emb_sem_count_t   initial;     /* default 0 */
    emb_sem_count_t   max;         /* default EMB_SEM_COUNT_MAX; 1 for a binary semaphore */
    uint8_t           flags;       /* EMB_SEM_SATURATE | EMB_OBJ_ABORT_WAITERS | EMB_OBJ_FIFO */
} emb_sem_attr_t;

void         emb_sem_attr_default(emb_sem_attr_t *out_attr);
void         emb_sem_attr_binary(emb_sem_attr_t *out_attr, bool initially_available);   /* max 1, SATURATE */
emb_status_t emb_sem_init(emb_sem_storage_t *storage, const emb_sem_attr_t *attr, emb_sem_t *out);
emb_status_t emb_sem_destroy(emb_sem_t s);
emb_status_t emb_sem_take(emb_sem_t s, emb_timeout_t timeout);        /* @ctx thread      @blocks timeout  O(1) fast path, O(waiters) enqueue */
emb_status_t emb_sem_take_until(emb_sem_t s, emb_instant_t deadline);
emb_status_t emb_sem_give(emb_sem_t s);                               /* @ctx thread isr  @blocks no       O(1) */
emb_sem_count_t emb_sem_count(emb_sem_t s);                           /* @ctx thread isr  O(1); a snapshot */
emb_status_t emb_sem_bind_notify(emb_sem_t s, emb_thread_t t, uint8_t bit);     /* ADR-027; SPEC-006 */
```

`emb_sem_count_t` is `uint32_t`; `CONFIG_EMB_SEM_COUNT_BITS` (8, 16, 32; default 32, tiny profile 8) narrows it and `EMB_SEM_COUNT_MAX` follows.

### 4.2 Semantics

- `take`: if `count > 0`, decrement and return `EMB_OK` (section 1); else the protocol. The unit arrives by hand-off (`wake_data = 1`), the count is untouched.
- `give`: if a waiter exists, hand the unit to the highest (or first, with `EMB_OBJ_FIFO`) waiter: the count stays as it is and no binding bit is set (SPEC-004 §6.3). Otherwise `count += 1`, bounded by `max`: at the bound, `EMB_EOVERFLOW` unless `EMB_SEM_SATURATE`, which returns `EMB_OK` and leaves the count unchanged (binary-semaphore signal semantics). A transition from 0 to 1 signals the binding (ADR-027).
- There is no `give_all` and no `reset`: broadcast is what event flags are for, and a reset with waiters is exactly the hazard ADR-020 refuses.
- A semaphore has no owner and therefore no inheritance; documentation says so where a mutex is the right tool.

## 5. Event flags

### 5.1 Interface

```c
typedef uint32_t emb_event_bits_t;                       /* CONFIG_EMB_EVENT_BITS: 8, 16, 32; default 32, tiny 8 */

typedef struct emb_event_attr {
    const char       *name;
    emb_event_bits_t  initial;
    uint8_t           flags;       /* EMB_OBJ_ABORT_WAITERS | EMB_OBJ_FIFO */
} emb_event_attr_t;

emb_status_t emb_event_init(emb_event_storage_t *storage, const emb_event_attr_t *attr, emb_event_t *out);
emb_status_t emb_event_destroy(emb_event_t e);
emb_status_t emb_event_set(emb_event_t e, emb_event_bits_t bits);                 /* @ctx thread isr  @blocks no  O(waiters) */
emb_status_t emb_event_clear(emb_event_t e, emb_event_bits_t bits);               /* @ctx thread isr  @blocks no  O(1) */
emb_event_bits_t emb_event_get(emb_event_t e);                                    /* @ctx thread isr  O(1); a snapshot */
emb_status_t emb_event_wait(emb_event_t e, emb_event_bits_t mask, uint8_t mode,   /* EMB_EVENT_ANY | EMB_EVENT_ALL, optionally | EMB_EVENT_CLEAR */
                            emb_timeout_t timeout, emb_event_bits_t *out_satisfied);  /* @ctx thread  @blocks timeout */
emb_status_t emb_event_wait_until(emb_event_t e, emb_event_bits_t mask, uint8_t mode, emb_instant_t deadline, emb_event_bits_t *out_satisfied);
emb_status_t emb_event_bind_notify(emb_event_t e, emb_thread_t t, uint8_t bit);
```

### 5.2 Semantics

- Each waiter records its own `mask` and `mode` in its wait context (the TCB's `wake_data` word and one byte); a waiter is **satisfied** when `(bits & mask) != 0` for `ANY` or `(bits & mask) == mask` for `ALL`.
- `wait`: evaluate in section 1; if satisfied, apply `CLEAR` (clear `bits & mask`) and return the satisfied bits; else the protocol. On wake, `out_satisfied` holds the bits delivered by hand-off; the waiter does not re-read the group (KRN-WAIT-012).
- `set`: `bits |= set_bits`; then walk the queue in order and, for every waiter now satisfied, hand it `bits & mask` and wake it; if that waiter asked for `CLEAR`, clear `bits & mask` **before** evaluating the next waiter. The walk is O(waiters) in one section (the bound is the number of waiters on this group, stated); satisfied waiters are readied as one batch with one reschedule (KRN-WAIT-013). If after the walk `bits` is nonzero and changed, the binding is signalled.
- `clear`: `bits &= ~clear_bits`; never wakes anyone.
- Deterministic consumption: because `CLEAR` is applied in queue order during `set`, two `ALL` waiters on overlapping bits with `CLEAR` see a defined outcome (the higher-priority one consumes; the other keeps waiting), unlike kernels that let woken waiters race for the bits.
- Event flags are shared objects; a per-thread notification (SPEC-006) is the cheaper tool when exactly one thread consumes.

## 6. Condition variable

```c
emb_status_t emb_cond_init(emb_cond_storage_t *storage, const emb_cond_attr_t *attr, emb_cond_t *out);
emb_status_t emb_cond_destroy(emb_cond_t c);
emb_status_t emb_cond_wait(emb_cond_t c, emb_mutex_t m, emb_timeout_t timeout);        /* @ctx thread  @blocks timeout, then the relock */
emb_status_t emb_cond_wait_until(emb_cond_t c, emb_mutex_t m, emb_instant_t deadline);
emb_status_t emb_cond_signal(emb_cond_t c);        /* @ctx thread  @blocks no  O(1) */
emb_status_t emb_cond_broadcast(emb_cond_t c);     /* @ctx thread  @blocks no  O(waiters) */
```

- `wait` requires the caller to own `m` with recursion count 1 (`EMB_EPERM` if not owner, `EMB_ESTATE` if count > 1). In section 1 it enqueues on `c`, then performs a full `unlock(m)` (§3.3, including hand-off to a mutex waiter) before releasing the object lock; the enqueue-then-unlock order makes the release and the wait atomic with respect to any `signal`.
- On any wake result the caller re-locks `m` with `EMB_WAIT_FOREVER` before returning, so the caller always owns `m` on return, including after `EMB_ETIMEDOUT`, `EMB_ECANCELED`, and `EMB_EDESTROYED`. The relock may block beyond the timeout; this is documented, and the relock is itself an inheritance-aware mutex lock.
- `signal` wakes the highest waiter (hand-off word 0); `broadcast` wakes all in queue order with one reschedule. Both may be called with or without the mutex held; calling them while holding it avoids the "woken, then blocks on the mutex" bounce, and the documentation recommends it. Neither is ISR-safe: a condition variable pairs with a mutex, and a mutex is never touched from an ISR.
- The kernel never generates spurious wakeups (a wake without a signal or broadcast); callers still re-check their predicate in a loop, because another thread may change it between the signal and the relock.
- `cond_wait` on a `CEILING` or `NONE` mutex is allowed; the relock follows that mutex's protocol.

## 7. Barrier (`CONFIG_EMB_BARRIER`)

```c
emb_status_t emb_barrier_init(emb_barrier_storage_t *storage, const emb_barrier_attr_t *attr /* .count */, emb_barrier_t *out);
emb_status_t emb_barrier_wait(emb_barrier_t b, emb_timeout_t timeout, bool *out_is_serial);   /* @ctx thread  @blocks timeout */
```

- The barrier counts arrivals; the arrival that makes `count` wakes every waiter (queue order, one reschedule), resets the arrival count, increments the barrier generation, and returns with `*out_is_serial = true`; the others return `EMB_OK` with `false`.
- A waiter that times out or is canceled leaves the barrier; the arrival count decrements; the others keep waiting. The barrier's queue is `FIFO` by default (`EMB_OBJ_FIFO` cleared selects priority order).
- Destroy with waiters follows KRN-OBJ-001.

## 8. Spinlocks (SMP builds)

Defined in SPEC-002 §4.3: `emb_spin_lock(lock, &key)` masks interrupts on the current CPU and then spins; `emb_spin_unlock(lock, key)` releases and restores. Rules, enforced by the spinlock validator in checked SMP builds (ADR-035; Zephyr `SPIN_VALIDATE`, R-001 §5):

- Never held across a blocking call, a scheduler lock operation that may reschedule, or a preemption point; always acquired with interrupts masked (the API does both).
- Not recursive: locking a spinlock the CPU already holds is a kernel fault.
- Lock order per subsystem is declared in the SMP specification; the validator records each CPU's held set with the lock's address and the acquiring program counter and faults on an order inversion.
- On uniprocessor builds the type is empty and the functions compile to `emb_irq_lock()` / `emb_irq_unlock()` (KRN-SMP-002).

## 9. Atomics

`<emb/atomic.h>` wraps the compiler builtins (SPEC-001 §10): `emb_atomic32_t` with load, store, add, sub, or, and, xor, exchange, compare-exchange, each in relaxed, acquire, release, and acq_rel forms, plus `emb_atomic_fence()`. Widths: 32-bit on 32-bit targets; on AVR only `emb_atomic8_t` is a hardware atomic and `emb_atomic32_t` is implemented with a critical section (09 §6), which the documentation states; no 64-bit atomic type exists (KRN-TIM-016). Pointer-sized `emb_atomic_ptr_t` is provided where the pointer is 32 bits or less. The kernel's own use of atomics is confined to the wait-state compare-and-set (SPEC-004 §5) and the SMP paths.

## 10. Priority changes

`emb_thread_set_priority(t, p)` sets `base_prio` and runs `embk_prio_propagate(t)`: inherited and ceiling contributions are preserved (a boosted owner stays boosted until it unlocks, KRN-SYNC-032), and if `t` waits on an `INHERIT` mutex the owner's contribution follows in the same operation. Lowering the base priority of a thread that is not boosted takes effect at once; a RUNNING thread that is no longer the highest is preempted at P2.

## 11. Misuse and checked-build diagnostics

| Condition | Checked build | Release build |
|---|---|---|
| `unlock` by a non-owner | fault `EMB_FAULT_API_OWNER` | `EMB_EPERM` |
| Non-recursive `lock` by the owner | fault `EMB_FAULT_API_OWNER` | `EMB_EDEADLK` |
| Deadlock detected in the walk | fault `EMB_FAULT_API_OWNER` | `EMB_EDEADLK` |
| Inheritance depth exceeded | fault `EMB_FAULT_API_LIFECYCLE` (the lock graph is deeper than the declared design parameter) | truncated inheritance, trace event |
| Ceiling violation (undeclared user or base above ceiling) | fault `EMB_FAULT_API_OWNER` | `EMB_EPERM` |
| Thread exit owning mutexes | per `CONFIG_EMB_MUTEX_OWNER_DEATH` (§3.6) | same |
| `cond_wait` without owning the mutex / with recursion count > 1 | fault `EMB_FAULT_API_OWNER` / `EMB_ESTATE` | `EMB_EPERM` / `EMB_ESTATE` |
| Mutex, condition variable, or barrier operation from an ISR | fault `EMB_FAULT_API_CONTEXT` | `EMB_EPERM` |
| `give` or `set` from a kernel-independent interrupt | undetectable by design (SPEC-002 §3) | undefined |
| Semaphore overflow without `SATURATE` | runtime condition | `EMB_EOVERFLOW` |
| Destroy with waiters, owned mutex destroyed by a non-owner | fault `EMB_FAULT_API_LIFECYCLE` | `EMB_EBUSY` |
| Owner chain or owned list inconsistent (internal) | fault `EMB_FAULT_KERNEL_INVARIANT` | undefined |

Checked builds additionally verify after every operation that touches priorities: `eff_prio(t) == effective(t)` for the threads involved (SPEC-005 §2.1), and that a mutex's owner is not in its own wait queue.

## 12. Observability

Trace events: `mutex_lock(thread, mutex, contended)`, `mutex_unlock(thread, mutex, handed_to)`, `prio_inherit(thread, from, to, cause: lock | unlock | timeout | cancel | set_priority | ceiling)`, `pi_depth_exceeded(thread, mutex)`, `deadlock_detected(thread, mutex)`, `sem_give(sem, handed)`, `event_set(event, bits, woken)`, `cond_signal(cond, woken)`. Statistics (optional): per object, contention count and maximum queue length (KRN-WAIT-020); per thread, the maximum inherited boost and the longest time spent boosted; per CPU, inheritance walk depth maximum.

## 13. Tiny profile

With `CONFIG_EMB_SCHED_TABLE` (ADR-036): wait queues are bitmaps, so `head waiter` is one count-leading-zeros and the walk's requeue is a no-op; `CONFIG_EMB_MUTEX_INHERIT` (default y) costs the owned list (two pointers per mutex, one per thread); `CONFIG_EMB_MUTEX_CEILING_STATIC_ONLY=y` removes the wait queue from ceiling mutexes; `EMB_SEM_COUNT_BITS=8`, `EMB_EVENT_BITS=8`; condition variables and barriers are off by default. Everything that is on passes the same conformance tests.

## 14. SMP note (FUTURE, interface fixed now)

Inheritance walks cross objects, so the per-object lock domain of SPEC-004 §8 is not enough on SMP: a hop needs the next mutex's queue and the scheduler lock. The SMP specification chooses between (a) one `mutex_lock` spinlock protecting every mutex's wait queue and owner field, ordered between object and scheduler locks (Zephyr's choice, simple, a global point of contention), and (b) hand-over-hand locking of mutexes along the chain in address order with the walk restarted on order violation (RTEMS's path acquisition). The interface above is the same under either; the uniprocessor implementation is unaffected. Default recommendation for the SMP spec: (a) first, measured, (b) only if contention shows (ADR-012 logic).

## 15. Reference model

`tools/model/` gains the mutex as a second object type over `_block_section`: `Lock(m, timeout)` and `Unlock(m)` ops, owned lists, effective priority recomputation and propagation with the depth bound, deadlock detection, hand-off on unlock with `OWNERDEAD`, the `RELEASE` owner-death policy on `Exit`, ceiling mutexes, and recursion. New invariants: `eff_prio == effective(base, owned)` for every thread; an `INHERIT` mutex's owner has effective priority at least that of every waiter on it; an owner is never in its own queue; the owned lists and owner fields agree; recursion count is positive exactly when owned. New scenarios: the classic three-thread inversion, a two-level nested chain with timeout disinheritance, deadlock between two lockers in both orders, owner death with a waiter and without, ceiling lock and unlock, a waiter's priority change propagating up and down the chain, cancellation of a waiter. Exhaustive exploration at section granularity as in SPEC-004 §13.

## 16. Requirements

Restated: KRN-SYNC-001 to 007 (v0.1 §8.6) and 008 to 017 (03 §5.2). New: KRN-SYNC-018 to 038, in `docs/requirements/KRN-SYNC.md`.

## 17. Decisions taken at acceptance (2026-10-07)

1. One mutex type with three protocols (`INHERIT` default, `CEILING`, `NONE`) and an optional `RECURSIVE` flag; no separate recursive-mutex type.
2. Both raises and lowerings of inherited priority propagate transitively, immediately, in the operation that caused them; nothing is deferred to the waiter's resumption. Depth bound `CONFIG_EMB_PI_MAX_DEPTH = 8`.
3. `EMB_EDEADLK = -32` is defined; self-relock of a non-recursive mutex and a detected cycle both return it. Detection is always on for `INHERIT` mutexes.
4. Owner death: `FAULT` by default; `RELEASE` hands mutexes over with `EMB_EOWNERDEAD` or marks them inconsistent; the inconsistent mark is cleared by the recovering owner's `unlock` or `emb_mutex_mark_consistent()`; no `ENOTRECOVERABLE` state. Answers 08 Q5 definitively.
5. Unlock hands the mutex to the highest waiter; the new owner inherits from the remaining waiters at once. No barging.
6. Semaphores: one type, `max` bound, `EMB_EOVERFLOW` at the bound unless `SATURATE`; `emb_sem_attr_binary()` helper; no `give_all`, no `reset`.
7. Event flags consume `CLEAR` bits in queue order during `set`, so overlapping waiters have a deterministic outcome; `set` is O(waiters) in one section.
8. Condition variables always relock before returning, on every result; `signal` and `broadcast` are thread-only.
9. Barriers are optional, FIFO by default, and survive a waiter's timeout.
10. Temporal-protection demotion lowers a separate `sched_base`; inheritance still lifts a demoted owner.
11. The reference model is extended with the mutex in this work item; condition variables, events, and barriers are specified here and modeled when their conformance tests are written (M2).
