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

For a local checkout, replace `/workspace/EmbLinkRTOS` with your checkout directory. Passing CTest should report twelve targets: nine conformance executables, the differential bridge, timer regressions and evidence workflow check. Individual case counts and skipped model scenarios are inside the runner logs. A zero-test run is a setup failure.

Experimental Linux x86-64 presets can compile the kernel with EmbCC while GCC supplies the POSIX host and final link. See [EmbCC native integration](../engineering/embcc-native-linux.md) for compiler selection, the ABI guard and qualification limits.

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
