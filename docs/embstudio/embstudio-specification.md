# EmbStudio: product and architecture specification

**Version 0.1, 2026-11-11.** The handoff specification for EmbStudio's developer. It settles what the project owner has decided, describes what we want built and in what order, and marks what is still open. The EmbLinkRTOS side (the generator, the schemas, the CLI it needs) is the owner's and is described here only where EmbStudio meets it.

- **Owner:** teo1747 (EmbLinkRTOS). Product decisions are the owner's.
- **EmbStudio developer:** builds the desktop application and EmbStudio Core.
- **Changes to this document:** proposed by pull request, agreed with the owner. Decisions are added to §2 with their date.

## 1. What EmbStudio is

EmbStudio is **the IDE made for EmbLinkRTOS**: a native desktop application, built with Qt, for creating, configuring, building, flashing, debugging and analysing firmware on EmbLinkRTOS. It is a long-term product, grown step by step like the RTOS itself, and meant to feel like no other IDE.

It is **not** a VS Code look-alike or a general-purpose editor. It is special because it understands the RTOS:
- its boards and chips, as data;
- its threads, objects, priorities and stacks;
- its driver classes and devices;
- its crash records and debug descriptor;
- its test and hardware evidence.

**What a developer can do with it, once complete:**
1. Create a project from a board, or from a template.
2. See and change the board: pins, clocks, peripherals, devices, DMA channels, flash partitions.
3. Add external parts (sensors, memories) from modules.
4. Configure the RTOS: threads, priorities, stacks, queues, mutexes, timers. See the budget (RAM, flash, stack) before building.
5. Write application code.
6. Build, flash, and watch the serial console.
7. Debug with the RTOS visible: threads and their states, stacks and their high-water marks, devices and their lifecycle.
8. Decode a crash record into a readable report.
9. Trace and time execution, and see power residency (later).
10. See the project's evidence: tests run, on which board, passed or not.

**Non-goals:**
- A general IDE for other RTOSes or bare-metal code.
- Its own compiler: it uses GCC, Clang or EmbCC, which it discovers or downloads.
- A VS Code extension. Not planned, though the core's interface (§5) would allow one later.
- Cloud features in the first releases.

## 2. Decisions already taken

| Date | Decision | Reason |
|---|---|---|
| 2026-11-11 | The name is **EmbStudio** | The owner's choice. Check the trademark first, since SEGGER Embedded Studio is close (§10) |
| 2026-11-11 | A **native desktop application in Qt 6**; no VS Code base | Full control of the interface; a product of its own; the developer knows Qt well |
| 2026-11-11 | **Step by step:** configuration-first, then build, flash and debug, then the editor grows | A useful product early, with the editor (the costliest part) grown over time |
| 2026-11-11 | **The hardware model stays in EmbLinkRTOS:** the SoC and board descriptions, their schemas, the validation rules and the generator (`emb-hwgen`) | One hardware model, versioned and tested with the kernel it generates for. EmbStudio shows and edits it, and never re-implements its rules |
| 2026-11-11 | **EmbStudio Core is a Python engine behind a CLI and a JSON-RPC server** (`emb serve`); the Qt application is its face | The RTOS's tools are Python already. Everything stays scriptable and testable without a GUI |
| 2026-11-11 | **Open core under Apache-2.0,** with a **contributor licence agreement**; paid "Pro" features possible later | The same licence as the RTOS; the agreement keeps commercial options open (§10) |
| 2026-11-11 | **No VS Code extension** for now | EmbStudio is the RTOS's own IDE |

## 3. The ecosystem around EmbStudio

```text
+-------------------------- EmbStudio (this repository) ---------------------------+
|  Qt 6 desktop application (C++)  <-- JSON-RPC 2.0 over stdio -->  EmbStudio Core |
|  views, editor, docking, DAP client                    (Python: emb serve)       |
+--------------------------------------------------------------+-------------------+
                                                               | calls, reads
          +-------------------------+--------------------------+------------------+
          |                         |                          |                  |
   EmbLinkRTOS               EmbLinkRTOS-parts         toolchains and tools
   emb-hwgen: schemas,       part drivers, blocks,     GCC, Clang, EmbCC,
   validation, generation;   templates                 OpenOCD, pyOCD, GDB,
   emb CLI, crashdec, HIL                              picotool, EmbSIM, QEMU
   runner; kernel, CMake
```

| Repository | Owns | Who |
|---|---|---|
| **EmbStudio** (new) | The Qt application; EmbStudio Core; the schemas of the project manifest, the lock file and the module manifest; EmbStudio's tests and installers | EmbStudio developer |
| **EmbLinkRTOS** | The kernel, the on-chip drivers, the hardware description schemas (SPEC-014), the generator, the `emb` CLI, crashdec, the HIL runner and the support matrix | Owner |
| **EmbLinkRTOS-parts** (new, later) | External part drivers and bindings, application blocks, project templates | Owner, then contributors |
| **EmbCC** | The compiler | Owner |

The R-004 design record in EmbLinkRTOS (`docs/research/R-004-ecosystem-addons-configurator-integrations.md`) explains the module contract and the configurator in more depth. This specification takes precedence where they differ on EmbStudio.

## 4. Architecture

Three layers. The rule that matters most: **all knowledge lives in the core or the RTOS's tools; the Qt application only shows and edits.**

| Layer | Language | Responsibility | Must not |
|---|---|---|---|
| **Desktop application** | C++17 or later, Qt 6 LTS (6.8) | Windows, docking, views, the editor, the debugger interface, settings, installers | Validate hardware, compute clocks, generate code, parse the descriptions on its own |
| **EmbStudio Core** | Python 3.11 or later | Projects, modules and the lock file, toolchains, build, flash, monitor, debug sessions, simulation; the editing model of the configuration; calls into the RTOS's tools | Re-implement the RTOS's rules: it calls `emb-hwgen` and reads its outputs |
| **The RTOS's tools** | Python, C | Descriptions and schemas, validation, generation, `hw.json`, reports, crash decoding, test runs | Know anything about EmbStudio |

**Process model.** The desktop application starts `emb serve` as a child process and talks JSON-RPC 2.0 to it over standard input and output, framed as the Language Server Protocol frames its messages (`Content-Length` headers). One core process serves one open workspace. Debugging uses the **Debug Adapter Protocol** (DAP), through a debug adapter the core starts, so the debugger interface is written once, whatever the probe (OpenOCD, pyOCD, J-Link).

**Why it is split this way:**
- Every action is also a CLI command, so CI, scripts and tests do what the IDE does.
- The core can be tested without a GUI, and the GUI tested against a fake core.
- A future frontend (a VS Code extension, a web view) needs no new logic.

## 5. The core's interface (JSON-RPC)

The interface is versioned. `initialize` exchanges `protocol_version` (semantic versioning; the desktop refuses a core of another major version) and the capabilities each side has. Methods are grouped by namespace. This list is the first version to agree, not final:

| Namespace | Methods (first version) | Notes |
|---|---|---|
| `project` | `create`, `open`, `validate`, `close`, `status` | `validate` returns every diagnostic with its file, line and path in the YAML |
| `board` | `list`, `describe`, `pins`, `clocks`, `memory` | From the generator's resolved model (`hw.json`); nothing computed in the GUI |
| `config` | `get`, `set`, `preview`, `apply` | Edits to the YAML through the core. `preview` returns what would change and the diagnostics, before anything is written |
| `devices` | `list`, `describe`, `add`, `remove` | The board's devices, their class, binding fields, DMA channels and chip selects |
| `rtos` | `threads`, `objects`, `budget` | `system.yaml`'s threads and objects; the RAM, flash and stack budget from the generator's report |
| `modules` | `list`, `search`, `add`, `update`, `remove` | Writes `emb-project.yaml` and the lock file |
| `generate` | `run`, `diff` | Runs `emb-hwgen`; `diff` shows what generated files would change |
| `build` | `configure`, `run`, `clean`, `targets` | CMake with the chosen toolchain; diagnostics as structured messages |
| `toolchain` | `list`, `detect`, `install` | Discovers GCC, Clang and EmbCC; downloads pinned versions with checksums |
| `flash` | `probes`, `run` | OpenOCD, pyOCD, picotool, by the board's `debug_probe` |
| `monitor` | `open`, `close`, `send` | Serial console; later the RTOS's framed channels (logs, trace, crash) |
| `debug` | `start` | Returns the DAP endpoint the desktop connects to |
| `crash` | `decode` | crashdec on a crash record or a log |
| `evidence` | `runs`, `matrix` | Hardware runs and the support matrix of the boards used |
| `sim` | `start`, `stop` | EmbSIM or QEMU for the board, where supported |

**Notifications** from the core: `progress` (long operations), `diagnostics` (a file's problems changed), `log` (output of a tool it runs), `files_changed` (the project changed on disk). **Errors** carry a code, a message for the user, and the tool output that caused them.

## 6. The project

A project is a directory under version control. **Everything EmbStudio shows comes from these files.** It keeps no hidden state but window layout and preferences, in `.embstudio/`, which is not committed.

```text
my-project/
  emb-project.yaml      the project: RTOS version, modules, board, toolchain
  emb-project.lock      exact commits and hashes of the RTOS and the modules (written by the core)
  system.yaml           threads, objects, parts' use, blocks (SPEC-014 section 5)
  boards/               only for a board of the project's own (A76's layout)
  src/  include/        the application's code
  generated/            the generator's outputs: never edited, regenerated
  CMakeLists.txt        builds against the RTOS's CMake package
  .embstudio/           EmbStudio's own preferences (ignored by git)
```

```yaml
# emb-project.yaml
schema_version: 0
kind: project
name: my-project
rtos: { url: https://github.com/EmbLinkOs/EmbLinkRTOS, rev: v0.3.0 }
modules:
  - { name: emblink-parts, url: https://github.com/EmbLinkOs/EmbLinkRTOS-parts, rev: v0.1.0 }
board: st/nucleo_f446re
toolchain: { family: gcc, version: 14.2.Rel1 }
system: system.yaml
```

**Rules:**
- The board and SoC descriptions follow the RTOS's schemas (SPEC-014, in `hw/schemas/`). EmbStudio writes them through the core and never invents a field.
- If the configuration views need a board split into smaller files (pins, clocks, devices), the RTOS adds an `include:` to its schema. EmbStudio asks for it; it does not add its own format.
- Generated files are only regenerated, never edited. User code calls the names the generator writes.
- A project created by EmbStudio builds from the command line with the same result.

## 7. What it looks like: the views

The design is the developer's, within these principles:
- the RTOS is visible everywhere;
- every number shown comes from the core, with its source one click away;
- a change shows its effect (diagnostics, budget) before it is applied.

| View | Shows and does | From |
|---|---|---|
| **Start** | New project from a board or a template; recent projects | `project`, `board.list` |
| **Project** | The project's files, modules and board; its status (valid, generated, built) | `project.status` |
| **Board** | The chip and board: memory map, peripherals, the board's devices | `board.describe`, `devices.list` |
| **Pin map** | The package drawn from the SoC's pin data: each pin's function, conflicts highlighted; assign by click | `board.pins`, `config.set` |
| **Clock tree** | Oscillators, PLL, buses and their frequencies as the generator computed them, limits marked | `board.clocks` |
| **Devices and DMA** | Each device's class, binding fields, chip selects, DMA channels; add a part from a module | `devices.*` |
| **RTOS** | Threads (priority, stack, entry), objects, priority ceilings; a budget bar for RAM, flash and stack | `rtos.*` |
| **Memory and partitions** | Regions, flash partitions, the image's use of them after a build | `board.memory`, the build report |
| **Build** | Diagnostics, size per component, warnings | `build.*` |
| **Flash and console** | Probe, flash, the serial console | `flash.*`, `monitor.*` |
| **Debugger** | Breakpoints, stepping, registers, memory, **RTOS threads and their states, stack high-water marks, devices and their lifecycle** | DAP; the RTOS's debug descriptor (A107) |
| **Crash report** | A decoded crash record: fault, thread, location, registers | `crash.decode` |
| **Evidence** | Hardware runs of this board, passed or failed; the support matrix | `evidence.*` |
| **Trace and timing** (later) | Thread timeline, interrupt latency, power residency | The RTOS's trace (M4.3), timing tools |

**The editor** grows in steps:
1. Syntax highlighting (KSyntaxHighlighting, MIT) and an "open in your editor" action.
2. C and C++ intelligence through a language-server client talking to clangd (completion, errors, go to definition), using the build's `compile_commands.json`.
3. Search, refactoring, git.

## 8. The plan: stages

Each stage ends with something usable and demonstrated, and its acceptance criteria are checked in CI where possible.

| Stage | Delivers | Accepted when |
|---|---|---|
| **S0 Setup** | The EmbStudio repository, its licence files and contributor agreement, CI on Windows, Linux and macOS, a Qt application that starts and talks to a stub core | CI builds and tests on all three systems; `initialize` succeeds |
| **S1 Core foundation** | `emb serve`, `project.open` and `validate`, `board.*` from `hw.json`, `generate.run` | Opening the RTOS's `nucleo_f446re` and `pico2_w` boards shows their model; validation reports the generator's errors with locations |
| **S2 Read-only views** | Board, pin map, clock tree, devices, memory, RTOS | Every view matches the generator's `board.md` and `build_report.md` for both boards |
| **S3 Editing** | `config.preview` and `apply`; pins, devices and threads editable; live diagnostics | A pin conflict is refused with the generator's message; the YAML written is what a person would write; generation stays clean |
| **S4 Build, flash, console** | Toolchain discovery, CMake build, flash by OpenOCD and picotool, serial console | A project is created, built, flashed and seen running on the NUCLEO-F446RE and the Pico 2 W |
| **S5 Debugging** | DAP sessions; the RTOS-aware views; crash decoding | Breakpoints and stepping on both boards; threads and their states shown; a crash record decoded |
| **S6 Modules and parts** | `modules.*`, the parts catalog, adding a part as a device | A BME280 added from the parts module is generated, built and read on a board |
| **S7 Evidence and trace** | The evidence view; the trace and timing views as the RTOS provides the data | The board's hardware runs listed; a trace shown |
| **S8 The editor** | clangd integration, search, git | Daily work is possible without another editor |
| **S9 Release** | Installers (Windows MSI, macOS DMG, Linux AppImage), signed; updates; first public release | Installs cleanly on a new machine and opens the examples |

**What the RTOS side provides, and when:**

| EmbStudio needs | Status in EmbLinkRTOS |
|---|---|
| `hw.json` and the generated outputs for each board | Exists |
| `emb-hwgen generate` and `check` | Exists |
| The CMake package for applications outside the tree | Exists (A91) |
| crashdec, the HIL runner's records, the support matrix | Exist |
| The debug descriptor and `emb devices` | Exist (A107) |
| The `emb` CLI | M4.3 |
| The module contract, the project manifest and the lock file | Phase E1 (R-004) |
| `system.yaml` generation: threads, objects, `emb_system_init()` | Phase E3 |
| `include:` in the board schema, if the views need it | On request |
| The trace ring and its format | M4.3 |
| Machine-readable diagnostics from the generator (JSON) | To add, early: S1 needs it |

Until the module contract and `system.yaml` exist, S1 and S2 work on what exists: the boards in the RTOS's tree and their generated model. The core's project and module schemas are designed with the owner, and the RTOS adopts them in phase E1.

## 9. Engineering rules

- **Qt:** Qt 6 LTS (6.8), under the LGPL-3.0: **linked dynamically**, with no GPL-only module (Qt Charts, Qt Graphs, Qt Virtual Keyboard and Qt Creator's code are GPL or commercial). The UI may mix Qt Widgets for the frame and docking with Qt Quick for custom views such as the pin map and the clock tree.
- **Third-party libraries:** only under licences compatible with the open core: MIT, BSD, Apache-2.0, LGPL linked dynamically, MPL-2.0. **No GPL libraries** in the application. Some widgets for terminals and docking are GPL; check each one. GPL tools (GDB, OpenOCD) are run as separate programs, never linked. Toolchains are downloaded from their publishers, not bundled.
- **A notices list** of every component and its licence, generated in CI and shown in the About box.
- **Tests:** Qt Test for the application; pytest for the core; golden projects (the RTOS's boards) whose generated outputs and builds are compared in CI; the GUI tested against a recorded core.
- **Reproducibility:** pinned dependencies, pinned toolchains with checksums, the lock file. The same project gives the same build in the IDE and on the command line.
- **Code:** clang-format for C++, ruff for Python; every commit signed off (`Signed-off-by`, the DCO).
- **Accessibility and languages:** keyboard navigation throughout; Qt's translation system from the start, even with only English at first.
- **Privacy:** no telemetry by default. Anything collected later is opt-in.

## 10. Licence, ownership and the name

- **Licence:** Apache-2.0 for everything in the open core: the application, the core and the schemas.
- **A commercial "Pro" tier** may come later. Which features it holds is the owner's decision; candidates are advanced trace and timing analysis, power profiling, certification-report generation, and team features. The open core must stay complete for everyday work.
- **Ownership.** Every contributor, the EmbStudio developer included, signs the **contributor licence agreement** before their first contribution is merged. It lets the project offer the code under other terms later, including the Pro tier. A lawyer checks it before signing; the Apache Software Foundation's individual agreement is a common model. The owner and the developer also agree in writing on credit and on any future revenue from EmbStudio.
- **The name.** "EmbStudio", "EmbLink" and "EmbLinkRTOS" are the project's names. A trademark policy says forks may use the code but not the names. Before the first public release, a trademark search checks "EmbStudio" against SEGGER Embedded Studio and others.

## 11. Open questions, to settle together

1. **The docking framework:** Qt Advanced Docking System (LGPL-2.1), or Qt's own docks? (KDDockWidgets is GPL or commercial.)
2. **Qt Widgets and Qt Quick:** where each is used.
3. **The installers and updates:** the tools, and signing on Windows and macOS.
4. **The core's packaging:** a bundled Python (for example built with PyInstaller or a bundled interpreter) or a system one?
5. **Which RTOS views come first** after S2: debugging (S5) or modules (S6)?
6. **The Pro tier:** which features, and when?
7. **The trademark check** on "EmbStudio".

## 12. Terms used in the RTOS

| Term | Meaning |
|---|---|
| SoC description | A YAML file describing a chip: memory, peripherals, interrupts, clocks, pins (`soc/`) |
| Board description | A YAML file describing a board: its chip, oscillators, pins, console, devices, partitions (`boards/<vendor>/<board>/board.yaml`) |
| System description | `system.yaml`: what a product does with the board: threads, objects, partitions |
| `emb-hwgen` | The generator: validates the descriptions and writes `generated/`, including `hw.json` (the resolved model, the core's main input) |
| Compatible | A `vendor,part` string that names the driver for a peripheral or part |
| Binding | The entry in `drivers/bindings.yaml` saying which driver, class and configuration fields a compatible has |
| Device | A peripheral or part the board uses, with its driver, configuration and dependencies (SPEC-016) |
| Driver class | The interface a kind of device has: UART, SPI, I2C, DMA, GPIO, watchdog, flash, RTC, entropy (SPEC-017) |
| Partition | A named range of flash, such as `app` or `storage` |
| Debug descriptor | A table the kernel keeps in memory, so a debugger can find threads, objects and devices (A107) |
| Crash record | What a fault leaves in memory that survives a reset; crashdec decodes it |
| HIL | Hardware in the loop: the test suite run on a real board, its records archived as evidence |
| EmbSIM | The project's own simulator of microcontrollers and boards, used in CI |
| Evidence | The records that prove a claim: test results, hardware runs, measurements |

## 13. Where to read further, in EmbLinkRTOS

- `docs/specs/SPEC-014-hardware-description-schema.md`: the descriptions and the generator's outputs
- `docs/specs/SPEC-016-device-and-driver-model.md` and `SPEC-017-driver-classes.md`: devices and classes
- `hw/gen/`, `hw/schemas/`: the generator and its schemas
- `boards/st/nucleo_f446re/`, `boards/rpi/pico2_w/`: two real boards and their `generated/` outputs
- `tools/crashdec/`, `tools/descriptor/`, `tools/hil/`, `tools/support/`: the tools EmbStudio drives
- `docs/research/R-004-ecosystem-addons-configurator-integrations.md`: modules, parts, the configurator
