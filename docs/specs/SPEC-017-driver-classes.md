# SPEC-017 - Driver Classes: UART, SPI, I2C, DMA, Watchdog, Flash, RTC, Entropy

**Implementation and release scope:** [Project status](../STATUS.md) records the implemented subset; [roadmap](../architecture/07-roadmap.md) defines release gates. Nothing in this specification is implemented yet; M4.2 implements it, class by class.

**Status:** Proposed 2026-11-09 for the project owner's review (M4.0).
**Requirements:** `docs/requirements/DRV.md` (DRV-004 to 010), FLT-004, SEC-012.
**Builds on:** SPEC-016 (devices, requests, completion, binding), 04 §3.2 (the contract checklist) and §3.3 (the class catalog), SPEC-001 §2 and §4 (names, status codes), ADR-057 (the STM32F4 and the RP2350), ADR-058 (flash for MCUboot), A109 (DMA and caches).

---

## 1. Scope

The eight classes M4 delivers, on the STM32F4 and the RP2350 (ADR-057). GPIO and pin control, clock and reset, and timers stay the boards' existing functions in M4, wrapped as EARLY devices (SPEC-016 §4); their classes are specified when a second driver needs them. A class is claimed for a board only when its section here answers every checklist item and an archived run on that board passes its tests (DRV-010).

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
| Dependencies | its clock, its pins; the chip-select GPIO of each target device; its DMA controller when configured |
| Requests | a transfer: transmit buffer, receive buffer (either may be absent), length, the target's mode (CPOL, CPHA), speed and word size |
| Blocking | `emb_drv_spi_transfer()` |
| Interrupts | completion by interrupt, or by DMA for transfers above a threshold the driver states |
| DMA | optional, both directions together |
| Concurrency | one transfer at a time per controller; a target device acquires the bus (SPEC-016 §6) to keep its chip select across several transfers |
| Timeouts | a canceled transfer stops after the frame in progress; chip select is released; `done` counts whole frames |
| Power | suspend waits for the transfer in progress |
| Errors | `EMB_ECRC` (hardware CRC, where enabled), `EMB_EMODEFAULT` |
| Capabilities | `DMA`, `CRC`, `LOOPBACK` |

**Drivers:** `drivers/st/stm32_spi.c` on SPI2 (PB13, PB14, PB15; PA5 is the NUCLEO's LED); `drivers/arm/pl022.c`, the RP2350's SPI. **Evidence:** the same `pl022.c` runs under QEMU's MPS2 machines, which model five PL022 controllers, in loopback, in CI; EmbSIM; on both boards, a MOSI-to-MISO jumper.

## 6. I2C (controller and target)

| Checklist | |
|---|---|
| Dependencies | its clock, its pins (open drain, with pull-ups on the board) |
| Requests | a transaction: an address and a list of write and read segments, joined by repeated starts; as a target, a read or write the controller makes to its address |
| Blocking | `emb_drv_i2c_transfer()`, and `write_read()` for the common register read |
| Interrupts | each byte by interrupt; DMA is not used in M4 |
| DMA | none in M4 |
| Concurrency | one transaction at a time; a target device acquires the bus for a sequence of transactions |
| Timeouts | a canceled or timed-out transaction ends with a stop condition; a bus held low is recovered by clocking out nine pulses and a stop |
| Power | suspend waits for the transaction in progress |
| Errors | `EMB_ENACK` (address or data not acknowledged), `EMB_EARBLOST`, `EMB_EBUSERR` |
| Capabilities | `TARGET`, `FAST_MODE` (400 kHz), `FAST_MODE_PLUS` |

**Drivers:** `drivers/st/stm32f4_i2c.c`; `drivers/rpi/dw_i2c.c` (the RP2350's DesignWare controllers). **Evidence:** EmbSIM with its BME280 and DS3231 models in CI; on each board, two of the chip's own controllers wired to each other, one as controller and one as target, at 100 kHz, edges checked on the analyzer.

## 7. DMA

| Checklist | |
|---|---|
| Dependencies | its clock |
| Requests | not used by applications directly: a driver allocates a channel (stream) for its device's request line, then submits memory-to-peripheral, peripheral-to-memory or memory-to-memory transfers |
| Blocking | none; the client driver's own request is what blocks |
| Interrupts | transfer complete and error, delivered to the client driver |
| DMA ownership | while a transfer runs, its buffer belongs to the hardware: the client must not touch it until completion. Buffers must be in a region the description marks `dma` (SPEC-014 §3); the driver refuses others with `EMB_EINVAL`. No cache maintenance on the M4F and M33 (A109) |
| Concurrency | channels are assigned at generation from `dma_requests`; two devices never share a channel |
| Timeouts | cancel stops the channel; `done` is the count the hardware reports; the partial data is in memory up to it (DRV-006) |
| Power | suspend refuses while a channel runs |
| Errors | `EMB_EXFER` (a bus error during the transfer) |
| Capabilities | `MEM_TO_MEM`, `CIRCULAR` |

**Drivers:** `drivers/st/stm32f4_dma.c` (DMA1 and DMA2, streams and channel selection); `drivers/rpi/rp2350_dma.c` (16 channels, DREQ pacing). **Evidence:** EmbSIM's DMA in CI; on both boards, SPI and UART transfers over DMA, and cancellation in the middle of one.

## 8. Watchdog

| Checklist | |
|---|---|
| Dependencies | its own oscillator (the STM32's LSI); none on the RP2350 beyond its tick |
| Requests | `start` with a timeout, `feed`; the software watchdog of FLT-004 is its only feeder in a system that uses it |
| Blocking | none: every operation is immediate |
| Interrupts | none (the STM32's IWDG has none; the window watchdog is not used) |
| DMA | none |
| Concurrency | `feed` is safe from any context |
| Timeouts | n/a. The class has no stop: the STM32's IWDG cannot be stopped once started, and the class does not stop the RP2350's, which could be, so both behave alike |
| Power | it keeps running in the low-power states that keep its oscillator; the power policy feeds it before sleeping longer than its timeout, or refuses the state (SPEC-018) |
| Errors | `EMB_EWINDOW` (fed too early, where a window is configured) |
| Capabilities | `WINDOW` |
| Reset reason | the board reads, after a reset, whether the watchdog caused it, and FLT-004's service records which party missed its window, in retained memory |

**Drivers:** `drivers/st/stm32f4_iwdg.c` (today's); `drivers/rpi/rp2350_watchdog.c`. **Evidence:** QEMU's CMSDK watchdog and EmbSIM's IWDG in CI; on both boards, a hung thread resets the part and the reason is reported after the reset.

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
