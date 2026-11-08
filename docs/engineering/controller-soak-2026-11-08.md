# The reference controller's long soak (2026-11-07 to 2026-11-08)

The [controller's run](controller-nucleo-2026-11-07.md) soaked it for 30 minutes. Here it ran overnight, for **10 hours 6 minutes**, at 100 Hz with 30% of each period busy. It processed 3,636,800 samples, every one it was due. No count of a miss, a drop or an overrun rose, the board never reset, and the host never lost the console.

The soak was planned for 24 hours and stopped at about 10 hours by choice: no specification asks for 24, and the board was needed. The record says so: `planned_hours` 24, `ended_because` `SIGTERM`.

**Board:** the NUCLEO-F446RE of the [controller's run](controller-nucleo-2026-11-07.md), at 180 MHz, ST-LINK firmware V2J49M36. Console at 115200 baud, `/dev/cu.usbmodem11103`.

**Image:** `examples/controller`, built by GCC 14.2.1 (Arm GNU Toolchain 14.2.Rel1), configuration `f7a7de6f6a2ea8bd`, from commit `d77b11f` on a clean tree.
- **Build id:** `851ef204ccd636544a116b86528abc188ccaac11`.
- **SHA-256 of the ELF:** `7d739f873f3c8085393d3f4c47f12f003e45b6a337496dc85d3773370019f564`, in the record.
- **Size:** 18,072 B of text, 24 B of data, 7,408 B of bss.

**Record:** the folder `docs/engineering/hil-runs/2026-11-07-nucleo_f446re-controller-soak`.
- `soak.json` is the record.
- `soak.log.gz` is the console as received, compressed: 36,393 lines, 7.2 MB unpacked, SHA-256 `874e011de7cd3f6a9681b8d58dba8a8a1ce43679bd4d03f5f8c6a4f26097a087` unpacked. `gzip -dc soak.log.gz` reads it.

## How it ran

`tools/hil/controller_soak.py` attached to the running controller; it did not flash or reset it. It:
1. sets `rate 100` and `load 30`;
2. reads the stacks' high-water marks with `status`;
3. reads one `TLM` line a second until stopped;
4. reads the high-water marks again.

It rewrote the record every 10 minutes, so a host failure would have lost at most one interval. The host ran under `caffeinate -ims`, and it did not sleep.

The controller's counters are cumulative and its `t` counts its own seconds. A reset would show as a boot line or as `t` starting again; a gap in the host's reading would show as a jump in `t`. The soak passes when neither happened, the rate held, and no miss, drop or overrun counter rose.

## Results

| | |
|---|---|
| Started | 2026-11-07 23:32:46 (host) |
| Ended | 2026-11-08 09:38:59 (host), by SIGTERM |
| Board time soaked | 36,368 s (10 h 6 min 8 s) |
| Telemetry lines | 36,369, `t` advancing by one each |
| Samples processed | 3,636,800: 100.0 a second |
| Missed, deadline misses, queue drops, ADC overruns, received-byte drops | 0, none rising |
| Shed, degraded | 0: normal mode throughout |
| Resets, board gaps, host gaps | none |
| Latency (`lat_max_us`) | at most 9 µs, the whole night |
| Processing (`proc_max_us`) | at most 3,009 µs, against the 10 ms period |
| Chip temperature | 24.4 to 26.2 °C |
| Outcome | **pass** |

Latency here is the cycle counter's measure, from the ADC's interrupt to the control thread at work. A logic analyzer measured the same path at 9.50 µs median and 10.0 µs at most ([timing report](controller-timing-2026-11-07.md)).

### Stacks

The high-water marks did not move between the start and the end of the soak:

| Stack | Start | End | Size | Static bound ([memory](controller-memory-2026-11-07.md)) |
|---|---|---|---|---|
| control thread | 236 B | 236 B | 1,024 B | 484 B |
| command thread | 284 B | 284 B | 1,024 B | 532 B |
| telemetry thread | 340 B | 340 B | 1,024 B | 612 B |
| interrupt stack | 368 B | 368 B | 2,048 B | 1,604 B |

## Not shown here

- **10 hours, not 24.** Nothing here speaks for longer runs, or for anything that happens only after days, such as a counter's wrap.
- **One steady load.** The soak held one rate and one load. Overload, 1 kHz, the injected fault and the hang were the [controller's run](controller-nucleo-2026-11-07.md).
- **The console was quiet.** Only the start and end commands crossed it; the command thread otherwise waited.
- **One board, one compiler.** The Clang image was not soaked.

## Reproducing it

```sh
cmake -S examples/controller -B build/controller -G Ninja -DEMB_BOARD=nucleo_f446re
cmake --build build/controller
openocd -f board/st_nucleo_f4.cfg -c "program build/controller/controller.elf verify reset exit"
caffeinate -ims python3 tools/hil/controller_soak.py --port /dev/cu.usbmodem* --out <record directory> \
        --hours 24 --checkpoint 600 --image build/controller/controller.elf
```

Stop it at any time with Ctrl-C or `kill -TERM` on the Python process; it writes the final record either way. Exit status 0 is a pass.
