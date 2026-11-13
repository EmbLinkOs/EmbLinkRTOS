# Tasks on the NUCLEO-F446RE and the Pico 2 W (2026-11-13)

The first run of tasks ([SPEC-020](../specs/SPEC-020-tasks.md)) on a board, generated from a system description ([A126](../specs/AMENDMENTS-M4.md)). The application is `examples/tasks`, with two tasks:
- **a door,** event-driven, with states closed, open and jammed. Its open state has a 2-second timeout and entry and exit handlers.
- **a blinker,** periodic every 500 ms, with states dark and lit. It toggles the LED and posts `open` to the door every fourth blink.

`emb-hwgen generate --system` wrote everything it builds from:
- the application's `CMakeLists.txt`, `src/main.c` and `src/tasks/`;
- `generated/nucleo_f446re/`.

The tasks' behaviour is written in the generated task files, as an application's author would. A later regeneration left those edits untouched.

**Commit:** `dab3513`, from a clean tree (branch `feat/configurator`, PR #146).

| Board and compiler | Image (text / data / bss) | Record |
|---|---|---|
| NUCLEO-F446RE: Arm GNU Toolchain 14.2.Rel1 (GCC 14.2.1) | 25,432 / 80 / 6,528 | `hil-runs/2026-11-13-nucleo_f446re-tasks-gcc/` |
| NUCLEO-F446RE: Homebrew Clang 23.1.5 with LLD 23.1.5 | 27,856 / 80 / 6,528 | `hil-runs/2026-11-13-nucleo_f446re-tasks-clang/` |
| Pico 2 W: Arm GNU Toolchain 14.2.Rel1 (GCC 14.2.1), at `b3ae713` | 24,964 / 0 / 7,216 | `hil-runs/2026-11-13-pico2_w-tasks-gcc/` |
| Pico 2 W: Homebrew Clang 23.1.5 with LLD 23.1.5, at `b3ae713` | 27,712 / 0 / 7,216 | `hil-runs/2026-11-13-pico2_w-tasks-clang/` |

The NUCLEO's images were flashed by OpenOCD and reset. The Pico's were loaded by picotool, their console read through the NUCLEO running the UART relay (the [Pico bench](hil-pico2w-2026-11-07.md)). Each console was read for 21 seconds, and all four printed the same sequence, five times, the Pico's starting 0.73 s later after picotool's load:

```text
[  3.38] door: open
[  5.38] door: closing by itself
[  5.38] door: closed
[  7.38] door: open
```

- **The periodic task:** a blink each second (lit every other 500 ms period), with the fourth blink posting the door's `open` every 4.00 s, to within the capture's 10 ms.
- **The event task and its timeout:** `open` moves the door to its open state, whose entry handler prints `door: open`. With no event for 2.00 s, the state's timeout gives the handler `EMB_EV_TIMEOUT`. It returns the closed state, and the exit handler prints `door: closed` (KRN-TSK-002).

## Found on the way

- **The tasks were not linked.** The first image printed nothing: the build compiled the system's `system.c` but not its `tasks.c`, so the board idled with no task thread. The build now compiles `tasks.c` (`dab3513`). Once it was compiled, `-Wmissing-field-initializers` caught the task object's initializer leaving out the task's data; it is now designated.
- **A silent first run on the Pico.** The first GCC load printed nothing. A HIL smoke test then printed all its results through the same relay, so the console path was sound. A diagnostic copy of the application (outside the tree, a thread printing both tasks' counters each second and returning the board to BOOTSEL) then showed both tasks running as on the NUCLEO. The unmodified image, loaded again, printed the sequence above. The first run's silence was the bench, most likely the relay just reflashed, not the code.
