# Xtensa (classic ESP32)

**Status:** a kernel port, run on hardware. The classic ESP32's PRO_CPU (Xtensa LX6, the ESP-WROOM-32 module on a DevKitC-style board) runs the kernel, built by EmbCC, and passes the conformance suite on the board: 21 of 21 tests, 157 cases, 74 fault checks that reset the board ([2026-11-05](../engineering/hil-esp32-port-2026-11-05.md)). The design is [ADR-054](../architecture/06-decision-records.md). It came after the [bring-up probe](#the-bring-up-probe), which checked the toolchain, the image, the flasher and the register addresses first.

**Sources:** `arch/xtensa/` (`xt_vectors.S`, `xt_port.c`, `include/emb_arch.h`), the board `boards/espressif/esp32_devkitc/`, the toolchain files `cmake/toolchains/embcc-xtensa.cmake` and `xtensa-esp32-gcc.cmake`.

**Scope of this milestone:**
- one core (PRO_CPU); the APP_CPU stays in reset, as the ROM leaves it;
- level-1 interrupts only, without nesting;
- the periodic tick: no tickless idle;
- no FPU context, no MPU;
- the CPU at the 40 MHz the ROM leaves it on. The PLL (160 or 240 MHz) comes with the board's clock driver.

## Build, flash, test

```sh
EMBCC=/path/to/embcc cmake --preset xt-esp32-hil -DEMB_HIL_PORT=/dev/cu.usbserial-XXXX
cmake --build --preset xt-esp32-hil
EMBFLASH=/path/to/embflash ctest --test-dir build/xt-esp32-hil
```

- **EmbCC.** It needs an `embld` that reads Xtensa linker scripts (EmbCC D-024, #174). `embld` is the one beside `embcc`, so `EMBCC` names both.
- **Espressif's GCC and Clang.** `cmake --preset xt-esp32-gcc` builds the same tree with `xtensa-esp32-elf-gcc` on `PATH`, and `xt-esp32-clang` with Espressif's `clang`, its newlib and LLD. CI builds both and keeps the images, and both passed the suite on the board (ADR-053). Upstream LLVM's Xtensa target is experimental and missing from most distributions; Espressif's fork is the one ESP-IDF uses.
- **Images.** Every image is also written as `<name>.bin`, the ROM's image format (`tools/esp/esp_image.py --chip esp32`). The ROM boots it from flash offset 0x1000.
- **Flashing.** `run_hil.py` flashes with `embflash --esp` (EmbCC D-023), then opens the console. It resets the board into a normal boot itself: EN low through RTS, IO0 high through DTR, as the auto-reset circuit wires them. So the console starts at the run's first line.

## The port

### Vectors and the frame

VECBASE takes `embn_xt_vectors`, the first thing in IRAM at 0x40080400. It is 1 KB aligned, at the LX6's offsets:
- the window overflow and underflow handlers of the windowed ABI, at 0x000 to 0x140;
- the kernel exception vector at 0x300, the user exception vector at 0x340, and the double exception vector at 0x3C0;
- level 2 to 5, debug and NMI, which are never enabled, stop in place.

A level-1 interrupt enters the user exception vector with EXCCAUSE 4. The entry saves a 128-byte frame below the interrupted stack pointer:

| Offset | Contents |
|---|---|
| 0, 4 | a0, a1 |
| 8–60 | a2–a15 |
| 64 | pc (EPC1) |
| 68 | ps |
| 72 | sar |
| 76, 80, 84 | lbeg, lend, lcount |
| 88–127 | unused |

The unused top words keep the frame off the 16 bytes below the interrupted stack pointer. That is where the windowed ABI spills the interrupted function's caller's a0–a3.

### The window spill

Then the entry spills every other register window of the interrupted thread to that thread's own stack. It does so with window exceptions on (PS.WOE set, PS.EXCM clear) and level-1 interrupts still masked: a no-op write to a12 of each rotated window raises the overflow of the frame that owns those registers, four rotations of three quads and one of four.

The spill must run while a1 still holds the interrupted stack pointer. The caller's spill address is computed from that register: an overflow handler stores the caller's a0–a3 below the a1 of the frame it called. Only after the spill does a1 move to the interrupt stack (`CONFIG_EMB_ISR_STACK_SIZE`, A90). From there the entry calls `embn_xt_irq()`, which returns the frame to resume.

The resume sets WINDOWSTART to the current window alone, loads the frame and executes `rfe`. The resumed thread's returns then refill its windows from its own stack (window underflow). That holds whether the frame is the same thread's or another's, whose windows were spilled the same way when it was switched out.

### Switching

Every switch happens in the level-1 entry, as Cortex-M's happen in PendSV:
- **A thread's switch** (`emb_arch_switch_to()`) records the kernel's decision and raises the core's software interrupt, internal interrupt 7. It then opens its critical section until the interrupt has been taken. The thread resumes there, in the same section.
- **A kernel-aware interrupt** takes the decision of the kernel's interrupt exit.
- **`embn_xt_irq()`** runs every pending source in one entry, then resumes the decided thread.

`emb_arch_switch_final()` leaves the exiting thread's stack in the same way. `emb_arch_switch_out_done()` reports the thread as in use until the handler has run.

A fresh thread's frame enters `embk_thread_launch(entry, arg)` with PS.CALLINC 1, as after a `call4`. Its `entry` instruction finds the arguments in the frame's a6 and a7, and its a1 is the stack's top.

### Critical sections

`emb_arch_irq_lock()` is `rsil 1`; the key is the whole previous PS. Unlock writes it back and executes `rsync`. Every kernel-aware interrupt is level 1, so level 1 masks them all.

### Interrupts

The interrupt numbers are the interrupt matrix's sources, 0 to 68:
- An enabled source is routed to CPU interrupt 1 (level 1, level-triggered).
- A disabled one goes to internal interrupt 6. There an external source has no effect, as ESP-IDF's `ETS_INVALID_INUM`.
- At reset every source maps to line 0, so `emb_arch_early_init()` parks them all.
- The dispatcher reads `PRO_INTR_STATUS_0..2` and runs each pending enabled source.

`EMB_XT_IRQ_FROM_CPU0` (source 24, FROM_CPU_INTR0) is the one software can raise. `emb_irq_pend()` writes 1 to `DPORT_CPU_INTR_FROM_CPU_0`, and the dispatcher clears it by writing 0. It is the board's test line (`EMB_HW_TEST_IRQ`).

### Tick and cycle counter

- **Tick.** CCOMPARE0 against CCOUNT, internal interrupt 6, at `CONFIG_EMB_XT_CPU_HZ / tick rate`. The handler keeps the due time apart from the compare value. A tick that fell behind is programmed just ahead of CCOUNT, so the missed ticks follow one after the other and none is lost. A compare value already passed would otherwise wait for CCOUNT to wrap, 107 s at 40 MHz.
- **Cycle counter.** CCOUNT, at the CPU clock.

### The alloca exception

MOVSP raises EXCCAUSE 5 when the caller's a0–a3 are not in the register file, which after the entry's spill is true of every resumed thread's callers. Clang emits MOVSP; EmbCC and GCC did not in the suite. The user-exception entry handles cause 5 as the ISA prescribes, as ESP-IDF's `_xt_alloca_exc` does. It refills the caller's window with the underflow handler for its call size, read from the top bits of the function's return address, with PS.OWB set to the function's window. The handler's `rfwu` then returns to the MOVSP, which runs again.

### Faults and reset

- **Faults.** Any exception but a level-1 interrupt drops the faulting context's frames and runs `embk_fault_raise_hw(EXCCAUSE, EXCVADDR, EPC1, sp)` on the interrupt stack. The crash record's architecture words are EXCCAUSE, EXCVADDR and EPC1.
- **Reset.** `emb_arch_system_reset()` sets RTC_CNTL_SW_SYS_RST. It keeps SRAM2, which holds the crash record and the test framework's record. The fault checks that reset the board and `test_crash_reset` show it on the board.

### Memory and stacks

`esp32.ld` places:
- the code in SRAM0's instruction side, from 0x40080400 (127 KB);
- `.rodata`, `.data`, `.bss` and the pre-kernel stack in SRAM2's data side, 0x3FFB0000 to 0x3FFE0000. That avoids the 8 KB below it and SRAM1 from 0x3FFE0000, which the ROM keeps while it loads the image.

The instruction RAM takes only 32-bit accesses, so nothing a byte load reads may be in IRAM (ADR-052). The crash record (192 bytes) and the framework's record (32 bytes) sit at the top of SRAM2, above the stack.

A thread that blocks in the kernel with an interrupt frame on top used 528 to 560 bytes on the board. The windowed ABI's save areas make every frame larger than RISC-V's. The port's minimum stack is therefore 768 bytes (`min_thread`), and the board's idle stack is 768 too. The samples and benchmarks take their size or the port's minimum, whichever is larger.

## Measured on the board

The release build, built by EmbCC, at 40 MHz, in CPU cycles ([2026-11-05](../engineering/hil-esp32-port-2026-11-05.md)):

| | cycles |
|---|---|
| notification hand-over (2 switches) | 1761 |
| semaphore hand-over (2 switches) | 1754 |
| semaphore give and take, no switch | 411 |
| interrupt wakes a higher-priority thread | 3265 |
| `emb_irq_pend()` to the handler (average; min 325, max 811) | 326 |

Measured one part at a time:
- A software-interrupt round trip that resumes the same thread costs about 318 cycles.
- Each further live frame adds about 54 (its spill and its refill).
- `emb_thread_yield()` with nothing to switch to costs 196.
- A call and return of an empty function costs 8, and a lock and unlock 8.5.

The window traffic is inherent to the windowed ABI. The kernel's own C path is the larger part, and it is the compiler's. The same source built by Espressif's GCC in CI, flashed on the same board, hands over in 1163 cycles against EmbCC's 2090 (checked builds), and gives and takes in 226 against 436. Espressif's Clang lands in between, close to GCC: 1325 and 218 ([report](../engineering/hil-esp32-port-2026-11-05.md)).

The comparison with FreeRTOS on this board is the next step: ESP-IDF's FreeRTOS, built in CI, timed in cycles.

**With Espressif's GCC,** newlib is built with `__DYNAMIC_REENT__` and asks for the running task's reentrancy block through `__getreent()`, which ESP-IDF supplies. The port supplies one block for every thread instead, as RISC-V's `tp` block (A88): `errno` is shared.

## The bring-up probe

Before the port, `tools/esp/bringup/` checked the toolchain, the image, the flasher and the register addresses with no kernel code. It ran on the board on [2026-11-05](../engineering/hil-esp32-2026-11-05.md): built by EmbCC, flashed by `embflash --esp`, booted by the ROM, printing.

`call_start_cpu0` stops the three watchdogs the ROM leaves guarding a flash boot: RTC_CNTL's, then TIMG0's and TIMG1's. For each one it writes the key 0x50D83AA1 to the write-protect register, clears WDTCONFIG0, and locks the register again. The port's `emb_arch_early_init()` does the same. The probe then prints `emb: hello from the ESP32 (Xtensa LX6), line N` about twice a second.

```sh
tools/esp/bringup/build.sh                      # build/esp32-bringup/esp32_hello.bin
embflash build/esp32-bringup/esp32_hello.bin --esp serial:/dev/cu.usbserial-XXXX --base 0x1000 --verify
```

`tools/esp/bringup/esp32.ld` puts the probe's code in IRAM (0x40080400) and its data in DRAM (0x3FFF0000): ESP-IDF's `bootloader.ld` `iram_seg` and `dram_seg`. With an `embld` older than D-024, `build.sh` falls back to `-Ttext 0x40080400 -Tdata 0x3FFF0000`. CI builds the probe and the port with Espressif's GCC, and `check_probe.py` checks the probe's image either way.
