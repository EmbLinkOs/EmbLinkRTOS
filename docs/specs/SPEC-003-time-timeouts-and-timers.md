# SPEC-003 - Time Source, Timeouts, and Software Timers

**Status:** Draft for review. Specification work item 3 of the roadmap (07 §3).
**Requirements:** `docs/requirements/KRN-TIM.md` (KRN-TIM-001 to 016 restated; new from 017).
**Builds on:** SPEC-001 §7 (time types, conversions, blocking bounds); SPEC-002 (timer interrupt level, critical sections, preemption points); 03 §4; v0.1 §7; ADR-010, ADR-017, ADR-019.
**Toolchain constraints applied:** 09 §6 (no 64-bit atomics on 32-bit targets; byte atomics only on AVR), 09 §7 (division by constant is a library call on AVR).

---

## 1. Scope and terms

| Term | Meaning |
|---|---|
| **Kernel clock** | The monotonic time base of scheduling, timeouts, and software timers; read with `emb_time_now()` |
| **Tick** | The unit of the kernel clock, `CONFIG_EMB_TICK_NS` nanoseconds |
| **Periodic tick mode** | The hardware timer interrupts every tick and the kernel counts |
| **Tickless mode** | The hardware timer interrupts only at the next deadline; the kernel reads elapsed time from a free-running counter |
| **Timeout** | A deadline attached to a blocked thread, after which its wait ends with `TIMEOUT` |
| **Software timer** | A kernel object that runs a callback once or periodically |
| **Timer source** | The hardware timer the architecture or SoC port drives the kernel clock from |
| **Next deadline** | The earliest instant at which the kernel has anything to do |
| **Cycle counter** | A high-resolution counter for measurement, not for scheduling |
| **Wall clock** | Calendar time, optional, never used for timeouts |

## 2. Time domains

```
  emb_time_now()      kernel monotonic clock   ticks, 64-bit (32-bit tiny profile)   scheduling, timeouts, timers
  emb_cycles_now()    cycle counter            hardware cycles, 32 or 64 bits        measurement only
  emb_wallclock_*()   calendar time            ns since the Unix epoch, int64        logs, filesystems, protocols; optional subsystem
```

Wall clock is monotonic clock plus an offset maintained by a time-synchronization service or an RTC driver; adjusting it moves no deadline (KRN-TIM-002, KRN-TIM-031). The cycle counter may wrap quickly and is never compared across sleeps.

## 3. Kernel clock representation

- `emb_tick_t` is `uint64_t`, or `uint32_t` when `CONFIG_EMB_TICK_32BIT=y` (tiny profile).
- `CONFIG_EMB_TICK_NS` is the tick length; default 1 000 000 (1 ms). Any value from 1 000 (1 µs) to 100 000 000 (100 ms) is accepted; the port states which values its timer source can produce exactly.
- The clock starts at zero at kernel start and never moves backward (KRN-TIM-001).

### 3.1 Reading a 64-bit clock on a 32-bit core

EmbCC refuses 8-byte atomics on 32-bit targets (09 §6), and on AVR only byte accesses are atomic. `emb_time_now()` therefore reads the clock in one of two ways, chosen per architecture at build time:

| Mechanism | Where | How |
|---|---|---|
| **Sequence lock** | Cortex-M, RISC-V, native | The writer (timer path) increments a sequence word, writes both halves, increments again; the reader loops until it sees an even, unchanged sequence. No masking; ISR-safe; bounded by the number of timer updates during the read, which is at most one in practice |
| **Critical section** | AVR | `in SREG; cli; read 4 bytes; out SREG`. A few cycles, and interrupts are already short on this core |

Both are O(1) and `@ctx thread isr prekernel`. In tickless mode the reader adds the elapsed hardware count since the last clock update, converted to ticks (§4.2), so `emb_time_now()` has hardware resolution without an interrupt per tick.

## 4. Clock modes

Both modes have identical semantics and pass identical tests (KRN-TIM-017); they differ only in cost and in resolution.

### 4.1 Periodic tick

The timer source interrupts every tick. The timer ISR (kernel-aware, highest kernel-aware level by default, SPEC-002 §11):

1. `ticks += 1` under the sequence lock or critical section;
2. expire every timeout and timer whose deadline is at or before `ticks` (§5.3);
3. charge the quantum of the running thread if time slicing applies to its level (§8);
4. set `reschedule_pending` if anything became READY or the quantum expired.

Resolution is one tick. Cost is one interrupt per tick whether or not anything is due, which is what tickless removes.

### 4.2 Tickless

The timer source is a free-running counter with a compare register (or an equivalent the port can emulate). The kernel programs the compare register for the **next deadline** and lets the core idle until then.

```
next_deadline = min( earliest timeout,
                     earliest software timer,
                     quantum expiry of the running thread if slicing applies,
                     next budget replenishment            (temporal protection, when enabled),
                     now + MAX_INTERVAL )                 (hardware counter range guard)
```

- `MAX_INTERVAL` is the longest sleep the counter can represent without ambiguity, declared by the port (`emb_arch_timer_max_ticks()`); a deadline beyond it is reached in several hops, each costing one interrupt.
- On **any** wake (timer interrupt or any other interrupt), the first kernel action is to update the clock: read the counter, compute elapsed ticks since the last update, add them, and process expiries before any scheduling decision (KRN-TIM-019). This keeps `emb_time_now()` correct even when the wake was not the timer.
- The next deadline is recomputed and reprogrammed whenever the set of deadlines changes: a timeout is armed or removed, a timer started or stopped, the running thread changes (quantum), or on wake. Reprogramming is O(1) given the ordered structures of §5.
- When no deadline exists (`EMB_WAIT_FOREVER` only), the compare interrupt is disabled and the clock is updated purely on wake; idle may enter the deepest state the power policy allows (04 §4).

Tickless requires a port whose counter keeps running in the chosen sleep state; the port declares which states keep it. The resolution of the kernel clock is still `CONFIG_EMB_TICK_NS`; a hardware period finer than the tick gives `emb_time_now()` sub-tick accuracy only in the sense that updates are exact, not that the API unit changes (KRN-TIM-009).

### 4.3 Architecture timer contract

```c
emb_status_t emb_arch_timer_init(void);                     /* configure the source for the selected mode */
emb_arch_timer_raw_t emb_arch_timer_now_raw(void);          /* free-running counter, tickless mode */
void         emb_arch_timer_set_deadline_raw(emb_arch_timer_raw_t when);   /* program the compare */
void         emb_arch_timer_cancel(void);                   /* no deadline */
uint32_t     emb_arch_timer_hz(void);                       /* counter frequency */
emb_tick_t   emb_arch_timer_max_ticks(void);                /* MAX_INTERVAL */
/* the port's timer ISR calls: */
void         embk_time_timer_isr(void);                      /* periodic: tick; tickless: deadline reached */
void         embk_time_on_wake(void);                        /* tickless: called at the outermost exit of any interrupt that ended idle */
```

Conversion between raw counts and ticks is exact when one period divides the other, which the configuration system checks; otherwise the port provides a scaled conversion with a documented maximum error per hop, and the error never accumulates because the clock is re-based from the counter at each update.

### 4.4 Choosing a mode

| Profile | Default | Reason |
|---|---|---|
| tiny (ATmega328P) | periodic tick, 1 ms, Timer1 compare | Simplest, 16-bit counter limits tickless gains; tickless available as an option |
| base | tickless when the SoC has a 32-bit timer that runs in sleep; else periodic | Power |
| isolated, multicore | tickless | Power, deadline precision |
| native | tickless over a virtual clock | Deterministic tests: the clock jumps to the next deadline when all simulated CPUs idle |

## 5. Timeouts

### 5.1 Node and structure

Every thread control block carries one intrusive `timeout_node`; every software timer carries one. The default structure (ADR-010) is a **doubly linked intrusive list ordered by absolute deadline**:

| Operation | Cost | Notes |
|---|---|---|
| arm (insert) | O(n) over armed nodes | walks from the head; the common case (a new deadline later than most) can walk from the tail |
| disarm (remove) | O(1) | doubly linked |
| peek next deadline | O(1) | head |
| expire at `now` | O(k) for k expired | pops from the head while `deadline <= now` |

The interface (`embk_timeout_arm`, `embk_timeout_disarm`, `embk_timeout_next`, `embk_timeout_expire`) hides the structure; a hierarchical timing wheel is a drop-in alternative for a profile whose measurements show the list walk on the critical path (KRN-TIM-021). On SMP the structure is per CPU (FUTURE; the interface already takes the CPU).

### 5.2 Deadlines and wrap

- A deadline is an `emb_instant_t`. Arming with a duration computes `now + duration` saturating at `EMB_TICK_MAX`; a saturated deadline is treated as forever.
- **32-bit profile:** comparisons use the signed difference `(int32_t)(a - b)`, which is correct as long as every armed deadline is within `2^31 - 1` ticks of `now`. `EMB_TIMEOUT()` and the arming path therefore clamp a finite timeout to `EMB_TIMEOUT_MAX_TICKS = 2^31 - 1` (about 24.8 days at 1 ms), and a `_until` deadline further away than that is rejected with `EMB_EOVERFLOW` (KRN-TIM-022). The 64-bit profile has no such limit in practice (584 years at 1 ns).
- A deadline at or before `now` is the `EMB_NO_WAIT` path (API-024).

### 5.3 Expiry

Expiry runs in the timer interrupt (periodic) or in the deadline interrupt and on wake (tickless), always with the timeout structure protected by a critical section, and always in deadline order:

```
expire(now):
  while head and head.deadline <= now:
      node = pop(head)
      if node is a thread timeout:  embk_wait_wake(thread, TIMEOUT)      -- the wait protocol (SPEC-004) decides the race
      else:                          embk_timer_expired(timer)           -- §7.4
  reprogram next deadline (tickless)
```

Expiry never calls application code directly except for `ISR_CONTEXT` timers (§7.4). Two nodes with the same deadline expire in arming order.

## 6. Sleep and yield

```c
emb_status_t emb_thread_sleep(emb_duration_t d);          /* @ctx thread  @blocks timeout  O(timeouts) arm */
emb_status_t emb_thread_sleep_until(emb_instant_t t);     /* @ctx thread  @blocks timeout */
void         emb_thread_yield(void);                      /* @ctx thread  @blocks no  O(1) */
```

- Sleep blocks the caller with reason `SLEEP` until the deadline (KRN-THR-009); the result is `EMB_OK` on expiry, `EMB_ECANCELED` if canceled. There is no object, so no `DESTROYED`.
- A zero duration, or a deadline in the past, behaves exactly as `emb_thread_yield()`: the caller moves behind equal-priority READY threads and returns `EMB_OK` (KRN-SCH-008 applies: never a lower-priority thread).
- Periodic loops use `sleep_until` with a running deadline (SPEC-001 §13); the kernel does not offer a "sleep for period minus elapsed" helper because `sleep_until` is the correct primitive.

## 7. Software timers

### 7.1 Object

```c
EMB_DECLARE_HANDLE(emb_timer_t);

typedef void (*emb_timer_fn_t)(emb_timer_t timer, void *arg);

typedef struct emb_timer_attr {
    const char    *name;
    emb_timer_fn_t fn;            /* required */
    void          *arg;
    emb_workq_t    workq;         /* EMB_HANDLE_NULL = system work queue */
    uint8_t        flags;         /* EMB_TIMER_PERIODIC | EMB_TIMER_ISR_CONTEXT */
} emb_timer_attr_t;
```

The object holds the timeout node, the period, the expiry count, the overrun count, and an embedded work item (03 §6.2) used to run the callback in thread context.

### 7.2 API

| Function | `@ctx` | `@blocks` | `@time` | Notes |
|---|---|---|---|---|
| `emb_timer_init(storage, attr, out)` | prekernel thread | no | O(1) | |
| `emb_timer_destroy(t)` | thread | no | O(1) | misuse if a callback is pending or running (§7.5) |
| `emb_timer_start(t, emb_duration_t first)` | thread isr | no | O(timeouts) | one-shot, or first expiry of a periodic timer whose period was set by `start_periodic` |
| `emb_timer_start_at(t, emb_instant_t first)` | thread isr | no | O(timeouts) | absolute form |
| `emb_timer_start_periodic(t, emb_duration_t first, emb_duration_t period)` | thread isr | no | O(timeouts) | period must be nonzero, `EMB_EINVAL` otherwise |
| `emb_timer_stop(t, emb_timer_state_t *out_state)` | thread isr | no | O(1) | disarms; cancels a pending work item; reports IDLE, PENDING, or RUNNING |
| `emb_timer_stop_sync(t, emb_timeout_t)` | thread | timeout | O(1) + wait | stops and waits for a running callback to finish; misuse from the callback itself |
| `emb_timer_restart(t)` | thread isr | no | O(timeouts) | re-arms with the last duration or period from now |
| `emb_timer_is_running(t)` | thread isr | no | O(1) | |
| `emb_timer_remaining(t, emb_duration_t *out)` | thread isr | no | O(1) | zero when not running |
| `emb_timer_get_counts(t, uint32_t *out_expiries, uint32_t *out_overruns)` | thread isr | no | O(1) | |

Starting a running timer re-arms it (the previous deadline is dropped); the counts are not reset. All ISR-safe operations preserve the caller's mask (KRN-IRQ-033).

### 7.3 Execution context

Default: the callback runs **on a work queue in thread context** (ADR-019, KRN-TIM-013). At expiry the kernel submits the timer's embedded work item to the configured queue, which is O(1) and ISR-safe; the queue's thread runs the callback later, at the queue's priority. Callback latency is therefore the work queue's latency, which the application controls by choosing the queue and its priority (03 §6.2). A callback may block, with the documented consequence that it delays every other item on that queue.

With `EMB_TIMER_ISR_CONTEXT` the callback runs **directly in the expiry path**, in interrupt context, with the timeout structure's critical section released but kernel-aware interrupts still masked by the timer interrupt's level. It is bounded, may call only ISR-safe API, and may not block (KRN-TIM-014). Checked builds fault on a blocking call from it. This flag exists for the few callbacks that must have interrupt-level latency (toggle a pin, kick a DMA); everything else uses the default.

### 7.4 Expiry, periodic re-arm, and overrun

```
embk_timer_expired(timer):
  timer.expiries += 1
  if timer.period != 0:
      timer.deadline += timer.period            -- from the PREVIOUS deadline: no drift (KRN-TIM-015)
      while timer.deadline <= now:               -- missed whole periods (system was asleep or overloaded)
          timer.deadline += timer.period
          timer.overruns += 1
      arm(timer)                                 -- before the callback, so stop() inside the callback works
  if ISR_CONTEXT:  timer.fn(timer, timer.arg)
  else:            submitted = emb_work_submit(timer.work)   -- idempotent: if the previous callback is still
                                                              -- queued, this expiry coalesces into it
                   if not submitted: timer.overruns += 1
```

Overruns are therefore counted in two cases: whole periods missed because the deadline was already past at expiry time, and callbacks coalesced because the previous one had not run. Both are visible through `emb_timer_get_counts()` and the trace point `timer_overrun`. A periodic callback that wants to know how many periods it represents reads the counts.

### 7.5 Lifecycle rules

- `emb_timer_destroy()` while the callback is pending on a queue or running is misuse: a kernel fault in checked builds and `EMB_EBUSY` in release builds (KRN-TIM-028). The correct sequence is `emb_timer_stop_sync()` then `destroy`, or `emb_timer_stop()` and checking the returned state.
- `emb_timer_stop()` from within the timer's own callback returns `RUNNING` and is the normal way a periodic timer ends itself; `stop_sync` from within the callback is misuse (it would wait for itself).
- A timer's `arg` and `fn` are fixed at init; changing them means destroy and init.

## 8. Time slicing hook

When `CONFIG_EMB_TIME_SLICING=y` and the running thread's priority level has a nonzero quantum (KRN-SCH-037), the per-CPU state holds `quantum_deadline`, set at dispatch to `now + quantum`. It is not a timer object: it is one more input to the next-deadline computation (tickless) or one more comparison in the tick ISR (periodic). At expiry, if another READY thread exists at the same effective priority, the running thread is moved to the tail of its class and `reschedule_pending` is set (v0.1 §5.8); otherwise the quantum is simply renewed. Temporal-protection budgets (03 §2.3) will plug into the same place when their specification lands.

## 9. Cycle counter

```c
emb_cycles_t emb_cycles_now(void);        /* @ctx thread isr prekernel  O(1); 32 or 64 bits per port */
uint32_t     emb_cycles_hz(void);
uint64_t     emb_cycles_to_ns(emb_cycles_t c);
```

For measurement (benchmarks, trace timestamps, critical-section statistics). At least 1 MHz where the hardware allows. Sources: `DWT_CYCCNT` on Cortex-M3 and up, a free-running timer on Cortex-M0+ (declared by the SoC), `mcycle` on RISC-V, Timer1 on the ATmega328P, the host's monotonic clock on native. It is never the scheduling clock and never used for timeouts (KRN-TIM-012).

## 10. Wall clock (optional subsystem)

```c
emb_status_t emb_wallclock_get(int64_t *out_ns_since_epoch);     /* CONFIG_EMB_WALLCLOCK */
emb_status_t emb_wallclock_set(int64_t ns_since_epoch);          /* privileged; sets the offset */
emb_status_t emb_wallclock_adjust(int64_t delta_ns);             /* slew or step, per attribute */
```

Wall clock is `monotonic + offset`. Setting or adjusting it changes the offset only; no timeout, timer, or deadline moves (KRN-TIM-031). An RTC driver may seed it at boot and a synchronization service may discipline it; both are middleware. Nothing in the kernel reads it.

## 11. Conversions and documentation

The macros and rules of SPEC-001 §7.3 apply. In addition:

- On AVR, integer division by a non-power-of-two constant is a library call (09 §7), so the tiny profile's default `CONFIG_EMB_TICK_NS` and timer prescaler are chosen so that millisecond and microsecond conversions are shifts or small multiplications; the port documents which conversions are cheap.
- Every port documents, in its `docs/ports/<arch>.md` time section: timer source, counter width, frequency and the tick values it produces exactly, `MAX_INTERVAL`, tickless capability and the sleep states in which the counter runs, wake latency from each state, and cycle counter source (KRN-TIM-034).

## 12. Checked-build diagnostics

| Condition | Detection |
|---|---|
| Arming a timeout node that is already armed | node state check in `embk_timeout_arm` |
| `_until` deadline beyond the 32-bit profile range | range check, `EMB_EOVERFLOW` in both build kinds |
| Blocking call from an `ISR_CONTEXT` timer callback | context check at the blocking entry |
| `emb_timer_destroy` with a pending or running callback | state check |
| `emb_timer_stop_sync` from the timer's own callback | identity check against the running work item |
| Clock read that observes the sequence lock odd for more than N iterations | debug counter, indicates a stuck writer |

## 13. Per-architecture timer sources

| | Cortex-M | RISC-V | AVR (ATmega328P) | native |
|---|---|---|---|---|
| Periodic source | `SysTick` (24-bit down counter) | `mtime`/`mtimecmp` (64-bit) or SoC timer | Timer1 CTC compare, prescaler chosen for exact 1 ms | virtual clock |
| Tickless source | SoC 32-bit timer or low-power timer that runs in sleep; `SysTick` only where no better source exists (24 bits, stops in deep sleep) | `mtime`/`mtimecmp` | Timer1 compare, 16-bit: `MAX_INTERVAL` about 4 s at 16 MHz with prescaler 1024 | virtual clock |
| Counter in deep sleep | SoC-dependent, declared in the hardware description | platform-dependent | Timer1 stops in power-down; tickless limited to idle and ADC-noise-reduction modes | n/a |
| Cycle counter | `DWT_CYCCNT` (M3 and up); SoC timer on M0+ | `mcycle`, `mcycleh` on RV32 | Timer1 | host monotonic |
| Clock read | sequence lock | sequence lock | critical section | sequence lock |

## 14. Reference model

The timeout structure, the expiry ordering, periodic re-arm with overrun counting, and the next-deadline computation are modeled alongside the wait protocol in work item 4. The model exposes `advance_to(deadline)` and `fire_timer()` operations and an oracle for the set and order of expired nodes; differential tests then compare the native port's behavior against it under randomized arm, disarm, start, stop, and advance sequences.

## 15. Open points for review

1. Zero-duration sleep behaves as yield (as in Zephyr). The alternative is to return immediately without yielding. Recommendation: yield.
2. Cortex-M tickless default source: a SoC 32-bit timer declared in the hardware description, with `SysTick` as the fallback. Recommendation: SoC timer, because `SysTick` stops in deep sleep and has 24 bits.
3. The 32-bit profile's maximum finite timeout of `2^31 - 1` ticks (24.8 days at 1 ms), with longer `_until` deadlines rejected as `EMB_EOVERFLOW`. Recommendation: accept; applications needing longer waits chain them.
4. `emb_timer_stop_sync` in 1.0 versus deferring it. Recommendation: include; without it there is no correct way to destroy a timer with a running callback.
