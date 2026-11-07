# EmbLinkRTOS and FreeRTOS on the ESP32 (2026-11-06)

The second measured comparison with another kernel (R-003 §6, [A96](../specs/AMENDMENTS-M3.md)), after the [Uno's](bench-uno-freertos-2026-11-05.md). It ran on the same physical board with the same benchmark source, counted in CPU cycles. One board, one configuration each: a comparison, not a ranking.

## Method

- **Board:** the ESP-WROOM-32 board of the [port's hardware runs](hil-esp32-port-2026-11-05.md), port `/dev/cu.usbserial-0001`, flashed by EmbCC's `embflash --esp`.
- **Source:** `tests/benchmarks/bench_compare.c`, one file for both kernels.
- **FreeRTOS as ESP32 projects run it:** ESP-IDF **v6.1** (release zip, SHA-256 `cdeea7db…cc16`) and its FreeRTOS **V10.5.1**, built by ESP-IDF's pinned GCC 15.2.0 (`esp-15.2.0_20251204`). `tools/bench/freertos-esp32/` is the ESP-IDF application. Its `sdkconfig.defaults` matches the EmbLinkRTOS release build:
  - one core (`CONFIG_FREERTOS_UNICORE`) and a 1 kHz tick;
  - `-Os` and assertions off;
  - no stack-overflow check at each switch (EmbLinkRTOS's release build has none either);
  - FreeRTOS in IRAM (`CONFIG_FREERTOS_IN_IRAM`), as the whole EmbLinkRTOS image is;
  - no task or interrupt watchdog.

  The full configuration is filed beside the record as `sdkconfig`.
- **EmbLinkRTOS:** board `esp32_devkitc`, base profile, release (`EMB_SET=EMB_CHECKED=n`, configuration `a6675cd36514cdfd`). It was built three times, by Espressif's GCC 16.1.0 (`xt-esp32-gcc`), by Espressif's Clang 22.1.4 with LLD (`xt-esp32-clang`), and by EmbCC (`xt-esp32-hil`).
- **Clock:** CCOUNT, the CPU's cycle counter, for both kernels: `esp_cpu_get_cycle_count()` and `emb_arch_cycles()`. The CPUs ran at different rates: ESP-IDF's lowest setting is 80 MHz, and the EmbLinkRTOS board stays at the 40 MHz the ROM leaves. Both run their code from IRAM and their data from internal SRAM, which take one cycle at either rate. Peripheral accesses do not: the interrupt trigger's register write and its read-back take a few cycles more at the higher rate, which touches only `isr_wake`.
- **Measurements:** the Uno's four, each a total over thousands of operations, divided by their number. A hand-over figure is one switch: the benchmark counts both directions. The tick's interrupts are inside the totals for both.
- **The interrupt:**
  - EmbLinkRTOS raises the board's test line, FROM_CPU_INTR0, through `emb_irq_pend()`. FreeRTOS uses FROM_CPU_INTR2, since ESP-IDF keeps 0 and 1 for its cross-core calls; its handler is registered with `esp_intr_alloc(..., ESP_INTR_FLAG_IRAM, ...)`.
  - A FROM_CPU write reaches the CPU some cycles later, and FreeRTOS's first run showed it: 2500 wakes for 5000 triggers, as triggers merged. On the ESP32 each trigger now waits for its wake, on both kernels.

## Results

CPU cycles per operation (`per_op_x100` / 100). The records are under `hil-runs/2026-11-06-esp32-bench-compare/`. Every run woke the echo thread 5000 times out of 5000.

| | FreeRTOS V10.5.1, ESP-IDF v6.1, GCC 15.2 | EmbLinkRTOS, GCC 16.1 | EmbLinkRTOS, Clang 22.1.4 | EmbLinkRTOS, EmbCC |
|---|---|---|---|---|
| `notify_handover` | 1539 | 976 (−37%) | 1143 (−26%) | 1761 (+14%) |
| `sem_handover` | 1967 | 976 (−50%) | 1062 (−46%) | 1754 (−11%) |
| `sem_give_take` | 317 | 209 (−34%) | 201 (−37%) | 411 (+30%) |
| `isr_wake` | 3215 | 1962 (−39%) | 2118 (−34%) | 3273 (+2%) |

Built by GCC, as FreeRTOS is, EmbLinkRTOS takes 34–50% fewer cycles than ESP-IDF's FreeRTOS on all four. Built by EmbCC, it is behind FreeRTOS on three of the four, by up to 30%. That is the same kernel and port, so the gap is EmbCC's code for the kernel's C, as the [port's report](hil-esp32-port-2026-11-05.md) found: EmbCC takes about 1.8 times GCC's cycles.

## Both at 160 MHz

Later the same day, the EmbLinkRTOS board moved its CPU to 160 MHz (the board's PLL setup), and FreeRTOS's build moved to 160 MHz too (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160`). Rerun on the same board, with the records under `hil-runs/2026-11-06-esp32-bench-compare-160mhz/`, CPU cycles per operation:

| | FreeRTOS V10.5.1 | EmbLinkRTOS, GCC 16.1 | EmbLinkRTOS, Clang 22.1.4 | EmbLinkRTOS, EmbCC |
|---|---|---|---|---|
| `notify_handover` | 1535 | 969 (−37%) | 1135 (−26%) | 1749 (+14%) |
| `sem_handover` | 1959 | 973 (−50%) | 1056 (−46%) | 1742 (−11%) |
| `sem_give_take` | 316 | 208 (−34%) | 200 (−37%) | 406 (+28%) |
| `isr_wake` | 3201 | 1951 (−39%) | 2105 (−34%) | 3247 (+1%) |

Every figure is within 1% of the run with different clocks above. Counting in cycles had removed the clock from the comparison, as intended. The EmbLinkRTOS images are from `d72aaeb` (GCC, Clang) and `957b45d` (EmbCC, rebuilt after a stale-object bug in EmbCC builds: CMake tracked no header dependencies for EmbCC, so the first EmbCC rerun still carried the 40 MHz tick). FreeRTOS's image is from `d72aaeb`.

## What differs besides the kernel

- **The kernels' scope.** ESP-IDF's FreeRTOS is built for two cores and keeps their machinery in unicore builds:
  - spinlock-taking critical sections;
  - per-core state;
  - a coprocessor-aware context save;
  - ESP-IDF's own level-1 dispatcher, which reads a table for each interrupt.

  That is part of what ESP32 projects pay, and part of what this comparison measures. EmbLinkRTOS's port is one core.
- **The compiler.** ESP-IDF v6.1 pins GCC 15.2, and EmbLinkRTOS was built by Espressif's GCC 16.1, the newer release. The same version for both would mean building ESP-IDF with a toolchain it does not pin.
- **The clock.** 80 MHz against 40 MHz in the first run, as above; both at 160 MHz in the rerun, with the same results.
- **No sizes.** An ESP-IDF image carries its bootloader, partition table, startup, heap and drivers, so its size says nothing about the kernel. The Uno's comparison could compare sizes; this one does not.

## Records

| Run | Commit | Image SHA-256 (first 16) |
|---|---|---|
| FreeRTOS: `bench_compare_freertos.elf` (`.bin` at 0x10000 `01027a47…e7b7`, bootloader at 0x1000 `9c26f885…2551`, partition table at 0x8000 `7f00b6c0…c820`) | `8dd949a`, clean | see `freertos/bench_compare_freertos.json` |
| EmbLinkRTOS GCC | `c59c92b`, clean | `e6763fe97258426c` |
| EmbLinkRTOS Clang | `c59c92b`, clean | `036f69c1d3bdd3ba` |
| EmbLinkRTOS EmbCC | `c59c92b`, clean | `cdf7ec66acef0b18` |

Between `c59c92b` and `8dd949a` only FreeRTOS's `sdkconfig.defaults` changed: FreeRTOS in IRAM, and no stack check. FreeRTOS from flash had measured the same within 0.1%: its cache hit. With the canary check it was 2% slower.

The FreeRTOS images were flashed by `embflash --esp` at their three offsets, and `run_hil.py --flash-cmd true` read the console after resetting the board.
