# Running on a physical board (HIL)

M3a's evidence comes from physical boards: the conformance suite run on the board, cycle counts, and latency measured from outside. The NUCLEO-F446RE is the M3a board. The Arduino Uno R3 runs the tiny profile the same way. This page is the procedure for both. The runner, the presets and the benchmark images are tested without a board too. The Uno has run on hardware ([2026-11-04](hil-uno-2026-11-04.md), and again with its fault checks executed, below). The NUCLEO ran on 2026-11-07, built by GCC and by Clang, checked and release, and against FreeRTOS on the same board ([report](hil-nucleo-2026-11-07.md)).

## What you need

- A NUCLEO-F446RE and a USB cable to its ST-LINK (CN1). The ST-LINK flashes the part over SWD, and also carries the console: USART2 through its virtual COM port.
  - **Update the ST-LINK's firmware first.** With V2J28M18 the virtual COM port stopped passing bytes from the host to the board, while the board's output still arrived. Only a USB replug brought it back, and it failed again later. V2J49M36 fixed it. The updater is ST's STSW-LINK007; its Java command line updates without the GUI: `java -jar STLinkUpgrade.jar -sn <serial> -update`, then `-checkVer`. Check the version after the next power cycle: on 2026-11-07 the first update did not persist. OpenOCD prints the version as it connects (`STLINK V2J49M36`).
- OpenOCD on the host. On macOS: `brew install open-ocd`. On Debian or Ubuntu: `apt install openocd`. The board's `hw.json` names its script: `board/st_nucleo_f4.cfg`.
- The Arm toolchain the other Cortex-M presets use.
- For external timing: an 8-channel 24 MHz USB logic analyzer and PulseView (sigrok).

The console's device:
- macOS: `/dev/cu.usbmodem*` (`ls /dev/cu.usbmodem*` after plugging in);
- Linux: `/dev/ttyACM0` (your user needs the `dialout` group).

### For M4's drivers, power and updates (to buy)

M4's driver classes, power states and updates need more than the boards (SPEC-017, SPEC-018, SPEC-019). The parts below are cheap; where possible they are parts EmbSIM also models (the BME280, the DS3231 and the 24LC256), so the same driver runs against the model in CI and the real part on the bench. Each evidence report names the exact part used.

| Part | For | Status |
|---|---|---|
| Resistors, 4.7 kΩ among them | I2C pull-ups: the chips' own are weak, and the analyzer shows the edges | to buy |
| Jumper wires | SPI loopback (MOSI to MISO), I2C between two controllers of one chip, the modules below | on the bench (2026-11-09) |
| A half-size breadboard | the pull-ups and the modules | to buy |
| A 3.3 V USB-to-serial adapter (CP2102 or CH340) | the Pico 2 W's own console, so the NUCLEO no longer relays it while it is also the board under test or the power-cut injector | to buy |
| A powered USB hub | plugging a device into the hub the ST-LINK shares made it re-enumerate in the middle of a run (2026-11-08) | to buy |
| BME280 module (I2C) | a real sensor for the field node and the I2C class; some modules sold as BME280 are BMP280s, without humidity | to buy |
| DS3231 module | a calendar clock for the Pico 2 W, whose RP2350 has none | to buy |
| 24LC256 EEPROM (I2C) | a simple I2C target for the class's tests | to buy |
| W25Q64 module (SPI NOR flash) | the external flash driver on the SPI class | to buy |
| INA219 module (optional) | current at the milliampere level, running and in light sleep. PWR defers current measurement until an instrument is on the bench; this one does not reach deep sleep's microamperes | optional |
| A MOSFET load-switch module (optional) | cutting the NUCLEO's power for the update's power-loss runs: a reset by NRST is not a loss of power | optional |
| Nordic Power Profiler Kit II (later) | deep-sleep current in microamperes | later |

The Pico 2 W needs no part for power cuts: the bench's NUCLEO pulls its 3V3_EN pin low, which turns the Pico's regulator off (SPEC-019).

## The Arduino Uno R3

The Uno flashes and prints over one USB serial port: its ATmega16U2, `/dev/cu.usbmodem*` on macOS, `/dev/ttyACM0` on Linux. The runner therefore uses the other order:
1. It flashes the image's `.hex` through Optiboot, from the `debug_probe` hint in `hw.json`. It uses EmbCC's own programmer, `embflash <hex> --stk500 serial:<port>@115200 --verify` (EmbCC #172), when the installed embflash has `--stk500`; otherwise avrdude, `avrdude -p atmega328p -c arduino -b 115200 -D -U flash:w:<hex>:i`. `--flasher` chooses explicitly.
2. It opens the console. Opening the port resets the board (DTR), so the image starts from its first line while the runner is listening.

Use an EmbCC built with #172 (`make embflash`), or install avrdude with `brew install avrdude` on macOS or `apt install avrdude` on Linux. Then:

```sh
cmake --preset avr-uno-hil -DEMB_HIL_PORT=/dev/cu.usbmodem1101
cmake --build --preset avr-uno-hil
ctest --preset avr-uno-hil
```

The preset is the tiny profile, checked, with the crash record turned on, so `test_crash_reset` shows the record across the part's own watchdog reset. A clone with a CH340 USB chip works too, as `/dev/cu.wchusbserial*`, though an older bootloader may need 57600 baud (`--flash-cmd`).

## The Raspberry Pi Pico 2 W

The Pico 2 W's USB port is the boot ROM's loader, not a console. The console is UART0 on GPIO0 (TX, pin 1) and GPIO1 (RX, pin 2), at 3.3 V, so it needs a USB-UART. Each image is loaded by [picotool](https://github.com/raspberrypi/picotool) while the board is in BOOTSEL.

**Loading.** On macOS, `brew install picotool`. The runner waits until `picotool info` finds a board in BOOTSEL, then runs `picotool load -v -x <image>.uf2 -t uf2`, which writes, verifies and boots the image. A test image built for the board (`EMB_TEST_EMULATOR=hil`) reboots into BOOTSEL through the boot ROM when it ends, so the next image needs no button. A fault check's reset reboots into the image itself, not into BOOTSEL. A new board, or one whose flash is blank, starts in BOOTSEL. Otherwise, and after an image that hung, hold BOOTSEL while plugging the board in.

**A console without a USB-UART adapter: the ESP32 DevKitC's.** The DevKitC's USB bridge (a CP2102 or CH340) is a 3.3 V USB-UART. Hold the ESP32 in reset and the bridge is free for the Pico:

| Wire | From | To |
|---|---|---|
| 1 | DevKitC `EN` | DevKitC `GND` (holds the ESP32 in reset; its UART pins go high-impedance) |
| 2 | Pico 2 W pin 1, GPIO0 (UART0 TX) | DevKitC `TX` (`TXD0`, GPIO1): the bridge's receive line, labelled from the ESP32's side |
| 3 | Pico 2 W pin 3, GND | DevKitC `GND` |

- **Power:** each board takes its power from its own USB cable. Join the grounds only, never 3V3 or 5V.
- **Input:** the tests only print, so the Pico's RX (pin 2) can stay unconnected.
- **Port:** the console is the DevKitC's port, `/dev/cu.usbserial-*` on macOS.

**Or the NUCLEO-F446RE's ST-LINK, through a relay.** Its morpho headers are male, so female-female jumper wires fit, and its ST-LINK has a virtual COM port. The STM32 relays the other board's UART to it in software, because on a stock NUCLEO the header pins named PA2 and PA3 (CN10 pins 35 and 37, Arduino D1 and D0) are not connected to the STM32: solder bridges SB62 and SB63 are open, and the ST-LINK keeps PA2 and PA3 for itself.

`tools/hil/nucleo-uart-relay/build.sh --flash` programs the relay. It works both ways, every byte as it is, at 115200 8N1: what the other board sends on PA10 (USART1 RX) goes out to the ST-LINK's port, and what the host sends to the port goes out of PA9 (USART1 TX) to the other board. Each way has a ring of its own, so a busy direction never holds up the other. The green LED toggles on each byte from the other board. Hold the NUCLEO with its USB connector at the top, and use the right-hand double header, CN10, in its inner column, the one nearer the STM32:

| Wire | From | To |
|---|---|---|
| 1 | Pico 2 W pin 1, GPIO0 (UART0 TX) | CN10 pin 33, PA10: the inner column's 17th pin from the top, the third from the bottom |
| 2 | Pico 2 W pin 3, GND | CN10 pin 9: the inner column's 5th pin from the top. The 4th is AVDD, the STM32's 3.3 V: a ground wire there shorts it. |
| 3 | CN10 pin 21, PA9: the inner column's 11th pin from the top | Pico 2 W pin 2, GPIO1 (UART0 RX): the host's way to the board, for the shell and update channels |

**Bench controls.** The relay also drives three pins for the bench, which later phases wire up: a reset (CN10 pin 23, PA8, to the Pico 2 W's RUN, pin 30), a power cut (CN10 pin 29, PB5, to its 3V3_EN, pin 37) and a wake-up pulse (CN10 pin 27, PB4). The reset and the power cut are released as inputs, so the Pico's own pull-ups hold its pins; the wake pin is driven low between pulses. The host commands them with SPEC-015 §7 frames on channel 15, which the relay reads from the host's stream and still passes on (an image under test leaves channel 15 alone): `reset` (20 ms low), `power off`, `power on`, `power cycle <ms>`, `wake <us>` and `ping`. Each is answered with a channel-15 frame, `ok <command>` or `? <command>`, sent between two of the other board's frames so it never cuts one:

```sh
python3 -c "import sys; sys.path.insert(0, 'tools/transport'); import frame; sys.stdout.buffer.write(b'\0' + frame.encode(15, b'reset'))" > /dev/cu.usbmodem31103
```

The console is then the NUCLEO's port, `/dev/cu.usbmodem*`. A wire one pin off is easy to place. Before the first run, put the other board's TX on PA5 (the 6th pin) for a moment: if the board drives it high, the NUCLEO's green LED lights.

A USB-UART adapter at 3.3 V, or a Raspberry Pi Debug Probe, replaces the DevKitC or the NUCLEO the same way. The Debug Probe also offers SWD, for which the runner has no support yet.

```sh
cmake --preset cm33-pico2w-hil -DEMB_HIL_PORT=/dev/cu.usbserial-0001
cmake --build --preset cm33-pico2w-hil
ctest --preset cm33-pico2w-hil
```

`-DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchains/arm-none-eabi-clang.cmake` builds the same images with Clang and LLD. The CPU runs at 150 MHz from PLL_SYS, and cycle counts are in 150 MHz units. The boot ROM's flash setup (quad reads at a divider of 3) already suits that clock.

## Running the suite

```sh
cmake --preset cm4-nucleo-hil -DEMB_HIL_PORT=/dev/cu.usbmodem1103
cmake --build --preset cm4-nucleo-hil
ctest --preset cm4-nucleo-hil            # the conformance images, then the benchmarks
ctest --preset cm4-nucleo-hil -L bench   # only the benchmarks
```

Each CTest entry runs `tools/hil/run_hil.py`, one at a time (`RUN_SERIAL`):
1. It opens the console at the baud rate `hw.json` gives (115200), raw, and discards anything already waiting.
2. It flashes the ELF and resets the part: `openocd -f board/st_nucleo_f4.cfg -c "program <elf> verify reset exit"`. `--flash-cmd` replaces this command, for example with `st-flash` or pyOCD.
3. It reads the console until the summary line and `EMB_TEST_END`. A benchmark ends at `BENCH_END` instead. With `--bench` (the benchmarks' CTest entries), the run also needs at least one `BENCH v1 name=... key=value` line, with every value an unsigned integer and a nonzero operation count and measurements. Reaching `BENCH_END` alone proves nothing.

The exit status is 0 on a pass, 1 on failures, and 2 on a flash failure or a timeout, as with the emulator runners. Each run also writes `<image>.console.log` next to the image. The runner's first line names the board, the probe, the port and the image's SHA-256, for the evidence record (05 §4.4).

**Fault checks run on the board (A89).** A misuse check (`EMB_ASSERT_FAULTS`, `EMB_ASSERT_MISUSE` in checked builds) runs its statement in place on a board whose reset keeps RAM (`EMB_BOARD_RESETS_IN` in `board.cmake`: the Uno and the NUCLEO on hardware and EmbSIM, the MPS2 and RV32 virt boards under QEMU).
- The kernel's fault resets the board through the test framework's fault hook, after the kernel has written the crash record.
- The next boot reads the framework's 64 retained bytes and skips the cases already finished. It replays the faulting case with its output held back, up to that check, and when the case ends it prints `fault checks that reset the board: N`. The record carries N across the boots.
- The run's console therefore spans several boots, and the runner reads straight through them.
- A check whose statement returns fails in a checked build. A fault fails in a release build.
- A reset check (`EMB_ASSERT_RESETS`, A120) runs the same way, for a statement that should reset the board itself, such as a starved watchdog. The statement calls `emb_test_reset_expected()` just before the reset should come. A reset before that call fails the check, and so does a fault or no reset at all. Elsewhere the check is skipped.

**The record of each run (A89).** Every CTest entry also passes `--archive ${EMB_HIL_ARCHIVE}` (default `<build>/hil-archive`). For each image the runner writes `<image>.log`, its `hil:` line followed by the console, and `<image>.json`. The JSON holds:
- the board, the port and the flash command;
- the SHA-256 of the image and of every file the command programs, such as the Uno's `.hex`;
- the git commit and whether the tree was dirty;
- the start and end times and the outcome;
- the summary, each case's `CASE` line, and the number of fault checks the board reset for;
- any `BENCH` results.

Without `EMB_HIL_PORT`, configuration builds the images and registers no tests. Only a board described in YAML has the `hw.json` the runner needs.

**Archive the outcome.** A run counts toward the board's tier only once it is archived ([support matrix](support-matrix.md), A81):
1. File the records: `python3 -I tools/hil/archive.py build/<preset>/hil-archive --to docs/engineering/hil-runs/<date>-<board>-<build> --preset <preset> --build <build> --report hil-<board>-<date>.md --update docs/engineering/hardware-runs.json`. It refuses records from more than one board, commit or configuration. It copies the logs and records, writes the run's `index.json`, and adds the entry, with its `archive`, to `hardware-runs.json`.
2. Write the report, a `hil-<board>-<date>.md` page that names the preset.
3. Run `python3 -I tools/support/matrix.py`, which also checks each entry against its archive's `index.json`.

Only a run with every test passed earns tier 1.

## Benchmarks

| Image | Measures | Unit |
|---|---|---|
| `bench_switch` | a notification round trip between two threads (two switches) | DWT cycles at 180 MHz |
| `bench_irq` | `emb_irq_pend()` on the test line to the entry of the `emb_irq_connect()` handler: the port's kernel-aware entry and dispatch; minimum, maximum and average over 1000 runs | DWT cycles at 180 MHz |

Under QEMU, `bench_irq` prints figures too (on `mps2-an386`: 60 to 130 cycles). Those are not evidence (SIM-003). Only the board's figures are recorded.

## External timing with the logic analyzer

The cycle counter sees from the pend onward. An external measurement adds the pin and the NVIC's own latency, and catches jitter that the core's view hides. Any analyzer sigrok supports will do. The one used here is a 24 MHz, 8-channel FX2 analyzer, recorded with `sigrok-cli` (`brew install sigrok-cli`, which brings the fx2lafw firmware).

The reference controller drives marker pins for it. Its analyzer leads have female ends, so they go on the morpho header's male pins: CN10's inner column, counted from pin 1 at the USB end, and CN7 for PA0.

| Channel | Pin | Header | Signal |
|---|---|---|---|
| D1 | PA0 (A0) | CN7, inner column, 14th | TIM2's channel 1, toggled in hardware at each sampling trigger |
| D2 | PA10 (D2) | CN10, inner column, 17th | high during the ADC's handler |
| D3 | PA8 (D7) | CN10, inner column, 12th | high while the control thread processes a sample |
| D4 | PA9 (D8) | CN10, inner column, 11th | high while a thread holds the console |
| D5 | PA5 (D13) | CN10, inner column, 6th | LD2 |
| GND | GND | CN10, inner column, 5th | the analyzer's ground |

`tools/hil/controller_timing.py --port /dev/cu.usbmodem* --out <record directory>` sets the controller's rate and load, captures each scenario and measures every sample ([report](controller-timing-2026-11-07.md)). Things to know:
- **The console can't be captured:** PA2 and PA3 reach only the ST-LINK on this board, because the links to the headers, SB62 and SB63, are open from the factory.
- **Capture every channel:** libsigrok 0.5.2's VCD output mislabels a subset chosen with `-C`, so the tool captures all eight.
- **Resolution:** at 24 MHz a sample is 41.7 ns, about 7.5 core cycles at 180 MHz. The tool uses 12 MHz, which this kind of analyzer sustains over USB. Either suffices for microsecond-scale latency and jitter, not for single instructions.

## The reference controller

`examples/controller` is M3a's first application (docs/adoption.md). `tools/hil/controller_session.py` flashes it, keeps the console open through the whole run and runs its scenario. The phases are boot, steady sampling, stacks, overload into degraded mode and back, 1 kHz, a soak, an injected fault and a hang:

```sh
cmake -S examples/controller -B build/controller -G Ninja -DEMB_BOARD=nucleo_f446re
cmake --build build/controller
python3 tools/hil/controller_session.py --port /dev/cu.usbmodem* --elf build/controller/controller.elf \
        --out docs/engineering/hil-runs/<date>-nucleo_f446re-controller --soak 1800
```

It writes `controller.log`, the console with each command marked, and `controller.json`, each phase's facts and outcome. After the fault it decodes the crash record with `tools/crashdec` against the ELF. The STM32 sets the pin flag on every internal reset, because the reset drives NRST, so a software or watchdog reset reads `software pin` or `watchdog pin`. `tools/hil/controller_soak.py` runs only a soak, for hours, on a controller already running: it checkpoints its record every ten minutes and writes the final record when stopped with Ctrl-C or SIGTERM ([report](controller-soak-2026-11-08.md)). By hand, the console takes `help`, `status`, `load <0-400>`, `rate <10-1000>`, `setpoint <mC>`, `fault` and `hang` at 115200 baud ([report](controller-nucleo-2026-11-07.md)).

## The first session, in order

1. `ctest --preset cm4-nucleo-hil`: the whole suite on silicon, the crash record's reset cases included (`test_crash_reset`, `test_crash_hw`).
2. `bench_switch` and `bench_irq`: keep the console logs. Their `meta:` lines carry the build identity.
3. Record the results in `docs/engineering/` as the first HIL runner evidence. Mark the descriptions `curated: true` where the board confirmed them (the clock, the console, the LED).
4. With the analyzer: the console on CH0 against the run, then LD2 toggles for a first look at jitter.
