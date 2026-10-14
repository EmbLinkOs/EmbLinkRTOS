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

For a local checkout, replace `/workspace/EmbLinkRTOS` with your checkout directory. Passing CTest should report nine conformance executables plus the differential bridge for this preset. Individual case counts and skipped model scenarios are inside the runner logs. A zero-test run is a setup failure.

## Reference model

```sh
cd tools/model
python3 -m unittest discover -v
```

Only the standard library is needed for the basic suite. Install Hypothesis to include randomized scenario tests. The model's bounded scenario exploration and kernel bridge are complementary checks; neither demonstrates physical timing or complete C memory safety.

## First sample

`samples/first_execution/main.c` shows static thread/object setup and execution. Build it with the native preset above. The long-running sample is useful for switching/notification behavior; the conformance target `test_first_execution` provides a finite assertion-based check. Native time is virtual and advances in idle, so host timing is not a board benchmark.

For repeatable supported operations, start with the generated current API, [core concepts](concepts.md), and [configuration](configuration.md). Consult [Project status](../STATUS.md) before attempting a planned service or hardware workflow.
