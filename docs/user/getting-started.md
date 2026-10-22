# Getting started

## Prerequisites

Native development requires a C compiler with POSIX threads, CMake 3.20 or newer, Ninja, Python 3.10 or newer and PyYAML. The documentation toolchain uses Python 3.12 and pinned packages; it does not require compiling the kernel. See [Building documentation](../engineering/documentation.md).

Use the existing checkout. Build outputs go under `build/` and virtual environments stay ignored. The cloud environment already has native tools in `.venv`; activate it for native commands. On another machine install the prerequisites with its supported package manager and Python environment.

```sh
cd /workspace/EmbLinkRTOS
source .venv/bin/activate
cmake --preset native-gcc
cmake --build --preset native-gcc --parallel 4
ctest --preset native-gcc --output-on-failure --no-tests=error
```

For a local checkout, replace `/workspace/EmbLinkRTOS` with your checkout directory. Passing CTest should report thirteen targets: ten conformance executables (including IRQ priorities), the differential bridge, timer regressions and evidence workflow check. Individual case counts and skipped model scenarios are inside the runner logs. A zero-test run is a setup failure.

Experimental Linux x86-64 presets can compile the kernel with EmbCC while GCC supplies the POSIX host and final link. See [EmbCC native integration](../engineering/embcc-native-linux.md) for compiler selection, the ABI guard and qualification limits.

## Embedded tests on EmbSIM

The Arduino Uno build's conformance suite also runs on [EmbSIM](https://github.com/EmbLinkOs/EmbSIM)'s
simulated Uno: the ATmega328P's core, timers and USART at its clock, with the board's circuit around it.
Build EmbSIM's command line, then configure the `avr-uno-embsim` preset with it:

```sh
cmake -S ../EmbSIM -B ../EmbSIM/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DEMBSIM_SERVER=OFF
cmake --build ../EmbSIM/build --target embsim_cli
cmake --preset avr-uno-embsim -DEMB_EMBSIM="$PWD/../EmbSIM/build/bin/embsim"
cmake --build --preset avr-uno-embsim
ctest --preset avr-uno-embsim --output-on-failure --no-tests=error
```

`tools/embsim/run_embsim.py` runs each test image with `embsim run --target emblink/arduino_uno --image
TEST.elf --until-text EMB_TEST_END --console` and reads the console as `tools/qemu/run_qemu.py` reads
QEMU's. Runs are deterministic: an image gives the same console on every run. Any board that sets
`EMB_BOARD_EMBSIM_TARGET` in its `board.cmake` can use `-DEMB_TEST_EMULATOR=embsim`. As with QEMU,
simulated time is never timing evidence (SIM-003).

The NUCLEO-F446RE build does the same on EmbSIM's NUCLEO-F446RE (`emblink/nucleo_f446re`): the
STM32F446RE's Cortex-M4F, RCC, PWR, flash interface, GPIO and USART2 on the board's circuit, the
ST-LINK's MCO as the clock input and its virtual COM port as the console. Use the
`cm4-nucleo-embsim` preset with `gcc-arm-none-eabi` installed:

```sh
cmake --preset cm4-nucleo-embsim -DEMB_EMBSIM="$PWD/../EmbSIM/build/bin/embsim"
cmake --build --preset cm4-nucleo-embsim
ctest --preset cm4-nucleo-embsim --output-on-failure --no-tests=error
```

The ESP32-C3-DevKitM-1 build runs on EmbSIM's DevKitM-1 (`emblink/esp32c3_devkitm1`) as the
board does: each test image is also written as `<name>.bin`, the flash image Espressif's boot
ROM loads from offset 0, and that is what EmbSIM flashes. Use the `rv32-c3-embsim` preset with
`gcc-riscv64-unknown-elf` and picolibc installed:

```sh
cmake --preset rv32-c3-embsim -DEMB_EMBSIM="$PWD/../EmbSIM/build/bin/embsim"
cmake --build --preset rv32-c3-embsim
ctest --preset rv32-c3-embsim --output-on-failure --no-tests=error
```

## Reference model

```sh
cd tools/model
python3 -m unittest discover -v
```

Only the standard library is needed for the basic suite. Install Hypothesis to include randomized scenario tests. The model's bounded scenario exploration and kernel bridge are complementary checks; neither demonstrates physical timing or complete C memory safety.

## First sample

`samples/first_execution/main.c` shows static thread/object setup and execution. Build it with the native preset above, then run:

```sh
./build/native-gcc/samples/first_execution/first_execution
```

The native sample exits after its configured two-million A/B iterations and prints `first_execution: switches A=... B=... ticks=...`. Its A/B counters count loop iterations, not a hardware-measured context-switch total. Embedded versions continue running and print progress where the board console permits. The conformance target `test_first_execution` supplies a shorter assertion-based check. Native time is virtual and advances in idle, so host timing is not a board benchmark.

For a shorter application, follow [Threads and periodic work](tasks.md), which exits after five iterations. Then try [Synchronization and notifications](synchronization.md) and [Debugging](debugging.md). These tutorials use existing native libraries and keep example outputs under ignored `build/tutorials/`.

For repeatable supported operations, start with the generated current API, [core concepts](concepts.md), and [configuration](configuration.md). Consult [Project status](../STATUS.md) before attempting a planned service or hardware workflow.
