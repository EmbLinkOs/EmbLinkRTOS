# The notification hand-over's profile on the NUCLEO-F446RE (2026-11-07)

PC samples from both kernels running `bench_compare` with `BENCH_PROFILE_NOTIFY`, which runs the notification hand-over forever. The samples were taken by `tools/hil/pcsample.py`: OpenOCD 0.12.0 read DWT_PCSR through the ST-LINK 50,000 times per kernel, about one read every 0.57 ms, without halting the core. The [report](../../perf-lto-2026-11-07.md) explains the results.

| File | What |
|---|---|
| `emblinkrtos-pcsr.txt.gz` | the samples of EmbLinkRTOS, release (`353b4657e18922e8`), GCC 14.2.1, `-Os`, no LTO |
| `emblinkrtos-profile.elf` | its image, debug sections stripped. The full image's SHA-256 was `95bf5813…3690`. |
| `freertos-pcsr.txt.gz` | the samples of FreeRTOS V11.1.0 (`tools/bench/freertos-cm4`), GCC 14.2.1, `-Os`, no LTO |
| `freertos-profile.elf` | its image, debug sections stripped. The full image's SHA-256 was `33f4d916…472d`. |

Fold them again:

```sh
python3 tools/hil/pcsample.py emblinkrtos-profile.elf --from-file <(gzip -dc emblinkrtos-pcsr.txt.gz) --period-cycles 1587.46
python3 tools/hil/pcsample.py freertos-profile.elf --from-file <(gzip -dc freertos-pcsr.txt.gz) --period-cycles 1245.74
```

The period is one round trip, two hand-overs, from the same session's release `bench_compare`: 793.73 and 622.87 cycles per hand-over.
