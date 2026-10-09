# SPEC-011 - Architecture Port Contract

**Status:** Accepted 2026-10-07 by the project owner ("do all"), with the defaults of §17. Specification work item 10 of the roadmap (07 §3), in enough detail to implement the native and AVR ports (M1) and to constrain the Cortex-M (M3) and RISC-V (M5) ports.
**Requirements:** `docs/requirements/PORT.md` (v0.1 §21 identifiers restated where they exist; new from the first free number).
**Builds on:** 02 §2 (what the port owns and must never do), §5 (dependency rules), §8 (native port); SPEC-002 (critical sections, preemption points, nesting, stacks, faults, per-architecture mapping), SPEC-003 §4 and §13 (timer contract and sources), SPEC-004 §12 (compare-and-set availability), SPEC-008 §4, §5, §10 (context init, switch-out completion, stacks), SPEC-010 §4, §5 (protection and trap hooks); 09 (EmbCC: inline assembly vocabulary, `naked` functions, startup forms, linking, atomics, debug-info limits); ADR-001, ADR-011.
**Research:** R-001 §2 and §9: the smallest port contracts (NuttX's returned-frame switch, ChibiOS's `port_*` set, FreeRTOS's `portmacro.h`) versus the largest (Zephyr's about 112 `arch_*` entry points, RTEMS's `no_cpu` template). This contract aims at the small end: one switch path per architecture, a fixed list of entry points, and everything the kernel needs stated as a feature flag in a manifest the generator reads.

---

## 1. Scope and terms

| Term | Meaning |
|---|---|
| **Port** | `arch/<arch>/`: the code that implements this contract for one architecture family (`native`, `avr`, `cortex_m`, `riscv`) |
| **Variant** | A sub-family selected at build time within a port (Armv6-M, Armv7-M, Armv8-M; RV32, RV64) |
| **Manifest** | `arch/<arch>/arch.yaml`: the port's declared properties and feature flags, consumed by the generator and the configuration |
| **Contract function** | An `emb_arch_*` function or macro the kernel calls; declared in `include/emb/arch.h`, implemented by every port |
| **Kernel hook** | An `embk_*` function the port calls (SPEC-002 §2 state, SPEC-003 timer ISR, fault dispatch, syscall dispatch) |

The port owns: CPU context, first-thread launch, context switch, interrupt and exception entry and exit, critical sections and masking, atomics and barriers where the compiler needs help, privilege transitions, protection-hardware primitives, the architecture timer hooks, the idle instruction, fault context decode, SMP boot and inter-processor interrupts (02 §2). It never decides scheduling policy, touches a ready structure, or knows which thread is highest priority (v0.1 §21).

## 2. Manifest

```yaml
arch: cortex_m
variants: [armv6m, armv7m, armv8m]
word_bits: 32
endian: little
stack: { align: 8, growth: down, min_thread: 192, context_frame: 64, context_frame_fpu: 136 }
features:
  irq_nesting: true            # hardware or software nesting of kernel-aware interrupts
  zero_latency_irqs: [armv7m, armv8m]    # kernel-independent class available (BASEPRI)
  cas: [armv7m, armv8m]        # atomic compare-and-swap in the ISA (SPEC-004 §12)
  interrupt_stack: true        # dedicated stack for interrupts
  hw_stack_limit: [armv8m]     # PSPLIM / MSPLIM
  mpu: [armv7m, armv8m]        # protection hardware model: armv7m | armv8m | pmp | none
  fpu_lazy: [armv7m, armv8m]
  cycle_counter: [armv7m, armv8m]        # DWT_CYCCNT; M0+ uses an SoC timer
  tickless: true
  smp: false                   # single core in 1.0 ports; RP2350 AMP via per-core images
  syscall_trap: svc            # svc | ecall | none
  fault_entry: true
  priority_bits: soc           # NVIC implemented bits come from the SoC description
tcb_extension_bytes: 0         # per-thread bytes the port needs in the TCB (native: host thread handle)
```

The kernel's configuration derives `EMB_ARCH_HAS_*` macros from the manifest; a kernel feature whose requirement the port lacks is refused at configuration time with the manifest line named (BLD-007 pattern): for example the kernel-independent interrupt class on Armv6-M, or the isolated profile on a port without `mpu`.

## 3. Types and the thread control block

```c
typedef uintptr_t emb_arch_sp_t;                   /* saved stack pointer of a switched-out thread */
typedef unsigned int emb_irq_key_t;                 /* SPEC-002 §4.1 */
typedef struct emb_arch_tcb {                       /* embedded in every TCB; size from the manifest */
    emb_arch_sp_t sp;                               /* the only field the kernel reads: for the debug descriptor */
    unsigned char ext[EMB_ARCH_TCB_EXTENSION];      /* port-private (native: host thread handle, gate state) */
} emb_arch_tcb_t;
typedef uint32_t emb_cycle_t;                       /* cycle counter width is 32 bits on every 1.0 port */
```

The thread's register context lives **on its own stack**; the TCB holds only the stack pointer (plus the port extension). This is the smallest possible TCB contribution, it keeps the kernel's layout independent of the register set, and it is what every C kernel in R-001 does except the MPU retrofits that moved contexts into the TCB. Isolated ports keep the partition's protection table pointer in the partition, not per thread.

## 4. Startup and kernel start

| Function | Called by | Does |
|---|---|---|
| `void emb_arch_early_init(void)` | SoC startup, before `main()` | vector table base, FPU enable and lazy stacking, stack limit registers for the pre-kernel stack, cache enable where the SoC asks, trap vector (`mtvec`) |
| `void emb_arch_init(void)` | `emb_kernel_init()` | interrupt stack installation, priority grouping, PendSV and SVC priorities (SPEC-002 §13), timer source selection, cycle counter start |
| `void emb_arch_kernel_start(emb_arch_sp_t first_sp) __attribute__((noreturn))` | `emb_kernel_start()` at P3 | switches from the pre-kernel stack to the first thread's context and never returns; afterwards the pre-kernel stack is the interrupt stack (ports with `interrupt_stack`) or the idle thread's stack (AVR), per SPEC-008 §10 |

Startup forms per target follow 09 §8: C `Reset_Handler` on Cortex-M, a `.S` entry on RISC-V and AVR; the SoC layer owns clocks and memory setup and calls `emb_arch_early_init` before touching anything interrupt-related.

## 5. Context

```c
emb_arch_sp_t emb_arch_context_init(void *stack_base, size_t stack_size, emb_thread_entry_t entry, void *arg,
                                    void (*exit_fn)(int), bool privileged);
void          emb_arch_switch_to(emb_thread_t next);        /* P2: save current, restore next; returns when current runs again */
void          emb_arch_reschedule_pend(void);               /* P1 request: PendSV, flag, or soft interrupt (SPEC-002 §6.2) */
bool          emb_arch_switch_out_done(emb_thread_t t);     /* SMP: has the old stack been released (SPEC-008 §5 step 5)? UP: always true */
```

- `context_init` builds the initial frame so that the first dispatch enters `entry(arg)` with `exit_fn(0)` as its return path (KRN-THR-005); `privileged` selects the privilege bit in the frame on isolated ports and is ignored elsewhere. The frame size is `stack.context_frame` from the manifest (plus the FPU frame when the thread touches the FPU on lazy-stacking ports); SPEC-008 §10 adds it to the stack budget.
- `switch_to` saves the callee-saved registers of the running thread on its stack, stores the stack pointer in its `emb_arch_tcb_t`, loads the next thread's pointer, restores, and resumes it. On Cortex-M it is implemented by pending PendSV and letting it run at the next instruction boundary with interrupts enabled, so that P1 and P2 share one switch path (SPEC-002 §6.3); on RISC-V and AVR it is a direct call into the `naked` switch routine; native: the gate (SPEC-013).
- The port decides the FPU strategy per variant: lazy stacking on Cortex-M (`FPCCR.LSPEN`), and on RISC-V with hardware float (not in 1.0, soft float only, 09 §2) a dirty-bit check in the switch. The kernel never saves FPU state itself.
- The switch is a full barrier (KRN-MM-002): the port guarantees it with the instructions its memory model needs (`dsb; isb` on ARM, `fence` on RISC-V, nothing extra on AVR).

## 6. Interrupts and critical sections

```c
emb_irq_key_t emb_arch_irq_lock(void);                      /* SPEC-002 §4.1 table; always_inline with "memory" clobber */
void          emb_arch_irq_unlock(emb_irq_key_t key);
bool          emb_arch_irq_locked(void);
bool          emb_arch_in_isr(void);                        /* IPSR, mcause-derived flag, or the kernel's depth counter (AVR) */
void          emb_arch_irq_enable(emb_irq_t irq);          /* controller operations for architecture-standard controllers */
void          emb_arch_irq_disable(emb_irq_t irq);
void          emb_arch_irq_set_level(emb_irq_t irq, uint8_t generic_level);   /* generic numbering of SPEC-002 §8 */
void          emb_arch_irq_clear_pending(emb_irq_t irq);
void          emb_arch_irq_pend_soft(emb_irq_t irq);
bool          emb_arch_irq_is_enabled(emb_irq_t irq);
```

- The port implements the kernel-aware entry and exit sequence of SPEC-002 §6: increment `irq_nesting_depth`, dispatch, decrement, and at the outermost exit perform the switch when `reschedule_pending` and the scheduler is not locked. Cortex-M: handlers are plain C functions and PendSV at the lowest priority is the switch; RISC-V: trap entry in `.S` saves caller-saved registers on the interrupt stack and checks the flag at depth 1; AVR: `EMB_ISR(vector)` expands to a `naked` vector that saves the full register file on the interrupted thread's stack and calls the epilogue (SPEC-002 §6.2).
- `EMB_ISR(name)` and `EMB_ISR_RAW(name)` are provided by every port with the semantics of SPEC-002 §8; on Cortex-M `EMB_ISR` is a plain function definition and `EMB_ISR_RAW` adds nothing but documentation because the hardware does the saving.
- Controller ownership: the port drives architecture-standard controllers (NVIC; RISC-V CLINT and CLIC); a platform-level controller instance (PLIC) is an SoC device driven through the same `emb_arch_irq_*` entry points, which the port routes by interrupt number range from the generated tables (02 §2: the SoC owns the instance, the port owns entry and exit).
- Interrupt stacks per SPEC-002 §7.2: the port installs the dedicated stack where the manifest says `interrupt_stack: true` and documents the per-thread reserve where it says `false` (`CONFIG_EMB_AVR_ISR_STACK_RESERVE`).
- Zero-latency interrupts (`features.zero_latency_irqs`): the port reserves the levels above the kernel mask and never touches kernel state from them (SPEC-002 §3).

## 7. Timer and cycle counter

The architecture timer contract of SPEC-003 §4 is part of this contract:

```c
emb_status_t         emb_arch_timer_init(void);                          /* source and mode from configuration */
void                 emb_arch_timer_start_periodic(uint32_t hz);         /* periodic tick mode */
emb_arch_timer_raw_t emb_arch_timer_now_raw(void);                       /* tickless: free-running counter */
void                 emb_arch_timer_set_deadline_raw(emb_arch_timer_raw_t when);
void                 emb_arch_timer_cancel(void);
uint32_t             emb_arch_timer_hz(void);
emb_tick_t           emb_arch_timer_max_ticks(void);                     /* MAX_INTERVAL */
uint32_t             emb_arch_timer_set_latency_ticks(void);             /* arming latency, measured at init or declared (KRN-TIM-036) */
emb_cycle_t          emb_arch_cycles(void);                              /* DWT_CYCCNT, mcycle, Timer1, host clock */
uint32_t             emb_arch_cycles_hz(void);
```

The port's timer interrupt calls `embk_time_timer_isr()` (SPEC-003 §4); the sources per architecture are SPEC-003 §13. The port measures its own arming latency at `timer_init` by programming a deadline and reading the counter when it fires, unless the hardware description declares it; the kernel subtracts it (KRN-TIM-036).

## 8. Atomics, barriers, compare-and-swap

Kernel atomics are C11 atomics through the portability layer (SPEC-005 §9, 09 §6); the port provides nothing hand-written for them. The port's manifest states `features.cas`; the wait protocol selects the compare-and-set or the critical-section implementation of its state transitions from it (SPEC-004 §12). `emb_arch_mb()`, `emb_arch_rmb()`, `emb_arch_wmb()` are the port's barriers, used by the kernel only where the C11 fences are insufficient for device memory (never on the 1.0 targets; present for the port that will need them).

## 9. Idle and power

```c
void emb_arch_idle(void);                   /* enable interrupts and wait for one, atomically */
void emb_arch_sleep(emb_power_state_t s);   /* enter the SoC-mapped state; the power core calls it (04 §4) */
```

`emb_arch_idle` must not lose a wake-up between enabling interrupts and waiting: `wfi` on ARM and RISC-V wakes on a pending interrupt even while masked, so the port masks, checks nothing, executes `wfi`, then unmasks; AVR uses `sei; sleep`, which the hardware guarantees to be atomic. The idle thread (or the inline idle of ADR-036) calls it when the power core is disabled (PWR-004).

## 10. Stacks

| Item | Port provides |
|---|---|
| `EMB_ARCH_STACK_ALIGN`, `EMB_ARCH_STACK_MIN`, growth | from the manifest; `EMB_THREAD_STACK()` uses them (SPEC-008 §10) |
| `bool emb_arch_stack_check(emb_thread_t t)` | at switch-out when `CONFIG_EMB_STACK_CHECK`: guard words at the limit, or the hardware limit register's fault having fired |
| `void emb_arch_stack_limit_set(void *limit)` | Armv8-M `PSPLIM` for the incoming thread; PMP stack entry on RISC-V; no-op elsewhere |
| interrupt stack fill and check | SPEC-002 §7.2 |
| `-fstack-usage` and call-graph support | all three compilers (09 §7); the port's `docs/ports/<arch>.md` states the handler frame sizes and the context frame |

## 11. Faults

```c
void emb_arch_fault_entry(void);            /* the architecture's fault vectors land here (SPEC-002 §9) */
typedef struct emb_fault_info { ... } emb_fault_info_t;   /* class, code, address, pc, sp, lr/ra, status registers, regs */
```

The port decodes the architecture state (Cortex-M `CFSR`, `MMFAR`, `BFAR`, `EXC_RETURN`; RISC-V `mcause`, `mtval`, `mepc`; AVR has no fault entry) into `emb_fault_info_t`, determines whether the faulting context was an unprivileged thread (SPEC-010 §6) or kernel or interrupt context, and calls `embk_fault_dispatch(&info)`. The register snapshot's layout is exported in the debug descriptor (§14). A recoverable-fault hook may ask the port to return to the faulting context with a modified `pc` (SPEC-002 §9); otherwise the port never returns from a fault.

## 12. Privilege and protection (isolated ports)

```c
void emb_arch_mpu_init(void);                                         /* background map, kernel regions, enable */
void emb_arch_mpu_apply(const emb_arch_region_set_t *set);            /* cross-partition switch (SPEC-010 §4.4) */
void emb_arch_mpu_guard_set(void *stack_limit);                       /* CONFIG_EMB_STACK_GUARD_MPU */
bool emb_arch_user_access_ok(const emb_arch_region_set_t *set, const void *p, size_t len, uint8_t access);   /* marshallers (SPEC-010 §5.4) */
void emb_arch_syscall_enter(void);                                    /* SVC / ecall vector: stack switch and return into embk_syscall_dispatch (SPEC-010 §5.2) */
void emb_arch_syscall_return(uintptr_t status) __attribute__((noreturn));
```

`emb_arch_region_set_t` is the generator's output per partition in the port's encoding (`RBAR`/`RASR` pairs on Armv7-M, `RBAR`/`RLAR` on Armv8-M, `pmpaddr`/`pmpcfg` on RISC-V); the kernel treats it as opaque. The generator consults the manifest's `mpu` model for the constraints it enforces (SPEC-010 §4.3). Ports without protection hardware do not provide these functions and the configuration refuses the isolated profile.

## 13. SMP (FUTURE, names fixed)

`emb_arch_cpu_id()`, `emb_arch_cpu_count()`, `emb_arch_cpu_start(cpu, entry, sp)`, `emb_arch_ipi_send(cpu)`, `emb_arch_ipi_ack()`, and per-CPU state access `emb_arch_percpu()` are reserved; spinlocks are C11 atomics over `emb_arch_irq_lock` (SPEC-002 §4.3). The RP2350 runs two images (AMP) in 1.0, not SMP.

## 14. Debug and trace hooks

- The port contributes to the debug descriptor (ADR-011): the context frame layout (register offsets from the saved stack pointer), the `emb_arch_tcb_t` layout, the fault record register layout, the interrupt stack bounds, and the port name and variant.
- `emb_arch_breakpoint()` (`bkpt`, `ebreak`, `break` on AVR via the debugWIRE convention, a host trap on native) is what `EMB_CHECK` and kernel faults use to stop under a debugger when `CONFIG_EMB_FAULT_HALT_FOR_DEBUG` is set.
- `emb_arch_trace_timestamp()` is `emb_arch_cycles()` unless the port has a better trace clock.
- On AVR, EmbCC emits no DWARF for functions, variables, or lines (09 §7); the AVR port's descriptor therefore carries everything a debugger needs that would otherwise come from debug info, and the port documentation says that symbolic debugging of AVR images uses GCC builds.

## 15. Toolchain rules for port code

From 09 §5 and §7, binding on every port so that one source serves EmbCC, GCC, and Clang:

1. Context switch, first-thread launch, and the AVR `EMB_ISR` prologue and epilogue are `naked` functions or `.S` files; no labels inside inline asm; branch targets `.+N` only.
2. Inline asm templates never name or clobber callee-saved registers (r4-r11, s0-s11, r2-r17 and Y).
3. Critical-section primitives are `static inline __attribute__((always_inline))` with a `"memory"` clobber (SPEC-002 §4.1).
4. No LTO, no `-fPIC`, no `-fstack-protector`, no `-fshort-enums`; attribute fields are fixed-width integers.
5. Vector tables are `const` arrays with `used`; on AVR the 26 `jmp` entries are the toolchain's and the port supplies `__vector_n` bodies.
6. Linker inputs come from the generator (HW-006): a GNU ld script on ARM and RISC-V, an `embld` option set on AVR; bracketed sections are the registration mechanism everywhere.
7. Instruction vocabulary gaps (`clrex`, `ldrd/strd`, byte exclusives, `lr/sc` in inline asm) are avoided by using C11 atomics and `.S` files; the port's documentation lists any gap it had to work around (09 §10).

## 16. Port deliverables and conformance

A port is complete when it ships:

- `arch/<arch>/arch.yaml`, `include/emb_arch.h`, the sources, and `docs/ports/<arch>.md` with: context frame layout, stack reserves and minimums, interrupt stack rules, latency figures from the harness (SPEC-002 §10), timer sources and their limits (KRN-TIM-034), fault classes decoded, protection model, toolchain gaps worked around;
- the `tests/arch/` group green: context init and switch, irq lock and unlock semantics and LIFO check, nesting depth, outermost-exit switch, timer accuracy and MAX_INTERVAL hop, cycle counter monotonicity, idle wake without loss, stack check and limit register, fault entry decode (fault injection), syscall trap round trip and user-access checks on isolated ports;
- the full kernel conformance suite green on the port (SIM-001 for native; the same suite on hardware for the others; emulated results are never timing evidence, SIM-003).

## 17. Per-architecture summary

| | native | AVR (ATmega328P) | Cortex-M Armv6-M | Cortex-M Armv7-M / Armv8-M | RISC-V RV32 |
|---|---|---|---|---|---|
| Context location | host thread; `ext` holds its handle | thread stack: 32 GPRs, `SREG`, return address | thread stack: r4-r11 + hardware frame | same + lazy FPU | thread stack: callee-saved + `mepc`, `mstatus` |
| Switch mechanism | gate hand-over (SPEC-013) | `naked` routine, direct call | PendSV | PendSV | `naked` routine; trap epilogue for P1 |
| Interrupt stack | host | none: per-thread reserve | `MSP` | `MSP` | `mscratch` swap |
| Critical section key | gate lock state | `SREG` | `PRIMASK` | `BASEPRI` or `PRIMASK` | `mstatus.MIE` |
| CAS | C11 | critical section | critical section | `ldrex/strex` | `lr/sc` or `amo` |
| Timer | virtual clock | Timer1 compare | `SysTick` | SoC timer or `SysTick` | `mtime`/`mtimecmp` |
| Cycle counter | host monotonic | Timer1 | SoC timer | `DWT_CYCCNT` | `mcycle` |
| Fault entry | signals | none | HardFault | HardFault, MemManage, BusFault, UsageFault | trap with `mcause` |
| Protection | software checks in the gate | none | none (Armv6-M MPU unsupported in 1.0) | MPU v7 / v8 | PMP (M5) |
| Syscall trap | simulated | none | n/a | `svc` | `ecall` |
| First M | M1 | M1 | M3 | M3 | M5 |

## 18. Decisions taken at acceptance (2026-10-07)

1. The register context lives on the thread's stack; the TCB holds the saved stack pointer and a port-declared extension only.
2. The port's properties are a manifest the generator and the configuration read; a kernel feature the port cannot support is refused at configuration time.
3. One switch path per architecture: PendSV on Cortex-M for both P1 and P2, direct `naked` routine on AVR and RISC-V, the gate on native.
4. Architecture-standard interrupt controllers are driven by the port; platform controllers are SoC devices behind the same entry points.
5. Kernel atomics are C11 atomics through the portability layer; the manifest's `cas` flag selects the wait protocol's transition implementation.
6. The port measures its timer arming latency at init unless the hardware description declares it.
7. `emb_arch_idle` is the atomic enable-and-wait; the power core's deeper states go through `emb_arch_sleep`.
8. Protection region sets are generator output in the port's encoding and opaque to the kernel.
9. Armv6-M MPU support is not part of 1.0 (its atomics helpers mask with `PRIMASK`, which does nothing unprivileged, 09 §2).
10. A port is complete only with its manifest, documentation page, the `tests/arch/` group, and the full conformance suite green.
