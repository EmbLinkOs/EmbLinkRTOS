### Added (the configurator: a system description, 2026-11-13)

- An application can describe its threads, kernel objects (mutex, semaphore, condition variable, event flags, message queue, timer, work queue), its devices' settings, its SPI and I2C targets and its watchdog in `system.yaml` (SPEC-014 §5, A125). `emb-hwgen generate --system` writes `generated/<board>/` for each board it names: the board's outputs with the product's settings, plus `system.h`, `system.c`, `system.json` and `system.md`. These are committed and kept current by `emb-hwgen check` ([application](docs/user/application.md)).
- Generation refuses, naming the line:
  - a priority outside the configuration, or an object type it leaves out;
  - a ceiling mutex user that is not a thread;
  - a device option its binding does not take, or a baud rate the clock cannot make;
  - an SPI clock below the controller's slowest, or a chip select or I2C address used twice;
  - an I2C speed or a watchdog timeout the driver does not support.
- After the link, a thread whose stack is below its worst case fails the build (`EMB_SYSTEM`, `EMB_STACK_ROOTS`).
- `examples/controller` declares its threads and objects in `system.yaml` on the NUCLEO-F446RE and F446ZE. Its image is unchanged, and it ran 9 of 9 phases on the board with GCC and Clang ([report](docs/engineering/configurator-controller-2026-11-13.md)).

### Fixed (the stack bound of Clang images, 2026-11-13)

- `tools/footprint/stack_bound.py` reads `llvm-objdump`'s disassembly as well as GNU objdump's. With `llvm-objdump`, which CMake picks under the Clang toolchain, it had seen no calls and reported each thread's own frame as its bound. It now also refuses a disassembly in which no function calls another.

### Added (tasks, editable generated files and their layout, 2026-11-13)

- Tasks (SPEC-020, `<emb/task.h>`, `CONFIG_EMB_TASK`) are threads written as state machines, event-driven or periodic, declared in `system.yaml`. The generator writes their states, queues, dispatch and threads, and the application writes one handler per state. `emb_task_post()` works from threads and interrupts, a state may have a timeout, entry and exit handlers run in order, a full queue is a counted drop, and a periodic task keeps a drift-free schedule.
- Every generated file is editable (A126). Regeneration leaves a file untouched where its generated text did not change, rewrites one that was not edited, and three-way merges one that was, against a committed baseline in `generated/.base/`. `check` refuses an unresolved conflict, and `check --strict` keeps the tree's boards exactly as generated.
- Generated directories are laid out by kind, `config/`, `include/`, `src/`, `linker/`, `build/` and `docs/`, for boards and applications. An application also gets its own editable `CMakeLists.txt`, `src/main.c` and task files.
- `examples/tasks` runs a door and a blinker on the NUCLEO-F446RE ([report](docs/engineering/tasks-nucleo-2026-11-13.md)).
