# The device model on hardware: NUCLEO-F446RE and Pico 2 W (2026-11-09)

M4.1's device model on a physical board. The NUCLEO-F446RE declares four devices in its description: the clock tree (`rcc`), the GPIO ports it uses (`gpioa`, `gpioc`) and its console (`usart2`). Every conformance image initializes them before its first thread runs, and the suite passed with GCC and with Clang.

**Board:** NUCLEO-F446RE (STM32F446RE, Cortex-M4F) at 180 MHz, as in the [first runs](hil-nucleo-2026-11-07.md).
- **Probe:** the on-board ST-LINK/V2-1, firmware V2J49M36.
- **Flashing:** OpenOCD 0.12.0 (`board/st_nucleo_f4.cfg`).
- **Console:** USART2 through the ST-LINK's virtual COM port, `/dev/cu.usbmodem11103`.

**Commit:** `afccf47`, from a clean tree (branch `feat/device-core`, PR #137).

## Results

`ctest` on `cm4-nucleo-hil`, checked, each run's records under `hil-runs/`:

| Compiler | Passed | Cases | Devices in every image |
|---|---|---|---|
| Arm GNU Toolchain 14.2.Rel1 (GCC 14.2.1) | 28 of 28 | 184 | 4 |
| Homebrew Clang 23.1.5 with LLD 23.1.5 | 28 of 28 | 184 | 4 |

What these runs show on the chip:
- **The board's devices.** `device_board_devices_ready_in_dependency_order` finds all four READY, each at the level the table gives and after the devices it depends on: the GPIO ports and the console after `rcc`, which gates their clocks.
- **The device core.** The ten cases of the core pass, with the fake driver's transfers finishing in a software interrupt. So do the three that take its optional operations and its misuse checks; each misuse check faulted in the kernel, reset the part and was settled on the next boot (A89).
- **The console survives its device.** `usart2`'s initialization finds the console already running at its rate and leaves it alone, so the console's output carries through the kernel's start unbroken.

## Pico 2 W

The RP2350's board support was split into drivers at `04ebb04`: the clock tree, resets and pins in `drivers/rpi/`, the PL011 console in `drivers/arm/pl011.c`. The board declares three devices: `clocks`, `io_bank0` after it, and `uart0`, its console, after that.

**Board:** Pico 2 W (RP2350A, Cortex-M33 in the Secure state) at 150 MHz from PLL_SYS, as in the [first runs](hil-pico2w-2026-11-07.md). Images load through picotool in BOOTSEL; the console is UART0 on GPIO0, carried to the host by the NUCLEO's UART relay.

**Commit:** `04ebb04`, from a clean tree.

`ctest` on `cm33-pico2w-hil`, checked, each run's records under `hil-runs/`:

| Compiler | Passed | Cases | Devices in every image |
|---|---|---|---|
| Arm GNU Toolchain 14.2.Rel1 (GCC 14.2.1) | 29 of 29 | 186 | 3 |
| Homebrew Clang 23.1.5 with LLD 23.1.5 | 29 of 29 | 186 | 3 |

The board-devices case finds the three READY in order. The suite also passed at `d5a3249`, before the split (29 of 29), so the drivers write what the board support did.

## The run before the fix

The first runs, at `7929a44` the evening before, also passed 28 of 28. In them only `test_device` had the board's devices: the kernel's weak empty table won over the board's at link time, so the other images started without devices. That is fixed in `afccf47`, which every board's table makes impossible to lose (the [generator](hwgen.md#devices-spec-016)). These runs replace them.
