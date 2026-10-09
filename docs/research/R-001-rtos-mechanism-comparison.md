# R-001 - RTOS Mechanism Comparison

**Status:** Research record, 2026-10-07.
**Purpose:** Compare, mechanism by mechanism, how eleven production and research kernels implement the things EmbLinkRTOS must implement, so that every design choice in the specifications is made knowing the state of the art and its failure modes. The differentiation decisions derived from this record are in `R-003`. Market, certification, and regulation facts are in `R-002`.
**Method:** Read-only source analysis at pinned commits (table below), one fixed question list per kernel, every claim tied to a file and symbol. The per-kernel notes in `notes/` carry the file:line citations; this record cites the note and the key symbol. Vendor documentation was used only where the clone lacked the code (ThreadX Modules, Zephyr tests) and is marked as such.

| Kernel | Commit | Language | Class | Note |
|---|---|---|---|---|
| FreeRTOS kernel V11.1+ | `8be86d4a24fd` | C | small kernel, SMP | [`notes/freertos.md`](notes/freertos.md) |
| Eclipse ThreadX 6.5.2 | `93387b0a6038` | C + asm | small certified kernel | [`notes/threadx.md`](notes/threadx.md) |
| Zephyr v4.5.0-rc1 | `d175d3bfb6c2` | C | platform RTOS, SMP, userspace | [`notes/zephyr.md`](notes/zephyr.md) |
| RTEMS (master) | `b3a3b372fa8d` | C | qualified SMP kernel | [`notes/rtems.md`](notes/rtems.md) |
| Apache NuttX (master) | `b463a4bf7a48` | C | POSIX RTOS, SMP, protected builds | [`notes/chibios-nuttx.md`](notes/chibios-nuttx.md) Part B |
| ChibiOS RT 8.0.0 / NIL 4.2.0 | `fd2e59c34878` | C | small kernel pair, SMP in RT | [`notes/chibios-nuttx.md`](notes/chibios-nuttx.md) Part A |
| Micrium uC/OS-III 3.08.02 | `9a3fc5f45d75` | C | small certified-lineage kernel | [`notes/ucos3.md`](notes/ucos3.md) |
| Hubris (Oxide) | `446dfcd5019a` | Rust | isolated-task microkernel | [`notes/hubris-tock.md`](notes/hubris-tock.md) Part A |
| Tock 2.x | `40b9e1378345` | Rust | process-isolating kernel | [`notes/hubris-tock.md`](notes/hubris-tock.md) Part B |
| Embassy | `b3e27baf6c20` | Rust | async executor | [`notes/embassy-riot.md`](notes/embassy-riot.md) Part A |
| RIOT | `f9e38576567b` | C | IoT microkernel | [`notes/embassy-riot.md`](notes/embassy-riot.md) Part B |

The eleven span every design family that matters for EmbLinkRTOS: classic preemptive C kernels at three sizes (uC/OS-III, FreeRTOS, ThreadX), a platform RTOS (Zephyr), a POSIX RTOS (NuttX), a qualified SMP kernel (RTEMS), a kernel pair that shares one API across two footprints (ChibiOS RT and NIL), two Rust isolation kernels (Hubris, Tock), an async executor (Embassy), and an IoT microkernel with a notable timer design (RIOT).

---

## 1. Scheduler

### 1.1 Ready structure and selection

| Kernel | Ready structure | Highest-ready selection | Equal priority | Priority cap |
|---|---|---|---|---|
| FreeRTOS | one dlist per priority, `uxTopReadyPriority` hint or bitmap | walk down, or `31 - clz` on the port-optimised bitmap | round robin via list cursor; append at tail | 32 on the CLZ path |
| ThreadX | per-priority self-linked circular dlist heads, `_tx_thread_priority_maps[]`, cached `_tx_thread_highest_priority` | lowest set bit (`m & (~m+1)` or RBIT+CLZ) | per-thread time slice; preemption threshold band | multiple of 32 |
| Zephyr | choice of sorted dlist, rbtree with order key, or per-priority dlists plus bitmap | backend-specific; UP caches `ready_q.cache` | FIFO; EDF within one priority (SCHED_DEADLINE) | configurable; MULTIQ excludes DEADLINE |
| RTEMS | plugin: 16x16 two-level bitmap plus chains (Deterministic Priority), one chain (Simple), rbtree (EDF), SMP variants | find-first-bit major then minor | FIFO or LIFO via the LSB "append" bit of a 64-bit `Priority_Control` | 64-bit priority space |
| NuttX | one `dq_queue_t` whose head is the running task; SMP adds `g_assignedtasks[cpu]` | head of the sorted list | FIFO; RR by `timeslice`; SPORADIC | `uint8_t` |
| ChibiOS RT | one circular priority-ordered dlist (`ch_priority_queue_t`) | pop head | FIFO behind, LIFO ahead when the quantum is unspent | no bitmap, O(n) insert |
| ChibiOS NIL | fixed array `nil.threads[prio]`, priority == index, no list | scan from the top for the first READY | not allowed (unique priorities) | `CH_CFG_MAX_THREADS` <= 16 |
| uC/OS-III | `OSPrioTbl[]` bitmap plus `OSRdyList[prio]` head and tail | `CPU_CntLeadZeros` over one or two words | tail only when readied at the current priority, else head | `OS_CFG_PRIO_MAX` |
| Hubris | fixed task table from `app.toml` | linear scan from `previous+1` | round robin within a level | 256, `u8` |
| Tock | one kernel thread; pluggable process scheduler (RR, priority, cooperative, MLFQ) | scheduler trait `next()` | timeslice in microseconds | n/a |
| Embassy | lock-free intrusive stack per executor, batch dequeue | iterate the batch; optional sort | cooperative within an executor | priority only across executors |
| RIOT | `sched_runqueues[16]` circular clists plus `runqueue_bitcache` | one `clz`/`ctz` | semi-cooperative; optional ztimer round robin | 32 (u32 bitcache) |

**Observations.**
- Everyone who cares about determinism uses a bitmap plus one FIFO per priority (ThreadX, FreeRTOS port-optimised, RTEMS, uC/OS-III, RIOT). The two-level 16x16 bitmap of RTEMS is the only one that scales past the word width without a cap. EmbLinkRTOS KRN-SCH-036 (one word up to the word width, two-level above) is this design.
- The outliers are instructive. ChibiOS RT keeps a single ordered list and pays O(n) insertion to save the bitmap; NIL removes the list entirely for tiny targets by making priority the array index (`nil/src/ch.c:762-793`). Hubris accepts an O(n) table scan for a kernel with a dozen tasks. Nobody offers both a bitmap scheduler and a table scheduler behind one API except ChibiOS, and ChibiOS does it as two kernels.
- Equal-priority policy differs in a detail that matters for latency: ChibiOS (`chSchWakeupS`) and uC/OS-III (`OS_RdyListInsert`) put a preempted thread *ahead* of its peers because its quantum is unspent; FreeRTOS and ThreadX always append. EmbLinkRTOS's "FIFO among equals" (03 §2.1) should state which it means for a preempted thread.
- ThreadX's preemption threshold is the only bounded-preemption mechanism in the field (`tx_thread_system_resume.c:238`, `_tx_thread_preempted_maps`); it costs a second bitmap and a re-selection path in suspend, and it is disabled under TX_SAFETY_CRITICAL only indirectly. Zephyr's meta-IRQ threads are the opposite tool: a band at the top that preempts even cooperative threads.

### 1.2 Scheduler lock

| Kernel | Mechanism | Readying under the lock |
|---|---|---|
| FreeRTOS | `uxSchedulerSuspended` counter, incremented outside a critical section (correctness argued by comment) | ISR-woken tasks parked on `xPendingReadyList`; ticks counted in `xPendedTicks`; drained in `xTaskResumeAll` |
| ThreadX | `_tx_thread_preempt_disable` counter; services refuse to block while non-zero | `_tx_thread_execute_ptr` is updated; the switch is deferred |
| Zephyr | per-thread `sched_locked` under the sched spinlock; meta-IRQs bypass | ready queue updated normally |
| RTEMS | per-CPU `thread_dispatch_disable_level`; dispatch on the 1 to 0 transition if `dispatch_necessary` | heir updated; `_Thread_Do_dispatch` loops |
| NuttX | per-TCB `lockcount`; readied higher tasks parked in `g_pendingtasks` and merged at unlock | yes, via pending list |
| ChibiOS | `chSysLock` is the only lock (masking), no separate scheduler lock | n/a |
| uC/OS-III | 8-bit `OSSchedLockNestingCtr` capped at 250; duration measured | ready list updated, switch deferred |
| RIOT | none (IRQ disable only) | n/a |

**Observation.** The scheduler lock is either a counter that defers the switch while the ready structure is kept current (ThreadX, Zephyr, RTEMS, uC/OS-III) or a lock that also freezes the ready structure and needs a side list (FreeRTOS, NuttX). The first is simpler and has no replay step; SPEC-002 chose it (scheduler lock defers the switch, ready structure stays current). RTEMS's `dispatch_necessary` flag per CPU is the same as SPEC-002's `reschedule_pending`.

### 1.3 Idle

ThreadX has no idle thread: PendSV spins with interrupts masked unless `TX_ENABLE_WFI` (`tx_thread_schedule.S:251-296`). uC/OS-III 3.08 made the idle task optional and idles inside `OSSched`. ChibiOS makes it optional (`CH_CFG_NO_IDLE_THREAD`). RIOT idles inside `sched_run` unless `core_idle_thread` is enabled. FreeRTOS, Zephyr, RTEMS, NuttX, and Hubris require an idle thread (Hubris panics without a runnable task). Idle without a thread saves a TCB and a stack, which is a real number on a 2 KB AVR; EmbLinkRTOS's tiny profile should allow it (R-003).

### 1.4 SMP

| Kernel | Ready structure | Locking | Cross-CPU wake |
|---|---|---|---|
| FreeRTOS | global lists scanned for an affinity-compatible non-running task | two spinlocks (task, ISR) in `vTaskSwitchContext`; nesting count moved into the TCB | `portYIELD_CORE` IPI |
| Zephyr | global run queue under `_sched_spinlock`; per-CPU only with PIN_ONLY | one scheduler spinlock; legacy `irq_lock` is a global recursive spinlock | `flag_ipi` with IPI_OPTIMIZE masks |
| RTEMS | per-scheduler-instance `Scheduled` chain plus ready structure; clustered scheduling; helping protocol | ticket, MCS, and seqlocks; per-CPU watchdog locks | atomic message plus IPI |
| NuttX | `g_assignedtasks[cpu]` plus global `g_readytorun` pool; `g_delivertasks[cpu]` | one global `g_cpu_irqlock` plus bitmask bookkeeping | IPI then `nxsched_process_delivered` |
| ChibiOS RT | one `os_instance_t` per core with its own ready list and timers | one global spinlock inside `port_lock` | `chSysNotifyInstance` |

**Observation.** Four of five start from a global structure under one lock; only RTEMS has per-instance scheduling and it is also the largest and hardest to audit (`schedulersmpimpl.h` over 2,000 lines). ADR-012 (global first, per-CPU only if measured) matches the field; the lesson from Zephyr and NuttX is that the *interrupt lock* must not become a global recursive spinlock, which is why SPEC-002 keeps `emb_irq_lock()` per-CPU and gives SMP its own spinlock API.

---

## 2. Context switch and interrupt exit

### 2.1 Where the switch happens

| Kernel | Cortex-M mechanism | Consequence |
|---|---|---|
| FreeRTOS, ThreadX, RIOT, Zephyr (legacy) | pend PendSV at the lowest exception priority; PendSV saves the callee-saved registers, selects, restores | one switch path; the hardware performs the "outermost ISR" check; Zephyr needs `SWAP_NONATOMIC` workarounds because PendSV sits below IRQs |
| ChibiOS RT/NIL | `__port_irq_epilogue` checks `RETTOBASE`, pushes a fake exception frame returning into `__port_switch_from_isr`, which switches then issues `svc` to drop the frame | reschedule runs in thread mode with the kernel still locked; fast IRQs above BASEPRI never see it |
| NuttX | every exception runs `arm_doirq(irq, regs)` and the assembly restores from the returned pointer; a switch is "return a different frame" | tiny uniform port; signal delivery rewrites the saved frame |
| Zephyr (USE_SWITCH) | `arm-m-switch.c` synthesises frames and hijacks the stacked LR to run exit code after return | required for SMP |
| Hubris, Tock | SVC entry stores the full register set into the task struct; PendSV runs `select` | all registers in the TCB, not on the task stack |
| Embassy | none; a "switch" is one indirect call `poll_fn` | no stacks per task |

**Observation.** SPEC-002's choice (PendSV lowest, `naked` handler, one switch path per architecture) is the mainstream and is validated by four kernels. The two alternatives buy something specific: ChibiOS's fake frame lets the reschedule run with the kernel lock held and without a second exception, which is why its switch latency is low; NuttX's returned-frame design is the smallest port contract. Neither changes SPEC-002, but both inform the Cortex-M port design: measure the PendSV path against the fake-frame path on the STM32F4 before freezing it (R-003 §6 harness).

### 2.2 Critical sections and interrupt classes

| Kernel | Primitive | Nesting | Zero-latency class |
|---|---|---|---|
| FreeRTOS | task-level BASEPRI raise plus static `uxCriticalNesting`; ISR-level `ulPortRaiseBASEPRI()` returns a key, no counter | counter in task context, key in ISR | priorities above `configMAX_SYSCALL_INTERRUPT_PRIORITY` may not call the API |
| ThreadX | `TX_DISABLE`/`TX_RESTORE` save PRIMASK or BASEPRI into a local | key-based | with `TX_PORT_USE_BASEPRI` |
| Zephyr | `arch_irq_lock` returns the prior BASEPRI or PRIMASK | key-based | `_IRQ_PRIO_OFFSET` levels, `IRQ_DIRECT_CONNECT`, no kernel calls |
| RTEMS | `_ISR_Local_disable(level)` cookie; `ISR_lock_Control` degrades to interrupt disable on UP | key-based | none in score |
| NuttX | `up_irq_save` returns BASEPRI; `enter_critical_section` counts `irqcount` or takes the SMP spinlock | counter or key | `CONFIG_ARCH_HIPRI_INTERRUPT` |
| ChibiOS | `chSysLock` raises BASEPRI to `CORTEX_BASEPRI_KERNEL`; `CH_IRQ_PROLOGUE/EPILOGUE` | state checker, not a counter (`lock_cnt`, `isr_cnt` in debug) | `CORTEX_FAST_PRIORITIES` (default 2) |
| uC/OS-III | `CPU_CRITICAL_ENTER/EXIT` plus `CPU_SR_ALLOC` from uC/CPU | key-based | none (deferred post removed in 3.08) |
| RIOT | `irq_disable()` returns PRIMASK | key-based | none |

**Observations.**
- Key-based save and restore is universal; counters appear only where a kernel wanted a cheaper fast path (FreeRTOS task level) or SMP bookkeeping (NuttX). SPEC-002's key-based `emb_irq_lock()`/`emb_irq_unlock(key)` is the field standard.
- Four kernels have a zero-latency interrupt class above the kernel mask (FreeRTOS, Zephyr, NuttX, ChibiOS). ChibiOS is the only one whose debug state checker refuses kernel calls from a fast IRQ by construction (`SV#` checks). SPEC-002's `CONFIG_EMB_IRQ_ZERO_LATENCY` with `EMB_ISR_RAW` matches this.
- uC/OS-III and RTEMS run entire list walks with interrupts masked (pend-list insertion, tick-list walk, watchdog tickle). NuttX runs most kernel paths under `enter_critical_section`. FreeRTOS limits masked sections by using the scheduler lock plus queue lock counters for the slow paths. Nobody publishes a bound on the longest masked section as a requirement; only uC/OS-III (via uC/CPU) and NuttX (critmonitor) measure it at runtime. This is a gap: SPEC-002 already requires latency metrics; R-003 turns the bound into a tested promise.

### 2.3 Misuse detection

| Kernel | ISR misuse | Context class checks |
|---|---|---|
| FreeRTOS | `portASSERT_IF_INTERRUPT_PRIORITY_INVALID` reads IPSR and the NVIC IPR byte at 20 call sites; boot-time NVIC probing; `configASSERT` defaults to nothing | none beyond asserts |
| ThreadX | `txe_` shells return TX_WAIT_ERROR, TX_CALLER_ERROR by `TX_THREAD_GET_SYSTEM_STATE()`; removable by name mapping | id magic, control-block sizeof, stack overlap |
| Zephyr | `z_pend_curr` panics on ISR pend in every build; `__ASSERT` elsewhere; `SPIN_VALIDATE` | `CHECKIF` tri-mode |
| RTEMS | `STATUS_MESSAGE_QUEUE_WAIT_IN_ISR`; `RTEMS_SCORE_ROBUST_THREAD_DISPATCH` faults on bad dispatch environment | fatal codes for bad dispatch level |
| NuttX | asserts; `_assert` dumps everything | n/a |
| ChibiOS | `SV#1..11` state machine over `isr_cnt`, `lock_cnt`; S/I/X suffix contract | yes, every primitive |
| uC/OS-III | 25 dedicated `*_ISR` error codes checked at every API when `OS_CFG_CALLED_FROM_ISR_CHK_EN` | out-parameter errors |
| RIOT | mixed: some return silently, some assert | none |

**Observation.** ChibiOS's state checker is the strongest design: it encodes the legal transitions between thread, locked, and ISR states and halts on any violation, in debug builds only, at a cost of two counters. ThreadX's name-mapped validation layer is the strongest *packaging*: the checks are a separable layer. SPEC-001's rule (misuse is a fault in checked builds and a status in release) needs both: a transition checker like ChibiOS's and a separable layer like ThreadX's so that release builds carry no check code. R-003 adopts both.

---

## 3. Time

### 3.1 Timebase

| Kernel | Tick counter | Width | Wrap handling |
|---|---|---|---|
| FreeRTOS | `xTickCount` | 16, 32, or 64 by config | two delayed lists swapped on wrap; timeouts as (overflow count, entry tick) |
| ThreadX | `_tx_timer_system_clock` | 32 | ignored |
| Zephyr | `curr_tick` | 64 (`TIMEOUT_64BIT` default) | none needed |
| RTEMS | per-CPU `Watchdog.ticks` plus FreeBSD timecounter | 64 | none needed |
| NuttX | `g_system_ticks` or hardware tick | 32 or 64 (`SYSTEM_TIME64`) | wrap-safe `clock_compare` |
| ChibiOS | `systime_t` | 16, 32, or 64 | modular arithmetic; 64-bit stamp extension |
| uC/OS-III | `OSTickCtr` | 32 | delta list, so wrap is implicit |
| Hubris | `Timestamp(u64)` | 64 | none needed |
| Embassy | driver `now()` | 64 required; 16/32-bit hardware extended in software | none needed; `Instant` arithmetic panics on overflow |
| RIOT ztimer | `ztimer_now_t` | 32 | only differences are meaningful; `max_value/2` extension with checkpoints |

**Observation.** The modern kernels (Zephyr, RTEMS, Hubris, Embassy) are 64-bit and simply have no wrap code; the small kernels pay for 32 bits with list swaps (FreeRTOS, duplicated in the timer module) or with user-visible rules (RIOT). SPEC-003 (64-bit default, 32-bit tiny profile with a wrap-safe comparison and a 2^31-1 cap) takes the modern position and borrows NuttX's wrap-safe compare for the tiny profile instead of FreeRTOS's list swap.

### 3.2 Timeout structure

| Kernel | Structure | Insert | Expiry check | Notes |
|---|---|---|---|---|
| FreeRTOS | absolute-sorted dlist plus overflow list; `xNextTaskUnblockTime` cache | O(n) | O(1) | separate structure in the timer daemon |
| ThreadX | 32-slot timer wheel; long timers re-queued every 32 ticks | O(1) | O(1) per slot | drift when the timer thread is late |
| Zephyr | delta dlist default; minheap, wheel, bucket, skiplist experimental | O(n) | O(1) | `inflight_timeout` superseded bit for SMP cancel |
| RTEMS | per-CPU RB-trees (TICKS, MONOTONIC, REALTIME) with cached `first` | O(log n) | O(1) | 64-bit `expire`; no tickless |
| NuttX | absolute-tick sorted list, wrap-safe compare | O(n) | O(1) | optional `HRTIMER` layer (list or rb-tree) |
| ChibiOS | delta list anchored at `lasttime`, lazy update; adaptive `lastdelta` | O(n) | O(1) | RFCU records insufficient-delta faults |
| uC/OS-III | delta dlist through TCB links; same code for timers | O(n) | O(1) | callbacks via a condvar-driven timer task |
| Hubris | one deadline per task, scanned every SysTick | O(1) | O(tasks) | multiplexed in userland |
| Embassy | intrusive singly linked list per executor; or `heapless::Vec` of 64 | O(n) | O(n) | one hardware alarm multiplexed |
| RIOT | relative-offset list per clock in a clock tree | O(n) | O(1) | `adjust_set`/`adjust_sleep` overhead compensation |

**Observations.**
- Sorted intrusive list is the default in eight of ten; the only O(log n) structure in a shipping kernel is RTEMS's RB-tree, and the only wheel is ThreadX's, whose 32-slot design makes long timers cost a re-queue every 32 ticks and makes periodic reload relative to processing time. ADR-010 (absolute-deadline list, wheel behind the same interface) is consistent with the field and avoids ThreadX's drift.
- The absolute-versus-delta choice has shifted: NuttX moved from deltas to absolute ticks; Zephyr keeps deltas and pays with `announce_remaining` bookkeeping; ChibiOS keeps deltas and needed an adaptive minimum delta plus fault logging. SPEC-003's absolute deadlines are the simpler modern choice.
- Two race-handling details are worth copying: Zephyr's superseded bit on the in-flight timeout so a cancel racing a running handler is safe on SMP (`timeout.c:51-70, 239-286`), and ChibiOS's stack-allocated timeout timer for a sleeping thread (`chSchGoSleepTimeoutS`), which SPEC-003's per-thread `timeout_node` already provides.
- RIOT is alone in compensating for the arming overhead of the hardware timer (`adjust_set`, `adjust_sleep`, auto-calibrated at boot). That is a cheap, measurable accuracy win that no mainstream kernel has.

### 3.3 Tickless

Supported: FreeRTOS (`portSUPPRESS_TICKS_AND_SLEEP`, 24-bit SysTick limit), ThreadX (not in base; `TX_LOW_POWER` hooks only), Zephyr (native), NuttX (`SCHED_TICKLESS`, RR and sporadic deadlines as ordinary wdogs), ChibiOS (`CH_CFG_ST_TIMEDELTA >= 2`), uC/OS-III (`OS_CFG_DYN_TICK_EN`, round robin forbidden with it), Embassy (no tick at all), RIOT (ondemand clocks). Not supported: RTEMS (tick only), Hubris (1 ms SysTick polling all tasks). SPEC-003 supports both modes with the same list; NuttX's trick of implementing the round-robin deadline as an ordinary timeout entry is the right way to make time slicing tickless-safe, which uC/OS-III could not do.

### 3.4 Software timers

| Kernel | Callback context | Periodic re-arm | Stop semantics |
|---|---|---|---|
| FreeRTOS | daemon task; API calls are asynchronous commands through a queue | previous expiry plus period, catch-up fires once per missed period | asynchronous: `xTimerStop` returns before the timer is stopped |
| ThreadX | system timer thread at priority 0, or ISR with `TX_TIMER_PROCESS_IN_ISR` | relative to the processing slot (drifts) | synchronous |
| Zephyr | timer ISR context, lock dropped around the handler | `K_TIMEOUT_ABS_TICKS(uptime + period)`, drift-free | `k_timer_status_sync`; cleanup spins out in-flight handlers |
| RTEMS | watchdog routine in ISR; timer server task variant | n/a | synchronous |
| NuttX | wdog under the critical section | n/a | synchronous |
| ChibiOS | always I-class (ISR) | continuous timers detect skipped deadlines | synchronous |
| uC/OS-III | timer task woken by a condvar with timeout = next expiry | re-link with Period | synchronous |
| Embassy | the awaiting task | `expires_at += period`, catch-up bursts | n/a |
| RIOT | ISR; `ztimer_periodic` | `last + interval`, immediate catch-up tick | synchronous |

**Observation.** Thread-context callbacks exist in FreeRTOS, ThreadX, uC/OS-III, and RTEMS's timer server; only FreeRTOS makes the control API asynchronous, which is its most complained-about timer property. Drift-free re-arm from the previous deadline appears in FreeRTOS, Zephyr, uC/OS-III, Embassy, and RIOT; ThreadX drifts. SPEC-003 (work-queue callbacks by default, ISR opt-in, synchronous `emb_timer_stop_sync`, re-arm from the previous deadline with overrun counting) combines the best of each and avoids the FreeRTOS asynchrony and the ThreadX drift. uC/OS-III's condvar-driven timer task is a neat implementation of "work queue woken at the next expiry" that the work-queue spec can reuse.

---

## 4. Wait and wake

### 4.1 Wait queue ordering

Priority order with FIFO among equals is the default in FreeRTOS, Zephyr, RTEMS (priority queues per scheduler), NuttX, ChibiOS (optional for semaphores), uC/OS-III, and RIOT. ThreadX is FIFO by default and needs explicit `tx_*_prioritize` calls or `TX_INHERIT` mutexes to get priority order, which is a frequent source of unexpected inversion. KRN-WAIT-001 (priority then FIFO) and KRN-WAIT-002 (pure FIFO as an option) match the majority and fix ThreadX's default.

### 4.2 The block and wake race

| Kernel | Protocol | Masked section |
|---|---|---|
| FreeRTOS | scheduler suspend plus queue lock counters (`cTxLock`, `cRxLock`); ISR posts to a locked queue only increment; `prvUnlockQueue` replays; `xTaskCheckForTimeOut` loops back to the top | short critical sections; the slow path is under scheduler suspend, not masking |
| ThreadX | set `tx_thread_suspending`, bump `tx_thread_suspension_sequence`, re-enable interrupts, call `_tx_thread_system_suspend`, which proceeds only if still suspending; cleanup functions re-validate cleanup pointer, sequence, object id, and state | short; the window between "decide to block" and "blocked" is explicitly open and made idempotent |
| Zephyr | "lock swap": take `_sched_spinlock`, unready, arm timeout, release the caller's lock, swap; wakers set `swap_retval` under the sched lock; timeout cancel marks in-flight handlers superseded and retries `-EAGAIN` | one scheduler spinlock held across the pend |
| RTEMS | wait flags READY / INTEND_TO_BLOCK / BLOCKED changed by CAS; enqueue sets INTEND_TO_BLOCK, releases the queue lock, arms the timeout, then CAS to BLOCKED; a surrender or timeout that lands in between wins the CAS and the blocker unblocks itself | queue ticket lock only; the thread state change is lock-free |
| NuttX | everything under `enter_critical_section`; timeout callback re-checks `task_state == TSTATE_WAIT_SEM` | long by construction |
| ChibiOS | everything under `chSysLock`; stack timer reset after wake | long-ish, but BASEPRI leaves fast IRQs running |
| uC/OS-III | everything under `CPU_CRITICAL_ENTER`; whoever runs first removes the TCB from the other list | long; includes O(n) pend-list walks |
| Hubris, Tock | single kernel thread; no race by construction | n/a |
| Embassy | wake sets RUN_QUEUED atomically and enqueues only if it was clear; idempotent | lock-free |
| RIOT | IRQ disable around state changes; `mutex_cancel` as the generic timeout | short |

**Observations.**
- There are three ways to make the race correct: (a) hold one lock across everything (NuttX, uC/OS-III, ChibiOS, Zephyr with its spinlock), (b) make the wake and the block idempotent with a sequence number or a state CAS so no lock spans the window (ThreadX, RTEMS, Embassy), (c) replay under a side lock (FreeRTOS). Option (b) gives the shortest masked sections and is the only one that scales to SMP without a global lock, which is why RTEMS uses it. Option (a) is what the small kernels do and is why their worst-case masked time is unbounded by object count.
- KRN-WAIT-004 to 007 require exactly-one-winner semantics and atomic removal from both the wait queue and the timeout structure. RTEMS's three-state wait flag plus Zephyr's superseded in-flight timeout bit, together, satisfy them with bounded masking. R-003 proposes that as the SPEC-004 protocol (ADR-026).

### 4.3 Multi-object wait

| Kernel | Mechanism | Limits |
|---|---|---|
| FreeRTOS | queue sets: the set is a queue of member handles; posting copies the member handle in | one extra copy per post; sets are queues of handles |
| ThreadX | none in the TCB; per-object `*_notify` callbacks forwarding to one event-flags group | one callback per object, runs in the setter's context |
| Zephyr | `k_poll`: events registered on per-object `poll_events` lists sorted by poller priority; condition checked under `poll_lock` at registration | one global `poll_lock`; NOTIFY_ONLY mode only |
| RTEMS | none (events are per-thread bits; system events separate) | n/a |
| NuttX | `poll()` over file descriptors; message queue `sigevent` | POSIX layer |
| ChibiOS | per-thread `epending` bits with listener lists off each source; broadcast ORs flags | 32 flags per thread; listeners are intrusive user-owned structs |
| uC/OS-III | removed `OSPendMulti`; task-built-in semaphore and queue are the substitute | n/a |
| RIOT | `thread_flags` with reserved bits set by IPC and timers; `sys/event` queues on top | 16 bits |
| Hubris | `recv` with a notification mask; IRQs and timers are notification bits | 32 bits |
| Embassy | `select` over futures | native to async |

**Observation.** The convergent design is **per-thread notification bits that other mechanisms set**: ChibiOS events, RIOT thread_flags, Hubris notifications, and uC/OS-III's task-built-ins all point there, and FreeRTOS's stream buffers and Zephyr's triggered work are built on the same idea. Zephyr's `k_poll` and FreeRTOS's queue sets are the heavier alternatives and each has a documented limitation. 03 §6.1 already makes notifications the preferred completion mechanism; what is missing is the binding from an object to a notification bit so that a thread can wait on several objects with one wait. R-003 proposes it (ADR-027).

### 4.4 Destroying an object with waiters

FreeRTOS only asserts (`vQueueDelete`); ThreadX resumes all with `TX_DELETED`; Zephyr has no generic teardown (sem reset wakes with `-EAGAIN`, pipe close with `-EPIPE`, `k_timer_cleanup` refuses with `-EAGAIN`); RTEMS flushes with a filter writing `STATUS_OBJECT_WAS_DELETED`; uC/OS-III offers `DEL_NO_PEND` (refuse) or `DEL_ALWAYS` (abort each waiter); NuttX recovers on task kill. ADR-020 (fault in checked builds unless `ABORT_WAITERS`, then wake with `DESTROYED` and bump the generation) is the uC/OS-III pair of options made explicit at creation time plus RTEMS's wake-reason encoding; no other kernel also bumps a generation so that stale handles fail afterwards.

---

## 5. Mutexes and priority inheritance

| Kernel | Transitive | Multi-mutex restore | Disinherit on waiter timeout | Owner death | Ceiling | Deadlock detection |
|---|---|---|---|---|---|---|
| FreeRTOS | no (raises only the direct holder) | only when the last mutex is released | only if exactly one mutex is held ("simplification") | none; a deleted holder leaves the mutex locked | no | no |
| ThreadX | no | scan of `tx_thread_owned_mutex_list` for the max `highest_priority_waiting` | via prioritize on put | `_tx_mutex_thread_release` puts every owned mutex on terminate | no (semaphore ceiling put only) | no |
| Zephyr | yes, bounded by `MUTEX_CHAIN_WALK_MAX_HOPS` (16) | recompute over `held_mutexes` | deferred: done by the waiter after it resumes, not in the timer ISR; chain priority-down not propagated | n/a (thread abort does not release) | `PRIORITY_CEILING` option | K_FOREVER deadlock detect option |
| RTEMS | yes, via the owner-chain action loop on priority aggregation trees | automatic: the queue aggregation is a contributor to the owner | automatic via aggregation | n/a | ceiling node; MrsP for SMP | yes, path acquire detects cycles, status or fatal |
| NuttX | no | `max(base, boost, highest waiter on every held sem)` | yes | `nxsem_recover` releases holders on kill | no | n/a |
| ChibiOS RT | yes, chain walk re-sorting the boosted thread wherever it waits | recompute from the owned-mutex stack, LIFO unlock order required | yes | n/a | no | no |
| uC/OS-III | yes, `OS_TaskChangePrio` chain loop | mutex group per task plus highest-pending scan | yes | `OS_MutexGrpPostAll` hands each mutex to its head waiter (with a likely bug at `os_mutex.c:1115`) | no | no |
| RIOT | opt-in module, single level | no | n/a | n/a | no | no |
| Hubris | none; "uphill send rule" by convention | n/a | n/a | restart with dead codes | n/a | n/a |

**Observations.**
- Only RTEMS gets every column right, by turning each priority influence into a node in a min-tree and recomputing minima on every change; the price is 64-bit priorities and RB-trees per thread. Among small kernels, uC/OS-III and ChibiOS are transitive and restore correctly across several mutexes; ChibiOS forces LIFO unlock order to make the restore cheap.
- FreeRTOS, the most used kernel, fails four of six columns. ThreadX, the certified one, is non-transitive and FIFO by default. Zephyr's inheritance is correct in the common case but its own header documents three limitations caused by lock ordering between `mutex_lock` and `_sched_spinlock`.
- Owner death is handled only by ThreadX (release on terminate), NuttX (recover on kill), and uC/OS-III (hand off, buggy). KRN-SYNC-011 (fault by default, or `EMB_EOWNERDEAD` to the next owner) is already stronger than all three because the next owner learns that state may be inconsistent.
- Deadlock detection exists in RTEMS (always) and Zephyr (option). It costs one chain walk that transitive inheritance performs anyway.
- A correct, small, bounded-masking inheritance algorithm with an owned-mutex list, transitive walk with configurable depth, disinheritance on timeout, deadlock detection during the walk, and owner-death handling does not exist in any small C kernel. This is the single clearest "beat" item (R-003 §3, ADR-028).

---

## 6. Inter-thread communication

| Kernel | Cheapest primitive | Copy model | Notable |
|---|---|---|---|
| FreeRTOS | direct-to-task notification: no object, no sorted insert, single receiver | queues copy by value; stream and message buffers copy with no lock and publish head/tail after | stream buffers assume one reader and one writer and assert it |
| ThreadX | event flags with notify callbacks | queues copy 1 to 16 words, direct copy into a waiting receiver's buffer | event chaining |
| Zephyr | `k_sem`, `k_event` | `k_msgq` ring copy; `k_pipe` direct copy into a pending reader's stack buffer; `k_mbox` zero-copy by descriptor; `k_queue` intrusive | pipe 2024 rewrite; mailbox async via dummy threads |
| RTEMS | events (32 bits per task) | message queue preallocated buffers with per-message priority; direct copy to a waiter | urgent prepend, priority insert |
| NuttX | signals, POSIX mq with `sigevent` | mq copy | POSIX |
| ChibiOS | events; messages are a zero-copy rendezvous (sender sleeps until `chMsgRelease`) | mailboxes (OSLIB) copy pointers | synchronous messages with zero copies |
| uC/OS-III | task semaphore and task queue built into the TCB | `OS_MSG` pool of pointers | pend-multi removed in favour of task-built-ins |
| RIOT | `thread_flags`; `msg_t` is 8 bytes (pid, type, value or pointer) | synchronous unless the receiver has a queue; `msg_try_send` drops | `msg_send_receive` rendezvous |
| Hubris | synchronous send/recv/reply, 256-byte cap, leases for larger buffers | one copy via `safe_copy` validated against both tasks' regions | leases revoked when the client resumes |
| Tock | upcalls delivered on yield; allow buffers swapped | buffers shared by pointer with liveness re-checked on each `enter` | `allow` swap semantics return the previous buffer |
| Embassy | `Signal`, `Channel`, `PubSubChannel` with `Lagged` | `zerocopy_channel` hands out `&mut T` slots | lagged-aware broadcast |

**Observations.**
- The per-thread notification is the cheapest wake in every kernel that has it (FreeRTOS, uC/OS-III, RIOT, Hubris, ChibiOS). 03 §6.1 adopts it as the base primitive. FreeRTOS's stream buffer shows how to build a lock-free single-producer, single-consumer byte stream on top of it; that design fits 03 §6.4 pipes.
- Zero-copy exists in four shapes: rendezvous (ChibiOS, RIOT, Hubris), descriptor exchange (Zephyr mailbox), slot hand-out (Embassy), and validated borrowing across isolation boundaries (Hubris leases, Tock allow). 03 §6.5's buffer pools with ownership transfer cover the intra-image case; Hubris's leases are the right model for the cross-partition case (Ports, 03 §6.6) because they require no kernel copy and are revoked by the blocking state machine itself (R-003, ADR-032).
- Broadcast with slow-consumer detection (Embassy `Lagged(n)`) has no equivalent in any C kernel; it is a small, useful addition to the event or stream design.

---

## 7. Isolation

| Kernel | Hardware model | Handle model | Pointer validation | Syscall plumbing | Restart | Not protected |
|---|---|---|---|---|---|---|
| FreeRTOS MPU v2 | MPU regions 0-3 fixed (privileged flash, unprivileged flash, syscall flash, privileged RAM), 4 task stack, 5-7 per task | index into `xKernelObjectPool`, per-task ACL bitmap | `xPortIsAuthorizedToAccessBuffer` against task regions | hand-written 5,323-line wrapper file plus 2,055-line assembly; per-task privileged syscall stack | none | privileged tasks unconstrained; O(N) handle scan; stack checks off |
| Zephyr userspace | memory domains of partitions with W^X checks; MPU reprogrammed in PendSV | gperf hash of kernel object addresses generated from the ELF's DWARF, per-thread permission bitmap | `K_SYSCALL_MEMORY_READ/WRITE` to `arch_buffer_validate` | generated marshallers from `__syscall` markers | thread abort only | supervisor code fully trusted; objects on stacks invisible to user mode |
| NuttX PROTECTED / KERNEL | one kernel blob, one user blob with a fixed-address `userspace_s` header; KERNEL adds address environments | pointers | none in PROTECTED stubs; MPU faults only | generated proxies and stubs from `syscall.csv`; nested syscall save area | task kill with `task_recover` | user pointers not validated |
| ThreadX | base kernel has no MPU; Modules (not in clone) load isolated code; M33 secure stacks via CMSE | pointers | n/a | n/a | n/a | n/a |
| RTEMS | none; stack checker after the fact | 32-bit object ids with class, API, node, index fields | n/a | n/a | n/a | everything |
| Hubris | 8 MPU regions per task, rewritten on every switch; fixed task set | `TaskId(u16)` = 10-bit index plus 6-bit generation; dead codes to stale ids | `safe_copy` validates both sides against region tables; leases checked on each borrow | SVC with arguments in r4-r10; 14 syscalls | supervisor reinit with generation bump; peers unblocked with dead code | supervisor crash reboots; uphill-send rule unenforced |
| Tock | MPU per process with a grow-both-ways app memory block; grants carved from process RAM | `ProcessId` minted anew on restart | `ProcessBuffer` liveness re-check on each `enter`; upcall pointer checked against executable memory | TRD104 fixed ABI; `SyscallFilter` per process | fault policy Panic, Restart, Stop | capsules trusted; kernel is one thread |

**Observations.**
- The two kernels that designed isolation in from the start (Hubris, Tock) have the smallest and clearest mechanisms: a generation-tagged id, a region table per task, validation on every cross-boundary access, and a supervisor or policy object outside the kernel. The three C kernels that retrofitted isolation each show the cost: FreeRTOS's 7,000 lines of hand-written wrappers and stubs, Zephyr's DWARF-scanning object table and 13,000 lines of Devicetree macros around it, NuttX's unvalidated pointers.
- Nobody in C offers capabilities (rights-bearing handles) on MCU-class hardware. FreeRTOS's ACL bitmap and Zephyr's permission bitmap are per-thread access lists, not derivable, grantable capabilities. 03 §7.2 (capabilities with rights, derivation, grant, generation revocation) is therefore unique in the C field, and Hubris's generation-plus-dead-code scheme is the proven way to implement its restart semantics (ADR-032).
- Tock's grants solve a problem every isolating kernel has and no C kernel addresses: where kernel state *about* a partition lives. Carving it from the partition's own memory makes restart free and makes kernel memory exhaustion impossible from user code. 03 §8 should adopt this for the isolated profile (ADR-033).
- Generated syscall plumbing (Zephyr from markers, NuttX from a CSV) is clearly better than hand-written wrappers (FreeRTOS). The hardware-as-data generator (04 §1) is the natural place to emit it.

---

## 8. Observability

| Kernel | Trace | Logging | Crash and fault | Debugger aids | Runtime monitors |
|---|---|---|---|---|---|
| FreeRTOS | 504 `trace*` hooks, all empty by default | none | `configASSERT` only | `pxCurrentTCB`, `uxTopUsedPriority`, queue registry | run-time counter without overflow protection |
| ThreadX | TraceX ring with object registry, inlined into every service, DWT timestamp, class filters | none | stack error handler | performance counters, `tx_thread_info_get` | execute log |
| Zephyr | CTF, SystemView, Percepio backends; async ring plus tracing thread | deferred cbprintf packaging; dictionary mode with strings in a DEVNULL section decoded from the ELF | `z_fatal_error`, coredump, ESF capture | thread analyzer, object core | runtime stats, spinlock validation |
| RTEMS | capture engine, record infrastructure, user extensions | none | fatal sources and codes, `_Terminate` | n/a | per-thread CPU time, stack checker |
| NuttX | `sched_note` typed event stream with runtime filters | syslog | `_assert` prints registers, stacks, backtraces, all tasks, pauses other CPUs, coredump | procfs | critmonitor: worst critical section, preemption-off, run time, with caller address and panic thresholds; IRQ monitor; CPU load |
| ChibiOS | 128-entry packed trace ring | none | `SV#` halts; RFCU fault collection | ROM `ch_debug` offsets table | statistics: worst and best critical-zone times |
| uC/OS-III | none | none | n/a | 92 `OSDbg_*` constants kept alive via volatile pointers | scheduler-lock and tick time, per-TCB interrupt-disable max, stat task |
| Hubris | `ringbuf!` static rings read by the debugger; no streaming | none by design | `FaultInfo` record, stack and registers preserved for the debugger | `no_mangle` statics; task layout from DWARF | GPIO profiling hooks |
| Tock | `trace_syscalls` | `debug!` buffer drained asynchronously | panic path prints every process | `ProcessStandardDebug` counters | timeslice expirations, dropped upcalls |

**Observations.**
- Each kernel has one strong piece and the rest is missing: ThreadX trace, Zephyr dictionary logging, NuttX monitors and crash dump, ChibiOS and uC/OS-III debugger exports, Hubris debugger-centric rings. No kernel ships trace plus deferred logging plus a retained crash record plus a versioned debug descriptor plus critical-section monitors as one coherent, compile-out set. 04 §7 specifies exactly that set; R-003 adds NuttX's critmonitor idea (worst-case masked time per thread *with the caller address*) as an OBS requirement because it is the piece that makes the latency promises of SPEC-002 self-verifying.
- ChibiOS's ROM offsets table and uC/OS-III's `OSDbg_*` constants are early forms of ADR-011's debug descriptor; both are unversioned and both proved that debugger vendors will consume them.

---

## 9. Configuration, portability, quality, evidence

| Kernel | Configuration | Port contract | Static allocation | Coding rules and evidence | Tests in tree |
|---|---|---|---|---|---|
| FreeRTOS | `FreeRTOSConfig.h` with `#ifndef` defaults and `#error`s | portmacro.h types and macros plus `pxPortInitialiseStack`, `xPortStartScheduler`, heap in port layer | dummy `Static*_t` mirrors with a sizeof assert (MISRA 11.3 deviation) | MISRA C:2012 with Coverity; CBMC and VeriFast proofs in the sibling repo | none (CI uses FreeRTOS/FreeRTOS) |
| ThreadX | `tx_user.h` with "max speed" and "min size" recipes; `TX_SAFETY_CRITICAL` gate | tx_port.h plus eight assembly files; TCB words 0-9 assembly-fixed | all control blocks and stacks are caller memory; public struct layout is the ABI | MISRA via 70 assembly shims; SGS-TÜV certificates (R-002 §3) | none in clone |
| Zephyr | Kconfig with three algorithm choices and many hidden selects; Devicetree | about 112 `arch_*` entry points | iterable sections, `K_*_DEFINE` | MISRA subset coding guidelines; `MISRA_SANE`; SIL 3 in progress on a 15 kLOC scope | twister (outside clone) |
| RTEMS | `confdefs.h` compile-time tables | `no_cpu` template: feature macros plus about 20 functions | static by default, "unlimited" option | spec-generated API headers; ESA QDP | tests outside clone |
| NuttX | Kconfig, about 1,800 lines in `sched/` alone | `up_*` functions; returned-frame switch | n/a (POSIX create) | `INVIOLABLES.md`; real-hardware `ostest` logs required | apps/ostest |
| ChibiOS | `chconf.h` checked by `chchecks.h` for every symbol and version | `port_*` plus `PORT_SETUP_CONTEXT`, `PORT_IRQ_PROLOGUE/EPILOGUE`, `port_timer_*` | caller-provided working areas | PC-lint MISRA annotations; `CH_CFG_HARDENING_LEVEL` | tests outside clone |
| uC/OS-III | `os_cfg.h` with mandatory defines | uC/CPU supplies critical sections, CLZ, timestamps | all caller-provided; `OSDbg_*` sizes | `OS_SAFETY_CRITICAL_IEC61508` start freeze | none |
| Hubris | `app.toml` to generated `kconfig.rs` | one `arm_m.rs` | everything static, sized at build | Rust; 128-byte panic messages | board tests |
| Tock | `KernelResources` trait per board | `MPU`, `SchedulerTimer`, chip crates | grants | TRDs with RFC 2119 keywords; Safety comments on every `unsafe` | some unit tests, QEMU, Treadmill HIL |

**Observations.**
- Static allocation of kernel objects is solved three ways: public struct layout as ABI (ThreadX, uC/OS-III, RIOT), dummy mirror types with a sizeof assert (FreeRTOS), or generated tables (Hubris). ADR-006 (generated `emb_<obj>_storage_t` from the real layout per configuration) is the Hubris approach applied to C and avoids both the ABI exposure and the mirror-drift risk.
- Configuration validation is strongest in ChibiOS (`chchecks.h` errors on any missing symbol and on a version mismatch) and ThreadX (`TX_SAFETY_CRITICAL` rejects unsafe option combinations). Both are cheap to adopt as generator checks (R-003).
- The evidence story is the field's weakest area. Only ThreadX is certified and its repository contains no test suite or certificate artifacts; Zephyr is certifying 15,000 of 2.4 million lines after the fact; FreeRTOS has formal proofs but no certification; RTEMS has a qualification data package produced by a space agency. uC/OS-III's safety-critical start freeze and ThreadX's safety gate are the only *mechanisms* that exist for a certified configuration. 05 §4 to §6 (requirements, traceability, coverage before code) is the only approach in the comparison that produces evidence as a by-product of development rather than as a retrofit.

---

## 10. Scorecard and gaps

| Mechanism | Best in the field | What nobody does | EmbLinkRTOS v0.2 position | Gap to exploit |
|---|---|---|---|---|
| Ready structure | RTEMS two-level bitmap; NIL table for tiny | one API over both a bitmap scheduler and a table scheduler | KRN-SCH-036, class interface KRN-SCH-038 | tiny profile table scheduler behind the class interface (ADR-036) |
| Scheduler lock | ThreadX, RTEMS: defer the switch, keep the ready structure current | n/a | SPEC-002 | none |
| ISR exit | PendSV lowest (4 kernels); ChibiOS fake frame for latency | measured comparison of the two on one board | SPEC-002 PendSV | measure both on STM32F4 before freezing the port |
| Critical sections | ChibiOS BASEPRI mask plus fast IRQs; key-based everywhere | a *tested bound* on the longest masked section | SPEC-002 key-based, zero-latency class, latency metrics | make the bound a requirement verified per release (R-003 §4) |
| Misuse detection | ChibiOS state checker; ThreadX separable validation layer | both at once, compiled out in release | SPEC-001 checked versus release | adopt both (R-003 §3) |
| Timebase | Zephyr, RTEMS, Hubris, Embassy 64-bit | n/a | SPEC-003 | none |
| Timeout structure | absolute sorted list (NuttX), RB-tree (RTEMS) | overhead-compensated arming (RIOT only) | ADR-010 absolute list | add arming-overhead calibration (R-003 §3) |
| Software timers | Zephyr drift-free; uC/OS-III condvar timer task | thread-context callbacks with synchronous control and drift-free re-arm together | SPEC-003 has all three | none; publish it |
| Wait and wake race | RTEMS wait-flag CAS; ThreadX sequence number; Zephyr superseded bit | small kernel with bounded masking | KRN-WAIT-004 to 007 | ADR-026: three-state wait flag plus superseded bit |
| Multi-object wait | ChibiOS events, RIOT thread_flags, Hubris notifications | object-to-notification binding in a C kernel with priority order | 03 §6.1 notifications | ADR-027 |
| Priority inheritance | RTEMS (all columns), uC/OS-III and ChibiOS (transitive, correct restore) | correct in every column in a small kernel | KRN-SYNC-009 to 013 | ADR-028: owned-mutex list, bounded transitive walk, timeout disinherit, deadlock detection, owner death |
| Zero copy | Hubris leases, Embassy slots, ChibiOS rendezvous | leases in a C kernel | 03 §6.5 pools, §6.6 ports | ADR-032: leases for Ports |
| Isolation | Hubris and Tock (designed in) | capabilities on MCU hardware in C; partition-local kernel storage in C | 03 §7, §8 | ADR-032 generation and dead codes, ADR-033 grants |
| Temporal protection | NuttX sporadic server; seL4 MCS and QNX APS (R-002 §5) | sliding-window budgets with idle sharing on an MCU | 03 §2.3 budgets | ADR-029 |
| Bounded preemption | ThreadX preemption threshold | n/a | none | ADR-031 decides (recommend reject for 1.0) |
| Ceiling protocol | RTIC compile-time ceilings (R-002 §5); RTEMS ceiling nodes | compile-time ceilings for threads in a C kernel | KRN-SYNC-012 | ADR-030 |
| Observability | ThreadX trace, Zephyr dictionary logs, NuttX monitors and crash dump | all of them as one compile-out set with a versioned descriptor | 04 §7, ADR-009, 011, 021 | add critmonitor-style worst-case with caller address (R-003 §3) |
| Static objects | Hubris generated tables; ChibiOS config checks | n/a | ADR-006 | add configuration consistency checks to the generator |
| Evidence | RTEMS QDP; ThreadX certificates | evidence as a by-product of development | 05 §4 to §6 | publish the harness and the matrix from M1 |

The gaps in the last column are the input to `R-003`.
