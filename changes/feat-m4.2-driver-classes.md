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
