# 03 - Kernel Architecture

**Status:** v0.1 LOCKED sections are summarized, not repeated. New material is PROPOSED unless marked otherwise. Requirement identifiers continue v0.1 numbering within existing groups and start fresh in new groups.

---

## 1. Execution model

### 1.1 Thread states (LOCKED, clarified; lifecycle specified in `docs/specs/SPEC-008-thread-lifecycle.md`)

The four states remain READY, RUNNING, BLOCKED, TERMINATED. v0.2 places two lifecycle situations v0.1 left implicit into the same model as **wait reasons**:

| Situation | State | Wait reason | Woken by |
|---|---|---|---|
| Created, not yet started | BLOCKED | `START` | `emb_thread_start()` |
| Suspended by another thread | BLOCKED | `SUSPEND` | `emb_thread_resume()` |
| Sleeping | BLOCKED | `SLEEP` | timeout |
| Waiting on object | BLOCKED | `OBJECT` | signal, timeout, cancel, destroy |
| Joining another thread | BLOCKED | `JOIN` | target termination |
| Waiting for notification bits | BLOCKED | `NOTIFY` | bits set |

A thread blocked with reason `SUSPEND` that also had an object wait pending resumes its object wait on resume; the timeout keeps running during suspension. This is specified to avoid the classic ambiguity of "suspend while waiting".

```
                 create
                   |
                   v
            BLOCKED(START)
                   |  start
                   v
   +----------> READY <-----------------------------+
   |              |  dispatch                        |
   |              v                                  |
   |           RUNNING ---- wait/sleep/suspend ----> BLOCKED(reason)
   |              |                                  |
   +-- preempt ---+                                  |
   |   / yield                                       |
   +------------------------- wake -------------------+
                  |
                  | return / exit / cancel point
                  v
             TERMINATED --- join reaps / object destroy --- (object storage released by owner)
```

**KRN-THR-009** Thread start, suspend, and resume shall be expressed through the common wait mechanism as wait reasons, not as additional scheduler states.
**KRN-THR-010** A suspended thread shall retain any pending object wait and its deadline; resume shall re-enter that wait without losing the thread's wait-queue position relative to later waiters.
**KRN-THR-011** Thread cancellation shall be cooperative: a cancel request is delivered as a wake result at the next blocking call or explicit cancellation point; threads are never asynchronously killed.
**KRN-THR-012** Join shall be supported for joinable threads; a detached thread's object storage becomes reusable immediately on termination according to its storage policy.
**KRN-THR-013** Thread-local storage slots shall be available as a compile-time option with a fixed per-thread slot count.
**KRN-THR-014** The kernel, ports, and drivers shall not use compiler thread-local storage (`_Thread_local`, `__thread`); EmbCC compiles it to one shared instance on embedded targets (09 §3). Kernel TLS slots are the only per-thread storage mechanism.

### 1.2 Execution contexts (LOCKED)

See 02 §6. ISR context is not a thread. Kernel-independent interrupts above the kernel masking level are permitted where hardware allows and are outside the kernel's guarantees.

### 1.3 Memory ordering model (PROPOSED, new)

**KRN-MM-001** Every kernel operation that acquires (lock, take, receive, wait-success) shall have at least acquire semantics; every operation that releases (unlock, give, send, signal) shall have at least release semantics, on uniprocessor and multiprocessor builds alike.
**KRN-MM-002** Context switch shall be a full barrier.
**KRN-MM-003** Data shared between an ISR and a thread without a kernel primitive is the application's responsibility; the kernel documents `emb_atomic_*` and `emb_barrier_*` for that purpose.
**KRN-MM-004** The kernel shall never rely on interrupt masking alone for inter-CPU ordering.

Rationale: code that works on AVR because interrupt masking implies ordering must not break when first compiled for SMP or with aggressive optimization.

## 2. Scheduler

### 2.1 Fixed-priority class (LOCKED)

All of v0.1 §4 and §5 stand: highest effective priority wins, FIFO among equals, optional round-robin, preemption at the next legal point, no aging, scheduler lock is nestable and non-masking, bitmap plus per-priority FIFO queues, RUNNING tracked per CPU.

Additions:

**KRN-SCH-036** Priority count shall be configurable from 4 to 256; the ready bitmap shall be one word for up to the word width and two-level above that.
**KRN-SCH-037** Time slicing shall apply only to threads at or below a configured threshold priority level (`CONFIG_EMB_TIMESLICE_PRIORITY`), with one configurable quantum, so that round robin can be enabled for background levels only without a per-level table (revised per R-003 §3.1; answers 08 Q8).
**KRN-SCH-041** A thread preempted by a higher-priority thread shall be placed ahead of its equal-priority peers when it becomes eligible again, because its quantum is unspent; a thread that yields or exhausts its quantum shall be placed behind them.
**KRN-SCH-042** The tiny profile may implement the fixed-priority class as a priority-indexed thread table with unique priorities and bitmap wait sets (ADR-036); the public API and the conformance suite are unchanged within the tiny profile's documented restrictions.
**KRN-SCH-043** The idle thread shall be optional; when absent, the scheduler shall idle inline with interrupts enabled at the preemption points of SPEC-002.

### 2.2 Scheduling classes (PROPOSED)

The scheduler is organized as an ordered list of classes. At a decision point it asks the highest class with eligible work for its pick. Class membership is a thread attribute.

```
  class order (highest first):
    TIME_TABLE   -- only threads of the partition owning the current window are eligible;
                    acts as an eligibility filter on the classes below, not a picker
    DEADLINE     -- earliest absolute deadline first, with budget (CBS) enforcement
    FIXED_PRIORITY
    IDLE
```

- **FIXED_PRIORITY** is mandatory and is the only class in 1.0.
- **DEADLINE** (earliest deadline first with constant bandwidth server) is for workloads whose structure is periodic with known execution budgets, where utilization-based admission is wanted. FUTURE, but the ready-structure interface must not assume a bitmap.
- **TIME_TABLE** is a static cyclic schedule of windows (a major frame) assigning CPU time to partitions. It is the primitive certifiers recognize for mixed-criticality. FUTURE. It is implemented as a timer-driven eligibility mask over partitions.
- **IDLE** is the per-CPU idle thread.

**KRN-SCH-038** The ready structure shall be reached through a class interface (`enqueue`, `dequeue`, `pick`, `requeue_on_priority_change`) so that additional classes do not modify fixed-priority code.
**KRN-SCH-039** The four-state thread model shall be identical across classes.
**KRN-SCH-040** When no optional class is configured, the class dispatch shall compile to a direct call into the fixed-priority implementation with zero overhead.

### 2.3 Temporal protection (PROPOSED detail; model fixed by ADR-029, accepted)

Fixed priority alone cannot stop a thread that runs longer than designed. v0.2 adds budgets without changing scheduling order among threads that are within budget. R-001 §1 and R-002 §5 showed two proven models beyond "capacity per period": the sporadic server (POSIX `SCHED_SPORADIC` in NuttX, seL4 MCS scheduling contexts) for threads, and the sliding-window share with idle-time sharing (QNX adaptive partitioning) for partitions. Both are adopted (ADR-029).

A **thread budget** is a sporadic server `(capacity, period, overrun_policy)`. The kernel charges execution time to the running thread's budget on every context switch and timer event; each consumed slice is replenished one period after it started, through a bounded replenishment list (`CONFIG_EMB_BUDGET_REFILLS`, default 2; overflow merges entries). When capacity is exhausted before replenishment:

| Overrun policy | Effect |
|---|---|
| `NOTIFY` | Set a notification bit on the supervisor or owner; keep running |
| `DEMOTE` | Lower effective priority to a configured background level until replenishment |
| `SUSPEND` | Block with reason `BUDGET` until replenishment |
| `FAULT` | Raise a partition fault |

A **partition share** is `(share_percent, window, critical_capacity)` measured over a sliding window (default 100 ms, a ring of sub-windows). When total demand exceeds the CPU, a partition over its share is eligible only when no partition under its share has runnable threads; this is an eligibility filter in the class order next to TIME_TABLE. When the system is not overloaded, ordering is pure fixed priority, so a share costs nothing in the common case. A thread marked `EMB_THREAD_CRITICAL` (typically one woken by an interrupt) may exceed its partition's share up to `critical_capacity` per window; exhausting that is a partition fault. **Budget donation**, where a server thread runs on the budget of the client it serves across a Port, is FUTURE with Ports.

**Deadline monitoring** is a lighter mechanism: a thread declares `period` and `relative_deadline`; the kernel records a deadline-miss event (trace plus optional notification) when a job completes late. It does not alter scheduling.

**KRN-TP-001** Per-thread CPU time accounting shall be available as a compile-time option and shall be the basis of budgets and statistics.
**KRN-TP-002** Budget enforcement shall be a compile-time option; when disabled it shall add no fields to the TCB and no code to the switch path.
**KRN-TP-003** Budget overrun policies NOTIFY, DEMOTE, SUSPEND, FAULT shall be supported; DEMOTE and SUSPEND shall be reversed exactly at replenishment.
**KRN-TP-004** Deadline-miss detection shall be observable through trace and optionally through a notification.
**KRN-TP-005** Budget enforcement shall never change the ordering among threads that are within budget.
**KRN-TP-006** Thread budgets shall replenish consumed time one period after its consumption started, through a bounded replenishment list whose overflow merges entries and never grants more than `capacity` per `period`.
**KRN-TP-007** Partition shares shall be enforced over a sliding window and only while total demand exceeds the CPU; unused share shall be available to other partitions.
**KRN-TP-008** A critical thread may exceed its partition's share up to the partition's critical capacity per window; exhausting it shall be a partition fault.
**KRN-TP-009** The `FAULT` overrun policy shall deliver the consumed time in the fault record.

### 2.4 Per-CPU state (LOCKED, extended)

```
cpu[n]:
  current_thread
  idle_thread
  scheduler_lock_depth
  irq_nesting_depth
  reschedule_pending
  class_state[]           (per class, optional)
  current_window          (TIME_TABLE, optional)
  last_switch_timestamp   (accounting, optional)
  local_timer_deadline    (tickless)
```

## 3. Wait and wake protocol

**PLANNED in v0.1; PROPOSED design here.** This is the most important internal contract in the kernel, because every blocking primitive is a thin layer over it.

The protocol is realized by a three-state wait flag (`READY`, `INTEND_TO_BLOCK`, `BLOCKED`) with a per-thread wait generation and a superseded bit on in-flight timeouts (ADR-026), so that no interrupt-masked section spans the window between deciding to block and being blocked. R-001 §4.2 compares the alternatives used by other kernels. The full protocol is `docs/specs/SPEC-004-wait-and-wake-protocol.md`; the requirements are `docs/requirements/KRN-WAIT.md`.

### 3.1 Wait object

```
wait_queue:
  waiters          intrusive list of threads, ordered by policy
  policy           PRIORITY_FIFO (default) | FIFO
  owner            optional (mutex), for inheritance
```

Each blocked thread records: `wait_object`, `wait_reason`, `deadline` (or none), `wake_result`, `wait_node`, `timeout_node`.

### 3.2 Wake results

`emb_wait_result`: `SATISFIED`, `TIMEOUT`, `CANCELED`, `DESTROYED`, `INTERRUPTED` (reserved for signal-like delivery), `BUDGET` (reserved).

### 3.3 Wait ordering

**KRN-WAIT-001** Default wake order shall be highest effective priority first, FIFO among equal priority.
**KRN-WAIT-002** Objects may be created with pure FIFO wake order where the application requires fairness over priority.
**KRN-WAIT-003** A waiter whose effective priority changes while blocked shall be repositioned in a PRIORITY_FIFO wait queue.

### 3.4 The wake race

The protocol must be correct against the race between a wake, a timeout expiry, and a cancel arriving at the same time on different CPUs or from nested interrupts.

**KRN-WAIT-004** Exactly one wake source shall win for a given blocked thread; the losers shall observe that the thread is no longer waiting and shall not modify it.
**KRN-WAIT-005** The winning wake source shall set the wake result before making the thread READY.
**KRN-WAIT-006** Removal from the wait queue and from the timeout structure shall be atomic with respect to each other, as seen by any other wake source.
**KRN-WAIT-007** The protocol shall be expressed in the executable reference model and verified by randomized and, where practical, exhaustive state exploration before implementation (see 05 §4).
**KRN-WAIT-008** The wait state shall be a three-state flag changed by compare-and-swap on cores that provide it and by a bounded critical section otherwise; no lock or masked section shall be held from the decision to block until the switch (ADR-026).
**KRN-WAIT-009** Every wait shall carry a generation; a timeout, cancel, or destroy that observes a different generation shall do nothing. On SMP a timeout whose handler is running on another CPU shall be marked superseded and its cancellation retried.

### 3.5 Blocking entry sequence (normative skeleton)

```
block_on(object, reason, timeout):
  enter critical
    if condition already satisfied: leave critical; return SATISFIED
    if timeout == NO_WAIT:          leave critical; return TIMEOUT
    if in ISR or scheduler locked:  kernel fault (checked) / EMB_EINVAL (release)
    thread.wait = (object, reason)
    enqueue wait_node by policy
    if finite timeout: arm timeout_node at now + timeout
    thread.state = BLOCKED
    request reschedule
  leave critical                      -- switch happens here
  -- resumes after wake --
  return thread.wake_result
```

## 4. Time

### 4.1 Representation (PROPOSED)

**KRN-TIM-008** Kernel monotonic time shall be a 64-bit tick count in the `base` and larger profiles; the `tiny` profile may select a 32-bit count with wrap-safe comparison.
**KRN-TIM-009** The tick unit shall be the configuration constant `CONFIG_EMB_TICK_NS`; in tickless mode the achievable resolution is bounded by the hardware timer period, and the unit of the API stays the configured one (SPEC-003 §4).
**KRN-TIM-010** Public time types shall be distinct: `emb_instant_t` (absolute), `emb_duration_t` (relative), `emb_timeout_t` (relative or sentinel `EMB_NO_WAIT` / `EMB_WAIT_FOREVER`). Mixing them is a compile error where the language allows.
**KRN-TIM-011** Absolute-deadline variants shall exist for sleep and for every blocking operation (`*_until`), so that periodic work does not accumulate drift.
**KRN-TIM-012** A high-resolution cycle counter API shall exist for measurement where hardware provides one; it is not the scheduling clock.
**KRN-TIM-016** 64-bit time values shall be read and written under a critical section or a sequence lock; the kernel shall not depend on 64-bit atomic loads, stores, or read-modify-write operations, which the 32-bit targets do not provide (09 §6).
**KRN-TIM-036** On tickless targets the architecture timer shall report its arming latency, measured at initialization or declared by the hardware description, and the kernel shall subtract it when programming deadlines so that sleeps and timeouts do not wake systematically late (R-003 §3.2; RIOT `adjust_set`). Pending amendment to SPEC-003 and `requirements/KRN-TIM.md`.

### 4.2 Timeout structure (PROPOSED default, ADR-010)

Default is a sorted intrusive list ordered by absolute deadline, wrap-safe in the 32-bit profile: O(n) insert, O(1) removal, O(1) expiry check, zero memory beyond the intrusive node. The interface permits a timing wheel for targets with hundreds of timers. The decision per profile is made by measurement, not opinion.

### 4.3 Software timers (PROPOSED)

**KRN-TIM-013** Software timer callbacks shall execute by default on the system work queue in thread context.
**KRN-TIM-014** A timer may be created with `ISR_CONTEXT` only when its callback is declared ISR-safe; such callbacks shall be bounded and shall not block.
**KRN-TIM-015** One-shot and periodic timers shall be supported; a periodic timer's next expiry is computed from the previous expiry, not from callback completion.

### 4.4 Tickless (LOCKED)

The timer subsystem exposes `next_deadline()`; the power core uses it. Both the tick and the tickless path must pass the same timeout and wraparound tests.

## 5. Synchronization

### 5.1 Primitive set

| Primitive | 1.0 | ISR-safe operations | Notes |
|---|---|---|---|
| Mutex | yes | none | Ownership, priority inheritance, optional recursion, optional ceiling |
| Binary and counting semaphore | yes | give | |
| Event flags | yes | set | Wait any / wait all, clear-on-exit option |
| Notification (per thread) | yes | set | See §6.1 |
| Condition variable | yes | none | Pairs with mutex |
| Barrier | optional | none | |
| Spinlock | SMP only | lock/unlock with IRQ save | Never blocks; never held across a blocking call |
| Reader-writer lock | FUTURE | none | Writer preference, inheritance to writer only |
| Atomics and barriers | yes | yes | Wrapped compiler builtins |

### 5.2 Mutex semantics (PROPOSED details; full specification in `docs/specs/SPEC-005-synchronization.md`, requirements in `docs/requirements/KRN-SYNC.md`)

**KRN-SYNC-008** Unlock by a non-owner shall be a kernel fault in checked builds and `EMB_EPERM` in release builds.
**KRN-SYNC-009** Priority inheritance shall be transitive through chains of mutexes with a configurable maximum depth; exceeding it is a kernel fault in checked builds.
**KRN-SYNC-010** On unlock, the owner's effective priority shall be recomputed as the maximum of its base priority and the highest priority waiter across all mutexes it still owns.
**KRN-SYNC-011** When a mutex owner terminates while owning mutexes, the behavior is a configuration choice: fault (default) or release-with-`EMB_EOWNERDEAD` delivered to the next owner.
**KRN-SYNC-012** Priority ceiling protocol shall be available as a per-mutex option on the same object type, using the effective-priority mechanism.
**KRN-SYNC-013** Recursive locking shall be a per-mutex creation option, disabled by default.
**KRN-SYNC-014** Each thread shall keep an intrusive list of the mutexes it owns; the recomputation of KRN-SYNC-010 shall use it and shall run at unlock, at waiter timeout or cancel, and at waiter priority change, immediately and not deferred to the waiter's resumption (ADR-028).
**KRN-SYNC-015** The inheritance walk shall detect a cycle that returns to the caller and shall report `EMB_EDEADLK` (kernel-reserved status range) in release builds and fault in checked builds.
**KRN-SYNC-016** Mutexes may be unlocked in any order; there is no LIFO requirement.
**KRN-SYNC-017** A ceiling mutex declared in the system description shall receive its ceiling from the generator as the highest base priority among its declared users; locking it shall raise the effective priority to the ceiling in O(1) and, when every user is declared, shall never need a wait queue (ADR-030).

### 5.3 Condition variables

Classic semantics: `wait(cv, mutex, timeout)` atomically releases and blocks; `signal` wakes one by wait order; `broadcast` wakes all. Spurious wakeups are not permitted by the kernel implementation; the API still documents that callers must re-check predicates.

## 6. Inter-thread communication

### 6.1 Notifications (PROPOSED detail; specified in `docs/specs/SPEC-006-notifications-and-work-queues.md`)

A per-thread 32-bit (configurable) bit set.

- `emb_notify_set(thread, bits)` from any context, O(1), never blocks.
- `emb_notify_wait(mask, mode, timeout)` by the owning thread only; mode is any or all, with optional clear.
- This is the preferred completion mechanism for drivers, timers, and ISRs, and the preferred IPC primitive on the `tiny` profile.

**KRN-NOTIF-001** Each thread shall own a notification bit set that can be set from ISR context without blocking.
**KRN-NOTIF-002** Only the owning thread shall wait on its notifications.
**KRN-NOTIF-003** Notification set and wait shall be O(1) and allocation free.

**Binding (ADR-027).** Any waitable object may be bound to one `(thread, bit)` pair with `emb_<object>_bind_notify()`. Whenever the object becomes ready for its bound operation (a queue gains a message, a semaphore becomes available, an event condition is met, a stream reaches its trigger level), the kernel sets the bit. A thread waiting on several objects waits once on its notification mask, then performs the operation with `EMB_NO_WAIT` on each signalled object and loops on `EMB_ETIMEDOUT` if another consumer was faster (SPEC-001 §4). This replaces queue sets, poll objects, and pend-multi with an O(1) mechanism that has no per-object poller lists and no global lock.

**KRN-NOTIF-004** Every waitable kernel object shall support binding to one notification bit of one thread; the set shall be O(1) and shall happen on every transition to the ready condition.
**KRN-NOTIF-005** Binding shall not change the object's own wait-queue semantics for threads blocked directly on it.
**KRN-NOTIF-006** The `base` profile shall reserve at least 16 notification bits for application use after the kernel-reserved bits; the `tiny` profile may select 8 bits with at least 4 reserved for the application.

### 6.2 Work queues (PROPOSED detail; specified in SPEC-006)

```
work_queue: thread + FIFO of intrusive work items
work_item:  node + handler + optional delay (delayed work uses a timer)
```

- System work queue always exists in `base` and above; its priority is configurable.
- Applications may create additional queues at other priorities to isolate latency classes.
- Submitting an already-queued item is idempotent (it is not queued twice).

**KRN-WQ-001** Work items shall be intrusive and statically allocatable; submission shall be ISR-safe and O(1).
**KRN-WQ-002** Work executes in FIFO order per queue in thread context.
**KRN-WQ-003** Delayed work shall be implemented over software timers, not a separate timing mechanism.
**KRN-WQ-004** Cancellation shall report whether the item was pending, running, or idle.

### 6.3 Message queues (specified in `docs/specs/SPEC-007-inter-thread-communication.md`)

Fixed-size messages by copy, ring storage provided by caller, full and empty blocking with timeout, ISR-safe send and receive with `NO_WAIT`, waiter order by wait policy, optional send-to-front for urgent messages.

### 6.4 Pipes and streams

Byte streams with blocking read/write and watermarks, for UART-like producers and consumers. PLANNED.

### 6.5 Buffer pools and zero-copy (PROPOSED)

Zero-copy is done through **buffer pools with ownership transfer**: fixed-size blocks allocated from a bounded pool; a block's ownership moves with the message. The receiving side must return it. Ownership violations are detectable in checked builds via a per-block owner field.

**KRN-IPC-006** Zero-copy transfer shall move ownership explicitly; a transferred block shall not be accessed by the sender after transfer.
**KRN-IPC-007** Buffer pools shall be bounded and allocation shall be O(1).

### 6.6 Ports (PROPOSED, FUTURE for 1.0)

A **Port** is a message endpoint usable across partition boundaries (with kernel copy or validated ownership transfer) and across core boundaries (via a transport: shared memory ring plus doorbell interrupt). Ports present the same send, receive, timeout, and wake semantics as queues. Intra-image ports degrade to queues.

**KRN-IPC-008** Cross-partition and cross-core messaging shall use the same handle, timeout, and wake semantics as intra-image queues.

**Leases (ADR-032, FUTURE with Ports).** A port message may carry a bounded number of lease descriptors `(base, length, rights)` over the client's memory. The server reads or writes leased memory only through kernel calls that validate the access against the client's regions; a lease is valid only while the client is blocked in that request and is revoked implicitly when the client resumes for any reason (reply, timeout, cancel, partition restart). Small messages are copied; large buffers are leased, never copied.

**KRN-IPC-009** Cross-partition buffers larger than the configured copy threshold shall be transferred by lease, validated on every access, and revoked by the client's wake.

## 7. Kernel objects and capabilities

### 7.1 Object model (LOCKED concept, PROPOSED detail)

```
kernel_object header (present in every object):
  type_tag        (checked builds and isolated profile)
  lifecycle       UNINIT | ACTIVE | DESTROYING
  generation      (optional, stale-handle detection)
  waiters         (where applicable)
  debug_name      (optional)
```

### 7.2 Capabilities (PROPOSED)

The public handle is a capability: `(object reference, rights)`. Its representation depends on profile:

| Profile | Representation | Validation |
|---|---|---|
| tiny | pointer | none (type tag in checked builds) |
| base | pointer plus generation in a tagged word | generation check on use (optional) |
| isolated, multicore | index into the calling partition's capability table | table lookup, type check, rights check, generation check at the syscall boundary |

Rights are object-type specific bit masks (for example for a queue: `SEND`, `RECEIVE`, `DESTROY`, `GRANT`). A capability may be **derived** with fewer rights and **granted** to another partition through a port or at image build time. Revocation is by generation bump on the object.

**KRN-CAP-001** Every public handle shall be a capability whose representation is selected by profile without changing API signatures.
**KRN-CAP-002** In isolated profiles, every kernel entry from an unprivileged partition shall validate the capability's type, rights, and generation before use.
**KRN-CAP-003** Rights shall be reducible on derivation and never increasable.
**KRN-CAP-004** Capability tables shall be statically sized per partition at build time.
**KRN-CAP-005** In the tiny profile the capability layer shall compile to pointer passing with no runtime cost.

### 7.3 Static allocation of opaque objects (ADR-006)

Public headers expose `EMB_<OBJECT>_STORAGE(name)` macros and `emb_<object>_storage_t` types with guaranteed size and alignment, generated per configuration from the kernel's real layout at build time. Kernel init functions take a pointer to caller storage and return the handle. This works on EmbCC, GCC, and Clang without extensions and allows objects in `const`-initialized tables when the configuration permits.

### 7.4 Lifecycle and destruction

**KRN-OBJ-001** Destroying an object with waiters is a kernel fault in checked builds unless the object was created with `ABORT_WAITERS`, in which case every waiter wakes with `DESTROYED`.
**KRN-OBJ-002** After destruction, the object's generation shall be bumped so stale capabilities fail validation in profiles that check them.
**KRN-OBJ-003** Object storage ownership belongs to whoever provided it; the kernel never frees caller storage.
**KRN-OBJ-004** `emb_system_freeze()` shall, once called, make every create and destroy operation fail with `EMB_EPERM` (fault in checked builds) for the rest of the run; the isolated reference configuration shall call it at the end of initialization (uC/OS-III `OSSafetyCriticalStart`, R-003 §3.4).
**KRN-OBJ-005** Thread and partition handles shall carry a generation in every profile that checks generations; a wait on a handle whose generation changed because of a restart shall complete with `EMB_ESTALE` (ADR-032).

## 8. Partitions

**PROPOSED.** The partition replaces v0.1's FUTURE "userspace" with a concept that exists at every scale.

```
partition:
  threads[]            threads belonging to it
  regions[]            memory regions it may access, with permissions
  cap_table            capabilities it holds (isolated profiles)
  privilege            PRIVILEGED | UNPRIVILEGED
  budget               optional CPU budget (temporal protection)
  window_id            optional TIME_TABLE window ownership
  fault_policy         HALT | RESTART | NOTIFY_SUPERVISOR | ESCALATE
  supervisor           partition that is notified of faults
  devices[]            device capabilities granted (MMIO regions, IRQ delivery)
```

Properties:

- The **kernel partition** is always present and privileged. In `tiny` and `base` profiles it is the only partition, and the type compiles to nothing.
- On MPU or PMP hardware, an unprivileged partition's regions become protection regions at context switch. Region count is bounded by hardware and checked at build time.
- A partition **restart** terminates its threads, releases its owned kernel objects according to their policies, re-initializes its data and bss from the image, and restarts its entry thread. Drivers it held are reset through their device lifecycle.
- The **supervisor** is an ordinary privileged partition (usually tiny) that receives fault notifications and applies policy. This is the Hubris pattern and keeps policy out of the kernel.
- Drivers may live in the kernel partition (default) or in their own unprivileged partition with MMIO regions and IRQ delivery granted as capabilities. The request/completion driver model (04 §3) makes both placements use the same driver source.

**KRN-PART-001** Every thread shall belong to exactly one partition.
**KRN-PART-002** Builds without protection hardware shall have exactly one partition and no partition code.
**KRN-PART-003** On protection hardware, memory access of an unprivileged partition shall be limited to its regions; violations shall be partition faults.
**KRN-PART-004** Partition restart shall not require system reboot and shall restore the partition's static data to its initial image.
**KRN-PART-005** Fault policy shall be per partition and applied by a supervisor partition, not hard-coded in the kernel.
**KRN-PART-006** Partition definitions shall be static, build-time data in 1.0; dynamic partition creation is FUTURE.
**KRN-PART-007** In isolated profiles, the kernel state that describes a partition (its threads, objects, wait nodes, capability table) shall live in a kernel-owned sub-region of that partition's memory, sized by the generator and re-initialized on restart; a partition shall not be able to consume kernel memory outside it (ADR-033).

### 8.1 System call boundary (isolated profiles)

- Unprivileged partitions enter the kernel through an architecture trap (`SVC`, `ecall`).
- The public API is identical for privileged and unprivileged callers; the header selects a direct call or a trap stub per partition privilege at build time.
- Argument validation: capabilities validated per §7.2; user pointers checked against the caller's regions; lengths bounded.
- Kernel stacks per CPU for trap handling; user stacks are never trusted.

## 9. Memory

### 9.1 Regions (PROPOSED)

Memory is described as **regions** in the hardware description and surfaced to the linker and the kernel:

```
region: name, base, size, attributes {X, W, R, DMA, CACHEABLE, RETAINED, SECURE, TCM, EXTERNAL}
```

Generated linker fragments place sections into regions. Partitions reference regions. DMA-capable buffers are placed into DMA regions by attribute. Retained regions hold crash records and the flight recorder across resets.

**KRN-MEM-007** Memory regions and their attributes shall be generated from the hardware description, not hand-written per board.
**KRN-MEM-008** DMA buffer placement and cache maintenance requirements shall be derivable from region attributes.

### 9.2 Allocation models (PLANNED, selected)

| Model | Bounded | Use |
|---|---|---|
| Caller-provided static storage | yes | Default for all kernel objects |
| Fixed-size pools and slabs | yes, O(1) | Buffers, messages, real-time paths |
| Arena / bump allocator | yes | Initialization-time allocation with reset |
| TLSF general heap | yes, O(1) | Optional application heap with documented fragmentation |
| Application hooks | n/a | For integrating external allocators |

### 9.3 Stacks

**KRN-MEM-009** Stack sizing methodology shall be documented per architecture: static analysis (EmbCC call graph where available), fill-pattern high-water measurement, and interrupt stack accounting.
**KRN-MEM-010** Stack overflow detection shall offer: fill-pattern check at switch (all profiles), hardware stack limit registers (Armv8-M, RISC-V with PMP), MPU guard regions (isolated profiles).
**KRN-MEM-011** Dedicated interrupt stacks shall be used wherever the architecture permits, so thread stacks are sized for thread work only.

## 10. Fault management

### 10.1 Taxonomy

| Class | Examples | Default severity |
|---|---|---|
| Application fault | assertion in application code, bad argument in release build | recoverable, reported to caller or partition |
| Partition fault | MPU violation, budget `FAULT`, stack overflow in unprivileged partition, unhandled exception in partition | contained: supervisor applies policy |
| Kernel fault | invariant violation, scheduler corruption, stack overflow in kernel partition, fault in the supervisor | fatal: crash record then configured action |
| Hardware fault | bus fault, ECC error, clock failure, watchdog | per SoC and product policy |

### 10.2 Crash record (PROPOSED)

A fixed-layout record written to a retained region before any recovery action:

```
magic, version, build_id, firmware_version, uptime, reset_reason,
fault_class, fault_code, cpu_id, partition_id, thread_id,
pc, lr/ra, sp, stack_bounds, architecture status registers,
general registers (as available), last N trace events (flight recorder tail),
checksum
```

Decoded on the host by EmbDebug or by a standalone script using the build's debug metadata. Also readable on next boot by the application for telemetry.

**FLT-001** Every fatal fault shall write a versioned crash record to retained memory before taking the configured action.
**FLT-002** Partition faults shall be delivered to the supervisor as a notification with a fault descriptor; the kernel shall not decide policy.
**FLT-003** Recovery actions shall include halt-for-debug, reboot, safe-state hook, and partition restart.
**FLT-004** A software watchdog service shall aggregate per-thread or per-partition check-ins and feed the hardware watchdog only when all monitored parties have checked in within their windows.

## 11. Minimal configurations

Two reference configurations anchor footprint and semantics claims:

**tiny / ATmega328P** (from v0.1 Appendix B, unchanged): 8 priorities, preemption on, FIFO, no time slicing, no dynamic memory, one partition, pointer capabilities, 32-bit time option, notifications on, work queue optional, logging minimal.

The tiny configuration uses the table scheduler of ADR-036 (`CONFIG_EMB_SCHED_TABLE`): eight unique priorities, a one-byte ready set, wait queues as one-byte thread sets, and no idle thread. Its footprint targets are T1 and T2 of R-003 §4.

**isolated / Cortex-M33 with MPU**: 32 priorities, 64-bit time, system work queue, software timers, 4 to 8 partitions, validated capabilities, supervisor partition, budgets on, full trace, crash record in retained SRAM.

Both must pass the identical conformance suite.
