# RISC-V port

**Status:** RV32IMAC in machine mode running the full conformance suite under QEMU's `virt` machine (checked, release and tiny builds); first 32-bit target with Cortex-M (ADR-038). PMP partitions, the PLIC, RV32F/D context and the RP2350's Hazard3 cores follow (07 §2, M3 and M5). No hardware run yet: QEMU timing is never evidence (SIM-003).

**Sources:** `arch/riscv/` (`rv_switch.S` frame, switch, trap entry, reset; `rv_port.c` trap handler, CLINT timer, software interrupt, startup, idle, faults), board `boards/qemu/riscv32_virt/`.

## Frame and switching

One 128-byte frame serves the thread switch, the trap path and the initial frame. Words from the saved stack pointer upward:

| words | contents |
|---|---|
| 0 | mepc |
| 1 | ra |
| 2-4 | t0-t2 |
| 5-6 | s0-s1 |
| 7-14 | a0-a7 |
| 15-24 | s2-s11 |
| 25-28 | t3-t6 |
| 29 | mstatus |
| 30-31 | padding to 16 bytes |

A context resumes by writing mstatus and mepc from its frame and executing `mret`, so MIE comes back from the frame's MPIE. A thread interrupted with interrupts on, or a fresh thread, resumes with MIE set. A thread that switched out inside its critical section resumes with MIE clear. gp and tp are not used by the port and are not saved.

- `emb_arch_switch_to(next)` is called with MIE clear. It saves the caller as a frame whose mepc is the return address and whose MPIE is clear, makes `next` current, and resumes it. The switch is synchronous, as on AVR.
- The trap entry saves the interrupted frame on the interrupted stack, records it in the current thread, and runs the C handler on a dedicated interrupt stack (`CONFIG_EMB_ISR_STACK_SIZE`). It then resumes whatever context the kernel's interrupt exit returns.
- Handlers run with MIE clear, so interrupts do not nest in this milestone.
- `emb_arch_kernel_start()` saves the boot context and switches to the first thread. Without an idle thread (tiny profile), the pre-kernel stack becomes the idle context, as on AVR. Interrupts never run on it because they have their own stack.

## Interrupts and critical sections

- The critical section is `csrrci mstatus, 8`; the key is the previous MIE bit, and unlock is `csrs mstatus, key`.
- Interrupt number 0 is the machine software interrupt (CLINT msip). `emb_irq_pend(0)` raises it, which is the test framework's software interrupt. Numbers 1 and up are reserved for the PLIC driver; until it exists they return `EMB_ENOTSUP`.
- Handlers are bound with `emb_irq_connect()`; `CONFIG_EMB_IRQ_DYNAMIC` is required.

## Time

The CLINT machine timer gives the periodic tick (`CONFIG_EMB_RV_MTIME_HZ`, 10 MHz on `virt`). Each timer interrupt advances mtimecmp by one period, using the RV32 write sequence that never exposes a smaller intermediate value. The cycle counter is mtime, because mcycle is not a timing source under QEMU. Tickless mode is refused by the configuration in this milestone.

## Faults

A synchronous exception calls `embk_fault_raise_hw()` with mcause as the code, mtval as the address, mepc as the pc and the frame address. The board prints the report and ends the emulator run with status 2 through QEMU's `sifive_test` device. `emb_arch_breakpoint()` does nothing, because machine mode cannot tell whether a debugger is attached and an `ebreak` without one would trap into the fault path again.

## Footprint

GCC 13.2 `-Os`, `samples/first_execution`, base profile, `riscv32_virt`:

| build | kernel text | kernel static RAM | thread control block |
|---|---:|---:|---:|
| release | 4473 | 712 | 120 |
| checked | 5820 | 732 | 128 |

RV32IMAC code is larger than Thumb-2 for the same kernel. Compressed instructions are on; static RAM includes the idle thread's stack and control block.

## Verified

Under QEMU 8.2 `virt` with `-bios none`: the nine conformance suites on the checked, release and tiny builds. The first-execution sample ran about 3 million switches in 15 seconds. The fork-based misuse tests are skipped, as on the other embedded ports. QEMU runs with `-icount`, so virtual time follows executed instructions and the timing tests do not depend on host load; without it, a stalled host made overdue timer interrupts fire back to back and a 3-tick sleep observed 5 ticks.

## Next

The PLIC with external interrupts and priorities, tickless mtimecmp, nested interrupts, RV32F context, PMP partitions (SPEC-010), and the RP2350's Hazard3 cores as the first RISC-V hardware.
