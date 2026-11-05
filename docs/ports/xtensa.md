# Xtensa (classic ESP32): bring-up

**Status:** not a port yet. The classic ESP32 (two Xtensa LX6 cores, the ESP-WROOM-32 module on an "ESP-32S" development board) is in scope under [ADR-051](../architecture/06-decision-records.md) (any board), and its images link under one GNU ld script, with EmbCC's `embld` or with Espressif's GCC ([ADR-052](../architecture/06-decision-records.md), [ADR-053](../architecture/06-decision-records.md)). What exists is a bring-up probe that checks, before any kernel code is written, that the toolchain, the image, the flasher and the register addresses are right. It ran on a physical ESP32 on [2026-11-05](../engineering/hil-esp32-2026-11-05.md): built by EmbCC, flashed by `embflash --esp`, booted by the ROM, printing.

**Sources:** `tools/esp/bringup/` (`esp32_hello.c`, `build.sh`), the image tool `tools/esp/esp_image.py --chip esp32`, and EmbCC's `embflash --esp` (EmbCC D-023).

## The probe

The ESP32's boot ROM loads an application image from flash offset **0x1000** (the C3 boots from offset 0) and calls its entry on the ROM's own stack. UART0 runs at 115200 baud, on the board's USB-serial bridge.

`call_start_cpu0` stops the three watchdogs the ROM leaves guarding a flash boot: RTC_CNTL's, then TIMG0's and TIMG1's. For each one it writes the key 0x50D83AA1 to the write-protect register, clears WDTCONFIG0, and locks the register again. It then prints `emb: hello from the ESP32 (Xtensa LX6), line N` about twice a second, forever. The addresses come from ESP-IDF v5.4's `components/soc/esp32/register/soc/`.

```sh
tools/esp/bringup/build.sh                      # build/esp32-bringup/esp32_hello.bin
embflash build/esp32-bringup/esp32_hello.bin --esp serial:/dev/cu.usbserial-XXXX --base 0x1000 --verify
```

Replace `XXXX` with the port the board shows (`ls /dev/cu.*`). embflash resets the board into download mode through DTR (IO0) and RTS (EN), as the board's auto-reset circuit expects. If the board does not have that circuit, hold BOOT while pressing EN. Read the output with any terminal at 115200 baud.

## Toolchain and image

- **Compiler.** EmbCC's `xtensa-none-elf`, or Espressif's `xtensa-esp32-elf-gcc` (`XTENSA_GCC=xtensa-esp32-elf-gcc tools/esp/bringup/build.sh`). Both use the windowed ABI, which the ROM's own calls and window vectors (VECBASE 0x40000000) expect. CI builds the probe with Espressif's GCC, and `check_probe.py` checks either image the same way.
- **Linker.** `tools/esp/bringup/esp32.ld` puts the code in IRAM (0x40080400) and `.rodata`, `.data` and `.bss` in DRAM (0x3FFF0000). These are ESP-IDF's `bootloader.ld` `iram_seg` and `dram_seg`, which the ROM loads into without overwriting itself. This needs an `embld` with Xtensa scripts (EmbCC D-024, #174, ADR-052). With an older one, `build.sh` falls back to `-Ttext 0x40080400 -Tdata 0x3FFF0000`.
- **Segment addresses.** `embld` stores `.data` after the code: its load address follows the text, its run address is 0x3FFF0000. `esp_image.py --chip esp32` therefore writes each segment at its run address. Its header gives chip ID 0, DIO at 40 MHz, 4 MB and maximum revision 3.99; its checksum and appended SHA-256 are the same as for the C3.
- **Byte data in DRAM.** The instruction RAM takes only 32-bit accesses, so everything a byte load reads must be in DRAM. The script does that for `.rodata`. The probe also keeps its strings in `.data`, so that the fallback link works too.

## Before a core port

A kernel port needs four parts:

- the windowed-register spill on a context switch;
- the CCOMPARE timer for the tick;
- the interrupt matrix and the level-1 vector;
- a linker layout that keeps read-only data out of IRAM.

The linker layout is decided: EmbCC's `embld` with a generated script (ADR-052). The core could be brought up first on QEMU's `sim` machine with a de212 core, as EmbCC's Xtensa tests run, since QEMU has no ESP32 machine and EmbSIM models only the ESP32-C3. Timing measured there would not count as evidence (SIM-003).
