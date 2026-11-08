# The reference controller's release builds on the NUCLEO-F446RE (2026-11-08)

The [first run](controller-nucleo-2026-11-07.md) of the reference controller used a checked build: every API call validated, every misuse a fault. A product ships a release build, with those checks compiled out. This run repeats the same 9-phase session, and the [external timing](controller-timing-2026-11-07.md), on three release images of the same source:

| Image | Built by | Session | Soak |
|---|---|---|---|
| **GCC release** | GCC 14.2.1 (Arm GNU Toolchain 14.2.Rel1) | 9/9 | 30 minutes, as the checked run |
| **GCC release, LTO** | the same, with `EMB_LTO=ON` ([ADR-056](../architecture/06-decision-records.md)) | 9/9 | 5 minutes |
| **Clang release** | Homebrew Clang 23.1.5, LLD | 9/9 | 5 minutes |

It is also the first time the controller ran as a Clang image. The checked report listed that as a gap.

**Board, probe and console:** as in the checked run: NUCLEO-F446RE at 180 MHz, the ST-LINK/V2-1 (V2J49M36), OpenOCD 0.12.0, USART2 at 115200 baud, here `/dev/cu.usbmodem11103`.

**Images:** `examples/controller` with `-DEMB_SET=EMB_CHECKED=n`, from commit `81cdd14` on a clean tree (every record says `"dirty": false`), configuration `15487535d65a4874`.
- **Build id:** `6e6e2371d2a8d5b9d58d36581b42f88379385851` in all three. It digested the commit and the configuration, not the compiler ([SBOM](sbom.md)), so the three shared it. Since the review of 2026-11-08 it digests the toolchain too (A72, amended), and the three would have three ids.
- **Image identity:** the images differ. Each session record holds its image's SHA-256: GCC `ba65ae56…`, LTO `a433d2b9…`, Clang `67f894fa…`.

**Records,** in `docs/engineering/hil-runs/`:

| Image | Session | Timing |
|---|---|---|
| GCC | `2026-11-08-nucleo_f446re-controller-release-gcc` | `2026-11-08-nucleo_f446re-controller-timing-release-gcc` |
| GCC, LTO | `…-controller-release-gcc-lto` | `…-controller-timing-release-gcc-lto` |
| Clang | `…-controller-release-clang` | `…-controller-timing-release-clang` |

Each session folder holds `controller.log` and `controller.json`, as before, plus the image's stack bound (`stack-bound.json`) and its memory report (`memory.txt`; for Clang, `size.txt`, see below). Each timing folder holds the captures (`<scenario>.vcd.gz`), `timing.json` and the images.

## Results

Every phase passed on every image:
- **boot:** reset cause and no crash record;
- **normal:** telemetry at 100 Hz with nothing missed;
- **stacks:** measured use;
- **overload, recovery:** 150% load: degraded mode, load shed, recovery;
- **fast:** 1 kHz;
- **soak;**
- **fault:** an injected trap, its crash record kept across the reboot and decoded;
- **hang:** the watchdog's reset.

| | Checked, GCC (2026-11-07) | Release, GCC | Release, GCC LTO | Release, Clang |
|---|---|---|---|---|
| Soak | 30 min | 30 min | 5 min | 5 min |
| Samples in the run | 187,613 | 187,617 | 37,619 | 37,614 |
| Missed samples, queue drops, ADC overruns, console drops | 0 | 0 | 0 | 0 |
| Worst latency in the soak, cycle counter | 9 µs | 6 µs | 5 µs | 5 µs |
| Stack use: control, command, telemetry, interrupt | 236, 284, 348, 376 B | 228, 276, 324, 368 B | 196, 244, 148, 144 B | 172, 220, 140, 316 B |
| The fault decoded to | `execute`, `main.c:357` | `execute`, `main.c:378` | `execute`, `main.c:378` | `execute`, `main.c:378` |
| Watchdog reset after the hang | 1.5 s | 1.7 s | 1.7 s | 1.7 s |

The fault's line moved because `main.c` changed between the runs. Both lines are the `EMB_TRAP()` of the `fault` command.

**Deadline misses happen only in the overload phase.** That is the phase's purpose: 15 ms of work per 10 ms period. The count when degraded mode takes over differs between runs (4 in the checked run, 4 to 6 here), because it depends on where in a period the `load` command lands. No miss occurs in any other phase.

## Latency, measured from outside

The logic analyzer (an fx2lafw 8-channel analyzer under sigrok-cli, 12 MHz), wired as in the [checked timing run](controller-timing-2026-11-07.md), measured each sample: from the conversion interrupt's entry to the control thread at work. Each image ran the same four scenarios:
- normal;
- 30% load;
- 1 kHz with 0.5 ms of work;
- overload in degraded mode.

| Image | Samples | Median | Max, any scenario |
|---|---|---|---|
| Checked, GCC (2026-11-07) | 24,998 | 9.50 µs | 10.0 µs |
| Release, GCC | 25,999 | 7.33 µs | 7.75 µs |
| Release, Clang | 25,999 | 6.25–6.33 µs | 6.58 µs |
| **Release, GCC LTO** | 25,999 | **5.83–5.92 µs** | **6.17 µs** |

**The spread is under half a microsecond in every scenario, overload included.** The control thread has the highest priority, and the work that causes the overload runs inside it, after the measured point. So a degraded controller still answers its interrupt as fast as an idle one.

**The timing records do not name the image they measured.** `controller_timing.py` does not flash; it measures whatever runs. Each capture started within 21 seconds of its image's session ending, with nothing flashed between, as the records' start and end times show.

## Memory and stack bounds

| Image | Flash (text + data) | RAM (data + bss) | Stack bound: control, command, telemetry, interrupt |
|---|---|---|---|
| Checked, GCC (2026-11-07) | 18,096 B | 7,624 B, from the memory report | 484, 532, 612, 1,604 B |
| Release, GCC | 16,432 B | 7,432 B | 280, 328, 392, 1,264 B |
| Release, GCC LTO | 14,000 B | 7,416 B | not established (below) |
| Release, Clang | 18,200 B | 7,416 B | 224, 272, 208, 1,120 B |

Flash and RAM here are `arm-none-eabi-size`'s sections. `memory_report.py` counts 7,624 B of RAM for the GCC release image: it also counts the retained crash-record area, which lies outside `.bss`. Every bound is below its stack (1 KiB per thread, 2 KiB for interrupts) and above the use measured on the board.

## What the run found

- **Under LTO, the stack bound cannot be computed.** `stack_bound.py` reads the call graph from the disassembly. LTO merges and renames functions (`raise_info.constprop.0`), and three indirect calls no longer match the roots file `examples/controller/stack-roots.json`: from `embn_cm_irq_entry`, `embn_cm_reset` and `embn_cm_systick`. The tool reports them as problems rather than guess. Its other figures for the LTO image are not a bound. The measured use on the board (144–244 B) is well inside the stacks, but measured use is not a bound.
- **Under LTO, the memory report cannot split flash by component.** LTO compiles the program as one unit, so the map gives everything to one input. `memory.txt` has the regions' totals only.
- **`memory_report.py` reads only GNU ld's map.** It takes the memory regions from the map's "Memory Configuration" table, which LLD's map does not have, and stops with an index error on the Clang image. CI runs the report on GCC builds only, so this had not shown. The Clang record has `arm-none-eabi-size`'s sections instead.
- **LTO's debug information keeps the absolute build path.** The LTO image's crash record decoded to `/Users/…/build/ctl-rel-lto/./examples/controller/main.c`. The GCC and Clang images decoded to `./examples/controller/main.c`, as `-ffile-prefix-map` intends ([reproducible builds](reproducible-builds.md)). Code generated at link time does not get the compile-time path mapping. So an LTO image depends on where it was built. CI's double build does not build with LTO, which is why it had not shown.
- **A USB device plugged into the same hub stopped the first attempt.** Connecting the analyzer made the ST-LINK re-enumerate during the first session's normal phase. The console read failed (`Device not configured`), and the run was repeated from the start. The record kept is the second run's.

## Not shown here

- **The LTO and Clang soaks are 5 minutes,** not 30.
- **The release builds' stack bounds** are for GCC and Clang only.
- **The heater is still simulated,** as in the checked run.
- **One board.** The NUCLEO-F446ZE image builds, but no board ran it.

## Reproducing it

```sh
cmake -S examples/controller -B build/ctl-rel -G Ninja -DEMB_BOARD=nucleo_f446re -DEMB_SET=EMB_CHECKED=n
cmake --build build/ctl-rel
python3 tools/hil/controller_session.py --port /dev/cu.usbmodem* --elf build/ctl-rel/controller.elf \
        --out <record directory> --soak 1800
python3 tools/hil/controller_timing.py --port /dev/cu.usbmodem* --out <timing record directory>
# LTO: add -DEMB_LTO=ON. Clang: add -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchains/arm-none-eabi-clang.cmake
python3 tools/footprint/stack_bound.py --elf build/ctl-rel/controller.elf --build build/ctl-rel \
        --roots examples/controller/stack-roots.json
python3 tools/footprint/memory_report.py build/ctl-rel/controller.elf.map --elf build/ctl-rel/controller.elf
```
