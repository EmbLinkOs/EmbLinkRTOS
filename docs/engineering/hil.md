# Running on a physical board (HIL)

M3a's evidence comes from physical boards: the conformance suite run on the board, cycle counts, and latency measured from outside. The NUCLEO-F446RE is the M3a board. The Arduino Uno R3 runs the tiny profile the same way. This page is the procedure for both. The runner, the preset and the benchmark images exist and are tested without a board. **Nothing on this page has been run on hardware yet.**

## What you need

- A NUCLEO-F446RE and a USB cable to its ST-LINK (CN1). The ST-LINK flashes the part over SWD, and also carries the console: USART2 through its virtual COM port.
- OpenOCD on the host. On macOS: `brew install open-ocd`. On Debian or Ubuntu: `apt install openocd`. The board's `hw.json` names its script: `board/st_nucleo_f4.cfg`.
- The Arm toolchain the other Cortex-M presets use.
- For external timing: an 8-channel 24 MHz USB logic analyzer and PulseView (sigrok).

The console's device:
- macOS: `/dev/cu.usbmodem*` (`ls /dev/cu.usbmodem*` after plugging in);
- Linux: `/dev/ttyACM0` (your user needs the `dialout` group).

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
3. It reads the console until the summary line and `EMB_TEST_END`. A benchmark ends at `BENCH_END` instead.

The exit status is 0 on a pass, 1 on failures, and 2 on a flash failure or a timeout, as with the emulator runners. Each run also writes `<image>.console.log` next to the image. The runner's first line names the board, the probe, the port and the image's SHA-256, for the evidence record (05 §4.4).

Without `EMB_HIL_PORT`, configuration builds the images and registers no tests. Only a board described in YAML has the `hw.json` the runner needs.

**Archive the outcome.** A run counts toward the board's tier only once it is archived ([support matrix](support-matrix.md), A81):
1. Add one entry per build to `docs/engineering/hardware-runs.json`: board, preset, build, date, configuration hash, passed and total.
2. Name the report that records the run, a `hil-<board>-<date>.md` page that names the preset.
3. Run `python3 -I tools/support/matrix.py`.

Only a run with every test passed earns tier 1.

## Benchmarks

| Image | Measures | Unit |
|---|---|---|
| `bench_switch` | a notification round trip between two threads (two switches) | DWT cycles at 180 MHz |
| `bench_irq` | `emb_irq_pend()` on the test line to the entry of the `emb_irq_connect()` handler: the port's kernel-aware entry and dispatch; minimum, maximum and average over 1000 runs | DWT cycles at 180 MHz |

Under QEMU, `bench_irq` prints figures too (on `mps2-an386`: 60 to 130 cycles). Those are not evidence (SIM-003). Only the board's figures are recorded.

## External timing with the logic analyzer

The cycle counter sees from the pend onward. An external measurement adds the pin and the NVIC's own latency, and catches jitter that the core's view hides.

| Channel | Pin (Arduino header) | Signal |
|---|---|---|
| CH0 | PA2 (D1) | USART2 TX: PulseView's UART decoder at 115200 8N1 shows the console |
| CH1 | PA5 (D13) | LD2: `emb_board_led_toggle()`, a marker a test or benchmark can drive |
| GND | any GND pin | the analyzer's ground |

At 24 MHz a sample is 41.7 ns, about 7.5 core cycles at 180 MHz. That suffices for microsecond-scale latency and jitter, not for single instructions. An external-trigger latency image has not been written yet: a GPIO input edge to its handler toggling a pin. It needs an EXTI line, which waits for the board's GPIO input driver.

## The first session, in order

1. `ctest --preset cm4-nucleo-hil`: the whole suite on silicon, the crash record's reset cases included (`test_crash_reset`, `test_crash_hw`).
2. `bench_switch` and `bench_irq`: keep the console logs. Their `meta:` lines carry the build identity.
3. Record the results in `docs/engineering/` as the first HIL runner evidence. Mark the descriptions `curated: true` where the board confirmed them (the clock, the console, the LED).
4. With the analyzer: the console on CH0 against the run, then LD2 toggles for a first look at jitter.
