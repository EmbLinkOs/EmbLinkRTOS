# NUCLEO-F446RE: the M3a board on hardware (2026-11-07)

The first runs of EmbLinkRTOS on a physical Cortex-M. The suite ran on the NUCLEO-F446RE, the M3a board, built by GCC and by Clang, checked and release. Every image passed. The same session measured FreeRTOS on the same board with the same benchmark source, and the kernel before and after ADR-055.

**Board:** NUCLEO-F446RE (STM32F446RE, Cortex-M4F) at 180 MHz from the PLL, its HSE taken from the ST-LINK's 8 MHz MCO. Flash runs at 5 wait states with the ART accelerator (prefetch, instruction and data caches) on.
- **Probe:** the on-board ST-LINK/V2-1, firmware V2J28M18.
- **Flashing:** OpenOCD 0.12.0 (`board/st_nucleo_f4.cfg`).
- **Console:** USART2 through the ST-LINK's virtual COM port, `/dev/cu.usbmodem1103`.

**Commit:** `56ba8ac`, from a clean tree (branch `feat/nucleo-hil`), for every run except the one before ADR-055 below.

## Results

`ctest` on `cm4-nucleo-hil`, each run's records under `hil-runs/`:

| Build | Compiler | Configuration | Passed | Cases | Fault checks that reset the part | Interrupt stack, most used |
|---|---|---|---|---|---|---|
| checked | Arm GNU Toolchain 14.2.Rel1 (GCC 14.2.1) | `ee4b66ea67cdeb7b` | 26 of 26 | 166 | 74 | 252 of 2,048 B |
| release | the same | `353b4657e18922e8` | 26 of 26 | 164 | none (release) | not measured (release) |
| checked | Homebrew Clang 23.1.5 with LLD 23.1.5 | `ee4b66ea67cdeb7b` | 26 of 26 | 166 | 74 | 276 of 2,048 B |
| release | the same | `353b4657e18922e8` | 26 of 26 | 164 | none (release) | not measured (release) |

What these runs proved on the chip:
- **Fault checks:** every misuse fault check ran. Each check faulted in the kernel, reset the STM32F446 through SYSRESETREQ, and was settled when the next boot replayed the case (A89).
- **Crash record:** it survived a real reset (`test_crash_reset`), and held a hardware fault's registers (`test_crash_hw`, the UsageFault of a `udf`).
- **FPU:** FPU state survived preemption (`test_fpu`).

The first count of fault checks was 31 for GCC and 3 for Clang, but the consoles show 74 for both. The reset cuts the case's `TEST` line while it is still leaving the UART, and the runner counted only at line starts. It is fixed (`56ba8ac`), and the runs above were repeated with it. The earlier Uno and ESP32 records count the same either way.

## Against FreeRTOS on the same board

`bench_compare` built for each kernel, CPU cycles per operation (DWT CYCCNT, 180 MHz). Both images were built from the same commit, and both used the board's own clock, pin and USART drivers.
- **FreeRTOS:** `tools/bench/freertos-cm4/build.sh nucleo_f446re`, FreeRTOS-Kernel V11.1.0 at `dbf7055`, port GCC/ARM_CM4F, with the configuration matched to the release build.
- **EmbLinkRTOS:** the release build above.

| Operation | FreeRTOS V11.1.0, GCC | EmbLinkRTOS, GCC | EmbLinkRTOS, Clang |
|---|---|---|---|
| `notify_handover` | 623 | 794 (+27%) | 724 (+16%) |
| `sem_handover` | 831 | 746 (−10%) | 673 (−19%) |
| `sem_give_take` | 178 | 194 (+9%) | 190 (+7%) |
| `isr_wake` | 1,474 | 1,514 (+3%) | 1,328 (−10%) |
| `bench_compare` image text, data + bss | 7,056 B, 5,344 B | 11,080 B, 6,104 B | 12,192 B, 6,096 B |

Built by GCC, as FreeRTOS is:
- **Ahead:** EmbLinkRTOS leads on the semaphore hand-over.
- **Close:** it is within 3% on the interrupt-to-thread wake.
- **Behind:** it trails on the uncontended give and take by 9%, and on the notification hand-over by 27%.

Clang's build is ahead of GCC's on every operation here, by 2–12%. By the project's rule, the GCC build is the one the comparison judges.

The notification hand-over is the gap to work on. Under QEMU the two kernels' instruction counts for it differed by about 10% (A97), but here the cycles differ by 27%, so the difference is in how the instructions run on this core: branches, the flash interface, and the exception entry and return. The next step is to profile that path on the board with the DWT, not in an emulator.

The images also differ in size beyond the kernels themselves:
- EmbLinkRTOS carries the crash record, the checked-build hooks it keeps in release, and its own startup and table registration.
- FreeRTOS's image has no crash record.

The size comparison here is of two whole images, not of two kernels.

## ADR-055 on this board

The same benchmarks from #84's head `befec33`, the kernel before ADR-055, in the same configuration (`353b4657e18922e8`), run in the same session:

| Operation | Before ADR-055 | After | Change |
|---|---|---|---|
| `notify_handover` | 878 | 794 | −9.6% |
| `sem_handover` | 842 | 746 | −11.4% |
| `sem_give_take` | 194 | 194 | 0 |
| `isr_wake` | 1,648 | 1,514 | −8.1% |

As the ESP32 showed (7–10%), keeping the running thread in its ready list takes 8–11% off every switching path, and leaves the uncontended give and take alone. A second run of the release benchmarks gave the same figures to the cycle.

## Interrupt latency

`bench_irq` pends the test line and counts cycles from the pend to the first instruction of its handler, 1,000 times:

| Build | Average | Minimum | Maximum |
|---|---|---|---|
| GCC release | 153 | 153 | 365 |
| Clang release | 150 | 150 | 193 |
| GCC checked | 174 | 174 | 631 |
| Clang checked | 156 | 155 | 560 |

This is the latency as the CPU counts it, from the pend inside the core. A latency measured from outside, from a pin edge with the logic analyzer, is still to be done ([HIL guide](hil.md)).

## How to repeat

```sh
cmake --preset cm4-nucleo-hil -DEMB_HIL_PORT=/dev/cu.usbmodem1103
cmake --build --preset cm4-nucleo-hil && ctest --preset cm4-nucleo-hil
# release: -DEMB_SET=EMB_CHECKED=n; Clang: -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchains/arm-none-eabi-clang.cmake
tools/bench/freertos-cm4/build.sh nucleo_f446re build/freertos-cm4-nucleo
python3 -I tools/hil/run_hil.py build/freertos-cm4-nucleo/bench_compare.elf \
  --hw-json boards/st/nucleo_f446re/generated/hw.json --port /dev/cu.usbmodem1103 --until BENCH_END --bench
```
