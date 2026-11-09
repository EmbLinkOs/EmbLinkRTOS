# The reference controller on the NUCLEO-F446RE (2026-11-07)

The first run of [docs/adoption.md](../adoption.md)'s application on its board. The run lasted 30 minutes and passed all 9 phases, among them overload, an injected fault and a hang. M3a's exit asks that the application handle overload and failure on physical hardware, and that the diagnostic record identify an injected failure. Both are shown here.

**Board:** NUCLEO-F446RE (STM32F446RE, Cortex-M4F) at 180 MHz, as in the [suite's runs](hil-nucleo-2026-11-07.md).
- **Probe:** the on-board ST-LINK/V2-1, firmware V2J49M36, updated for this run (see What the run found).
- **Flashing:** OpenOCD 0.12.0 (`board/st_nucleo_f4.cfg`).
- **Console:** USART2 through the ST-LINK's virtual COM port at 115200 baud, `/dev/cu.usbmodem11203`.

**Image:** `examples/controller`, built by GCC 14.2.1 (Arm GNU Toolchain 14.2.Rel1), configuration `f7a7de6f6a2ea8bd`, from commit `dc91e59` on a clean tree (branch `feat/controller`).
- **Build id:** `7b8fc41d2f3afced190e0e57486e16b621d0057f`, the one the crash record carries.
- **Dirty flag:** the run's record says `"dirty": true`. It reads the tree's state when the run ends, and a documentation index line was edited during the run. The image was built before that edit: its generated `build_id.c` names `dc91e59` with no `+dirty`.

**Record:** the folder `docs/engineering/hil-runs/2026-11-07-nucleo_f446re-controller`. `controller.log` is the console as received, with each command marked `>>>`. `controller.json` holds each phase's facts and outcome, and the decoded crash record.

## The application

| Part | What it does |
|---|---|
| Acquisition | TIM2's update event starts an ADC1 conversion of the chip's temperature sensor in hardware, 100 times a second by default. The conversion's interrupt timestamps the reading with the cycle counter and posts it to an 8-slot message queue without waiting. |
| Control thread (priority 6) | Takes each sample with a timeout of three periods, converts it with the factory calibration, filters it, and switches a simulated heater (the green LED) with ±0.5 °C hysteresis around a 40 °C setpoint. Processing that ends after the next sample is due is a deadline miss. Three in a row enter degraded mode, which processes every other sample. Two seconds of processing that fits one period leave it. |
| Command thread (priority 4) | Lines on the console, received by interrupt: `help`, `status`, `load`, `rate`, `setpoint`, `fault`, `hang`. `load` adds busy work to each processing, as a percentage of a period. |
| Telemetry thread (priority 2) | One `TLM` line a second. It feeds the independent watchdog (2 s) only when the control thread has made progress since the last line. |
| Shared console | Replies and telemetry share USART2 under a priority-inheriting mutex. |
| Boot | Prints the reset's cause from RCC_CSR, then the crash record left by the last run, if any, as an `EMB_CRASH` line, and clears it. |

The configuration (`controller.conf`) sets these options:
- the reboot fault action and the crash record;
- the fault's function name;
- stack statistics;
- no breakpoint before the fault action.

## Results

`tools/hil/controller_session.py` ran the phases in order on one open console:

| Phase | What happened | Outcome |
|---|---|---|
| boot | Flashed; reset cause `pin`; `CTL crash none` | pass |
| normal | 5 s at 100 Hz: 100.0 samples a second, no miss, missed sample, dropped sample or ADC overrun; the chip at 25.5 °C | pass |
| stacks | The `status` command's stack use (table below), each under its size | pass |
| overload | `load 150` asks for 15 ms of work per 10 ms period. Four deadline misses, degraded mode entered once and kept; every other sample shed; processing at 15.0 ms against degraded mode's 20 ms deadline, with no miss after the entry | pass |
| recovery | `load 0`; back to normal within three seconds, 100 samples a second again | pass |
| fast | `rate 1000` with `load 50`: 1,000.0 samples a second for 5 s, processing at 510 µs against the 1 ms period, no miss | pass |
| soak | 1,800 s at 100 Hz with `load 30`: 179,900 samples processed. Missed samples, dropped samples, ADC overruns, deadline misses and received-byte drops all stayed at zero. Latency at most 9 µs, processing at most 3,010 µs; no reset | pass |
| fault | `fault` executes a trap (`udf`) in the command thread. The kernel recorded the fault and rebooted the part: reset cause `software`, and the next boot reported the record, decoded below | pass |
| hang | `hang` stops the control thread in a loop at the highest priority. Telemetry stops, so the watchdog is not fed. The part reset 1.5 s after the command's reply, reset cause `watchdog`, and no crash record | pass |

The STM32 sets the pin flag on every internal reset, because the reset drives NRST, so the causes read `software pin` and `watchdog pin`.

### Overload, line by line

The TLM lines after `load 150` (fields trimmed):

```
TLM t=7  mode=degraded samples=670 deadline_miss=4 shed=33  degraded=1 lat_max_us=10018 proc_max_us=25018 load=150
TLM t=8  mode=degraded samples=720 deadline_miss=4 shed=83  degraded=1 lat_max_us=9     proc_max_us=15009 load=150
...
TLM t=12 mode=degraded samples=920 deadline_miss=4 shed=283 degraded=1 lat_max_us=9     proc_max_us=15009 load=150
>>> load 0
TLM t=14 mode=degraded samples=1020 shed=381 lat_max_us=8 proc_max_us=11 load=0
TLM t=15 mode=normal   samples=1106 shed=395 lat_max_us=8 proc_max_us=11 load=0
```

The first second holds the transition. Three consecutive misses entered degraded mode, and the queued sample behind them was late too: four misses, latency up to 10 ms while the backlog drained. After that, the controller processes 50 samples a second and sheds 50, each processing within its two-period deadline. It stays degraded while the overload lasts. It leaves once two seconds of processing have fitted one period, within three seconds of the load's removal.

### The injected failure, decoded

The boot after the fault printed:

```
emb: kernel fault class 10 code 6 pc 134219980 at architecture fault
CTL boot: EmbLinkRTOS reference controller, rate=100 Hz
CTL reset software pin
CTL crash class=10 code=6 pc=0x080008cc uptime_ticks=1826461
EMB_CRASH 454d4243...
```

`tools/crashdec`, given the line and the ELF, accepted the record's build id and decoded it:

| Field | Value |
|---|---|
| fault | `hardware` (class 10), code 6: the UsageFault exception |
| CFSR | `0x00010000`: UNDEFINSTR, an undefined instruction |
| thread | index 1, the command thread |
| pc | `0x080008cc`, in `execute` at `examples/controller/main.c:357`: the `EMB_TRAP()` of the `fault` command |
| uptime | 1,826,461 ticks (about 30 minutes 26 s) |
| then | `reboot` |

The record names the failure, the thread and the source line.

## Memory

Stack use from `CONFIG_EMB_STACK_STATS`, the most each stack had held when the stacks phase ran, after the normal phase and before the overload:

| Stack | Used | Size |
|---|---|---|
| control thread | 236 B | 1,024 B |
| command thread | 284 B | 1,024 B |
| telemetry thread | 348 B | 1,024 B |
| interrupt stack | 376 B | 2,048 B |

The image, from `arm-none-eabi-size`, occupies these sections:

| Section | Size | Holds |
|---|---|---|
| text | 17,864 B | flash: the code and constants |
| data | 24 B | initialized RAM |
| bss | 7,408 B | zeroed RAM; the stacks are part of it |

The STM32F446RE has 512 KiB of flash and 128 KiB of RAM. Clang 23.1.5 builds the same application to 19,808 B of text. Both compilers build it for the NUCLEO-F446ZE too, from that board's description, and CI builds it out of the tree for both boards.

## Latency

`lat_max_us` is the time from the conversion's interrupt to the start of its processing in the control thread, by the cycle counter: the interrupt's exit, the kernel's wake of the control thread, and the switch. Over the soak it never exceeded 9 µs, about 1,600 cycles, with the command and telemetry threads printing to the shared console.

It is the core's own view. It starts at the handler's timestamp, after the interrupt's entry, and it cannot show jitter in the trigger itself. A logic analyzer on marker pins measured it from outside the same evening: 9.50 µs at the median and at most 10.0 µs, also at 1 kHz and in degraded mode ([report](controller-timing-2026-11-07.md)).

## What the run found

The first attempts failed. Each failure was a real defect, fixed before the run above:

- **The reboot action halted the part.** The four hardware boards' `emb_board_fault_report()` ended with `emb_board_exit()`, which halts, and the port's `emb_arch_reset()` calls it before its reset. No configuration had selected the reboot action on hardware before. The first fault came back only through the watchdog. Fixed in the boards (A106).
- **The fault halted at a breakpoint, and the watchdog froze.** The checked build's halt-for-debug breakpoint fires when DHCSR.C_DEBUGEN is set. The ST-LINK leaves it set after flashing, even with no debugger connected. OpenOCD's F4 setup also freezes the IWDG while the core is halted (DBGMCU), so the part stayed halted. The controller turns `CONFIG_EMB_FAULT_HALT_FOR_DEBUG` off, as a deployed image should.
- **Degraded mode oscillated.** Under a lasting overload the controller left degraded mode every two seconds, after two seconds without a miss, and re-entered it. Leaving now needs processing that fits the normal deadline.
- **The session tool parsed a partial line.** The first 30-minute soak passed, then the tool failed on a TLM line still arriving. It reads complete lines now, and keeps the log on any failure.
- **The ST-LINK's virtual COM port stopped passing bytes to the board.** With firmware V2J28M18 the board's output kept arriving but commands did not. A USB replug brought it back only for a while. V2J49M36 fixed it ([HIL guide](hil.md)).

## Not shown here

- **The heater is simulated.** The LED stands in for it, and the chip's own temperature sensor, which the heater does not affect, stands in for the plant. The control loop's timing and its handling of overload and failure are real; its regulation is not.
- **The soak is 30 minutes.** A later overnight soak ran 10 hours 6 minutes with nothing missed or dropped ([soak report](controller-soak-2026-11-08.md)).
- **Stack use was read before the overload.** The run's later phases may have used more; a `status` at the end of the run is the next session's addition.
- **Latency in this run comes from the cycle counter.** The external measurement is a [separate report](controller-timing-2026-11-07.md).
- **One board, one compiler on the board.** The Clang image was built but not run. It ran on 2026-11-08, with the release builds ([release builds](controller-release-2026-11-08.md)).
- **Hardware run index:** the run is not in `hardware-runs.json`, whose entries are the suite's runs (`emb-hil-run v1`).

## Reproducing it

```sh
cmake -S examples/controller -B build/controller -G Ninja -DEMB_BOARD=nucleo_f446re
cmake --build build/controller
python3 tools/hil/controller_session.py --port /dev/cu.usbmodem* --elf build/controller/controller.elf \
        --out <record directory> --soak 1800
```
