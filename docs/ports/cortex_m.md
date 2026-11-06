# Cortex-M port

**Status:** Armv6-M (Cortex-M0, A86), Armv7-M (Cortex-M3), Armv7E-M (Cortex-M4F and Cortex-M7, A83) and Armv8-M Mainline (Cortex-M33, A78) running the full conformance suite under QEMU (`microbit`, `mps2-an385`, `mps2-an386`, `mps2-an500`, `mps2-an505`); the M4F also on EmbSIM's NUCLEO-F446RE. This was the first 32-bit target, together with RISC-V (ADR-038). Armv8-M Baseline follows (07 §2, M3). No hardware run yet: QEMU timing is never evidence (SIM-003).

**Sources:** `arch/cortex_m/` (`cm_port.c` switching, interrupts, SysTick, faults; `cm_vectors.c` vector table, reset, PendSV, fault entry; `cm_services.c` semihosting exit, halt and FPU probes), boards `boards/arm/mps2_an385`, `boards/arm/mps2_an386`, `boards/arm/mps2_an500`, `boards/arm/mps2_an505` (their board support in `soc/arm/mps2/`), `boards/rpi/pico2_w` (the RP2350's board support in `soc/rpi/rp2350/`, built only), and `boards/st/nucleo_f446re`. Every Cortex-M board is a `board.yaml` and its generated `generated/` directory ([hardware generator](../engineering/hwgen.md)). The NUCLEO-F446RE has no C of its own: it is `board.yaml` plus its [generated](../engineering/hwgen.md) `generated/` directory, built by the STM32F4 board support (`soc/st/stm32f4/board.c`) over the drivers in `drivers/st/`. So is `boards/st/nucleo_f446ze`, a second board on the STM32F446, which runs the suite on EmbSIM's NUCLEO-F446ZE (preset `cm4-nucleo-f446ze`).

## Switching

Threads run in thread mode on PSP; handlers on MSP, the interrupt stack ([A90](../specs/AMENDMENTS-M3.md)).
- It is `embn_cm_isr_stack`, `CONFIG_EMB_ISR_STACK_SIZE` bytes (2048 by default), placed by the linker script first in RAM, so an overflow runs off the bottom of RAM into a bus fault instead of into `.data`. A link-time `ASSERT` refuses an image whose `.data` and `.bss` would reach the retained memory at the top of RAM.
- `main()` runs on it before the kernel starts, and the first PendSV resets MSP to its top, so none of it stays used.
- Checked builds fill it at `emb_kernel_init()` and check its lowest 8 bytes at every outermost interrupt exit, as they check the interrupted thread's guard, which took the exception frame. `emb_isr_stack_info()` reports its high-water mark, and every test run prints it as `ISR_STACK size=... used=...`: 140 of 2048 bytes on QEMU's MPS2. Every context change is made by PendSV at the lowest priority, so it happens only when no other handler is active.

- The port keeps `embn_cm_running` (the thread whose registers are on the CPU) and `embn_cm_next` (the kernel's latest decision).
- `emb_arch_switch_to(next)` is called in the kernel's critical section. It stores `next`, pends PendSV, sets BASEPRI to 0 for the instant PendSV needs, and on resumption restores its own BASEPRI from a local of its frame, so each thread gets its own critical-section state back.
- A kernel-aware interrupt brackets its handler with `embk_isr_enter()` and `embk_isr_exit()`, stores the decision, and pends PendSV when it differs from the running thread. An interrupt that lands in a thread's switch window simply overwrites the decision; PendSV always installs the latest one.
- PendSV masks the kernel's interrupts, saves r4-r11 and EXC_RETURN (and s16-s31 for a thread with an active FP context) on the outgoing PSP, installs the decision, and restores the same layout. It always runs at BASEPRI 0, so every context it resumes was saved at 0.
- The exiting thread's last switch (`emb_arch_switch_final`) clears the running pointer so PendSV skips the save; `emb_arch_switch_out_done()` reports the thread in use until PendSV has left its stack.
- The first switch is PendSV's, from no context. On the tiny profile the inline idle context gets a 256-byte port stack, because the pre-kernel stack becomes the handler stack.

Frame, from the saved stack pointer upward: r4..r11, EXC_RETURN, then the hardware frame r0, r1, r2, r3, r12, lr, pc, xPSR. With an FP context, s16..s31 sit between EXC_RETURN and the hardware frame, and the hardware's lazily stacked s0..s15 and FPSCR follow it. The initial frame enters `embk_thread_launch(entry, arg)` with r0 and r1.

## Interrupts and critical sections

- The critical section raises BASEPRI to `CONFIG_EMB_CM_KERNEL_BASEPRI` (default 32, level 1 with three priority bits). Interrupts above it are zero-latency: never masked by the kernel and not allowed to call it (SPEC-002 §3). Unlock writes BASEPRI back and issues `isb`, so a pending interrupt is taken at once.
- Every external interrupt starts at raw `CONFIG_EMB_CM_IRQ_PRIORITY` (default 128). `emb_irq_set_level()` accepts advertised generic levels, with larger values more urgent: `encoding = (256 - 2*S) - S*level`, `S = 1 << (8 - CONFIG_EMB_CM_IRQ_PRIORITY_BITS)`. MPS2 exposes three bits (levels 0–5 at BASEPRI 32); F446 exposes four (0–12). Invalid values are rejected before any write. `PRIGROUP=0` makes the declared 3–7 high bits preemption bits; the least-urgent encoding stays reserved for PendSV. `CONFIG_EMB_CM_TIMER_LEVEL` defaults to the highest kernel-aware level. Threshold/default encodings are validated and public limits are derived; non-nesting configurations require a single kernel-aware level; raw kernel-independent vectors are not implemented and enabling that class fails configuration. See [IRQ evidence](../engineering/m2-02-irq-priority.md).
- All external vectors enter one common entry, which reads IPSR and runs the handler bound with `emb_irq_connect()`; `CONFIG_EMB_IRQ_DYNAMIC` is required. `EMB_ISR(name)` declares such a handler.
- `emb_irq_pend()` pends a line through NVIC ISPR, which is a true software interrupt. The test framework uses the board's spare line `EMB_HW_TEST_IRQ` (30 on MPS2).
- Nesting is the NVIC's. The kernel decides only at the outermost exit (KRN-SCH-021).

## Time

SysTick on the processor clock gives the periodic tick (`CONFIG_EMB_CM_CPU_HZ`, 25 MHz on MPS2, a reload of 25,000 for 1 ms). The cycle counter is derived from the tick count and SysTick, because QEMU does not model DWT CYCCNT. Tickless mode is refused by the configuration in this milestone. The reload is 24 bits, so a tick of more than 2^24 processor cycles (about 93 ms at 180 MHz) fails the build instead of running at a wrong rate.

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

Under QEMU 11.1 on `microbit` (Cortex-M0, presets `cm0-microbit` and `-release`, tiny profile, A86): every image passes, 14 of 14 in each build, Thumb-1 (`Tag_CPU_arch: v6S-M`). This includes the priority, mutex-inheritance and first-execution cases.

Under QEMU 11.1 on `mps2-an500` (Cortex-M7 with FPv5-D16, presets `cm7-an500` and `-release`, A83): every image passes, 21 of 21 in each build, with no change to the port. The double-precision FPU's d8 to d15 are the s16 to s31 the switch saves.

Under QEMU 11.1 on `mps2-an505` (Cortex-M33 with FPv5, presets `cm33-an505`, `-release`, `-tiny`): every conformance image passes, 22 in the checked and release builds (with `test_stack_limit`, A80) and 15 in the tiny build. That includes `test_fpu`, `test_irq_priority`, the hardware-fault crash record and the reset case. The images carry `Tag_CPU_arch: v8-M.mainline` and `Tag_FP_arch: FPv5`.

Under QEMU 8.2, on `mps2-an385` (Cortex-M3) and `mps2-an386` (Cortex-M4F, checked, release and tiny): the original nine conformance suites, plus `test_fpu` on the M4F. The subsequent `test_irq_priority` adds register, range, pending-order, nested-preemption, equal-priority and masking checks; its evidence is recorded separately in the IRQ report. `test_fpu` checks that s16-s31 survive a blocking switch and that a float computation preempted by a float-using thread at every tick gives the bit-identical result. Disabling the PendSV FP save makes both tests fail. The first-execution sample ran 2.5 million switches in 15 seconds on the M4F. The fork-based misuse tests are skipped, as on AVR. QEMU runs with `-icount`, so virtual time follows executed instructions and the timing tests do not depend on host load; without it, a stalled host made overdue timer interrupts fire back to back and a 3-tick sleep observed 5 ticks.

On EmbSIM's NUCLEO-F446RE (`emblink/nucleo_f446re`, preset `cm4-nucleo-embsim`, checked base profile): the original ten test images, built as for the board, followed by the new `test_irq_priority` image in the IRQ correction. `emb_board_init` brings the part from its reset state (HSI at 16 MHz) to 180 MHz as RM0390 orders it: HSE in bypass from the ST-LINK's MCO, the PLL (M 8, N 360, P 2), over-drive and its switch, five wait states, then SYSCLK to the PLL with APB1 at 45 MHz and APB2 at 90 MHz. Each ready flag is polled a bounded number of times; if HSE never comes ready (a board whose MCO is not on PH0), the console starts on HSI, says so, and the run halts. The console is USART2 at 115200 baud from APB1. The framework test interrupt is line 61, reserved on the F446 (no Ethernet); the priority fixture also uses reserved line 62. A test run ends in `emb_cm_halt` (PRIMASK set, WFI) after the last frame's TC; EmbSIM stops at the console's `EMB_TEST_END`. The test_time run ends at 122 ms of virtual time after 113 ticks, so SysTick does run from a 180 MHz HCLK. Since the generator took over the board, the same sequence runs from `hw_config.h`'s solver output, with the pinned factors and identical register values. The 18 CTest targets pass as before, and each image is 176 to 192 bytes larger in text, the cost of the table-driven pin setup. The board has not run on a physical NUCLEO yet.

## Armv6-M (Cortex-M0/M0+)

Variant `armv6m` ([A86](../specs/AMENDMENTS-M3.md)):
- **Critical sections** use PRIMASK, since there is no BASEPRI, so no interrupt is zero-latency.
- **PendSV** saves the same frame in Thumb-1: r8 to r11 move through the low registers, and each assembly block switches GCC's divided syntax to unified and back.
- **Priority registers** take word accesses only, so a byte is read, modified and written back in its word. Two priority bits are implemented: two kernel-aware levels, with the lowest encoding left to PendSV.
- **Faults:** HardFault only, without status registers. The crash record keeps the stacked frame.
- **No CAS and no FPU.**

Every change is under `__ARM_ARCH_6M__`, and the Armv7-M port objects are identical in their disassembly. The reference board is QEMU's `microbit` (nRF51822, `boards/bbc/microbit`, board support in `soc/nordic/nrf51/`, tiny profile).

## Armv8-M Mainline (Cortex-M33)

Variant `armv8m-main` ([A78](../specs/AMENDMENTS-M3.md)) runs in the Secure state with TrustZone unused, which is how QEMU and the RP2350's boot ROM start the core. The port runs unchanged in that state:

- the thread-mode EXC_RETURN values 0xFFFFFFFD and 0xFFFFFFED have their Secure-state meaning;
- the frames, BASEPRI, PendSV and FPv5 lazy stacking behave as on Armv7E-M.

The header accepts `__ARM_ARCH_8M_MAIN__`, and the manifest lists the variant for zero-latency interrupts, CAS, lazy FP stacking and the cycle counter.

**Stack limits ([A80](../specs/AMENDMENTS-M3.md)).**
- Each thread's PSPLIM is its stack's base, saved in its frame below r4 and restored by PendSV before PSP.
- A push below the base faults at once (CFSR.STKOF), before the thread writes outside its stack. The fault is reported as `EMB_FAULT_STACK_OVERFLOW`, with the fault status in the crash record and no frame: the hardware does not stack one at the limit.
- MSPLIM bounds the interrupt stack (A90). The reset sets it 384 bytes above the stack's bottom, and CCR.STKOFHFNMIGN so that HardFault may run below it. The fault entry clears it first, so the fault path runs in those 384 bytes. An interrupt that overflows faults as `EMB_FAULT_STACK_OVERFLOW` (`isr_stack_limit_stops_an_overflowing_interrupt`).

**Not yet used:** the Armv8-M MPU, the Security Attribution Unit, and non-secure images.

**The reference board.** `mps2_an505`, QEMU's `mps2-an505`, is described from QEMU 11.1's `hw/arm/mps2-tz.c`:
- code at the SSRAM's Secure alias 0x10000000, where INITSVTOR points;
- RAM at 0x38000000, 4 MiB;
- the CMSDK UART0 and the FPGA I/O at their Secure aliases, 0x50200000 and 0x50302000;
- the processor clock at 20 MHz, which SysTick also uses, since the SSE-200 wires no reference clock;
- 124 NVIC lines. The test lines are 120 and 121, which no device drives.

It reuses the MPS2 board support unchanged.

## Next

The Armv6-M crash and priority tests on a physical Cortex-M0, tickless SysTick, DWT cycle counter on hardware, the STM32F407 Discovery board, and the NUCLEO-F446RE on hardware (M3).
