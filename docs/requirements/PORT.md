# PORT - Compiler Portability and the Architecture Port Contract

Group `PORT` with two series: `PORT-ABI` (compiler portability, from v0.1 §21.5) and `PORT` (the architecture port contract, SPEC-011). Design: `docs/specs/SPEC-011-architecture-port-contract.md`; SPEC-001 §10 (compiler portability layer); `docs/architecture/09-embcc-toolchain-profile.md`. Related groups: KRN-IRQ, KRN-TIM, KRN-THR, KRN-PART, ARCH-AVR, SIM, BLD, OBS.

PORT-ABI-001 to 005 originate in the v0.1 specification (§21.5) and are restated here as the authoritative copy. PORT-001 onward are new.

---

### PORT-ABI-001  No EmbCC-specific extension in the public API
**Statement.** The public kernel API shall not require an EmbCC-specific language extension.
**Rationale.** GCC and Clang builds are first-class (ADR-023).
**Status.** Accepted 2026-10-07
**Verification.** Build matrix: public headers compile with `-std=c11 -pedantic` on GCC and Clang.
**Trace.** SPEC-011 §15; SPEC-001 §10; v0.1 §21.5

### PORT-ABI-002  Portability layer for attributes and builtins
**Statement.** Compiler-specific attributes and builtins shall be isolated behind portability interfaces.
**Rationale.** No compiler conditionals scattered through generic code (v0.1 §21.2).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: CI grep rejects `__attribute__` and `__builtin_` outside `include/emb/compiler/` and `arch/`.
**Trace.** SPEC-011 §15; SPEC-001 §10

### PORT-ABI-003  Documented and tested compiler matrix
**Statement.** Supported compiler and version combinations shall be documented and continuously tested where infrastructure permits.
**Rationale.** Support is stated per architecture and version, not claimed globally (v0.1 §21.3).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: the matrix of 09 §11 is generated from CI results each release.
**Trace.** 09 §11; 05 §2

### PORT-ABI-004  Early detection of incompatible combinations
**Statement.** The build shall detect incompatible architecture, ABI, and configuration combinations as early as practical.
**Rationale.** A refused configuration beats a wrong image.
**Status.** Accepted 2026-10-07
**Verification.** Build tests: hard-float and soft-float mix, a kernel feature the port manifest lacks, a profile the port cannot support, each fails at configuration.
**Trace.** SPEC-011 §2; BLD-007

### PORT-ABI-005  Optimizations never change semantics
**Statement.** Toolchain-specific optimizations may improve performance but shall not change documented kernel semantics.
**Rationale.** One conformance suite across compilers and optimization levels.
**Status.** Accepted 2026-10-07
**Verification.** The conformance suite runs at `-O0`, `-Os`, and `-O2` on every compiler of the matrix.
**Trace.** 05 §4; 09 §7

### PORT-001  Port manifest
**Statement.** Every port shall declare its properties and feature flags in `arch/<arch>/arch.yaml` (word size, endianness, stack rules, nesting, zero-latency class, compare-and-swap, interrupt stack, hardware stack limit, protection model, lazy FPU, cycle counter, tickless, SMP, trap kind, fault entry, TCB extension); the configuration shall derive `EMB_ARCH_HAS_*` from it and refuse kernel features the port cannot support.
**Rationale.** The contract is checkable data, not folklore.
**Status.** Accepted 2026-10-07
**Verification.** Schema validation of every manifest; configuration tests for refused combinations.
**Trace.** SPEC-011 §2

### PORT-002  Context on the thread stack
**Statement.** A thread's register context shall be saved on its own stack; the TCB shall hold only the saved stack pointer and the port's declared extension bytes.
**Rationale.** Smallest TCB; kernel layout independent of the register set.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `emb_arch_tcb_t` definition per port; footprint test.
**Trace.** SPEC-011 §3

### PORT-003  Startup entry points
**Statement.** A port shall provide `emb_arch_early_init`, `emb_arch_init`, and a non-returning `emb_arch_kernel_start` with the responsibilities of SPEC-011 §4, following the startup form of 09 §8 for its target.
**Rationale.** SoC startup and the kernel need fixed hooks.
**Status.** Accepted 2026-10-07
**Verification.** Test: `arch/boot_sequence` on each port.
**Trace.** SPEC-011 §4

### PORT-004  Context init and single switch path
**Statement.** `emb_arch_context_init` shall build a frame that enters the entry function with its argument and returns into the exit function; `emb_arch_switch_to` shall save callee-saved state, exchange stack pointers, and resume; each architecture shall have exactly one switch path serving P1 and P2; the switch shall be a full memory barrier.
**Rationale.** KRN-THR-005, KRN-MM-002, SPEC-002 §6.
**Status.** Accepted 2026-10-07
**Verification.** Test: `arch/context_init_launch`, `arch/switch_roundtrip`, `arch/switch_from_isr_and_thread_interoperate`.
**Trace.** SPEC-011 §5

### PORT-005  Interrupt entry, exit, and controller operations
**Statement.** A port shall implement the kernel-aware entry and exit sequence of SPEC-002 §6, `EMB_ISR` and `EMB_ISR_RAW`, the key-based critical-section primitives as always-inline functions with a memory clobber, `emb_arch_in_isr`, and the controller operations `enable`, `disable`, `set_level` (generic numbering), `clear_pending`, `pend_soft`, `is_enabled`, routing platform controllers from the generated tables.
**Rationale.** SPEC-002 made these the kernel's expectations.
**Status.** Accepted 2026-10-07
**Verification.** `tests/arch/irq_*`; KRN-IRQ tests on each port.
**Trace.** SPEC-011 §6; SPEC-002

### PORT-006  Timer and cycle counter contract
**Statement.** A port shall implement the architecture timer contract of SPEC-003 §4 plus `emb_arch_timer_start_periodic`, `emb_arch_timer_set_latency_ticks`, `emb_arch_cycles`, and `emb_arch_cycles_hz`, and shall measure its arming latency at init unless the hardware description declares it.
**Rationale.** KRN-TIM-020, 036.
**Status.** Accepted 2026-10-07
**Verification.** `tests/arch/timer_*`: accuracy, MAX_INTERVAL hop, latency measurement, cycle monotonicity.
**Trace.** SPEC-011 §7; SPEC-003 §4

### PORT-007  Atomics through the portability layer
**Statement.** Kernel atomics shall be C11 atomics through the portability layer; a port shall provide only barriers and its `cas` manifest flag, from which the wait protocol selects its transition implementation.
**Rationale.** One source for EmbCC, GCC, and Clang (09 §5, §6).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: no hand-written exclusive-access asm in `arch/`; the wait-protocol tests pass with both transition forms.
**Trace.** SPEC-011 §8; SPEC-004 §12; SPEC-005 §9

### PORT-008  Atomic idle
**Statement.** `emb_arch_idle` shall enable interrupts and wait for one without a window in which a wake-up can be lost; `emb_arch_sleep` shall enter the power core's mapped state.
**Rationale.** A lost wake-up in idle is an unbounded latency.
**Status.** Accepted 2026-10-07
**Verification.** Test: `arch/idle_no_lost_wakeup` with an interrupt pending at idle entry.
**Trace.** SPEC-011 §9

### PORT-009  Stack rules and checks
**Statement.** A port shall declare stack alignment, minimum, and growth; implement the switch-out stack check and the hardware limit register where present; install or document the interrupt stack per SPEC-002 §7.2; and document handler and context frame sizes.
**Rationale.** KRN-MEM-009 to 011, KRN-THR-023.
**Status.** Accepted 2026-10-07
**Verification.** Test: `arch/stack_guard_detects_overflow`; documentation review.
**Trace.** SPEC-011 §10

### PORT-010  Fault entry and decode
**Statement.** A port with fault hardware shall route every synchronous fault to `emb_arch_fault_entry`, decode the architecture state into `emb_fault_info_t`, classify the faulting context, call `embk_fault_dispatch`, and never return except through a recoverable-fault hook; a port without fault hardware shall say so in its manifest and documentation.
**Rationale.** SPEC-002 §9; the crash record needs the decode.
**Status.** Accepted 2026-10-07
**Verification.** Test: `arch/fault_decode` by injection on each fault class.
**Trace.** SPEC-011 §11

### PORT-011  Protection and trap hooks
**Statement.** A port with protection hardware shall implement `emb_arch_mpu_init`, `emb_arch_mpu_apply`, `emb_arch_mpu_guard_set`, `emb_arch_user_access_ok`, `emb_arch_syscall_enter`, and `emb_arch_syscall_return` with the semantics of SPEC-010 §4 and §5; the region set encoding shall be the port's and opaque to the kernel.
**Rationale.** Isolation mechanics belong to the port; policy to the kernel and generator.
**Status.** Accepted 2026-10-07
**Verification.** Isolated-profile tests on each MPU port; `arch/syscall_roundtrip`.
**Trace.** SPEC-011 §12; SPEC-010

### PORT-012  Debug descriptor contribution
**Statement.** A port shall contribute the context frame layout, TCB extension layout, fault register layout, interrupt stack bounds, and its name and variant to the debug descriptor, and shall provide `emb_arch_breakpoint` and `emb_arch_trace_timestamp`.
**Rationale.** ADR-011 needs architecture facts a debugger cannot infer.
**Status.** Accepted 2026-10-07
**Verification.** Descriptor consumer test on each port.
**Trace.** SPEC-011 §14; OBS-008

### PORT-013  Toolchain rules for port code
**Statement.** Port code shall follow the rules of SPEC-011 §15: `naked` functions or `.S` files for multi-block assembly, no labels in inline asm, no callee-saved registers in templates, always-inline critical sections with a memory clobber, no LTO, PIC, stack protector, or short enums, generated linker inputs, bracketed sections for registration, and documented workarounds for instruction vocabulary gaps.
**Rationale.** One source for three compilers (09 §5, §7).
**Status.** Accepted 2026-10-07
**Verification.** Build on EmbCC, GCC, and Clang for each port; CI grep for forbidden constructs.
**Trace.** SPEC-011 §15; 09

### PORT-014  Port deliverables
**Statement.** A port shall be complete only with its manifest, headers and sources, `docs/ports/<arch>.md` (frame layout, stack rules, latency figures, timer sources, fault classes, protection model, toolchain gaps), the `tests/arch/` group green, and the full kernel conformance suite green on the port.
**Rationale.** A port without its evidence is not a port (01 §6).
**Status.** Accepted 2026-10-07
**Verification.** Release checklist; CI gates.
**Trace.** SPEC-011 §16
