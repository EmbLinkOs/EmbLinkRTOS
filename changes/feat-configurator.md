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
