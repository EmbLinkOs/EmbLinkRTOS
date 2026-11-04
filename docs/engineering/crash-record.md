# The crash record

A kernel fault leaves a record of itself for the next boot ([SPEC-015 §4](../specs/SPEC-015-observability-formats.md), OBS-016). The application reads it with `emb_crash_record_get()`, uploads `raw`, and discards it with `emb_crash_record_clear()`. `tools/crashdec` decodes it. This page covers the first increment ([A71 to A73](../specs/AMENDMENTS-M3.md)).

## What a fault does

`raise_info()` in `kernel/fault/fault.c` runs these steps in order, with interrupts masked:
1. It fills `emb_fault_info_t`.
2. It emits the fault trace record.
3. **It writes the crash record.**
4. It calls the hook.
5. It takes the configured action: halt, or reboot through `emb_arch_reset()`.

Because the record is written before the hook, a hook that resets the part (as `test_crash_reset` does) still leaves the record behind. `emb_kernel_init()` validates the record once, checking magic, version, length and CRC-32. A record that does not check is ignored.

| Field | Version 1 (32-bit, native) | Version 2 (AVR) |
|---|---|---|
| Size | 92 bytes | 34 bytes |
| Build id | 20 bytes (`emb_build_id`) | first 4 bytes |
| Firmware version | 8 bytes (`CONFIG_EMB_FW_VERSION`) | absent |
| Uptime | 64-bit ticks | 32-bit ticks |
| Fault | class, argument, 16-bit code, API ID, thread | the same, 8-bit thread |
| Addresses | fault address, pc, sp (32-bit), lr, stack bounds (0) | fault address, pc, sp (16-bit) |
| Registers, flight recorder | counts 0 (not captured yet) | absent |

A software fault's pc is the return address into the code that raised it. On a hardware fault, the port supplies the faulting pc. On AVR it is the word address avr-gcc reports.

## Where it survives

| Port | Memory | Survives |
|---|---|---|
| Cortex-M, RISC-V | 128 bytes reserved by the linker script at the top of RAM, above the stack, as symbols and not a section | startup (no section clears it) and the emulators' reset: QEMU's `virt` re-zeroes every ELF segment, but not memory outside them |
| AVR | 34-byte `.noinit` array | avr-libc's startup; the watchdog reset |
| Native | process buffer, mirrored to `$EMB_NATIVE_CRASH_FILE` when set | process exit, through the file |

## Verification by target

| Target | `test_crash` (in one boot) | `test_crash_reset` (across a real reset) |
|---|---|---|
| Native presets (not tiny) | yes, plus a real fault in a forked child read back from the file | not applicable: a process reset is a new process (the forked case covers it) |
| QEMU MPS2 AN385 and AN386 (not tiny) | yes | yes, SYSRESETREQ |
| QEMU RV32 virt (not tiny) | yes | yes, the sifive_test reset |
| QEMU arduino-uno | no: the tiny profile leaves the record off | no: the machine has no watchdog, so the reset never comes |
| EmbSIM NUCLEO-F446RE | yes | yes, SYSRESETREQ |
| EmbSIM Arduino Uno (`avr-uno-embsim`, record turned on) | yes | yes, the watchdog reset |
| EmbSIM ESP32-C3 | yes | no: the board has no `emb_board_system_reset()` yet |

The table comes from scratch probes: a `.noinit` word, a reset, and a report on the second boot. The probes ran on QEMU 11.1.1 and on EmbSIM `c528293` and `2529ab8`. `EMB_BOARD_RESETS_IN` in a board's `board.cmake`, or `simulation.resets_in` in its `board.yaml`, names the emulators where `test_crash_reset` is built.

## AVR: the watchdog after its own reset

On the ATmega328P a watchdog reset leaves the watchdog enabled, with its shortest timeout. Until this change, the fault action REBOOT therefore reset the part again every 15 ms. The port now clears WDRF and stops the watchdog in `.init3`, before the C runtime starts. EmbSIM shows both behaviors.

## Decoding: tools/crashdec

```sh
python3 -I tools/crashdec/crashdec.py run.log                                     # EMB_CRASH lines in a console log
python3 -I tools/crashdec/crashdec.py "$EMB_NATIVE_CRASH_FILE"                    # the native port's raw file
python3 -I tools/crashdec/crashdec.py run.log --elf image.elf --tool-prefix arm-none-eabi-
```

`crashdec` accepts versions 1 and 2 and refuses a wrong magic, an unknown version, a wrong length or a failing CRC-32 (exit 2). It names the fault class from `include/emb/fault.h` and the API from `tools/fault/api-ids.json`.

- **With `--elf`.** It reads `emb_build_id` from the image and refuses a record from another build (exit 1; `--force` decodes anyway). It then symbolizes the pc with `<prefix>addr2line`, doubling a version 2 (AVR) word address first. Both decode to the faulting line: the Cortex-M4 record from QEMU and the Uno's from EmbSIM name `crash_record_survives_reset` at the line after its `emb_fault()` call.
- **Native.** Native images are position independent on Linux, so their pc does not symbolize without the load address. The QEMU Cortex-M CI job decodes and symbolizes the record `test_crash_reset` left instead.
- **Tests.** `tools/crashdec/test_crashdec.py` decodes a record the native kernel printed, which ties the C serializer to the decoder.
## The tiny profile

The record is off by default on the tiny profile ([A73](../specs/AMENDMENTS-M3.md)). On AVR its code costs about 1 KiB of flash, which the tiny footprint gates (`tools/footprint/thresholds-avr-uno*.json`, baseline plus 10 percent) do not have room for. A tiny build that wants it sets `CONFIG_EMB_CRASH_RECORD=y`, as the `avr-uno-embsim` preset does, so that CI still verifies the AVR record and its survival across the watchdog reset.

## Limits

- No register snapshot, flight recorder or partition mini-records. The register snapshot needs the ports' capture, and the flight recorder the trace rings.
- The build id is fixed at configure time: two dirty builds of one commit and configuration share it.
- `reset_reason` is the kernel's action, not the SoC's reset cause.
- The F446's backup SRAM is not used.
- Nothing has been run on a physical board.
