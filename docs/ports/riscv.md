# RISC-V port

**Status:** RV32IMAC in machine mode running the full conformance suite under QEMU's `virt` machine (checked, release and tiny builds), and RV32IMC on EmbSIM's ESP32-C3-DevKitM-1 (checked base build); first 32-bit target with Cortex-M (ADR-038). PMP partitions, the PLIC, RV32F/D context and the RP2350's Hazard3 cores follow (07 §2, M3 and M5). No hardware run yet: QEMU timing is never evidence (SIM-003).

**Sources:** `arch/riscv/` (`rv_switch.S` frame, switch, trap entry, reset, the ESP32-C3's vector table; `rv_port.c` trap handler, startup, idle, faults; the SoC's tick, interrupt controller and cycle counter behind `rv_soc.h`: `rv_clint.c` on a CLINT, `rv_esp32c3.c` on the ESP32-C3, chosen by `CONFIG_EMB_RV_ESP32C3`), boards `boards/qemu/riscv32_virt/` and `boards/espressif/esp32c3_devkitm1/`, the image tool `tools/esp/esp_image.py`.

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

## ESP32-C3

Written from Espressif's ESP32-C3 Technical Reference Manual v1.4. The part is RV32IMC: no A extension, so no compare-and-swap (`variant: rv32imc`), and none of the standard interrupt CSRs (`mie` and `mip` are illegal; the reset code leaves `mie` alone there).

- **Interrupts.** Sources reach CPU interrupts 1-31 through the interrupt matrix; each CPU interrupt has a type, a priority and an enable bit, and is taken above a threshold. mtvec is vectored on a 256-byte boundary: 32 jumps (`norvc`, four bytes each) to the one trap entry, which reads mcause as on `virt`. Interrupt 0 is SYSTEM's FROM_CPU_INTR0 (source 50) on CPU interrupt 1, level-triggered: `emb_irq_pend(0)` writes 1, the handler writes 0. The tick is on CPU interrupt 2. Both run at priority 1 over a threshold of 1. Numbers 1 and up return `EMB_ENOTSUP` until a driver maps the matrix's other sources.
- **Time.** The tick is SYSTIMER's comparator 0 in period mode on unit 0 (16 MHz from the 40 MHz crystal, `CONFIG_EMB_RV_MTIME_HZ=16000000`): the period, COMP0_LOAD, then the work enable, and the interrupt cleared through INT_CLR. The cycle counter is unit 0's 52-bit count, latched by UPDATE.
- **Boot.** There is no second-stage bootloader. The build writes each image also as `<name>.bin` (`tools/esp/esp_image.py`): Espressif's application image format (the header with chip ID 5, DIO at 80 MHz and 4 MB, each loadable segment, the checksum and the appended SHA-256). Flashed at offset 0, the boot ROM checks it, copies the code to SRAM1 through the instruction bus (0x40380000) and the data to SRAM1 through the data bus (0x3FCA0000), and jumps to `embn_rv_reset`. The ROM leaves the CPU at XTAL_CLK / 2 (20 MHz) and UART0 at 115200 baud on GPIO21/20, the CP2102N's port, and both are kept. It also leaves two watchdogs guarding the flash boot: early initialisation stops the RTC watchdog and the timer groups' (whose configuration takes effect only through WDT_CONF_UPDATE_EN), and sets the super watchdog to feed itself, as ESP-IDF's startup does.
- **The board.** The DevKitM-1's only LED is the SK6812 on GPIO8, an addressable one the CPU cannot bit-bang at 20 MHz: `emb_board_led_toggle()` keeps its state only. A run ends in `emb_rv_halt()` (MIE clear, WFI) once UART0's FIFO has drained.

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

Under QEMU 8.2 `virt` with `-bios none`: the nine conformance suites on the checked, release and tiny builds. On EmbSIM's ESP32-C3-DevKitM-1 (`emblink/esp32c3_devkitm1`, preset `rv32-c3-embsim`): the same twelve test images, booted by Espressif's own ROM from their flash images; test_first_execution counts 2001 ticks in 2.003 s of virtual time. The board has not run on a physical DevKitM-1 yet. The first-execution sample ran about 3 million switches in 15 seconds. The fork-based misuse tests are skipped, as on the other embedded ports. QEMU runs with `-icount`, so virtual time follows executed instructions and the timing tests do not depend on host load; without it, a stalled host made overdue timer interrupts fire back to back and a 3-tick sleep observed 5 ticks.

## Next

The PLIC with external interrupts and priorities, the ESP32-C3's other interrupt sources and its 160 MHz PLL, tickless mtimecmp, nested interrupts, RV32F context, PMP partitions (SPEC-010), and the RP2350's Hazard3 cores as the first RISC-V hardware.
