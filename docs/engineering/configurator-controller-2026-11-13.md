# The controller from its system description, on the NUCLEO-F446RE (2026-11-13)

The configurator's first user (A125): `examples/controller`'s threads and objects come from `examples/controller/system.yaml`, not from C. `emb-hwgen generate --system` writes them into `generated/<board>/system.c`, and the build compiles that with the controller and checks each thread's stack after the link.

The image is unchanged. It has the same sizes as main's build with the C definitions (30,360 bytes of text, 24 of data, 8,432 of bss with GCC) and the same defined symbols. On the board, the controller's whole session passed with GCC and with Clang.

**Commit:** `40f86a5`, from a clean tree (branch `feat/configurator`, PR #146).

**Bench:** the NUCLEO-F446RE as in the [controller's first run](controller-nucleo-2026-11-07.md), its console on the ST-LINK's port. No analyzer was connected. `tools/hil/controller_session.py` flashed each image and ran its nine phases: boot, normal sampling, stacks, overload, recovery, 1 kHz, a 60-second soak, a decoded fault and the watchdog reset.

| Compiler | Image (text / data / bss) | Phases | Record |
|---|---|---|---|
| Arm GNU Toolchain 14.2.Rel1 (GCC 14.2.1), checked | 30,360 / 24 / 8,432 | 9 of 9 | `hil-runs/2026-11-13-nucleo_f446re-controller-system-gcc/` |
| Homebrew Clang 23.1.5 with LLD 23.1.5, checked | 33,408 / 24 / 8,424 | 9 of 9 | `hil-runs/2026-11-13-nucleo_f446re-controller-system-clang/` |

Each record holds the session (`controller.json`), the console as received (`controller.log`), the image's sizes and the build's stack check (`stack-bound.json`).

## The stacks: checked after the link, measured on the board

The build ran the stack check after the link. It reads the threads from `generated/nucleo_f446re/system.json` and the interrupt levels and indirect calls from `stack-roots.json`. The session's `stacks` phase then read each stack's high-water mark on the board. Every measured figure is under its static bound, as a worst case must be:

| Stack | Size | GCC: bound / measured | Clang: bound / measured |
|---|---|---|---|
| `control_thread` | 1,024 | 484 / 236 | 416 / 188 |
| `command_thread` | 1,024 | 532 / 284 | 464 / 236 |
| `telemetry_thread` | 1,024 | 612 / 340 | 408 / 156 |
| interrupt stack | 2,048 | 1,684 / 384 | 1,480 / 312 |

## Found on the way

- **The stack tool did not read Clang's disassembler.** Under the Clang toolchain, CMake's objdump is `llvm-objdump`, which writes a call's target as `0x8006d90 <f>`. The tool's pattern matched only GNU objdump's `8006d90 <f>`, so it saw no calls: each thread's bound was its entry's own frame (96 to 144 bytes), and the check passed. CI had not shown it, because its own calls name `arm-none-eabi-objdump`. The tool now reads both, gives the same bounds from either on the Clang image, and refuses a disassembly in which no function calls another.
- **The F446ZE has no watchdog device in its description.** The chip has an IWDG, but `boards/st/nucleo_f446ze/board.yaml` does not list it, so a system on both boards cannot name the watchdog. The controller keeps its watchdog's timeout in C for now.
