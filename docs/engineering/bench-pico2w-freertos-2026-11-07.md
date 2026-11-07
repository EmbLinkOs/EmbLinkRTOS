# EmbLinkRTOS and FreeRTOS on the Pico 2 W (2026-11-07)

The fourth same-board comparison with FreeRTOS, after the [Uno](bench-uno-freertos-2026-11-05.md), the [ESP32](bench-esp32-freertos-2026-11-06.md) and the [NUCLEO-F446RE](perf-lto-2026-11-07.md), and the first on a Cortex-M33 (A105). Both kernels ran the same benchmark source on the same board, the same clocks and the same console, counted in CPU cycles. One board, one configuration each: a comparison, not a ranking.

## Method

- **Board:** the Raspberry Pi Pico 2 W of the [hardware runs](hil-pico2w-2026-11-07.md): RP2350A, Arm cores in the Secure state, 150 MHz from PLL_SYS, executing in place from QSPI flash.
  - **Loading:** picotool in BOOTSEL. Each image reboots into BOOTSEL as it ends.
  - **Console:** UART0 through the NUCLEO-F446RE's relay (`tools/hil/nucleo-uart-relay`).
- **Source:** `tests/benchmarks/bench_compare.c`, one file for both kernels.
- **FreeRTOS:** `tools/bench/freertos-cm33/build.sh`, FreeRTOS-Kernel V11.1.0 at `dbf7055`.
  - **Port:** its GCC/ARM_CM33_NTZ port in the Secure state: FPU on, no MPU, no TrustZone, `configRUN_FREERTOS_SECURE_ONLY`.
  - **Configuration:** as the NUCLEO's: static allocation, preemption without time slicing, 32-bit ticks at 1 kHz, no assertions, stack checks, trace, hooks or timers.
  - **Board bring-up:** the board's own EmbLinkRTOS support (`soc/rpi/rp2350/board.c`) sets the clock, the pins and UART0, so the two kernels share them exactly.
- **EmbLinkRTOS:** the release build (`EMB_SET=EMB_CHECKED=n`, configuration `097edee415c887c6`) at `7965bf1`, which includes `EMBK_HOT` (A104).
- **Compiler:** GCC 14.2.1 (Arm GNU Toolchain 14.2.Rel1) for both, at `-Os`. Each kernel was also built with link-time optimization (`EMB_LTO`, `BENCH_LTO=1`).
- **Clock:** SysTick-derived cycles for both, as each port counts them.

## Results

CPU cycles per operation (`per_op_x100` / 100). The records are under `hil-runs/2026-11-07-pico2_w-freertos-*` and `hil-runs/2026-11-07-pico2_w-emblinkrtos-*`.

| Operation | FreeRTOS `-Os` | EmbLinkRTOS `-Os` | FreeRTOS LTO | EmbLinkRTOS LTO |
|---|---|---|---|---|
| `notify_handover` | 599 | 482 (−20%) | 545 | 402 (−26%) |
| `sem_handover` | 733 | 456 (−38%) | 668 | 402 (−40%) |
| `sem_give_take` | 211 | 151 (−28%) | 168 | 125 (−26%) |
| `isr_wake` | 1,226 | 873 (−29%) | 1,151 | 775 (−33%) |
| image text | 7,064 B | 10,920 B | 6,248 B | 9,016 B |

EmbLinkRTOS takes fewer cycles than FreeRTOS on every operation here, built either way, by 20–40%.

## Against the NUCLEO-F446RE

The same benchmark on the NUCLEO-F446RE, release, built with LTO, puts the two kernels much closer: the notification hand-over at parity (567 against 565 cycles), the give and take 11% behind. The two boards differ in more than the core:
- **FreeRTOS's ports:** on the Cortex-M33 it uses its Armv8-M port, which takes more cycles per operation than its Cortex-M4F port. Its notification hand-over is 545 cycles on the Pico against 565 on the NUCLEO, but its semaphore paths and give and take are slower than its own NUCLEO figures relative to EmbLinkRTOS.
- **Memories:** the RP2350 executes from QSPI flash through a cache, the STM32F446 from on-chip flash through its ART accelerator. Layout affects each differently.

No profile of FreeRTOS on the Pico was taken. The figures say how the two kernels compare on this board, not why.

## What differs besides the kernel

- **Image sizes:** EmbLinkRTOS's images are larger. They carry its startup, table registration and crash record, and its build of the board code; FreeRTOS's image links only what its `bench_platform.c` uses from the board code.
- **Configuration:** EmbLinkRTOS's release build keeps the handle checks of its API contract; FreeRTOS's configuration has `configASSERT` off.
