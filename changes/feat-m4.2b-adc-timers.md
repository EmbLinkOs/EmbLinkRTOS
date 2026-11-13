### Added (the counter, PWM and ADC classes' descriptions, 2026-11-13)

- The STM32F446's description gains TIM3, TIM4 and TIM5 and their channels' pins, its ADC1's inputs, temperature sensor, internal reference and DMA2 streams, and the analog function of PA0 to PA7, PB0, PB1 and PC0 to PC5. The RP2350's gains its ADC, the PWM block's twelve slices, TIMER0 and TIMER1, PLL_USB at 48 MHz for clk_adc, each line's PWM function and the ADC's inputs on GPIO26 to GPIO29 ([A129](docs/specs/AMENDMENTS-M4.md)).
- The generator takes a binding per class for a peripheral several classes can drive, keyed `compatible:class`, and a board device's `class:` picks one; it computes the STM32F4 timers' doubled clock, solves a PLL of a fixed rate, configures analog pins, and reads a timer's `counter_bits` and an ADC's `adc:` facts ([hwgen](docs/engineering/hwgen.md)).

### Added (the counter class, 2026-11-13)

- `<emb/drv_counter.h>`, behind `CONFIG_EMB_DRV_COUNTER` (SPEC-017 §11a, [A129](docs/specs/AMENDMENTS-M4.md)): a free-running counter at a known rate, widened to 64 bits; one-shot alarms on its channels at an absolute count, never early, `EMB_EPAST` for one already passed; and periodic callbacks without drift, late periods skipped. Each callback runs in the counter's interrupt.
- Drivers: the STM32's general-purpose timers (`drivers/st/stm32_tim_counter.c`; the NUCLEO-F446RE's TIM5, 32 bits, and TIM9, 16), and the RP2350's TIMER0 and TIMER1 (`drivers/rpi/rp2350_timer.c`; the Pico 2 W's TIMER1).
- The status codes of the counter and PWM classes: `EMB_EPAST` (−96) and `EMB_ENOSIGNAL` (−100).
- The NUCLEO-F446RE's description gains TIM9, two channels (`timer_channels:`), and a binding's fields read a peripheral's number, `peripheral.index` (TIMER1's tick generator).

### Added (the PWM class, 2026-11-13)

- `<emb/drv_pwm.h>`, behind `CONFIG_EMB_DRV_PWM` (SPEC-017 §11b, [A129](docs/specs/AMENDMENTS-M4.md)): a device's output channels set by period and pulse in nanoseconds, rounded to the nearest the hardware makes and reported back, sharing the device's period, inverted on request; and an input's period and pulse captured, `EMB_ENOSIGNAL` when no whole cycle comes.
- Drivers: the STM32's general-purpose timers (`drivers/st/stm32_tim_pwm.c`: PWM mode 1, one cycle captured exactly in PWM input mode; the NUCLEO-F446RE's TIM3 out on D12 and TIM4 in on D10) and the RP2350's slices (`drivers/rpi/rp2350_pwm.c`: A and B out, B averaged over a gate; the Pico 2 W's slice 7 out on GPIO14 and slice 0 in on GPIO17).
- `test_pwm_loop`, an output wired to a capture input, configured by `EMB_TEST_PWM_LOOP`.

### Added (the ADC class, and circular DMA, 2026-11-13)

- `<emb/drv_adc.h>`, behind `CONFIG_EMB_DRV_ADC` (SPEC-017 §11c, [A129](docs/specs/AMENDMENTS-M4.md)): one conversion, waiting or polled from any context; a count's millivolts against the board's reference; the internal temperature sensor; and a sequence of inputs sampled at a rate into a buffer by DMA, a callback at each half, `EMB_EOVERRUN` when the hardware loses samples.
- Drivers: the STM32F4's ADC1 (`drivers/st/stm32_adc.c`: paced by TIM2's TRGO, moved by DMA2, its temperature from the factory calibration, set up by each request so the reference controller keeps driving it by hand) and the RP2350's ADC (`drivers/rpi/rp2350_adc.c`: on clk_adc, 48 MHz from PLL_USB, a round robin drained through its FIFO by DMA).
- The DMA class's circular jobs (`EMB_DRV_DMA_CIRCULAR`, SPEC-017 §7): the STM32's circular mode, and on the RP2350 one channel that triggers itself again over its address ring.
- The board's devices: the NUCLEO-F446RE's ADC1 on A2 and A3 and DMA2; the Pico 2 W's ADC on GPIO26 and GPIO27. `test_adc_board`, configured by `EMB_TEST_ADC_LOOP`.
- EmbSIM runs the PWM loop (D12 to D10) and the ADC's inputs (A2 to 3V3, A3 to GND) on the NUCLEO-F446RE in CI: `simulation.loopbacks` takes a PWM pair and an ADC's rails.
- The benchmarks read the board's `hw.json` from its `docs/`, where A126 put it.

### Changed (applications in MPLAB Harmony's form, 2026-11-13)

- An application's generated files follow MPLAB Harmony v3 ([A130](docs/specs/AMENDMENTS-M4.md)). Each board's are in Harmony's configuration folder, `src/config/<board>/`: `definitions.h`, `configuration.h`, `initialization.c` with `SYS_Initialize()`, `tasks.c` with the tasks and `SYS_Tasks()`, `interrupts.c`, and the board's own outputs under `peripheral/`, `linker/`, `build/` and `docs/`. `system.h`, `system.c` and `tasks.h` are gone.
- `src/main.c` calls `SYS_Initialize(NULL)` and then `SYS_Tasks()`, and each task is `src/<task>.c` and `.h` in Harmony's template, with its banner, sections and function blocks, and Harmony's names: `DOOR_STATES`, `DOOR_EVENTS`, `DOOR_DATA doorData`, `DOOR_Initialize()`, `DOOR_OnOpen()`. A periodic task is Harmony's `BLINK_Tasks()` switch, run every period without drift; an event task keeps a handler per state. A task's `data:` is gone: its data is its `<TASK>_DATA`.
- A system's `interrupts:` lists the application's own interrupt handlers, by line number, by `<peripheral>.<interrupt>` or by board, each checked against the SoC, the devices' lines and the port's levels. `examples/tasks` raises one, a spare line on each SoC, and its handler opens the door.
- `examples/tasks` and `examples/controller` are regenerated; the tasks example ran on the NUCLEO-F446RE and the Pico 2 W, the door opening by the interrupt every 4 s and closing itself 2 s later. The naming check is relaxed for applications' files alone (`examples/.clang-tidy`).
