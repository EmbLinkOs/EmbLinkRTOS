# Cortex-M port

**Status:** Armv7-M (Cortex-M3) and Armv7E-M (Cortex-M4F) running the full conformance suite under QEMU (`mps2-an385`, `mps2-an386`); first 32-bit target with RISC-V (ADR-038). Armv6-M and Armv8-M follow (07 §2, M3). No hardware run yet: QEMU timing is never evidence (SIM-003).

**Sources:** `arch/cortex_m/` (`cm_port.c` switching, interrupts, SysTick, faults; `cm_vectors.c` vector table, reset, PendSV, fault entry; `cm_services.c` semihosting exit and FPU probes), boards `boards/arm/mps2_an385`, `boards/arm/mps2_an386`, shared code in `boards/arm/mps2_common/`.

## Switching

Threads run in thread mode on PSP; handlers on MSP, which is `main()`'s stack. Every context change is made by PendSV at the lowest priority, so it happens only when no other handler is active.

- The port keeps `embn_cm_running` (the thread whose registers are on the CPU) and `embn_cm_next` (the kernel's latest decision).
- `emb_arch_switch_to(next)` is called in the kernel's critical section. It stores `next`, pends PendSV, sets BASEPRI to 0 for the instant PendSV needs, and on resumption restores its own BASEPRI from a local of its frame, so each thread gets its own critical-section state back.
- A kernel-aware interrupt brackets its handler with `embk_isr_enter()` and `embk_isr_exit()`, stores the decision, and pends PendSV when it differs from the running thread. An interrupt that lands in a thread's switch window simply overwrites the decision; PendSV always installs the latest one.
- PendSV masks the kernel's interrupts, saves r4-r11 and EXC_RETURN (and s16-s31 for a thread with an active FP context) on the outgoing PSP, installs the decision, and restores the same layout. It always runs at BASEPRI 0, so every context it resumes was saved at 0.
- The exiting thread's last switch (`emb_arch_switch_final`) clears the running pointer so PendSV skips the save; `emb_arch_switch_out_done()` reports the thread in use until PendSV has left its stack.
- The first switch is PendSV's, from no context. On the tiny profile the inline idle context gets a 256-byte port stack, because the pre-kernel stack becomes the handler stack.

Frame, from the saved stack pointer upward: r4..r11, EXC_RETURN, then the hardware frame r0, r1, r2, r3, r12, lr, pc, xPSR. With an FP context, s16..s31 sit between EXC_RETURN and the hardware frame, and the hardware's lazily stacked s0..s15 and FPSCR follow it. The initial frame enters `embk_thread_launch(entry, arg)` with r0 and r1.

## Interrupts and critical sections

- The critical section raises BASEPRI to `CONFIG_EMB_CM_KERNEL_BASEPRI` (default 32, level 1 with three priority bits). Interrupts above it are zero-latency: never masked by the kernel and not allowed to call it (SPEC-002 §3). Unlock writes BASEPRI back and issues `isb`, so a pending interrupt is taken at once.
- Every external interrupt is set to `CONFIG_EMB_CM_IRQ_PRIORITY` (default 128) at init; `emb_irq_set_level(irq, n)` maps level n to BASEPRI + 32n, capped below PendSV.
- All external vectors enter one common entry, which reads IPSR and runs the handler bound with `emb_irq_connect()`; `CONFIG_EMB_IRQ_DYNAMIC` is required. `EMB_ISR(name)` declares such a handler.
- `emb_irq_pend()` pends a line through NVIC ISPR, which is a true software interrupt. The test framework uses the board's spare line `EMB_HW_TEST_IRQ` (30 on MPS2).
- Nesting is the NVIC's. The kernel decides only at the outermost exit (KRN-SCH-021).

## Time

SysTick on the processor clock gives the periodic tick (`CONFIG_EMB_CM_CPU_HZ`, 25 MHz on MPS2, a reload of 25,000 for 1 ms). The cycle counter is derived from the tick count and SysTick, because QEMU does not model DWT CYCCNT. Tickless mode is refused by the configuration in this milestone.

## Faults

HardFault, MemManage, BusFault and UsageFault enter one naked entry that finds the stacked frame on PSP or MSP and calls `embk_fault_raise_hw()` with the exception number, the faulting address (MMFAR or BFAR when valid), the stacked pc and the frame address. The fault action follows; the board's `emb_board_fault_report()` prints and, on MPS2, ends the emulator run with status 2. The checked build's halt-for-debug breakpoint fires only when a debugger is attached (DHCSR.C_DEBUGEN); without one, `bkpt` inside a fault handler would lock the core up.

## Footprint

GCC 13.2 `-Os`, `samples/first_execution`, base profile, `mps2_an386`:

| build | kernel text | kernel static RAM | thread control block |
|---|---:|---:|---:|
| release | 3862 | 929 | 120 |
| checked | 4873 | 941 | 128 |

Static RAM includes the idle thread's 256-byte stack and control block. R-003 T4 (control block at most 96 bytes on the base profile) is not met yet; the base profile's 64-bit time and doubly linked lists are the main cost.

## Verified

Under QEMU 8.2, on `mps2-an385` (Cortex-M3) and `mps2-an386` (Cortex-M4F, checked, release and tiny): the nine conformance suites, plus `test_fpu` on the M4F. `test_fpu` checks that s16-s31 survive a blocking switch and that a float computation preempted by a float-using thread at every tick gives the bit-identical result. Disabling the PendSV FP save makes both tests fail. The first-execution sample ran 2.5 million switches in 15 seconds on the M4F. The fork-based misuse tests are skipped, as on AVR.

## Next

Armv6-M (PRIMASK critical sections, no `basepri`), Armv8-M Mainline (PSPLIM stack limits, `mps2-an505`), tickless SysTick, DWT cycle counter on hardware, and the STM32F407 Discovery and NUCLEO-F446RE boards (M3).
