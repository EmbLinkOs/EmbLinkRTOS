# SPEC-017 - Driver Classes: UART, SPI, I2C, DMA, Watchdog, Flash, RTC, Entropy

**Implementation and release scope:** [Project status](../STATUS.md) records the implemented subset; [roadmap](../architecture/07-roadmap.md) defines release gates. Nothing in this specification is implemented yet; M4.2 implements it, class by class.

**Status:** Accepted 2026-11-09 by the project owner at the M4.0 review, as written, with the defaults of §13.
**Requirements:** `docs/requirements/DRV.md` (DRV-004 to 010), FLT-004, SEC-012.
**Builds on:** SPEC-016 (devices, requests, completion, binding), 04 §3.2 (the contract checklist) and §3.3 (the class catalog), SPEC-001 §2 and §4 (names, status codes), ADR-057 (the STM32F4 and the RP2350), ADR-058 (flash for MCUboot), A109 (DMA and caches).

---

## 1. Scope

The eight classes M4 delivers, on the STM32F4 and the RP2350 (ADR-057), and the GPIO class the SPI class's chip selects need (§5a, A116). Pin control, clock and reset, and timers stay the boards' existing functions in M4, wrapped as EARLY devices (SPEC-016 §4); their classes are specified when a second driver needs them. A class is claimed for a board only when its section here answers every checklist item and an archived run on that board passes its tests (DRV-010).

## 2. The common template

Every class has, in `include/emb/drv/<class>.h`:

| Part | Form |
|---|---|
| Request | `emb_drv_<class>_req_t`, beginning with `emb_drv_request_t` (SPEC-016 §5) |
| Submit | `emb_drv_<class>_submit(dev, req)`, from a thread or an interrupt |
| Blocking | `emb_drv_<class>_<op>(dev, ..., timeout)`, the wrapper of SPEC-016 §5.2, threads only |
| Polled | `emb_drv_<class>_poll_<op>(dev, ...)`, only for the classes SPEC-016 §7 names |
| Cancel | `emb_drv_cancel(dev, req)` (SPEC-016 §5.3) |
| Capabilities | `EMB_DRV_<CLASS>_CAP_*` bits from `emb_device_caps(dev)` |
| Status codes | `EMB_E<word>` in the class's sub-range (§3) |

**Emulators.** EmbSIM (as of `e4616fa`, 2026-11-09) models the STM32F446's USART, SPI, I2C, DMA, IWDG, window watchdog, RTC and flash controller, and the BME280 and DS3231 parts; "EmbSIM" below means its NUCLEO-F446RE. It models the RP2040, not the RP2350, so the RP2350's drivers have the board as their evidence, except the PL022, which QEMU's MPS2 machines also model. CI pins EmbSIM per job, and the NUCLEO job's pin (`2529ab8`) predates these models: the first class to use them moves it (M4.2).

Each section below answers the checklist of 04 §3.2 in the same order: dependencies, requests, blocking wrappers, interrupt behaviour, DMA, concurrency, timeouts and cancellation, power, errors, capabilities, isolation. The last is the same for every class in M4: drivers run in the kernel's partition; placing a driver in its own partition waits for M3b.

## 3. Status code sub-ranges (DRV-008)

The driver range is −64 to −127 (API-010). M4's classes take −64 to −95, four codes each; −96 to −127 stay for later classes and subsystems.

| Class | Range | Codes |
|---|---|---|
| UART | −64 to −67 | `EMB_EFRAMING` −64, `EMB_EPARITY` −65, `EMB_EOVERRUN` −66 |
| SPI | −68 to −71 | `EMB_ECRC` −68, `EMB_EMODEFAULT` −69 |
| I2C | −72 to −75 | `EMB_ENACK` −72, `EMB_EARBLOST` −73, `EMB_EBUSERR` −74 |
| DMA | −76 to −79 | `EMB_EXFER` −76 |
| Watchdog | −80 to −83 | `EMB_EWINDOW` −80 |
| Flash | −84 to −87 | `EMB_EWRPROT` −84, `EMB_EVERIFY` −85 |
| RTC | −88 to −91 | `EMB_ENOTSET` −88, `EMB_EOSCFAIL` −89 |
| Entropy | −92 to −95 | `EMB_EHEALTH` −92 |

The common codes of SPEC-016 §10 (`EMB_ESTATE`, `EMB_EIO`, `EMB_ETIMEDOUT`, `EMB_ECANCELED`, `EMB_EBUSY`, `EMB_ENOTSUP`, `EMB_EINVAL`) serve every class.

## 4. UART

| Checklist | |
|---|---|
| Dependencies | its clock, its pins; its DMA controller when configured |
| Requests | `write` (bytes out), `read` (bytes in, up to a length, ending early on line idle when asked), `break` |
| Blocking | `emb_drv_uart_write()`, `emb_drv_uart_read()` with a timeout |
| Interrupts | transmit by interrupt or DMA; receive into a ring the driver owns, sized by the board's configuration, so bytes arriving with no read pending are kept; line idle ends a read early when the request asks for it |
| DMA | optional, per direction, from the device's `dma_requests` |
| Concurrency | one write and one read in progress at once (full duplex); further requests queue |
| Timeouts | a canceled write stops after the byte in progress; a canceled read returns what it received |
| Power | suspend waits for the transmitter to empty, then gates the clock; bytes received while suspended are lost unless the board keeps the receiver awake |
| Errors | `EMB_EFRAMING`, `EMB_EPARITY`, `EMB_EOVERRUN` (the ring was full: the count of lost bytes is kept) |
| Capabilities | `DMA`, `IDLE_DETECT`, `FLOW_CONTROL` |
| Polled | `emb_drv_uart_poll_write()`, `poll_read()`: the console before the kernel and the fault report |

**Drivers:** `drivers/st/stm32_usart.c` (from today's polled functions); `drivers/arm/pl011.c` (the RP2350's UARTs). **Evidence:** EmbSIM's NUCLEO and native fakes in CI; on both boards, the console and a binary echo through the bench's two-way relay.

## 5. SPI (controller)

| Checklist | |
|---|---|
| Dependencies | its clock, its pins; the chip-select line of each target (§5a), a board's set up deasserted at boot; its DMA controller when configured |
| Requests | a transfer: transmit buffer, receive buffer (either may be absent; none to send sends ones), length, the target's mode (CPOL, CPHA), speed (a ceiling: the fastest divider not above it, `EMB_ENOTSUP` below the slowest), word size and chip select |
| Blocking | `emb_drv_spi_transfer()` |
| Interrupts | completion by interrupt, or by DMA for transfers above a threshold the driver states |
| DMA | optional, both directions together |
| Concurrency | one transfer at a time per controller. A target's chip select is asserted once the controller is set up for it and released after its last frame. `emb_drv_spi_acquire()` and `emb_drv_spi_release()` hold the bus for one target, its chip select asserted across several transfers, with a recursive mutex with priority inheritance (SPEC-016 §6): other threads' transfers wait within their timeouts, and a transfer for another target, from any context, is refused with `EMB_EBUSY` |
| Timeouts | a canceled transfer sends nothing more; the frames in flight come back whole and the driver's interrupt completes it, so the controller is never turned off mid-frame; chip select is released; `done` counts whole frames. A transfer's timeout covers its wait for the bus and the transfer together |
| Power | suspend waits for the transfer in progress |
| Errors | `EMB_ECRC` (hardware CRC, where enabled), `EMB_EMODEFAULT` |
| Capabilities | `DMA`, `CRC`, `LOOPBACK` |

**Drivers:** `drivers/st/stm32_spi.c` on SPI2 (PB13, PB14, PB15; PA5 is the NUCLEO's LED); `drivers/arm/pl022.c`, the RP2350's SPI. **Evidence:** the same `pl022.c` runs under QEMU's MPS2 machines, which model five PL022 controllers, in loopback, in CI; EmbSIM; on both boards, a MOSI-to-MISO jumper.

## 5a. GPIO (A116)

| Checklist | |
|---|---|
| Dependencies | its controller's clock gate (the STM32F4's ports) or reset (the RP2350's bank 0); the board's pin table sets up the lines it names |
| Requests | none: each operation acts at once. `emb_drv_gpio_configure()` sets a line up as an input or output, with a pull, open drain, and an output's first level written before it drives; `emb_drv_gpio_write()` drives it; `emb_drv_gpio_read()` reads the level at the pin, an output's included. A named line (`emb_drv_gpio_line_t`: controller, number, sense) is asserted or released with `emb_drv_gpio_assert()` |
| Blocking | none |
| Interrupts | none; edge interrupts are a later addition |
| DMA | none |
| Concurrency | callable from any context. `write()` and `read()` are single accesses to the controller's set, clear and input registers; `configure()` runs inside the class's critical section, since a controller's mode registers hold all its lines |
| Timeouts | none |
| Power | the lines keep their state across the controller's suspend |
| Errors | `EMB_EINVAL` (a line the controller does not have); `EMB_ENOTSUP` (open drain or a pull-down the controller lacks) |
| Capabilities | `OPEN_DRAIN`, `PULL_DOWN` |

**Drivers:** `drivers/st/stm32_devices.c` on the STM32F4's GPIO ports, 16 lines each, with open drain and both pulls; `drivers/rpi/rp2350_devices.c` on the RP2350's bank 0 through the SIO, its line count from the package description (30 on the RP2350A), with both pulls, and open drain by the output enable, since its pads have none. **Evidence:** the class's fake on the host; on both boards, the SPI loop test reads its chip select back at the pin.

## 6. I2C (controller and target)

| Checklist | |
|---|---|
| Dependencies | its clock, its pins (open drain, with pull-ups on the board) |
| Requests | a transaction, on lane 0: a target's address (7 bits, 0x08 to 0x77) and speed, and a list of write and read segments, joined by repeated starts and ended by a stop; `done` counts the bytes moved, acknowledged writes and reads. As a target, on lane 1 (A114): a listen answers the next transaction to its own address, what the controller writes stored up to its buffer and what it reads taken from another, then 0xFF; while no listen waits the address is not acknowledged |
| Blocking | `emb_drv_i2c_transfer()`, `emb_drv_i2c_write_read()` for the common register read, and `emb_drv_i2c_listen()` |
| Interrupts | each byte by interrupt; DMA is not used in M4 |
| DMA | none in M4 |
| Concurrency | one transaction at a time, beside one listen. `emb_drv_i2c_acquire()` and `emb_drv_i2c_release()` hold the bus for one target across transactions, on the held bus the SPI class shares (`<emb/drv_bus.h>`): other threads' transactions wait within their timeouts, and one to another target, from anywhere, is refused with `EMB_EBUSY` |
| Timeouts | a canceled or timed-out transaction ends with a stop condition. A bus a target holds low is recovered by `emb_drv_i2c_recover()`: with the bus to itself, the class takes SCL and SDA as open-drain GPIO lines (a binding's `pins.scl` and `pins.sda`, A118), clocks SCL at about 100 kHz until SDA is released, nine times at most, ends with a stop, gives the lines back to the controller (`EMB_DRV_GPIO_FUNCTION`) and resets it (`emb_drv_i2c_api_t.reset`) |
| Power | suspend waits for the transaction in progress |
| Errors | `EMB_ENACK` (address or data not acknowledged), `EMB_EARBLOST`, `EMB_EBUSERR` |
| Capabilities | `TARGET`, `FAST_MODE` (400 kHz), `FAST_MODE_PLUS`, `TARGET_NACK` (A117: a target refuses bytes beyond its listen's buffer; one without acknowledges and drops them) |

**Drivers:** `drivers/st/stm32_i2c.c`, controller and target at once, standard and fast mode; `drivers/snps/dw_i2c.c`, Synopsys DW_apb_i2c (the RP2350's I2C0 and I2C1), up to fast mode plus, a controller or a target at a time: a transaction while a listen waits runs in controller mode, and the address is not acknowledged meanwhile. The lines are open drain, with the chip's own pull-ups where the board has no resistors (the pin table's I2C entries). **Evidence:** EmbSIM's NUCLEO-F446RE with I2C1 wired to I2C2 in CI; on each board, two of the chip's own controllers wired to each other, one as controller and one as target, at 100 kHz and 400 kHz. To come: EmbSIM's BME280 and DS3231 models, and the edges on the analyzer.

## 7. DMA

| Checklist | |
|---|---|
| Dependencies | its clock |
| Requests | not used by applications directly. A device's channels are given at generation (A119): its binding's `dma.<request>` fields, `emb_drv_dma_chan_t`. Its driver starts a job on one, `emb_drv_dma_start()`: memory to peripheral, peripheral to memory or memory to memory, of 1, 2 or 4-byte items, the memory side advancing or fixed (a fill, a sink). Channels run at once, each its own, so a job does not go through the device's request queue |
| Blocking | none; the client driver's own request is what blocks |
| Interrupts | a job's end, its status and the items moved, delivered to the client driver's callback in the DMA interrupt |
| DMA ownership | while a transfer runs, its buffer belongs to the hardware: the client must not touch it until completion. Buffers must be in a region the description marks `dma` (SPEC-014 §3); the driver refuses others with `EMB_EINVAL`. No cache maintenance on the M4F and M33 (A109) |
| Concurrency | channels are assigned at generation from `dma_requests`; two devices never share a channel. Starting a running channel is refused with `EMB_EBUSY` |
| Timeouts | `emb_drv_dma_stop()` stops the channel and gives the items moved, the partial data in memory up to it (DRV-006); no end is delivered after it returns. A client's own cancel stops its transmit job, lets the data already sent arrive, then stops its receive job: an SPI transfer canceled by DMA ends after whole frames |
| Power | suspend refuses while a channel runs |
| Errors | `EMB_EXFER` (a bus error during the transfer) |
| Capabilities | `MEM_TO_MEM`, `CIRCULAR` |

**Drivers:** `drivers/st/stm32_dma.c` (DMA1 and DMA2, a job per stream, direct mode for a peripheral and the FIFO for memory to memory, which only DMA2 does); `drivers/rpi/rp2350_dma.c` (16 channels, DREQ pacing, what moved counted from the advancing address, which an abort leaves as it was). **Clients:** the SPI drivers run transfers of 16 frames and more by DMA where the board gives the device channels and the buffers are where DMA reaches, and by interrupt otherwise; the UART drivers do the same for writes of 16 bytes and more, their reads staying with the interrupt and the class's ring. **Evidence:** the class's fake on the host; EmbSIM's NUCLEO-F446RE, SPI2 over DMA1 in CI; on both boards, SPI over DMA through the loop wire, with a cancel in the middle of a transfer; UART writes by DMA, the NUCLEO-F446RE's console's and the Pico 2 W's loop UART's, one canceled part-way.

## 8. Watchdog

| Checklist | |
|---|---|
| Dependencies | its own oscillator (the STM32's LSI); on the RP2350, its microsecond tick from the crystal (the TICKS block) |
| Requests | `emb_drv_wdt_start()` once with a timeout, `emb_drv_wdt_feed()`, `emb_drv_wdt_caused_reset()` (A120); the software watchdog of FLT-004 is its only feeder in a system that uses it |
| Blocking | none: every operation is immediate |
| Interrupts | none (the STM32's IWDG has none; the window watchdog is not used) |
| DMA | none |
| Concurrency | `feed` is safe from any context; a second `start` is refused with `EMB_ESTATE` |
| Timeouts | the timeout is a floor, counted at the oscillator's fastest (the STM32's LSI at 47 kHz, its datasheet's upper bound), so the part is never reset early; a slow LSI lets it run up to some 2.8 times longer. One of 0, or past what the watchdog counts (some 22 s on the STM32's IWDG, 16.7 s on the RP2350's), is refused with `EMB_EINVAL`, and the watchdog is not started. The class has no stop: the STM32's IWDG cannot be stopped once started, and the class does not stop the RP2350's, which could be, so both behave alike. It counts under a debugger too |
| Power | it keeps running in the low-power states that keep its oscillator; the power policy feeds it before sleeping longer than its timeout, or refuses the state (SPEC-018) |
| Errors | `EMB_EWINDOW` (fed too early, where a window is configured): reserved, no M4 driver has a window |
| Capabilities | `WINDOW`: none in M4 |
| Reset reason | the driver reads at its initialization whether it caused the last reset, and the answer stays the boot's: the STM32's RCC_CSR IWDGRSTF, its flags read once for every reader, the crash record's too (`emb_stm32f4_reset_flags_take()`); the RP2350's REASON timer bit, counted only when SCRATCH4 holds the mark `start` left, since the boot ROM's own reboots go through the watchdog too. FLT-004's service records which party missed its window, in retained memory |

**Drivers:** `drivers/st/stm32_iwdg.c`, the smallest prescaler that counts the timeout; `drivers/rpi/rp2350_watchdog.c`, resetting everything but the oscillators. A board's watchdog is named `EMB_SOC_WATCHDOG_DEVICE`. **Evidence:** the class's fake on the host; EmbSIM's NUCLEO-F446RE in CI, whose IWDG resets the board; on both boards, the watchdog fed for half a second and then starved resets the part, and after the reset it says it did (`EMB_ASSERT_RESETS`, A120). QEMU's MPS2 boards are not used: their CMSDK watchdog raises an NMI before it resets, and they have no reset-cause register for `caused_reset`.

## 9. Flash

| Checklist | |
|---|---|
| Dependencies | its controller's clock |
| Requests | `read`, `program` (a multiple of the write unit, at an aligned address), `erase` (whole sectors), within a partition the description names (SPEC-014 §4) |
| Blocking | `emb_drv_flash_read()`, `program()`, `erase()` |
| Polled | `emb_drv_flash_poll_*()`: MCUboot's flash map (ADR-058) and early boot |
| Interrupts | the end of an erase or a program by interrupt on the STM32; the RP2350's boot ROM routines run polled, from RAM |
| DMA | none |
| Concurrency | one operation at a time per device. On the STM32F446, which has one flash bank, the CPU stalls on any fetch or read from flash until the operation ends, so code that must keep running (a time-critical interrupt handler) is placed in RAM; on the RP2350, execution from flash (XIP) is off during the operation, so the driver and whatever runs meanwhile must be in RAM, with interrupts held off or their handlers in RAM |
| Timeouts | an erase or program in progress cannot be stopped; cancel waits for it, then removes the queued rest |
| Power | suspend waits for the operation in progress |
| Errors | `EMB_EWRPROT`, `EMB_EVERIFY` (the read-back differs), `EMB_EINVAL` (unaligned or outside the partition) |
| Capabilities | the geometry: sector sizes and offsets, write unit, erased value, all from the description |

**Partitions:** `include/emb/flash_area.h`, generated from the description's `flash_partitions`, gives each partition's device, offset and size; MCUboot's flash map is built on it.

**Drivers:** `drivers/st/stm32f4_flash.c` (sector map from the description: 4 × 16 KiB, 64 KiB, 3 × 128 KiB on the F446RE); `drivers/rpi/rp2350_flash.c` (4 KiB sectors, 256-byte pages, through the boot ROM). **Evidence:** a native flash simulator in CI, with power loss at any operation and torn writes (BOOT-007); EmbSIM; on both boards, a write, erase and read stress, and the measured erase time.

## 10. RTC and wall clock

| Checklist | |
|---|---|
| Dependencies | its oscillator (the F446RE's LSE crystal at 32.768 kHz); the backup domain |
| Requests | `get` and `set` time, `alarm` at a time, `wakeup` after an interval (the power policy's wake source, SPEC-018) |
| Blocking | `get` and `set` are immediate; an alarm completes later |
| Interrupts | alarm and wake-up, through EXTI on the STM32 |
| DMA | none |
| Concurrency | one alarm per device in M4 |
| Timeouts | a canceled alarm is disarmed |
| Power | it keeps running in every state, OFF included where the backup domain is powered |
| Errors | `EMB_ENOTSET` (the time was lost with power, and not set since), `EMB_EOSCFAIL` (the oscillator failed to start) |
| Capabilities | `CALENDAR`, `SUBSECONDS`, `BACKUP_REGISTERS` |
| Wall clock | at boot, a set RTC seeds `emb_wallclock_set()` (KRN-TIM-031); kernel time and deadlines never depend on it (KRN-TIM-002) |

**Drivers:** `drivers/st/stm32f4_rtc.c` (calendar, wake-up timer, alarms, 20 backup registers); `drivers/rpi/rp2350_aon.c` (the always-on timer: a counter, no calendar, so `CALENDAR` is absent and the time is kept as seconds since an epoch the application sets). **Evidence:** EmbSIM's RTC in CI; on both boards, the clock's drift over the soak, against the host's.

## 11. Entropy

| Checklist | |
|---|---|
| Dependencies | its clock |
| Requests | `read` a number of bytes |
| Blocking | `emb_drv_entropy_read()` |
| Interrupts | data ready, on the RP2350's TRNG |
| DMA | none |
| Concurrency | one read at a time |
| Timeouts | a canceled read returns the bytes it had |
| Power | suspend stops the source; a read after resume waits for the source's start-up |
| Errors | `EMB_EHEALTH`: the source's health test failed, and no byte from that block is returned |
| Capabilities | `HEALTH_TEST` |
| Absent | a board without a source has no entropy device; capability discovery says so and the class returns `EMB_ENOTSUP`. Nothing stands in for it (SEC-012). The STM32F446 has none |

**Drivers:** `drivers/rpi/rp2350_trng.c`. **Evidence:** a native fake in CI; on the Pico 2 W, 1 MiB archived with the host's statistical sanity checks, which are a check of the driver, not a certification of the source.

## 12. Optional parts

Drivers for external parts on these classes, written once the class is in: `drivers/sensor/bme280.c` on I2C (EmbSIM models it) and `drivers/flash/jedec_spi_nor.c` on SPI. A part is a device with a `bus:` (SPEC-014 §4) and its own `compatible`.

## 13. Defaults to confirm at review

- The status sub-ranges of §3, four codes per class, and −96 to −127 kept for later classes.
- No DMA for I2C in M4.
- The UART receive ring sized by configuration, keeping bytes that arrive with no read pending.
- On the RP2350, flash operations run with execution from flash off; the driver states which code must be in RAM.
- The RP2350's always-on timer as its RTC device, without a calendar.
