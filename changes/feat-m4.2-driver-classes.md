### Changed (request lanes, 2026-11-09)

- A114, proposed: a device has two request lanes, each its own first-in-first-out queue whose head starts whatever the other lane does, so a full-duplex device writes while it reads. A request names its lane (`emb_drv_request_t.lane`, 0 by default); a device that needs one lane behaves as before ([amendments](docs/specs/AMENDMENTS-M4.md)).

### Added (the driver classes' status codes, 2026-11-09)

- The status codes of SPEC-017 §3, four per class in −64 to −95: `EMB_EFRAMING`, `EMB_EPARITY`, `EMB_EOVERRUN` (UART), `EMB_ECRC`, `EMB_EMODEFAULT` (SPI), `EMB_ENACK`, `EMB_EARBLOST`, `EMB_EBUSERR` (I2C), `EMB_EXFER` (DMA), `EMB_EWINDOW` (watchdog), `EMB_EWRPROT`, `EMB_EVERIFY` (flash), `EMB_ENOTSET`, `EMB_EOSCFAIL` (RTC) and `EMB_EHEALTH` (entropy), each named by `emb_status_name()` (DRV-008).

### Added (the UART class, 2026-11-09)

- `<emb/drv_uart.h>`, behind `CONFIG_EMB_DRV_UART` (SPEC-017 §4): write and read requests that run at once, a write on one lane and a read on the other; `emb_drv_uart_write()` and `emb_drv_uart_read()`, blocking with a timeout, a read ending early when the line goes idle if it asks; `emb_drv_uart_poll_write()` and `emb_drv_uart_poll_read()` for the console before the kernel and the fault report. Drivers share its receive core: a ring that keeps the bytes no read is waiting for, the read in progress, damaged bytes ending a read with `EMB_EFRAMING` or `EMB_EPARITY`, and bytes lost to a full ring reported by the next read as `EMB_EOVERRUN`.
- `emb_device_caps()` returns a device's capability bits, from its driver's operations (SPEC-016 §11).
- The UART class's drivers: the STM32 USART (`drivers/st/stm32_uart.c`), the PL011 (`drivers/arm/pl011.c`) and the MPS2's CMSDK UART (`drivers/arm/cmsdk_uart.c`) write by interrupt, receive into the class's ring, and detect the idle line (the STM32's IDLE flag, the PL011's receive timeout). A driver connects its interrupt at initialization and turns it on with the first request, so an application that drives the UART itself, as `examples/controller` does, keeps the line. The boards' consoles are these devices, and a new test drives each console by request.
- A binding's `buffers` give a device RAM the description sizes, a UART's receive ring among them (`rx_buffer`, 64 bytes by default). `soc_devices.h` names the board's devices (`EMB_SOC_HAS_<DEVICE>`) and its console device (`EMB_SOC_CONSOLE_DEVICE`).

### Changed (a driver's start, 2026-11-09)

- A115, proposed: a driver's `start()` returns `EMB_EBUSY` while its request runs on, or the request's final status when it finished there; the core starts a lane's requests in a loop, so completion never recurses and the stack stays bounded however long the queue.

### Fixed (the Pico 2 W's receive line, 2026-11-09)

- The RP2350's UART receive pins are pulled up. Its pads come out of reset pulled down, so a receive line left open read as a continuous break, and every read failed with a framing error.
- `test_uart_loop` checks a UART receiving on real hardware, its transmit pin wired to its receive pin: a full-duplex echo, the ring keeping bytes no read waits for, the idle line ending a read, a kilobyte intact and an overrun reported. It builds where the bench has the wire (`EMB_TEST_UART_LOOP=<device>`); the NUCLEO-F446RE gains `uart4` (A0 to A1) and the Pico 2 W `uart1` (GPIO4 to GPIO5) for it.
- EmbSIM runs the loopback test with the wire too: a board's `simulation.loopbacks` names the UART and the ports to join, and `tools/embsim/run_embsim.py --net` runs the image inside a project with that wire, so CI receives through the STM32 USART driver on every pull request.
- `emb_drv_uart_req_break()` prepares a break on the write lane, at least two character times long. The STM32 USART sends a break frame with its SBK bit; the PL011, whose BRK holds the line until cleared, times its break with a software timer that runs in its expiry interrupt, so nothing waits; the CMSDK UART has none and completes the request with `EMB_ENOTSUP`. On a loopback a break arrives as a framing error, which ends the read waiting for it.

### Added (the SPI class, 2026-11-09)

- `<emb/drv_spi.h>`, behind `CONFIG_EMB_DRV_SPI` (SPEC-017 §5): a transfer to a target in its mode (0 to 3), at a clock it does not exceed and with 8-bit frames; no transmit buffer sends ones, no receive buffer drops what comes in. `emb_drv_spi_submit()` queues one; `emb_drv_spi_transfer()` waits with a timeout and stops the transfer when it runs out. A rate below the controller's slowest, a frame size it does not do, or a capability it lacks completes with `EMB_ENOTSUP`; another controller driving the bus, with `EMB_EMODEFAULT`.
- The SPI class's drivers: the PL022 (`drivers/arm/pl022.c`), on the RP2350's `spi0` and `spi1` and on QEMU's MPS2 boards, keeps up to its FIFO's eight frames in flight; the STM32 SPI (`drivers/st/stm32_spi.c`), on the F446's `spi2` (PB13, PB14, PB15), one frame at a time. Both interrupt-driven, both choosing the fastest divider not above the target's clock.
- `test_spi_loop` checks a controller with its MOSI looped to its MISO: every mode, a slow and a fast clock, a clock too slow refused, ones sent with no transmit buffer, 256 frames intact. It runs on QEMU through the PL022's own loopback mode (`EMB_DRV_SPI_LOOPBACK`), on EmbSIM's NUCLEO-F446RE through a wire from CN10-26 to CN10-28, and on the bench where the wire is fitted (`EMB_TEST_SPI_LOOP=<device>`: `spi2` on the NUCLEO-F446RE, `spi1` on the Pico 2 W, GPIO11 to GPIO12).
- A board's `simulation.loopbacks` entry names a UART or an SPI device and either the ports EmbSIM joins (`ports`) or the controller's own loopback (`internal: true`), which the MPS2 boards use for `spi0`.

### Added (the GPIO class and chip selects, 2026-11-09)

- A116, proposed: `<emb/drv_gpio.h>`, behind `CONFIG_EMB_DRV_GPIO` (SPEC-017 §5a). `emb_drv_gpio_configure()` sets a line up as an input or an output, with a pull, open drain, and an output's first level before it drives; `emb_drv_gpio_write()` and `emb_drv_gpio_read()` drive it and read the level at the pin; `emb_drv_gpio_assert()` asserts a named line (`emb_drv_gpio_line_t`) by its sense. Each is O(1) and callable from any context. The STM32F4's GPIO ports and the RP2350's bank 0, through the SIO, are its drivers; the RP2350's pads have no open drain, and say so with `EMB_ENOTSUP`.
- An SPI target names its chip select, asserted once the controller is set up for it and released after its last frame. `emb_drv_spi_acquire()` and `emb_drv_spi_release()` hold the bus for one target, its chip select asserted across several transfers, with a recursive mutex with priority inheritance: other threads' transfers wait within their timeouts, and one for another target gets `EMB_EBUSY`. A transfer's timeout now covers its wait for the bus too.
- A board's `devices[].cs` names chip selects: a device on an SPI bus its own (`EMB_SOC_<DEVICE>_CS`), an SPI controller spare ones (`EMB_SOC_<DEVICE>_CS<n>`), each a line of one of the board's GPIO devices, set up by the pin table as an output, deasserted, at boot. The NUCLEO-F446RE's SPI2 has PB12 (CN10-16), the Pico 2 W's SPI1 has GPIO13 (pin 17), and `test_spi_loop` reads each back at the pin. A binding's field may read the SoC description's own facts (`soc.gpio.lines`).

### Fixed (console lines cut by a reset, 2026-11-09)

- On Cortex-M, `emb_arch_system_reset()` first calls the board's `emb_board_reset_prepare()`, as the Xtensa port does: the RP2350 and STM32F4 boards let their consoles finish there. The test framework's fault-check resets had cut the line before them on the Pico 2 W, whose PL011 holds 32 bytes, and the PL011's flush now waits as long as a full FIFO takes.
- `tools/footprint/stack_bound.py` takes a roots file's indirect target written `name?` as one only some images link, a device one board has and another does not; every other name must still be in the image.

### Added (the I2C class, 2026-11-09)

- `<emb/drv_i2c.h>`, behind `CONFIG_EMB_DRV_I2C` (SPEC-017 §6): a transaction of write and read segments to a target's address and speed, joined by repeated starts and ended by a stop; `emb_drv_i2c_transfer()` and `emb_drv_i2c_write_read()` wait for it, and a canceled or timed-out one ends with a stop. As a target, a listen answers one transaction to the controller's own address, on its second lane, beside its own transactions; `emb_drv_i2c_listen()` waits for one. A refused address or byte completes with `EMB_ENACK`, a lost arbitration with `EMB_EARBLOST`, a misplaced start or stop with `EMB_EBUSERR`.
- A117, proposed: `EMB_DRV_I2C_CAP_TARGET_NACK` says whether a target refuses bytes beyond its listen's buffer, or acknowledges and drops them.
- The I2C class's drivers: the STM32F4's I2C (`drivers/st/stm32_i2c.c`), controller and target at once, standard and fast mode, its receiver's acknowledge managed for reads of one, two and more bytes as RM0390 asks; and Synopsys DW_apb_i2c (`drivers/snps/dw_i2c.c`), the RP2350's I2C0 and I2C1, up to fast mode plus, a controller or a target at a time. The F446's I2C1 to I2C3 and the RP2350's I2C0 and I2C1 are in their descriptions, the alternate functions checked against ST's open pin data and the pico-sdk's.
- A pin table's I2C lines are open drain with the chip's pull-ups (`EMB_STM32_PIN_OPEN_DRAIN`, `EMB_STM32_PIN_PULL_UP`; the RP2350's pull-up), so two of a chip's controllers wired to each other work without resistors at 100 and 400 kHz.
- `test_i2c_loop` checks two controllers wired line to line, one answering the other: writes, register reads of one, two, three and eight bytes, an address nobody answers, bytes beyond a target's buffer, fast mode and a listen that times out. It runs on EmbSIM's NUCLEO-F446RE (I2C1 to I2C2 by D15–D6 and D14–D3, a board's `simulation.loopbacks` entry now naming an I2C peer and two nets) and on the bench with `EMB_TEST_I2C_LOOP=<controller>,<target>`.

### Added (a held I2C bus, 2026-11-09)

- `emb_drv_i2c_acquire()` and `emb_drv_i2c_release()` hold the bus for one target across transactions, as the SPI class's do: other threads' transactions wait within their timeouts, and one to another target, from anywhere, gets `EMB_EBUSY`. Both classes now build on one held bus (`<emb/drv_bus.h>`, `emb_drv_bus_t` first in a controller's RAM): a recursive mutex with priority inheritance and the target holding it. A driver sets it up with its class's `emb_drv_spi_bus_init()` or `emb_drv_i2c_bus_init()`.

### Added (an I2C bus's recovery, 2026-11-09)

- A118, proposed: `emb_drv_i2c_recover()` frees a bus a target holds low, cut off in the middle of a byte it sends: with the bus to itself, the class takes SCL and SDA as open-drain GPIO lines, clocks SCL at about 100 kHz until SDA is released, nine times at most, ends with a stop, gives the lines back and resets the controller. `test_i2c_loop` cuts a target off in its third bit by hand and recovers it, on EmbSIM and on both boards.
- A binding's field may read `pins.<signal>`, the GPIO line the device's pin is on; the generator also names it `EMB_SOC_<DEVICE>_<SIGNAL>`. `EMB_DRV_GPIO_FUNCTION` gives a borrowed line back to what the board's pin table made it, and the STM32F4's pin table now sets each pin's output type and pull exactly. The RP2350's GPIO driver makes open drain with the output enable, since its pads have none.

### Added (the DMA class, 2026-11-09)

- A119, proposed: `<emb/drv_dma.h>`, behind `CONFIG_EMB_DRV_DMA` (SPEC-017 §7), for drivers: a job on a channel the device was given, memory to peripheral, peripheral to memory or memory to memory, its end delivered to a callback in the DMA interrupt; `emb_drv_dma_stop()` gives the items moved. Memory outside the board's DMA regions is refused.
- A SoC's DMA controllers are peripherals with a `dma` key, fixed (the STM32F4's streams and channel selects) or dreq (the RP2350's channels paced by a request number), and a peripheral's `dma_requests` names its lines. A board device asks for its lines with `dma: [tx, rx]`; the generator gives each a channel no other device has, names it `EMB_SOC_<DEVICE>_DMA_<REQUEST>`, and writes the board's DMA regions into `soc_devices.c`. The F446's request tables come from RM0390, cross-checked with EmbSIM's model; the RP2350's DREQs from the pico-sdk.
- The DMA class's drivers: the STM32F4's DMA1 and DMA2 (`drivers/st/stm32_dma.c`) and the RP2350's DMA (`drivers/rpi/rp2350_dma.c`). The SPI drivers run transfers of 16 frames and more by DMA where the board gives the device channels: the NUCLEO-F446RE's SPI2 on DMA1 streams 4 and 3, the Pico 2 W's SPI1 on channels 0 and 1. A canceled transfer by DMA ends after whole frames. `test_spi_loop` checks the channel is the SPI's during a transfer and cancels one part-way, on EmbSIM and both boards.
- The UART drivers write 16 bytes and more by DMA where the board gives the device a channel (`dma: [tx]`) and DMA reaches the bytes; their reads stay with the interrupt and the class's ring. The NUCLEO-F446RE's console, USART2, writes by DMA1 stream 6 (`EMB_SOC_CONSOLE_DMA_TX`), and the Pico 2 W's UART1 by channel 2. A write canceled part-way has sent what it counts.

### Added (the watchdog class, 2026-11-10)

- A120, proposed: `<emb/drv_wdt.h>`, behind `CONFIG_EMB_DRV_WDT` (SPEC-017 §8): a hardware watchdog started once with its timeout, fed from any context, never stopped, and whether it caused the last reset, read when its driver starts and kept for the boot. The timeout is a floor, counted at the oscillator's fastest; one the watchdog cannot count is refused and nothing starts. A board's watchdog is `EMB_SOC_WATCHDOG_DEVICE`.
- The class's drivers: the STM32F4's IWDG (`drivers/st/stm32_iwdg.c`) and the RP2350's watchdog (`drivers/rpi/rp2350_watchdog.c`), whose cause is counted only when the mark it left is there, since the boot ROM reboots by the watchdog too. The NUCLEO-F446RE's `iwdg` and the Pico 2 W's `watchdog` are board devices. The STM32F4's reset flags are read and cleared once and kept for every reader, so the crash record and the watchdog both see them.
- The test framework's `EMB_ASSERT_RESETS(statement)`: on a board that resumes a case after a reset, the statement must reset the board after calling `emb_test_reset_expected()`; a reset before, a fault, or none fails it. `test_wdt_board` feeds the board's watchdog for half a second, starves it, and checks after the reset that it caused it, on EmbSIM and both boards.
