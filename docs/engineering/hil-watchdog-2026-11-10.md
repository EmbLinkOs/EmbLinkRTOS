# The watchdog class on hardware: NUCLEO-F446RE and Pico 2 W (2026-11-10)

The watchdog class of SPEC-017 §8 (A120). On each board, the board's watchdog is started, fed for half a second, then starved. It resets the part, and after the reset its driver reports that it caused the reset. The whole conformance suite ran on each board with GCC and with Clang, and every image passed. The same STM32 driver runs in CI on EmbSIM's NUCLEO-F446RE, whose IWDG resets the simulated board.

**Commit:** `4b3f187`, from a clean tree (branch `feat/m4.2-driver-classes`, PR #140).

**Bench:** the wires of the UART, SPI and I2C reports, unchanged; the watchdog needs none. The watchdogs, from the boards' descriptions:
- **NUCLEO-F446RE:** the IWDG on the LSI. A 100 ms timeout is counted at 47 kHz, the LSI's fastest: prescaler 4 and a reload of 1175.
- **Pico 2 W:** the watchdog on a 1 µs tick from the 12 MHz crystal (the TICKS block), resetting everything but the oscillators.

`ctest` on `cm4-nucleo-hil` and `cm33-pico2w-hil`, checked, with the UART, SPI and I2C loops as before, each run's records under `hil-runs/`:

| Board | Compiler | Passed | Cases recorded |
|---|---|---|---|
| NUCLEO-F446RE | Arm GNU Toolchain 14.2.Rel1 (GCC 14.2.1) | 39 of 39 | 251 |
| NUCLEO-F446RE | Homebrew Clang 23.1.5 with LLD 23.1.5 | 39 of 39 | 251 |
| Pico 2 W | Arm GNU Toolchain 14.2.Rel1 (GCC 14.2.1) | 40 of 40 | 253 |
| Pico 2 W | Homebrew Clang 23.1.5 with LLD 23.1.5 | 40 of 40 | 253 |

## What the runs show

- **A starved watchdog resets the board** (`test_wdt_board`, `EMB_ASSERT_RESETS`):
  - the watchdog starts with 100 ms, and a second start is refused with `EMB_ESTATE`;
  - fed every 25 ms for half a second, it does not reset the part: a reset before the case calls `emb_test_reset_expected()` would fail it;
  - starved, it resets the part. The framework's retained record carries the case across the reset (`fault checks that reset the board: 1`);
  - after the reset, `emb_drv_wdt_caused_reset()` says the watchdog caused it: the STM32's RCC_CSR IWDGRSTF, and the RP2350's REASON timer bit together with the mark `start` left in SCRATCH4.
- **What it cannot count is refused** (`test_wdt_board`): a timeout of 0, and one of a day, give `EMB_EINVAL`, and the watchdog stays stopped.
- **The class's core** (`test_wdt`): its fake watchdog's two cases, misuse among them, on the board as on the host.

## Found on the way

- **The boot ROM's reboots.** The RP2350's boot ROM reboots through the watchdog too: picotool's reboot does, and so does the reboot into BOOTSEL that ends every test image. After either, REASON's timer bit alone would have read as the watchdog's own reset. The driver now counts it only when SCRATCH4 holds the mark its `start` left, as pico-sdk's `watchdog_enable_caused_reboot()` does, and clears the mark once read.
- **One reader of the reset flags.** The STM32's RCC_CSR flags are cleared when read, and the crash record and the watchdog both want them. `emb_stm32f4_reset_flags_take()` now reads and clears them once, and returns the same value to every later caller.

## The software watchdog, FLT-004's demonstration (`b76b15f`)

The software watchdog of A121 over the board's watchdog. Every suite ran again on both boards, `cm4-nucleo-hil` and `cm33-pico2w-hil`, from a clean tree, with the same wires.

| Board | Compiler | Passed | Cases recorded |
|---|---|---|---|
| NUCLEO-F446RE | Arm GNU Toolchain 14.2.Rel1 (GCC 14.2.1) | 41 of 41 | 254 |
| NUCLEO-F446RE | Homebrew Clang 23.1.5 with LLD 23.1.5 | 41 of 41 | 254 |
| Pico 2 W | Arm GNU Toolchain 14.2.Rel1 (GCC 14.2.1) | 42 of 42 | 256 |
| Pico 2 W | Homebrew Clang 23.1.5 with LLD 23.1.5 | 42 of 42 | 256 |

- **A hung thread resets the board** (`test_wdog_board`, `EMB_ASSERT_RESETS`):
  - the service starts the board's watchdog with 400 ms and looks at its parties every 100 ms;
  - the test's thread watches a party with a 200 ms window and checks it in every 20 ms. A second thread, above it, watches another with the same window, checks it in ten times, then suspends itself for good, as a deadlocked thread would;
  - until then, the board is not reset;
  - then the service finds the hung thread's party past its window, while the other is on time. It writes the crash record and stops feeding, and the board's watchdog resets the part;
  - after the reset, the crash record holds class `EMB_FAULT_WATCHDOG`, the hung party's id (7), a thread that is not the test's, and `reset_reason` 3. `emb_drv_wdt_caused_reset()` says the watchdog caused the reset.
- **The service over a fake watchdog** (`test_wdog`), on the board as on the host: fed while both parties check in, never again once one misses, the record naming the one that missed; its refusals and misuse, the latter by reset.
