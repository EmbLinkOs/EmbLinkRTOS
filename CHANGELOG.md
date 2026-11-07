# Changelog

User-visible changes are recorded here. There are no released versions or release tags in the reviewed repository; `0.2.0` in the headers identifies development, not a supported release. Move relevant Unreleased entries into a dated version section only when the [release checklist](docs/engineering/releases.md) is complete. Record support scope and known limits with each release.

## Unreleased

### Added (the controller's memory, 2026-11-07)

- `tools/footprint/memory_report.py` accounts for an image's flash and RAM, by component and by purpose, reconciled with its sections.
- `tools/footprint/stack_bound.py` computes a static worst-case bound for each thread's stack and the interrupt stack. The call graph comes from the image's disassembly and the frames from GCC's `-fstack-usage`. A roots file resolves the indirect calls.
- For the reference controller ([report](docs/engineering/controller-memory-2026-11-07.md)): 18,096 bytes of flash and 7,624 of RAM. The bounds are 484, 532 and 612 bytes for its threads (1,024 each) and 1,604 for the interrupt stack (2,048), each above what the board measured. CI checks both for the two NUCLEO-F446 boards.
- `footprint.py` names the drivers and the C library, and can count merged string constants apart.

### Added (the controller's timing, 2026-11-07)

- The reference controller drives marker pins for a logic analyzer: its timer's trigger in hardware, its ADC handler, its control thread and its console.
- `tools/hil/controller_timing.py` measures the controller with any analyzer sigrok supports, and keeps the captures and SVG images.
- Measured on the NUCLEO-F446RE from the ADC's interrupt to the control thread at work: 9.50 µs at the median, 10.0 µs at most, over 24,998 samples at 100 Hz and 1 kHz and in degraded mode ([report](docs/engineering/controller-timing-2026-11-07.md)).
- The STM32 drivers gain `emb_stm32_gpio_set()`/`_clear()` and `emb_stm32_tim_ch1_toggle_on_update()`.

### Added (the reference controller, 2026-11-07)

- `examples/controller`, M3a's first application ([report](docs/engineering/controller-nucleo-2026-11-07.md)), an interrupt-driven controller on the NUCLEO-F446RE.
  - TIM2 triggers ADC1 conversions of the temperature sensor; the control thread runs a filter and a heater with hysteresis.
  - Deadline misses are counted, and the controller enters degraded mode on three in a row. A watchdog is fed only while control progresses.
  - Commands and telemetry share the console under a priority-inheriting mutex.
  - At boot it reports the reset cause and the crash record.
  - It builds with GCC and Clang, for both NUCLEO-F446 boards; CI builds it out of the tree.
- STM32F4 drivers for it: a timer's periodic trigger, the temperature sensor on ADC1, the independent watchdog, the console's receive interrupt and the reset flags.
- `tools/hil/controller_session.py` runs the controller's scenario on the board and keeps a transcript and a JSON record.
- The [HIL guide](docs/engineering/hil.md) says to update the NUCLEO's ST-LINK firmware first. With V2J28M18 the virtual COM port stopped passing bytes to the board.

### Fixed (the reference controller, 2026-11-07)

- `CONFIG_EMB_FAULT_ACTION_REBOOT` halted the part instead of rebooting it on the STM32F4, RP2350, ESP32 and ESP32-C3 boards (A106). The board's fault report ended the run before the port's reset. It now returns under the reboot action.

### Changed (fast paths, 2026-11-07)

- Six helpers on the API's fast paths are always inline on 32-bit ports (`EMBK_HOT`, A104). GCC at `-Os` had kept them out of line. On the NUCLEO-F446RE with `EMB_LTO`, the notification hand-over is now at parity with FreeRTOS built the same way (567 against 565 cycles). The semaphore hand-over is 22% faster and the interrupt wake 12% faster than FreeRTOS. The uncontended give and take is within 11%. AVR code is unchanged or smaller.

### Added (link-time optimization, 2026-11-07)

- `-DEMB_LTO=ON` builds the kernel, the port, the board and the application with link-time optimization (ADR-056, A103). This works with GCC and Clang. On the NUCLEO-F446RE the release build's switching paths take 17–20% fewer cycles and its image is 18% smaller ([report](docs/engineering/perf-lto-2026-11-07.md)). The notification hand-over drops from 794 to 632 cycles, the semaphore hand-over from 746 to 648, and the interrupt wake from 1,514 to 1,285. The suites pass with it on every QEMU and EmbSIM preset and on the NUCLEO, built by GCC and by Clang.
- `tools/hil/pcsample.py` profiles a Cortex-M board as it runs, by sampling its DWT PC sample register through OpenOCD.

### Changed (Pico 2 W at 150 MHz, 2026-11-07)

- The Pico 2 W runs at 150 MHz from its PLL, as pico-sdk does, instead of its 12 MHz crystal (A102). The hardware generator handles a PLL with two post-dividers. At 12 MHz four timing cases failed on the board; at 150 MHz they pass.
- The Pico 2 W runs the suite on hardware ([report](docs/engineering/hil-pico2w-2026-11-07.md)): 27 of 27, checked and release, built by GCC and by Clang. The checked runs executed 75 fault checks that reset the RP2350, and its Armv8-M stack limit caught an overflow.
- Benchmarks end through `emb_board_exit()`, so a Pico returns to BOOTSEL after each one and QEMU exits. The AVR board gains `emb_board_exit()`.
- `tools/hil/nucleo-uart-relay` turns a NUCLEO-F446RE into a USB-UART for another board's console ([HIL guide](docs/engineering/hil.md)).

### Added (NUCLEO-F446RE on hardware, 2026-11-07)

- The M3a board runs the suite ([report](docs/engineering/hil-nucleo-2026-11-07.md)): `cm4-nucleo-hil` passed 26 of 26, checked and release, built by GCC and by Clang. The checked runs executed 74 fault checks that reset the STM32F446, the crash record survived a real reset, and a hardware fault's registers were recorded.
- Against FreeRTOS V11.1.0 on the same board, release, GCC, CPU cycles: semaphore hand-over 746 against 831 (−10%), interrupt wake 1,514 against 1,474 (+3%), give and take 194 against 178 (+9%), notification hand-over 794 against 623 (+27%). Clang's build is 2–12% faster than GCC's.
- ADR-055 measured on the NUCLEO: 8–11% fewer cycles on every switching path.

### Fixed (NUCLEO-F446RE on hardware, 2026-11-07)

- `run_hil.py` counted fault checks only at a line start. On the NUCLEO a reset cuts the case's line, so the first count was 31 of 74.

### Added (Pico 2 W on the bench, 2026-11-06)

- The Pico 2 W runs the suite from `run_hil.py` (`cmake --preset cm33-pico2w-hil`, A101): picotool loads each image in BOOTSEL, and each test image reboots into BOOTSEL when it ends, so no button is pressed between images. The console is UART0 through any 3.3 V USB-UART, and [the HIL guide](docs/engineering/hil.md) shows the ESP32 DevKitC's bridge doing that job. Built by GCC and by Clang with LLD.

### Added (Clang cross builds, 2026-11-06)

- The Cortex-M and RISC-V ports build with Clang and link with LLD (A100), against the GNU toolchains' C libraries: `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi-clang.cmake` or `riscv-clang.cmake`, or the presets `cm0-microbit-clang`, `cm3-an385-clang`, `cm4-an386-clang`, `cm4-an386-release-clang`, `cm7-an500-clang`, `cm33-an505-clang`, `rv32-virt-clang` and `rv32-virt-release-clang`. Every suite passes under QEMU with Clang 23.1.5 and LLD, and CI's `clang-cross` job runs them with Ubuntu's Clang. `EMB_CLANG` names another `clang`.
- The AVR port builds with Clang too (`avr-clang.cmake`, presets `avr-uno-clang` and `avr-uno-t1-clang`), linked by avr-gcc's driver. Its code is about 28% larger than avr-gcc's, so `test_mutex` and, checked, `test_msgq` do not fit the Uno's flash.
- On the boards ([report](docs/engineering/hil-clang-2026-11-06.md)): Espressif's Clang build of the ESP32 port passed 24 of 24, checked and release, with 74 fault checks resetting the part. The Clang build of the AVR port passed every image that fits on the Uno: 18 of 18 checked, with 37 fault checks, and 19 of 19 release. On the Uno, Clang's code is 3–8% faster than avr-gcc's and 16% larger. On the ESP32, Espressif's GCC stays 4–18% ahead of its Clang.

### Added (Xtensa port, 2026-11-05)

- The kernel on the classic ESP32 (Xtensa LX6, PRO_CPU), the board `esp32_devkitc` ([port](docs/ports/xtensa.md), ADR-054, A95). It runs on hardware: 21 of 21 tests, 157 cases and 74 fault checks that reset the board, built by EmbCC ([report](docs/engineering/hil-esp32-port-2026-11-05.md)). Every switch happens in the level-1 entry, after every register window is spilled to the thread's own stack. The tick is CCOMPARE0, the interrupt numbers are the interrupt matrix's sources, and the CPU stays at 40 MHz in this milestone.
- The ESP32 board runs its CPU at 160 MHz from the PLL, ESP-IDF's default; the ROM leaves it on the 40 MHz crystal. All six builds (EmbCC, GCC and Clang, checked and release) pass on the board at 160 MHz.
- EmbCC builds track header dependencies (`cmake/toolchains/embcc-depfile.cmake`). Before, CMake asked EmbCC for no dependency files, and a changed header such as the generated `<emb/config.h>` left stale objects behind.
- `cmake --preset xt-esp32-hil` builds it with EmbCC for a run on the board. CI builds `xt-esp32-gcc` with Espressif's GCC and `xt-esp32-clang` with Espressif's Clang and LLD, and keeps the images. Both passed the suite on the board. `run_hil.py` flashes an ESP32 with `embflash --esp` and resets it into a normal boot once its console is open.
- `bench_compare` runs on the ESP32 too, with the board's test line raising the interrupt. The first numbers, in cycles at 40 MHz, are in the report.
- A measured comparison with FreeRTOS on the ESP32 ([report](docs/engineering/bench-esp32-freertos-2026-11-06.md), A96): ESP-IDF v6.1's FreeRTOS V10.5.1, configured as the EmbLinkRTOS release build, counted in CPU cycles on the same board. Built by GCC, EmbLinkRTOS takes 37% fewer cycles per notification switch, 50% fewer per semaphore switch, 34% fewer per uncontended give and take, and 39% fewer per interrupt-to-thread wake. Built by EmbCC it is behind on three of the four. `tools/bench/freertos-esp32/` builds the FreeRTOS side, locally or in CI's ESP-IDF container.

### Fixed (technical review, 2026-11-05)

- Mutex ceiling: a thread above a `EMB_MUTEX_CEILING` mutex's ceiling is refused when the mutex is owned too (SPEC-005 §3.4). Before, it blocked and later received the mutex by hand-off with no check. Checked builds fault, release builds return `EMB_EPERM`.
- Table scheduler (tiny profile): a thread that exits while raised by a mutex now returns to its base priority through `embk_sched_prio_changed()`. Before, the count of raised threads stayed one too high, and every later scheduling decision took the slow scan.
- RISC-V: `tp` points at a thread-local block from reset, so picolibc's `errno` works (A88). Before, the first libc call that set `errno` faulted at address 8. The block is shared by every thread. The toolchain file passes `--specs=picolibc.specs` when compiling, so applications find the C library's headers.
- Cortex-M: a tick that needs more than 2^24 SysTick counts no longer clamps silently to a wrong rate. It fails the build (a static assertion over `CONFIG_EMB_CM_CPU_HZ` and `CONFIG_EMB_TICK_NS`).
- AVR: `EMB_THREAD_STACK_MIN` includes `CONFIG_EMB_AVR_ISR_STACK_RESERVE`, as the option's help always said. `emb_thread_init()` refuses a stack too small for its interrupt reserve (96 + 64 = 160 bytes by default).
- The AVR, RISC-V and Cortex-M manifests no longer claim `tickless: true`. All three ports refuse `EMB_TICKLESS`, and their timer deadline functions are stubs.
- AVR: the idle context's stack restarts at `RAMEND` when the kernel starts (A89). Before, the idle loop and the interrupts it takes ran below `main()`'s dead frames, within about 200 bytes of `.noinit`. Running the fault checks on the Uno found it.
- Delayed work: an item that a nested interrupt cancelled and scheduled again while its expiry was running no longer runs at once (A92). It keeps the new delay. The window needs a kernel-aware interrupt above SysTick's level, so it can only open with `CONFIG_EMB_CM_TIMER_LEVEL` lowered from its default.

### Added (technical review, 2026-11-05)

- A measured comparison with FreeRTOS 11.1.0 on the Uno ([report](docs/engineering/bench-uno-freertos-2026-11-05.md), A94). It uses one benchmark source for both kernels, the same compiler, flags and timer, and FreeRTOS built by `tools/bench/freertos-avr/build.sh`. EmbLinkRTOS release: interrupt-to-thread 5% faster, semaphore hand-over level, notification hand-over 12% slower, give-and-take 34% slower, 34% less RAM, 14% more flash.
- The native port's asynchronous interrupt mode (A93): `emb_native_async_start()` or `EMB_NATIVE_ASYNC_US` lands ticks between any two statements through SIGUSR2. `tests/stress/test_async_irq` runs the kernel under it, with `embk_invariants_check()` at every gate crossing. That checker now backs `CONFIG_EMB_NATIVE_INVARIANTS`, which did nothing before, in every checked native run.
- The interrupt stack is bounded and checked (A90, PORT-016).
  - Cortex-M: MSP is a `CONFIG_EMB_ISR_STACK_SIZE` array (2048 bytes by default) first in RAM, and the first PendSV drops `main()`'s frames from it.
  - Armv8-M Mainline: MSPLIM stops an overflowing interrupt.
  - Every port: checked builds check the interrupt stack's guard, and the interrupted thread's, at every outermost interrupt exit.
  - `emb_isr_stack_info()` reports the high-water mark, and test runs print it. Before, the Cortex-M main stack had no bound, and an overflow overwrote `.bss` silently.
- User guide: an [Interrupts](docs/user/interrupts.md) page, covering handlers, what they may call, nesting and stacks. Its table of interrupt-safe functions is generated from the headers' `@ctx` tags (`tools/docs/isr_table.py`, checked in CI). [Getting started](docs/user/getting-started.md) gains the QEMU path with `run_<image>`, and [Debugging](docs/user/debugging.md) gains crash records on a board and GDB through OpenOCD.
- Corrected documentation (F13): the native CTest count and setup (no `/workspace`, no preinstalled `.venv`), the synchronization and adoption tables that called message queues, event groups, work queues and timers unimplemented, the "no hardware run" sentences of the AVR and HIL pages, the RISC-V suite counts, and the BASEPRI wording: 32 is the API's level 5, not level 1.
- The classic ESP32's bring-up probe ran on a physical board: built by EmbCC, flashed by `embflash --esp` from EmbCC #173 ([report](docs/engineering/hil-esp32-2026-11-05.md)).
- Applications outside the tree (A91): `find_package(EmbLinkRTOS)` before `project()` picks the board's toolchain, then `emb_kernel()` and `emb_add_executable()` build the image, and `run_<name>` runs it natively or under QEMU. `examples/hello_app/` and the new [user guide page](docs/user/application.md) show it, and CI builds it for four boards and runs it on two. It ran on the physical Uno too.
- Fault checks run on boards (A89). `EMB_ASSERT_FAULTS` and checked-build `EMB_ASSERT_MISUSE` were skipped on every embedded build. On a board whose reset keeps RAM (QEMU's MPS2 and RV32 virt, EmbSIM, the Uno and the NUCLEO on hardware), the statement now runs in place, the fault resets the board, and the next boot replays the case to the check and records it. On the MPS2 AN386 that executes 72 checks that were skipped.
- Hardware runs keep their record (A89). `run_hil.py --archive` writes each image's console and a JSON record with the SHA-256 of what was flashed, the git commit, the cases and the benchmark values. `tools/hil/archive.py` files a run under `docs/engineering/hil-runs/`, and `matrix.py` checks `hardware-runs.json` against it.
- `bench_switch` and `bench_irq` print `BENCH v1` lines, and their HIL entries fail without valid, nonzero values (`--bench`). Before, reaching `BENCH_END` passed.
- `EMB_AT_LEAST(d)` and `emb_ticks_at_least()`: a relative wait that lasts at least `d` of real time (A87). A relative wait of n ticks ends at the n-th tick edge after the call, so between n − 1 and n periods pass. This is now stated in SPEC-003 §5.2, KRN-TIM-024 and `emb_thread_sleep()`.

### Changed

- The MPS2 AN385 and AN386 and QEMU's RV32 virt build from their generated descriptions (A75). Their hand-written board files are gone, and their board support moved to `soc/arm/mps2/` and `soc/qemu/virt/`. The configuration hashes are unchanged, and the images are identical except for the build id. Only the ESP32-C3 is still hand-written.

- AVR: the port stops the watchdog after a watchdog reset, in `.init3`. Before, the fault action REBOOT reset the part again every 15 ms.

- The Arduino Uno builds from its generated description (A70).
  - Its hand-written `board.c`, `hw_config.h`, linker script, `board.conf` and `board.cmake` are gone. The ATmega board support (`soc/microchip/atmega/`) and inline drivers (`drivers/microchip/include/`) take every address from `hw_config.h`.
  - The images are byte-identical in `.text` and `.data`, and the configuration hashes are unchanged.
  - The generated linker script limits text to the 32,256-byte app partition, so an image that would overwrite Optiboot no longer links.
  - `generated/embld.opts` gives embld the same layout (HW-006).

- The NUCLEO-F446RE builds from its generated description (A69).
  - Its hand-written `board.c`, `hw_config.h`, linker script, `board.conf` and `board.cmake` are gone. The board directory holds `board.yaml` and `generated/`.
  - The STM32F4 board support (`soc/st/stm32f4/`) and drivers (`drivers/st/`) take every address, gate, pin and frequency from `hw_config.h`.
  - The configuration hash, the register values and the EmbSIM results are unchanged. Each image is 176 to 192 bytes larger in text.
  - Configuration refuses `generated/` files older than their descriptions, and `emb-hwgen.py check` fails on address-like constants in `soc/` and `drivers/` C.

- The tiny thread table has no slot for idle, and `CONFIG_EMB_FAULT_HOOK_RUNTIME=n` binds the fault hook at link time through an application-defined `emb_fault_hook()` (weak empty default), keeping no hook pointer in RAM. The `footprint_t1` reference configuration uses it; AVR `footprint_t1` kernel static RAM is 39 bytes and its gate is lowered from 64 to that value.

- The idle context is no longer reachable through a thread handle, and `emb_thread_self()` returns `EMB_HANDLE_NULL` in it (KRN-THR-026, ADR-050). On the tiny profile without an idle thread only the prefix of its control block is allocated: AVR `footprint_t1` kernel static RAM drops from 64 to 43 bytes.

- The historical M2-01 `cancel_waiter_disinherits` seed-5 report is closed as not reproducible (input unavailable, cause unknown) after bounded triage: not fixed, not classified, still listed among the known limits, with named reopen triggers. `timing-and-evidence.md` defines that disposition.

- `emb_sem_init()` and `emb_mutex_init()` refuse storage that still holds an active object of their type (fault in checked builds, `EMB_EBUSY` in release) instead of overwriting it, when `CONFIG_EMB_OBJ_LIST` is on (default except on the tiny profile). Storage is reused only through destroy. Semaphore and mutex storage grows by one list node where the option is on. KRN-OBJ-007 now specifies `EMB_EBUSY` instead of `EMB_EEXIST` (ADR-045).

### Added

- Toolchain neutrality (ADR-053). There is one port per architecture, and toolchains differ in three places only: the compiler macros, the port assembly and the linker layout. The lint refuses raw `__attribute__`, `__builtin_`, `typeof` and statement expressions outside `arch/` and `include/emb/compiler/`. The support matrix lists the toolchains CI builds each board with. A new `xtensa-gcc` CI job builds the ESP32 probe with Espressif's GCC, pinned by checksum, from the same `esp32.ld` that EmbCC's `embld` reads. ADR-052 is revised to match. IAR and Keil wait for a license.

- Armv6-M, the Cortex-M0 and M0+ (A86). Critical sections use PRIMASK, PendSV is written in Thumb-1, priority registers are written a word at a time, and faults are HardFault only. Two priority bits are accepted for this variant. The new board `microbit`, QEMU's nRF51822, passes all 14 tests in the checked and release tiny builds. The other variants' port code is unchanged.

- The STM32 pin importer (`hw/import/pins.py`, A85) drafts a package's bonded pins and its pins' alternate functions from ST's open pin data. For the STM32F446RE and STM32F446ZE it agrees with the curated files, with 0 differences.

- SoC descriptions outside the tree (A84). An `EMB_BOARD_ROOT` may hold `soc/<vendor>/<family>/`, with its descriptions and board support, beside its boards. A project with a new chip family then needs no change to this tree. `emb-hwgen.py check --board-root` runs the hardware-facts check on the project's C too.

- The Xtensa linker decision (ADR-052): ESP32 images link with EmbCC's `embld` under a linker script, which puts `.rodata` and data in DRAM, since the ESP32's IRAM takes only 32-bit loads. EmbCC #174 adds Xtensa scripts to `embld`. The bring-up probe links with `tools/esp/bringup/esp32.ld`, and falls back to `-Ttext`/`-Tdata` with an older `embld`.

- The Cortex-M7 (A83): the new board `mps2_an500`, QEMU's `mps2-an500`, comes from its description alone and passes all 21 tests in the checked and release builds (presets `cm7-an500`, `-release`) with no change to the port.

- The DeviceTree board importer (`hw/import/dts.py`, A82) drafts a `board.yaml` from a Zephyr board's `.dts`. The draft covers the oscillators, the clock and PLL, the console and its pins, the LEDs, the buttons and the flash. Zephyr's NUCLEO-F446RE agrees with the curated board on every board fact, and its draft, with only a `simulation` line added, builds and passes 19 of 19 on EmbSIM. The generator now refuses a Cortex-M app partition that does not start where the core boots.

- The board support matrix (docs/engineering/support-matrix.md, A81). It gives each board's tier from evidence: hardware (an archived passing run on the board), emulated (CI runs the suite on its emulator) or described (CI builds it). `tools/support/matrix.py` generates the page, and CI keeps it current. It also refuses a CI preset missing from the evidence inventory, which caught eight such presets. Today it lists one board on hardware (the Uno), eight emulated and one described (the Pico 2 W).

- Hardware stack limits on the Cortex-M33 (A80). Each thread's PSPLIM is its stack's base, so an overflowing thread faults at its first push below the base, before it writes outside its stack. The fault is reported as a stack overflow, and the crash record shows CFSR.STKOF. The limit travels in the thread's saved frame, which costs four bytes of stack per thread on Armv8-M and nothing on other variants. New test `test_stack_limit`.

- The Raspberry Pi Pico 2 W (RP2350A on its Cortex-M33 cores, A79), from its description. The SoC was drafted by the SVD importer and curated. The generator now supports a numbered GPIO bank (`GPIO0.f2` pins), and the RP2350's boot ROM block. Every image is also written as a UF2 that the boot ROM's drive accepts, and the build refuses one without its IMAGE_DEF. The board builds (preset `cm33-pico2w`) and has not run yet.

- Cortex-M33 (Armv8-M Mainline) in the Cortex-M port (A78), in the Secure state with TrustZone unused. The new board `mps2_an505`, QEMU's `mps2-an505`, comes from its description alone, and every conformance image passes there: 21 in the checked and release builds, 15 in the tiny build (presets `cm33-an505`, `-release`, `-tiny`). Hardware stack limits (PSPLIM) come later.

- The SVD importer (`hw/import/svd.py`, A77) drafts a SoC description from a CMSIS-SVD file through EmbCC's `embsvd`. The draft covers the core, the interrupt controller and the peripherals, with each peripheral's `compatible` and clock gate taken from a vendor table. It is uncurated, and lists what an SVD lacks as `todo`. `--against` compares the draft with a curated description and never writes it. Against ST's current STM32F446 SVD, the curated F446 die agrees on every fact both hold except where the PVD interrupt sits.

- Boards outside the tree: `-DEMB_BOARD_ROOT=<dir>` builds a project's own board (a custom PCB, say) from its `board.yaml`, on this tree's SoC descriptions. `emb-hwgen.py check --board-root <dir>` checks those boards in the project's CI. The generator stamps such a board's files as `@board/...`, so outputs are the same on every machine. ADR-051 (proposed) sets the board-support model this begins: cores are ported, SoCs imported, boards described, with support tiers taken from evidence (A76).

- A bring-up probe for the classic ESP32 (docs/ports/xtensa.md), ahead of any Xtensa port: `tools/esp/bringup/` builds a kernel-free image with EmbCC. The image stops the ROM's watchdogs and prints on UART0; it is flashed at 0x1000 with `embflash --esp`. `tools/esp/esp_image.py --chip esp32` writes the ESP32's image (chip 0, 40 MHz flash), with each segment placed at its run address. Not yet run on hardware.

- The first hardware run (docs/engineering/hil-uno-2026-11-04.md): the checked tiny suite on an Arduino Uno R3, flashed with embflash, passed 17 of 17. That includes the crash record surviving the watchdog reset, decoded to the faulting line, The release (15/15) and T1 (12/12) builds pass too. Hand-overs measure 81, 68 and 66 µs per switch in the checked, release and T1 builds (about 1,300, 1,085 and 1,062 cycles).

- The HIL runner drives the Arduino Uno R3 (`avr-uno-hil`). It flashes through Optiboot with avrdude, from the board's new `debug_probe.avrdude` hint, then opens the console, which resets the board; the same USB serial port serves both. On the Uno and the NUCLEO-F446RE, the crash record's reset cases run on the board too (`resets_in: hil`).

- Hardware-in-the-loop runs (docs/engineering/hil.md).
  - `tools/hil/run_hil.py` flashes an image with the probe the board's `hw.json` names (OpenOCD), reads the board's console through termios, and reports as the emulator runners do.
  - `EMB_TEST_EMULATOR=hil` with `EMB_HIL_PORT` registers the suite and the benchmarks, run one at a time; `cm4-nucleo-hil` is the preset.
  - New benchmark `bench_irq`: cycles from `emb_irq_pend()` to the connected handler.
  - Tested with a pseudo-terminal; not yet run on a board.

- Hardware faults put their registers in the crash record (A74).
  - Cortex-M: CFSR, HFSR, MMFAR, BFAR and the stacked r0 to r3, r12, lr, pc and xPSR. RISC-V: mcause, mtval, mepc, mstatus, ra and a0 to a7.
  - New port function `emb_arch_crash_regs()`; the retained area grows to 192 bytes.
  - `crashdec` names the registers and decodes the CFSR, HFSR and mcause.
  - `test_crash_hw` checks the registers of a real trap across a reset.

- The NUCLEO-F446ZE runs its tests on EmbSIM's NUCLEO-F446ZE (20 CTest targets, preset `cm4-nucleo-f446ze`). The EmbSIM CI job for the NUCLEO boards now builds EmbSIM `2529ab8`.

- `tools/crashdec` decodes crash records.
  - It reads a raw record file, a console log with `EMB_CRASH` lines, or a hex string, for versions 1 and 2.
  - It refuses a wrong magic, an unknown version, a wrong length or a bad CRC.
  - It names the fault class and the API. With `--elf`, it refuses a record from another build and symbolizes the pc with `addr2line`.
  - CI decodes the records that survived a reset on QEMU Cortex-M4 and EmbSIM's Uno, down to the faulting function and line.

- The crash record (SPEC-015 §4, A71 to A73; `CONFIG_EMB_CRASH_RECORD`, on by default except on the tiny profile, where its AVR code does not fit the footprint gates).
  - A kernel fault writes a CRC-checked record before the hook and the fault action. Version 1 (92 bytes) on 32-bit and native ports holds the build id, firmware version, uptime, fault class, code, API and argument, thread, address, pc and sp. Version 2 (34 bytes) serves AVR.
  - The record lives in memory the reset leaves alone: 192 bytes above the stack on Cortex-M and RISC-V, `.noinit` on AVR, `$EMB_NATIVE_CRASH_FILE` on native.
  - The next boot validates it, and `emb_crash_record_get()` and `emb_crash_record_clear()` (API IDs 145 and 146) read and discard it.
  - A software fault's pc is now the return address into the code that raised it.
  - New port function `emb_arch_system_reset()`.
  - `test_crash_reset` shows the record across a real reset on QEMU MPS2 and RV32 virt and on EmbSIM's NUCLEO-F446RE and Uno.

- A second board on the STM32F446, the NUCLEO-F446ZE, described in YAML only (SPEC-014 §11).
  - It builds with the same STM32F4 board support and drivers, with its console on USART3 (PD8 and PD9) and a solved 180 MHz PLL.
  - The new `cm4-nucleo-f446ze` preset and a CI step build it. Nothing runs it: EmbSIM has no NUCLEO-F446ZE.
  - The description is `curated: false` until checked on a physical board.

- The hardware generator's first increment (SPEC-014, A66 to A68): `hw/gen/emb-hwgen.py`.
  - It validates SoC and board descriptions against JSON Schema 2020-12 files in `hw/schemas/` and against the semantic rules. Each error names the file, line and node.
  - It solves the clock tree and writes a board's `generated/` directory: `hw_config.h`, `board_pins.c`, `regions.ld`, `board.conf`, `board.cmake`, `hw.json`, `board.md` and `build_report.md`, deterministically and stamped with input hashes.
  - Descriptions exist for the STM32F446 (family, die, RE and ZE parts) and the NUCLEO-F446RE. CI regenerates and compares the outputs.

- A CI job runs the experimental EmbCC-kernel / GCC-host native presets (`native-embcc`, `-release`, `-tiny`, `-periodic`) with the EmbCC `0.0.1-beta` release, pinned by version and SHA-256; the merge gate requires it.

- Work-queue statistics (`CONFIG_EMB_WORKQ_STATS`, off by default; KRN-WQ-011, A65): `emb_workq_get_stats()` reports the worst submit-to-start latency, the longest handler with its function and the deepest backlog per queue, in ticks; `emb_workq_reset_stats()` restarts them.

- Static definitions for the other object types (A64): `EMB_SEM_DEFINE`, `EMB_MUTEX_DEFINE`, `EMB_EVENT_DEFINE`, `EMB_COND_DEFINE`, `EMB_MSGQ_DEFINE`, `EMB_WORKQ_DEFINE` and, for interrupt-context callbacks, `EMB_TIMER_DEFINE` declare storage (with the ring or stack) and the handle and construct the object in `emb_kernel_init()`.

- Round-robin time slicing (`CONFIG_EMB_TIME_SLICING`, off by default): a thread at or below `CONFIG_EMB_TIMESLICE_PRIORITY` that runs for `CONFIG_EMB_TIMESLICE_TICKS` without blocking moves behind its READY equal-priority peers; without a peer its quantum is renewed (KRN-SCH-007, 026, 037; A63). The quantum is a deadline in the tickless computation and a comparison in the periodic tick. The reference model implements it, the new `native-timeslice` preset checks it through the differential bridge, and `native-timeslice-tickless` runs the conformance cases with the tickless clock. List scheduler only.

- The M2 contract decisions are accepted: amendments A29 to A61 and ADR-045 to ADR-048 and ADR-050 (project-owner review, 2026-10-31).

- The M2 exit review (`docs/engineering/m2-exit-review.md`): every checklist item with its pull requests and evidence, the reconciliation of APIs, configuration, model and requirements, the defects found, and the remaining M3a hardware work.

- The M2-10 integrated campaign: combined model scenarios across primitives, a `native-coverage` preset with `tools/coverage/report.py` (statement and branch coverage, run in CI), and `tools/model/campaign.py`, whose 158,800-run summaries are archived under `tools/model/evidence/m2-10/` (see `docs/engineering/m2-10-campaign.md`).

- Delayed work (`emb_dwork_t`, M2-09, KRN-WQ-003/007): an item that an internal interrupt-context timer submits to a work queue after a delay or at an instant, with idempotent schedule, reschedule, cancel reporting a stopped delay or removed item as pending, and `cancel_sync`. The queue cannot be destroyed while a delay names it. The reference model, the differential bridge and the work queue conformance suite cover it.

- Software timers (`CONFIG_EMB_TIMER`, `include/emb/timer.h`, M2-09): one-shot and periodic timers whose callbacks run on a named work queue or, with `EMB_TIMER_ISR_CONTEXT`, in the expiry interrupt. Periodic timers re-arm from the previous deadline, skip missed periods in closed form and count them with coalesced callbacks as overruns; `stop` reports the callback's state and `stop_sync` waits for it. Timers have their own deadline list, merged with thread timeouts at expiry (ADR-047). The reference model, the differential bridge and a conformance suite cover them; they require the list scheduler, so the tiny profile has none in this milestone.

- The RISC-V reset code is in its own `.text.embn_rv_reset` section, so an application function named `reset` built with `-ffunction-sections` can no longer take the reset vector's place.

- Work queues (`CONFIG_EMB_WORKQ`, `include/emb/work.h`, M2-08): a kernel thread per queue runs submitted items one at a time in FIFO order. Submit is O(1) and ISR-safe, idempotent while queued and kept while running; cancel reports the state, `cancel_sync` and `flush` wait for completion, and destroy joins the queue's thread. The system work queue (`CONFIG_EMB_SYSTEM_WORKQ`) is linked only into images that reference `emb_workq_system()`. The reference model, the differential bridge and a conformance suite cover them; they require the list scheduler, so the tiny profile has none in this milestone.

- Message queues (`CONFIG_EMB_MSGQ`, `include/emb/msgq.h`, M2-07): fixed-size items copied by value through a caller-provided ring declared with `EMB_MSGQ_RING()`, with direct hand-off to a waiting receiver, slot hand-off from a waiting sender, `send_front`, `peek`, `count` and `free`, `EMB_NO_WAIT` calls from interrupts, timeouts and absolute deadlines, cancellation, destroy with ABORT_WAITERS, and a receive-side notification binding. Items are bounded by `CONFIG_EMB_MSGQ_MAX_ITEM` (64 bytes, 16 on tiny, where `CONFIG_EMB_IPC_SMALL` keeps 8-bit counts). The reference model, the differential bridge and a conformance suite cover them; they are off in the tiny `footprint_t1` reference configuration.

- The reference model covers copy-by-value message queues ahead of their implementation (KRN-IPC-022, M2-07): send, send_front, receive and peek, ISR calls with `EMB_NO_WAIT`, hand-off to a waiting receiver, refill from a waiting sender, destroy, and the empty-to-nonempty binding, checking in every explored state that each accepted item is received once, still stored or dropped by destroy, and that no side waits while the other could proceed.

- Condition variables (`CONFIG_EMB_COND`, `include/emb/cond.h`, M2-06): an atomic enqueue-and-release wait, signal and broadcast, and a mandatory, non-cancelable relock on every result, with `EMB_EOWNERDEAD` and relock failures taking precedence. The reference model, the differential bridge and a conformance suite cover them; they are off on the tiny profile.

- Event flags (`CONFIG_EMB_EVENT`, `include/emb/event.h`, M2-05): ANY/ALL waits with optional CLEAR, deterministic consumption in queue order, ISR-safe set and clear, timeouts, cancellation, destroy with ABORT_WAITERS, and notification binding with unbind. The reference model, the differential bridge and a conformance suite cover them; they are off in the tiny `footprint_t1` reference configuration.

- A reviewed resource-bound inventory (`docs/engineering/resource-bounds.json` and its page) for every cost input in the public `@time` annotations, with `tools/evidence/check_bounds.py` failing CI when a non-constant cost has no entry (M2-04).

- Executed case evidence (M2-04): conformance runs print each case's requirement citations and outcome with assertion and skip counts; `tools/evidence/collect.py` gathers them from the CTest log of any port, CI uploads them per configuration, and `tools/evidence/matrix.py` derives the requirement-by-configuration matrix from executed, asserting cases only.

- `emb_sem_unbind_notify()` (KRN-NOTIF-013). A thread counts the bindings that name it, and `emb_thread_destroy()` refuses it while any does; `emb_sem_bind_notify()` refuses terminated (`EMB_ESTATE`) and kernel (`EMB_EPERM`) targets (KRN-NOTIF-014, ADR-046). The reference model and the differential bridge cover bind, unbind and thread destroy.

- `CONFIG_EMB_OBJ_LIST`: per-type lists of active semaphores and mutexes (KRN-OBJ-011).
- A `.clangd` and exported `compile_commands.json` for editor support; `.clang-tidy` disables, with reasons, the checks that contradict the coding standard.

- Stable numeric IDs in public API misuse fault records, a generated header and downloadable decoder map, with checked-in assignments and compatibility checks. Shared helpers retain the public caller ID and location strings remain optional.

- The Arduino Uno conformance suite on EmbSIM's simulated Uno (`avr-uno-embsim` preset,
  `EMB_TEST_EMULATOR=embsim`, `tools/embsim/run_embsim.py`) and an `avr-embsim` CI job that builds
  EmbSIM from source (SIM-002).
- The NUCLEO-F446RE board (`boards/st/nucleo_f446re`): the STM32F446RE at 180 MHz from the PLL
  fed by the ST-LINK's 8 MHz MCO (HSE bypass, voltage scale 1 with over-drive, five flash wait
  states), the console on USART2 (the ST-LINK's virtual COM port, PA2/PA3), LD2 on PA5, and a
  run that ends masked in WFI (`emb_cm_halt`). Its conformance suite runs on EmbSIM's
  NUCLEO-F446RE (`cm4-nucleo-embsim` preset) in a `cm4-embsim` CI job.
- The ESP32-C3-DevKitM-1 board (`boards/espressif/esp32c3_devkitm1`) and the RISC-V port on the
  ESP32-C3 (`CONFIG_EMB_RV_ESP32C3`, variant `rv32imc`): SYSTIMER's comparator 0 as the tick,
  FROM_CPU_INTR0 as interrupt 0 through the interrupt matrix, a vectored mtvec, the boot ROM's
  watchdogs stopped; each image is also written as the flash image the ROM boots
  (`tools/esp/esp_image.py`). The conformance suite runs on EmbSIM's DevKitM-1
  (`rv32-c3-embsim` preset) in an `rv32-embsim` CI job. The CLINT code moved to
  `arch/riscv/rv_clint.c`; QEMU virt is unchanged.

- Experimental Linux x86-64 presets for an EmbCC-built kernel with a GCC POSIX host, a build-time ABI layout guard and scoped executed conformance/model evidence.

- Searchable Sphinx documentation with native user tutorials, extracted C API, a design-requirement catalog and downloadable metadata.
- Strict documentation builds with complete extracted API annotations and integrity checks, plus optional GitHub Pages deployment.
- Bug/feature issue forms, a pull-request checklist and an ordered M2 implementation checklist.
- Stable CI merge checks, authored-commit DCO checking and a reviewable `main` branch ruleset policy.
- M2-01 native deep-campaign evidence, exact replay inputs and the original timeout-arm diagnosis, with separate corrected-source outcomes.
- Exact native timer-boundary regression checks and a periodic-clock native preset in CI.
- Automatic differential failure bundles, exact offline/live replay, bounded confirmed schedule reduction, a native evidence workflow check and CI retention of default failure bundles.
- Bounded historical seed-5 triage evidence and M2-02 contract proposals for review before kernel implementation.
- Pinned EmbCC ten-item capability review, scoped local compiler/tool/EmbSim evidence and current RTOS integration findings.

### Fixed

- Raising a ready thread's priority above the running thread with `emb_thread_set_priority()` now preempts at once; before, the caller ran on until its next block or yield (found by the M2-10 campaign).

- The conformance suites run under AddressSanitizer on macOS: registration-table entries no longer get Mach-O redzones that the table walk read as entries.

- Thread diagnostic IDs remain stable across priority changes on tiny and unique among live objects on every profile. IDs are released only by successful destroy; exhaustion returns `EMB_ENOMEM` without partial construction.

- Generic IRQ priorities now increase with urgency on Cortex-M, with board-derived bounds and validation before register writes; fixed-level native/AVR/RV32 reject nonzero levels. SysTick defaults to highest kernel-aware urgency. Raw kernel-independent vectors remain unsupported.

- EmbCC now emits storage-layout probe markers without an injected header; generation and kernel-archive compilation pass on eight configurations. Full Linux-native compilation advances to the separate missing-POSIX-header boundary.
- The reference model now delivers already-due tickless timers between sections without requiring another tick; the archived seed-3 trace and reduced cases agree. The periodic differential harness now stops idle ticking when finite waits end. Kernel/architecture implementations are unchanged; the historical seed-5 finding remains open.

### Changed

- Thread storage is reused only through `emb_thread_destroy()` followed by `emb_thread_init()` (SPEC-008 §5, §12; KRN-THR-015). `emb_thread_init()` on storage that still holds an undestroyed thread, including a joined or detached terminated one, is now misuse: a fault in checked builds, `EMB_EBUSY` in release. Re-initializing without destroy previously leaked the diagnostic identity and, with the all-threads list, linked the object twice.

- IRQ callers must use advertised limits; previously ignored/clamped priority requests now fail. Cortex-M configuration derives the public domain from declared priority width and the validated masking threshold; non-nesting configurations require a single kernel-aware level.

- Reconciled design contracts, current implementation limits, timing/evidence obligations and the staged first-release scope.
- Clarified API parameter/storage lifetimes, wait outcomes, configuration and current interrupt-priority/thread-index limitations.
- Refreshed obsolete EmbCC limitations in compiler/AVR/debug/linker/SVD/C++ documentation while retaining incomplete RTOS compiler qualification and existing kernel policies.
- CI test commands reject zero registered tests instead of accepting an empty run.
- Official GitHub Actions are pinned to reviewed Node 24 release commits, replacing deprecated Node 20 action versions.

### Known limits

- The blocking kernel remains incomplete: event flags, condition variables, work queues, software timers and message queues are planned.
- Deep differential failures and known port/introspection inconsistencies remain open. No physical-board, isolation, universal requirement coverage or supported-release claim is made.
- Repository policy files do not activate GitHub settings themselves. Consult the [repository workflow](docs/engineering/repository-workflow.md) and verify the live ruleset and check results.

See [Project status](docs/STATUS.md) for the complete implementation/evidence inventory. This changelog begins with the documentation/workflow preparation; it does not invent a historical release for the existing M1 source.
