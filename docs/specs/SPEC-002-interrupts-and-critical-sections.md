# SPEC-002 - Interrupt, Exception, and Critical-Section Model

**Status:** Draft for review. Specification work item 2 of the roadmap (07 §3).
**Requirements:** `docs/requirements/KRN-IRQ.md` (KRN-IRQ-001 to KRN-IRQ-036; v0.1 identifiers restated, new ones from 017).
**Builds on:** SPEC-001 (contexts, misuse behavior, annotations); 03 §1.2, §2.4 (per-CPU state), §10 (faults); v0.1 §3.7, §6, §4.5.
**Toolchain constraints applied:** document 09 §5 and §8 (naked functions, `.S` trap entry on RISC-V, `signal` handlers on AVR, PRIMASK and BASEPRI vocabulary on Cortex-M).

---

## 1. Scope and terms

| Term | Meaning in this specification |
|---|---|
| **Interrupt** | An asynchronous hardware event that transfers control to a handler |
| **Exception** | A synchronous event raised by the executing instruction: fault, trap, system call |
| **ISR** | The handler that runs in interrupt context for one interrupt source |
| **Kernel-aware interrupt** | An interrupt whose handler may call ISR-safe kernel API; the kernel may mask it |
| **Kernel-independent interrupt** | An interrupt the kernel never masks; its handler may not call the kernel |
| **Kernel masking level** | The interrupt priority boundary between the two classes, `CONFIG_EMB_IRQ_KERNEL_LEVEL` |
| **Critical section** | A region with kernel-aware interrupts masked on the current CPU, entered with `emb_irq_lock()` |
| **Scheduler lock** | A region in which the current thread cannot be preempted by another thread; interrupts still run |
| **Preemption point** | A place where the kernel may switch the running thread |
| **Reschedule pending** | Per-CPU flag: a scheduling decision is owed at the next preemption point |
| **Outermost exit** | Return from the only active interrupt on this CPU, back to thread context |

## 2. Per-CPU context state

The per-CPU state of 03 §2.4 gains the fields this specification needs:

```
cpu[n]:
  current_thread
  irq_nesting_depth        0 in thread context; N inside N nested kernel-aware interrupts
  sched_lock_depth         scheduler lock nesting
  reschedule_pending       bool
  irq_lock_depth           checked builds only: critical-section nesting, for LIFO and misuse checks
  irq_lock_enter_cycles    optional statistics: start of the current outermost critical section
  irq_lock_max_cycles      optional statistics: longest critical section observed
  isr_stack                base and size of the dedicated interrupt stack, where one exists
```

### 2.1 State machine

```
             kernel start
  PREKERNEL ---------------> THREAD (depth 0)
                               |   ^
         kernel-aware IRQ      |   | outermost exit: if reschedule_pending && sched_lock_depth == 0
         entry: depth 1        |   |                 then switch threads before returning
                               v   |
                             ISR (depth 1) <----+
                               |                | nested exit: depth-1, never switches
         nested IRQ: depth+1   |                |
                               v                |
                             ISR (depth 2..N) --+

  Kernel-independent interrupts are outside this machine: they do not touch cpu[n] and may
  preempt any state, including a critical section.
```

`emb_context()` (SPEC-001 §5.1) is derived from this state: `PREKERNEL` before start, `ISR` when `irq_nesting_depth > 0`, else `THREAD`. On architectures with a hardware exception-active register (Cortex-M `IPSR`) the port may read that instead of the counter; the result must be identical.

## 3. Interrupt classes

### 3.1 Kernel-aware interrupts

- Priority at or below the kernel masking level.
- Masked by `emb_irq_lock()` and by the kernel's own critical sections.
- May call any function annotated `@ctx thread isr` (ISR-safe), and nothing else.
- Enter and exit through the kernel's interrupt prologue and epilogue (hardware-provided or `EMB_ISR` macro, §6).
- May wake threads; the wake sets `reschedule_pending`, and the switch happens at the outermost exit.

### 3.2 Kernel-independent interrupts

- Priority above the kernel masking level.
- Never masked by the kernel, so their latency is the hardware's latency plus nothing from EmbLinkRTOS.
- May not call any kernel function, read kernel state, or touch kernel objects. They communicate with the rest of the system through lock-free single-producer structures of their own and, when they need the kernel, by pending a kernel-aware software interrupt with `emb_arch_irq_pend_soft()`, whose handler does the kernel work.
- Exist only where the architecture provides interrupt priority masking (Cortex-M Armv7-M and Armv8-M with `BASEPRI`; RISC-V with a CLIC or an interrupt controller that supports threshold masking). On Armv6-M (PRIMASK only), AVR (one global enable), and the native port, the class is unavailable and `CONFIG_EMB_IRQ_ZERO_LATENCY` is rejected at configuration time.

### 3.3 Configuration

```
CONFIG_EMB_IRQ_ZERO_LATENCY    bool   enable the kernel-independent class (architecture-gated)
CONFIG_EMB_IRQ_KERNEL_LEVEL    int    highest generic priority that may call the kernel (§8)
CONFIG_EMB_IRQ_NESTING         bool   allow kernel-aware interrupts to nest (architecture-gated)
CONFIG_EMB_IRQ_MAX_NESTING     int    depth the interrupt stack is sized for; exceeding it is a fault in checked builds
CONFIG_EMB_IRQ_LOCK_STATS      bool   measure critical-section durations with the cycle counter
CONFIG_EMB_IRQ_DYNAMIC         bool   runtime `emb_irq_connect()`; otherwise the vector table is generated and static
```

## 4. Critical sections

### 4.1 Primitives

```c
typedef unsigned int emb_irq_key_t;             /* the architecture's saved mask state */

emb_irq_key_t emb_irq_lock(void);               /* @ctx thread isr prekernel  @blocks no  @time O(1) */
void          emb_irq_unlock(emb_irq_key_t key); /* @ctx thread isr prekernel  @blocks no  @time O(1) */
bool          emb_irq_is_locked(void);           /* @ctx thread isr prekernel  @blocks no  @time O(1) */
```

`emb_irq_lock()` masks kernel-aware interrupts on the current CPU and returns the previous mask state. `emb_irq_unlock(key)` restores exactly that state. Nesting is therefore natural and costs nothing: the inner unlock restores a state in which interrupts were already masked. Unlocks must be in reverse order of locks (LIFO); checked builds verify it with `irq_lock_depth`.

The primitives are `static inline __attribute__((always_inline))` wrappers over `emb_arch_irq_lock()` and `emb_arch_irq_unlock()` with a `"memory"` clobber, so they are both a hardware mask and a compiler barrier (09 §5). They never allocate, never block, never yield.

| Architecture | `emb_irq_lock()` | Key |
|---|---|---|
| Cortex-M Armv7-M, Armv8-M, zero-latency on | `mrs basepri; msr basepri_max, #KERNEL_LEVEL` | previous `BASEPRI` |
| Cortex-M Armv7-M, Armv8-M, zero-latency off | `mrs primask; cpsid i` | previous `PRIMASK` |
| Cortex-M Armv6-M | `mrs primask; cpsid i` | previous `PRIMASK` |
| RISC-V | `csrrci mstatus, MIE` | previous `mstatus` |
| AVR | `in SREG; cli` | previous `SREG` |
| native | simulator lock on the current virtual CPU | previous lock state |

### 4.2 Rules inside a critical section

- Bounded and short: the kernel's own critical sections have a per-architecture cycle budget (§10) that is measured, not assumed.
- No blocking, no thread-only API, no scheduler lock operations that would reschedule, no loops whose bound depends on data (KRN-RT-002, KRN-IRQ-014).
- An ISR-safe kernel call made inside a critical section preserves the caller's mask: the kernel never unmasks interrupts behind a caller's back (KRN-IRQ-033). Kernel internals therefore use the same key-based primitive and restore what they found.
- A critical section never spans a preemption point. If a thread blocks while a critical section it opened is still active, that is misuse (SPEC-001 §5.3) and a fault in checked builds.

### 4.3 Multiprocessor

`emb_irq_lock()` protects against interrupts and preemption on the **current CPU only**. Data shared across CPUs is protected with a spinlock that also masks:

```c
emb_status_t emb_spin_lock(emb_spinlock_t *lock, emb_irq_key_t *out_key);   /* masks, then spins */
void         emb_spin_unlock(emb_spinlock_t *lock, emb_irq_key_t key);
```

On uniprocessor builds the spinlock compiles to the critical section alone (KRN-SMP-002). Lock ordering rules belong to the SMP specification; this specification only fixes that a spinlock is never held across a blocking call and is always acquired with interrupts masked.

### 4.4 Scoped form

No scoped macro is provided in C (a `for`-trick macro evaluates its argument once but hides control flow and violates the header rules of SPEC-001 §8). The C++ wrappers provide an RAII guard.

## 5. Scheduler lock

```c
void     emb_sched_lock(void);        /* @ctx thread  @blocks no  @time O(1) */
void     emb_sched_unlock(void);      /* @ctx thread  @blocks no  @time O(1), plus one reschedule at depth 0 */
unsigned emb_sched_lock_depth(void);  /* @ctx thread isr  @blocks no */
```

- Thread context only. In an ISR the scheduler is implicitly non-preemptive until the outermost exit, so a scheduler lock there is misuse.
- Nestable by depth counter (KRN-SCH-017). Interrupts are not masked (KRN-SCH-019); kernel-aware ISRs still run and may make threads READY, which sets `reschedule_pending` (KRN-SCH-016).
- The unlock that brings the depth to zero performs the pending reschedule immediately, in the unlocking thread's context (KRN-SCH-018).
- Blocking with a nonzero timeout while locked is misuse (SPEC-001 §5.3). Yield while locked is a no-op that keeps `reschedule_pending` set.
- A scheduler lock is a latency hazard for every higher-priority thread: it has the same cycle budget discipline as a critical section (§10) and emits the trace points `sched_lock` and `sched_unlock` with duration when tracing is on.
- It is not a synchronization primitive: it protects nothing against interrupts, DMA, other CPUs, or kernel-independent handlers (v0.1 §4.5).

## 6. Preemption points and reschedule on exit

### 6.1 The only places a thread switch may occur

| Point | Condition |
|---|---|
| **P1** Outermost exit of a kernel-aware interrupt to thread context | `reschedule_pending` and `sched_lock_depth == 0` |
| **P2** Inside a thread-context kernel operation that changes schedulability: block, wake, yield, priority change, budget action, thread exit, `emb_sched_unlock()` reaching depth 0 | not inside a critical section and `sched_lock_depth == 0` |
| **P3** Kernel start | the first thread is launched |

Nowhere else. In particular: never inside a nested interrupt (KRN-SCH-021, KRN-IRQ-013), never inside a critical section, never at a tick that has not changed any thread's state (KRN-SCH-004). Application code that makes no kernel call is preempted only through P1.

### 6.2 How an ISR causes a switch

1. An ISR-safe operation makes a thread READY whose effective priority exceeds the current thread's, or otherwise changes the scheduling decision.
2. The kernel sets `cpu.reschedule_pending` and calls `emb_arch_reschedule_pend()`.
3. At the outermost exit, the architecture's epilogue sees the flag (or the pended switch exception runs), clears it, and performs the context switch: save the interrupted thread's remaining context, select the highest eligible thread, restore it, return into it.

| Architecture | Mechanism for P1 |
|---|---|
| Cortex-M | `emb_arch_reschedule_pend()` sets `PendSV`. PendSV is configured at the lowest exception priority, so it runs exactly when every other exception has returned: the hardware provides the outermost-exit check. Kernel-aware handlers are plain C functions (09 §8); the PendSV handler is `naked` and switches `PSP` between thread stacks. |
| RISC-V | The trap entry (a `.S` file, 09 §8) saves caller-saved registers on the interrupt stack, increments `irq_nesting_depth`, dispatches in C, and at depth 1 on the way out checks `reschedule_pending`; if set it saves the callee-saved registers into the interrupted thread's context, selects, restores, and `mret`s into the next thread. `emb_arch_reschedule_pend()` sets the flag only; on cores with a software interrupt (`MSIP`) the kernel may also use it to force a trap from thread context. |
| AVR | No hardware nesting or pend mechanism. Kernel-aware ISRs are declared with `EMB_ISR(vector)`, which expands to a `naked` vector that saves the full register file on the interrupted thread's stack, calls the C body, then calls the epilogue; the epilogue performs the switch when pending, else restores the same context and `reti`s. Handlers that never call the kernel use `EMB_ISR_RAW(vector)`, an ordinary `signal` handler that saves only what the compiler needs (09 §8). |
| native | The simulator stops the running host thread at a safe point, runs the handler on an interrupt context, and applies P1 on return. |

### 6.3 Thread-context switches (P2)

A thread-context kernel operation that changes schedulability ends with `embk_sched_reschedule_if_needed()`, which, when not locked and not in a critical section, calls `emb_arch_switch_to(next)`. On Cortex-M this too is done by pending PendSV and letting it run at return from the SVC or at the next instruction boundary with interrupts enabled, so there is exactly one switch path per architecture; on RISC-V and AVR it is a direct call into the switch routine.

## 7. Nesting and stacks

### 7.1 Nesting

- Kernel-aware interrupts nest where the architecture permits and `CONFIG_EMB_IRQ_NESTING=y`: Cortex-M always (NVIC preemption), RISC-V only when the trap handler re-enables `MIE` after saving `mepc`, `mcause`, and `mstatus` and the interrupt controller provides preemption levels, AVR never (kernel-aware handlers run with `I` clear).
- `irq_nesting_depth` is observable (`emb_irq_nesting_depth()`, KRN-IRQ-012) and bounded by `CONFIG_EMB_IRQ_MAX_NESTING`; exceeding the bound is a kernel fault in checked builds.
- Only the outermost exit may switch threads (§6).

### 7.2 Interrupt stacks

| Architecture | Where interrupt frames live | Accounting |
|---|---|---|
| Cortex-M | Dedicated main stack (`MSP`); threads run on `PSP`. Hardware stacks 8 words (plus 18 with an active FPU context) per level, then the handler's frame | `CONFIG_EMB_ISR_STACK_SIZE` sized from `CONFIG_EMB_IRQ_MAX_NESTING` and the handlers' `-fstack-usage` |
| RISC-V | Dedicated per-CPU interrupt stack, entered by swapping `sp` with `mscratch` at trap entry | same |
| AVR | On the interrupted thread's stack (no alternative on this core) | every thread stack budget includes `CONFIG_EMB_AVR_ISR_STACK_RESERVE`, the worst single kernel-aware handler frame plus the full-context save of `EMB_ISR` (33 bytes) |
| native | Host-provided | n/a |

Interrupt stack overflow is detected in checked builds with a fill pattern checked at the outermost exit, and by `MSPLIM` on Armv8-M where the toolchain exposes it (09 §10 gap 1).

## 8. Interrupt management API

### 8.1 Priority numbering

Generic interrupt priorities follow the thread convention: a larger number is more urgent. `0` is the least urgent kernel-aware level. The architecture port maps generic levels to hardware encodings (NVIC's inverted numbering and its implemented priority bits, PLIC priorities, CLIC levels); the hardware description stores generic levels and the generator emits encoded values.

```
0 .. CONFIG_EMB_IRQ_KERNEL_LEVEL        kernel-aware
CONFIG_EMB_IRQ_KERNEL_LEVEL+1 .. MAX    kernel-independent (only with CONFIG_EMB_IRQ_ZERO_LATENCY)
EMB_IRQ_LEVEL_KERNEL_MAX                 alias of CONFIG_EMB_IRQ_KERNEL_LEVEL
EMB_IRQ_LEVEL_MAX                        highest level the hardware implements
```

A configuration whose kernel level is the hardware maximum has no kernel-independent class; the configuration system reports that as information, not an error.

### 8.2 Functions

```c
typedef uint16_t emb_irq_t;            /* interrupt number from the generated SoC table */

emb_status_t emb_irq_enable(emb_irq_t irq);                    /* @ctx thread isr prekernel */
emb_status_t emb_irq_disable(emb_irq_t irq);                   /* @ctx thread isr prekernel */
bool         emb_irq_is_enabled(emb_irq_t irq);                /* @ctx thread isr prekernel */
emb_status_t emb_irq_set_level(emb_irq_t irq, uint8_t level);  /* @ctx thread prekernel; EMB_EINVAL above EMB_IRQ_LEVEL_MAX */
emb_status_t emb_irq_pend(emb_irq_t irq);                      /* @ctx thread isr; software-trigger where supported */
emb_status_t emb_irq_clear_pending(emb_irq_t irq);             /* @ctx thread isr */
emb_status_t emb_irq_connect(emb_irq_t irq, emb_isr_fn_t fn, void *arg);  /* only with CONFIG_EMB_IRQ_DYNAMIC; @ctx thread prekernel */
```

All are `@blocks no`, `@time O(1)`. The default is a **generated static vector table**: the hardware description binds each interrupt to its handler symbol at build time, so no RAM table and no runtime connect exist in the tiny and base profiles unless `CONFIG_EMB_IRQ_DYNAMIC` is selected (then a RAM table of `(fn, arg)` pairs is indexed by the first-level handler).

### 8.3 Writing a handler

```c
EMB_ISR(USART2_IRQn)            /* kernel-aware: may call @ctx thread isr functions */
{
    uint8_t byte = USART2->RDR;
    (void)emb_notify_set(g_rx_thread, RX_BIT);      /* sets reschedule_pending if needed */
}

EMB_ISR_RAW(TIM1_CC_IRQn)       /* kernel-independent or raw: no kernel calls allowed */
{
    capture_buf[idx++ & MASK] = TIM1->CCR1;
}
```

`EMB_ISR(name)` expands per architecture to the correct declaration (plain function on Cortex-M, `naked` full-context vector on AVR, the dispatcher entry on RISC-V and native) and is the only portable way to declare a kernel-aware handler. `EMB_ISR_RAW` declares a handler that the kernel treats as kernel-independent in every respect except masking on architectures without priority masking; calling the kernel from it is undefined, and checked builds detect it on architectures where `emb_context()` can tell (Cortex-M, RISC-V, native).

## 9. Exceptions and faults

| Event | Route |
|---|---|
| Synchronous fault: hard fault, bus or memory fault, usage fault, MPU or PMP violation, illegal instruction, misaligned access, divide by zero where trapped | `emb_arch_fault_entry()` captures the architecture context into an `emb_fault_info_t` and calls `embk_fault_dispatch()`. Faults from an unprivileged partition become partition faults; faults in the kernel partition or in interrupt context become kernel faults (03 §10) |
| System call trap (`SVC`, `ecall` from user mode) | Not a fault: the trap entry routes it to the system-call dispatcher of the isolated profile (03 §8.1) |
| Kernel-aware interrupt | §6 |
| Kernel-independent interrupt | handler only; the kernel is not involved |
| Debug monitor, breakpoint | Passed to the debug agent where one is configured; otherwise a kernel fault |
| Non-maskable interrupt | Treated as kernel-independent: it may not call the kernel; the port documents its stack and nesting behavior |

The fault entry runs on the interrupt stack with kernel-aware interrupts masked, writes the crash record before any recovery action (FLT-001), and never returns into the faulting instruction unless a registered recoverable-fault hook says so (for example an MPU fault used for stack-guard growth detection in a diagnostic mode).

## 10. Latency: definitions, budgets, and measurement

### 10.1 Metrics

| Metric | From | To |
|---|---|---|
| **Interrupt latency** | interrupt asserted at the controller | first instruction of the handler body |
| **Kernel masking latency** | the longest time kernel-aware interrupts are masked by the kernel itself (its critical sections) plus the architecture's own entry cost | |
| **ISR overhead** | handler body end | thread resumes when no switch is needed |
| **ISR-to-thread latency** | an ISR wakes a higher-priority thread | that thread's first instruction |
| **Scheduler-lock latency** | longest scheduler lock held by the kernel | |

### 10.2 Budgets

Each architecture port publishes, per reference board and configuration, the measured worst case of every metric with the metadata of 05 §4.4. The kernel's own critical sections and scheduler locks have a design budget stated in the port's documentation; `CONFIG_EMB_IRQ_LOCK_STATS` records the longest observed and checked builds fault when a declared budget is exceeded in CI stress runs. No number appears in this specification: the budgets are evidence, produced by the benchmark pipeline, not promises written ahead of measurement (KRN-RT-007).

### 10.3 Instrumentation

Cycle counters per architecture (`DWT_CYCCNT` on Cortex-M3 and up, `mcycle` on RISC-V, a free-running timer on Cortex-M0+ and AVR), trace points `irq_enter`, `irq_exit`, `irq_lock`, `irq_unlock`, `sched_lock`, `sched_unlock`, `resched_pend`, `switch`, and a GPIO toggle hook for oscilloscope confirmation on HIL.

## 11. Timer interrupt

The system timer (tick or tickless comparator) is a kernel-aware interrupt. Its default level is the **highest kernel-aware level**, so timeout and deadline expiry are delayed only by critical sections and by kernel-independent interrupts, not by other kernel-aware handlers; its body is bounded (move expired timeouts to READY, set `reschedule_pending`, program the next comparator). The level is configurable for boards where another kernel-aware interrupt must win.

## 12. Checked-build diagnostics

| Condition | Detection |
|---|---|
| Thread-only API from an ISR | `emb_context()` check at API entry (SPEC-001 §5.3) |
| Blocking with nonzero timeout inside a critical section or with the scheduler locked | `irq_lock_depth` / `sched_lock_depth` check at the blocking entry |
| `emb_irq_unlock()` out of LIFO order | depth and key consistency check |
| Scheduler lock operations from an ISR | context check |
| Nesting beyond `CONFIG_EMB_IRQ_MAX_NESTING` | counter check at interrupt entry |
| Interrupt stack overflow | fill-pattern check at outermost exit; `MSPLIM` where available |
| Kernel call from an `EMB_ISR_RAW` handler | context check on architectures that can distinguish; documented as undetectable on AVR |
| Critical section longer than the declared budget | `CONFIG_EMB_IRQ_LOCK_STATS` with a configured threshold |

Each raises a kernel fault with the class from SPEC-001 §5.3 and the location, recorded in the crash record.

## 13. Per-architecture summary

| | Cortex-M Armv7-M / Armv8-M | Cortex-M Armv6-M | RISC-V | AVR | native |
|---|---|---|---|---|---|
| Masking primitive | `BASEPRI` (zero-latency on) or `PRIMASK` | `PRIMASK` | `mstatus.MIE` | `SREG.I` | simulator |
| Kernel-independent class | yes with `BASEPRI` | no | with CLIC or threshold-capable controller | no | no |
| Nesting | hardware | hardware | optional, software re-enable | no | simulated |
| Interrupt stack | `MSP` | `MSP` | `mscratch` swap | thread stack plus reserve | host |
| Switch path (P1) | PendSV, `naked` | PendSV, `naked` | trap epilogue in `.S` | `EMB_ISR` epilogue, `naked` | gate |
| Handler declaration | plain C function | plain C function | `EMB_ISR` dispatch entry | `EMB_ISR` or `EMB_ISR_RAW` | `EMB_ISR` |
| Reschedule pend | `SCB->ICSR.PENDSVSET` | same | flag, optional `MSIP` | flag | flag |
| Timer | `SysTick` or SoC timer | `SysTick` | `mtime`/`mtimecmp` or SoC timer | Timer1 compare | virtual clock |
| Fault entry | HardFault, MemManage, BusFault, UsageFault | HardFault | trap with `mcause` | none (reset only) | signals |

## 14. Reference model

The per-CPU context state machine of §2 and the preemption-point rules of §6 are the first module of the executable reference model (ADR-013): a small state machine with operations `irq_enter`, `irq_exit`, `irq_lock`, `irq_unlock`, `sched_lock`, `sched_unlock`, `make_ready`, `block`, `yield`, and an oracle that says whether a switch may occur after each operation. Work item 4 (wait and wake protocol) builds on it. The model is written when work item 4 starts, so that both are explored together.

## 15. Open points for review

1. Timer interrupt at the highest kernel-aware level by default (chosen for timeout jitter), versus the lowest (FreeRTOS's habit, simpler reasoning about tick preemption). Recommendation: highest.
2. `EMB_ISR_RAW` on AVR saves a full context when the handler calls nothing: accepted cost, or should AVR offer only `EMB_ISR` to remove the undetectable-misuse case? Recommendation: keep both; the raw form is what makes short AVR handlers affordable.
3. RISC-V nesting as an option versus always off in 1.0. Recommendation: option, off by default, enabled for CLIC-equipped parts.
4. Whether `emb_irq_set_level()` is allowed at runtime from thread context or only pre-kernel. Recommendation: both, since drivers suspend and resume devices at runtime.
