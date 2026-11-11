# EmbStudio: product and architecture specification

**Version 0.2, 2026-11-11.** The handoff specification for EmbStudio's developer: what the owner has decided, what to build and in what order, and what is still open. v0.2 closes gaps found in a review of v0.1 and settles every open question (§11). The EmbLinkRTOS side (the generator, the schemas, the CLI it needs) is the owner's and is described here only where EmbStudio meets it.

- **Owner:** teo1747 (EmbLinkRTOS). Product decisions are the owner's.
- **EmbStudio developer:** builds the desktop application and EmbStudio Core, in [EmbLinkOs/EmbStudio](https://github.com/EmbLinkOs/EmbStudio) (private until the trademark check, §10).
- **Changes to this document:** proposed by pull request, agreed with the owner. Decisions are added to §2 with their date.

## 1. What EmbStudio is

EmbStudio is **the IDE made for EmbLinkRTOS**: a native Qt desktop application for creating, configuring, building, flashing, debugging and analysing EmbLinkRTOS firmware. It is a long-term product, grown step by step like the RTOS. It is not a VS Code look-alike or a general-purpose editor: it understands the RTOS.

- Its boards and chips, as data.
- Its threads, objects, priorities and stacks.
- Its driver classes and devices.
- Its crash records and debug descriptor.
- Its test and hardware evidence.

**What a developer can do with it, once complete:**

1. Create a project from a board or a template.
2. See and change the board: pins, clocks, peripherals, devices, DMA channels, flash partitions.
3. Add external parts (sensors, memories) from modules.
4. Configure the RTOS: threads, priorities, stacks, queues, mutexes, timers, and see the RAM, flash and stack budget before building.
5. Write application code.
6. Build, flash, and watch the serial console.
7. Debug with the RTOS visible: threads and their states, stack high-water marks, devices and their lifecycle.
8. Decode a crash record into a readable report.
9. Trace and time execution, and see power residency (later).
10. See the project's evidence: tests run, on which board, passed or not.

**Non-goals:**

- A general IDE for other RTOSes or bare-metal code.
- Its own compiler: it uses GCC, Clang or EmbCC, which it discovers or downloads.
- A VS Code extension. Not planned, though the core's interface (§5) would allow one later.
- Cloud features in the first releases.

A project whose YAML has things the views don't cover yet still opens. Those fields are shown read-only with their source, and are kept byte for byte when the project is saved (§6).

## 2. Decisions already taken

Seventeen decisions are taken, all dated 2026-11-11: seven from v0.1, and the ten answers to §11's questions, which the owner accepted as recommended. A new decision is added here, with its date, once the owner agrees it.

| Date | Decision | Reason |
| --- | --- | --- |
| 2026-11-11 | The name is **EmbStudio** | The owner's choice. A trademark check comes first, since SEGGER Embedded Studio is close (§10) |
| 2026-11-11 | A **native desktop application in Qt 6**, with no VS Code base | Full control of the interface; a product of its own; the developer knows Qt well |
| 2026-11-11 | **Step by step:** configuration first, then build, flash and debug, then the editor grows | A useful product early, with the editor (the costliest part) grown over time |
| 2026-11-11 | **The hardware model stays in EmbLinkRTOS:** the SoC and board descriptions, their schemas, the validation rules and the generator (`emb-hwgen`) | One hardware model, versioned and tested with the kernel it generates for. EmbStudio shows and edits it, and never re-implements its rules |
| 2026-11-11 | **EmbStudio Core is a Python engine behind a CLI and a JSON-RPC server**; the Qt application is its face | The RTOS's tools are Python already. Everything stays scriptable and testable without a GUI |
| 2026-11-11 | **Open core under Apache-2.0**, with a **contributor licence agreement**; paid "Pro" features possible later | The same licence as the RTOS; the agreement keeps commercial options open (§10) |
| 2026-11-11 | **No VS Code extension** for now | EmbStudio is the RTOS's own IDE |
| 2026-11-11 | Docking: **Qt Advanced Docking System**, linked dynamically | §11 q1 |
| 2026-11-11 | **Qt Widgets** for the frame, docks, tables, forms and the editor; **Qt Quick** for the drawn views | §11 q2 |
| 2026-11-11 | **Installers:** signed MSI (WiX), notarized universal DMG, AppImage; a signed release feed for updates | §11 q3 |
| 2026-11-11 | **The core ships with a bundled, relocatable CPython**, not PyInstaller | §11 q4 |
| 2026-11-11 | **S5 (debugging) comes before S6 (modules)** | §11 q5 |
| 2026-11-11 | **The Pro tier is decided after S7**; nothing Pro-only until then | §11 q6 |
| 2026-11-11 | **A trademark search** (classes 9 and 42) before any public announcement; the repository stays private until then | §11 q7 |
| 2026-11-11 | **Platforms:** Windows 10 22H2 and 11, macOS 13 or later (universal), Ubuntu 22.04 and 24.04 | §11 q8 |
| 2026-11-11 | **The repository is `EmbLinkOs/EmbStudio`** on GitHub | §11 q9 |
| 2026-11-11 | **Semantic versioning**, with the supported RTOS range stated in each release | §11 q10 |

## 3. The ecosystem around EmbStudio

EmbStudio is one new repository. It drives three groups of code it does not own, and none of them knows about EmbStudio.

```text
+--------------------------- EmbStudio repository (EmbStudio developer) ---------------------------+
|  Desktop application                 JSON-RPC, stdio            EmbStudio Core                  |
|  Qt 6, C++17                   <-------------------------->     Python 3.11, embstudio-core     |
|  views, editor, docking                    DAP                  projects, build, flash, debug   |
|  DAP client; only shows and edits  <---------------------->     every action is also a CLI      |
+---------------------------------------------------------------------------+--------------------+
                                                                            | runs and reads
             +------------------------------------+-------------------------+---------+
             v                                    v                                   v
  EmbLinkRTOS (owner)                 EmbLinkRTOS-parts (S6)              Toolchains and tools
  emb-hwgen: schemas, rules,          owner, then contributors            third party; EmbCC is the owner's
  generation, hw.json                 part drivers, bindings              GCC, Clang, EmbCC
  emb CLI, crashdec, HIL              application blocks                  GDB, OpenOCD, pyOCD
  kernel, CMake package               project templates                   picotool, J-Link, EmbSIM, QEMU
```

The core runs the generator from the project's pinned RTOS revision, never a bundled copy (§4).

| Repository | Owns | Who |
| --- | --- | --- |
| **EmbStudio** (new) | The Qt application; EmbStudio Core; the protocol schemas; the schemas of the project manifest, the lock file and the module manifest; EmbStudio's tests and installers | EmbStudio developer |
| **EmbLinkRTOS** | The kernel, the on-chip drivers, the hardware description schemas (SPEC-014), the generator, the `emb` CLI, crashdec, the HIL runner and the support matrix | Owner |
| **EmbLinkRTOS-parts** (new, later) | External part drivers and bindings, application blocks, project templates | Owner, then contributors |
| **EmbCC** | The compiler | Owner |

The R-004 design record in EmbLinkRTOS explains the module contract and the configurator in more depth (§14). This specification takes precedence where they differ on EmbStudio. In particular, R-004's phase E4, a VS Code extension, is replaced by EmbStudio.

## 4. Architecture

Three layers. The rule that matters most: **all knowledge lives in the core or the RTOS's tools; the Qt application only shows and edits.**

| Layer | Language | Responsibility | Must not |
| --- | --- | --- | --- |
| **Desktop application** | C++17 or later, Qt 6 (§9) | Windows, docking, views, the editor, the debugger interface, settings, installers | Validate hardware, compute clocks, generate code, or parse the descriptions itself |
| **EmbStudio Core** | Python 3.11 or later | Projects, modules and the lock file, toolchains, build, flash, monitor, debug sessions, simulation; the editing model of the configuration; calls into the RTOS's tools | Re-implement the RTOS's rules: it runs `emb-hwgen` and reads its outputs |
| **The RTOS's tools** | Python, C | Descriptions and schemas, validation, generation, `hw.json`, reports, crash decoding, test runs | Know anything about EmbStudio |

**The core's executable is `embstudio-core`**, and its server is `embstudio-core serve`. v0.1 called the server `emb serve`, but the `emb` CLI belongs to EmbLinkRTOS (§3, M4.3), so two repositories would have shipped the same command. Every RPC method also has a CLI form, such as `embstudio-core project validate`. The core calls the RTOS's `emb` and `emb-hwgen`, and never the other way round.

**The core runs the project's own RTOS tools.** A project pins an RTOS revision (§6), so the core runs the `emb-hwgen` from that pinned checkout, as a subprocess with JSON output. It never uses a copy bundled with EmbStudio. Then a project built in the IDE and on the command line uses the same generator, and one EmbStudio release can serve projects on different RTOS versions. The core states which range of the `hw.json` and diagnostics formats it supports, and refuses a project outside that range with a clear message.

**Process model.**

- The desktop starts `embstudio-core serve` as a child process. They talk JSON-RPC 2.0 over standard input and output, framed with `Content-Length` headers as in the Language Server Protocol. Standard error carries the core's own log, never protocol traffic.
- One core process serves one open workspace. It can work on several requests at once: a build runs while the views keep asking for data.
- If the core exits unexpectedly, the desktop shows that, keeps the views read-only, and offers a restart, which reopens the project. Nothing is lost, since all state is on disk (§6).
- **Debugging** uses the Debug Adapter Protocol (DAP). `debug.start` launches GDB's own DAP interpreter (`gdb -i=dap`, GDB 14 or later, built with Python) against the probe's GDB server (OpenOCD, pyOCD or J-Link). The core sits between them as a thin DAP proxy, and adds RTOS-aware requests (threads, stacks, devices) by reading the kernel's debug descriptor through `readMemory`. The debugger interface is written once, whatever the probe.

**Why it is split this way:**

- Every action is also a CLI command, so CI, scripts and tests do what the IDE does.
- The core can be tested without a GUI, and the GUI can be tested against a fake core.
- A future frontend (a VS Code extension, a web view) needs no new logic.

## 5. The core's interface (JSON-RPC)

The interface is versioned and described by schemas in the EmbStudio repository: one JSON Schema per method's parameters and result, under `protocol/`. Both sides are tested against them. The method list below is the first version to agree, not the final one.

**Lifecycle.**

1. `initialize`: the desktop sends its `protocol_version` (semantic versioning) and capabilities; the core answers with its own and its version. The desktop refuses a core of another major version.
2. `initialized`: a notification from the desktop; after it, the core may send notifications.
3. `shutdown`: the core finishes or cancels its work and answers.
4. `exit`: a notification; the core exits with code 0 after `shutdown`, and 1 without it.

**Methods (first version):**

| Namespace | Methods | Notes |
| --- | --- | --- |
| `project` | `create`, `open`, `trust`, `validate`, `close`, `status` | `validate` returns every diagnostic with its file, line and path in the YAML. `trust`: see §9, Security |
| `board` | `list`, `describe`, `pins`, `clocks`, `memory` | From the generator's resolved model (`hw.json`); nothing computed in the GUI |
| `config` | `get`, `set`, `preview`, `apply`, `undo`, `redo` | Edits to the YAML through the core. `preview` returns the change as a diff and its diagnostics before anything is written. `undo` and `redo` step through applied changes (§6) |
| `devices` | `list`, `describe`, `add`, `remove` | The board's devices: their class, binding fields, DMA channels and chip selects |
| `rtos` | `threads`, `objects`, `budget` | `system.yaml`'s threads and objects; the RAM, flash and stack budget from the generator's report |
| `modules` | `list`, `search`, `add`, `update`, `remove` | Writes `emb-project.yaml` and the lock file |
| `generate` | `run`, `diff` | Runs `emb-hwgen`; `diff` shows which generated files would change |
| `build` | `configure`, `run`, `clean`, `targets` | CMake with the chosen toolchain and build configuration (§6); diagnostics as structured messages |
| `toolchain` | `list`, `detect`, `install` | Discovers GCC, Clang and EmbCC; downloads pinned versions and checks their checksums |
| `flash` | `probes`, `run` | OpenOCD, pyOCD or picotool, chosen by the board's `debug_probe` |
| `monitor` | `open`, `close`, `send` | Serial console; later the RTOS's framed channels (logs, trace, crash) |
| `debug` | `start`, `stop` | `start` returns the DAP endpoint the desktop connects to (§4) |
| `crash` | `decode` | crashdec on a crash record or a log |
| `evidence` | `runs`, `matrix` | Hardware runs and the support matrix for the boards used |
| `sim` | `start`, `stop` | EmbSIM or QEMU for the board, where supported |

**Notifications** from the core: `progress` (long operations), `diagnostics` (a file's problems changed), `log` (output of a tool it runs), `files_changed` (the project changed on disk).

**Long operations** (build, flash, generate, toolchain install) take a `progress_token`. The core reports `progress` against it, and the desktop can cancel with `$/cancelRequest`, as in LSP. A cancelled request answers with the error `cancelled`.

**Errors** use JSON-RPC's error object. `code` is one of the table below, `message` is written for the user, and `data` holds the tool output that caused it and, where it applies, the file and line.

| Code | Name | When |
| --- | --- | --- |
| -32700 to -32600 | JSON-RPC's own | Malformed message, unknown method, bad parameters |
| 1001 | `not_initialized` | A call before `initialize` |
| 1002 | `no_project` | A project call with no project open |
| 1003 | `untrusted` | An action that runs project code before `project.trust` |
| 1004 | `cancelled` | The request was cancelled |
| 1010 | `tool_failed` | A tool the core ran (generator, CMake, probe) failed; its output is in `data` |
| 1011 | `tool_missing` | A tool or toolchain is not installed |
| 1020 | `conflict` | The file changed on disk since the core read it (§6) |
| 1030 | `unsupported_rtos` | The project's RTOS revision is outside the range the core supports (§4) |

## 6. The project

A project is a directory under version control, and **everything EmbStudio shows comes from its files.** The only state kept outside them is window layout, preferences and undo history, in `.embstudio/`, which is not committed.

```text
my-project/
  emb-project.yaml      the project: RTOS version, modules, board, toolchain
  emb-project.lock      exact commits and hashes of the RTOS and the modules (written by the core)
  system.yaml           threads, objects, parts' use, blocks (SPEC-014 section 5)
  boards/               only for a board of the project's own (laid out as A76 says)
  src/  include/        the application's code
  generated/            the generator's outputs: never edited, regenerated
  CMakeLists.txt        builds against the RTOS's CMake package
  CMakePresets.json     the build configurations (debug, release), shared by the IDE and the CLI
  build/                build trees, one per configuration (ignored by git)
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
- If the configuration views need a board split into smaller files (pins, clocks, devices), the RTOS adds an `include:` to its schema. EmbStudio asks for it, and does not add a format of its own.
- Generated files are only regenerated, never edited. User code calls the names the generator writes.
- A project created by EmbStudio builds from the command line with the same result. Build configurations live in `CMakePresets.json`, so `cmake --preset debug` gives the IDE's debug build.

**Editing YAML that people also write by hand:**

- **Round-trip.** The core edits YAML with a round-trip parser (ruamel.yaml), so comments, key order, quoting and blank lines survive an edit. An edit changes only the lines it must, and a change made in the IDE reads in `git diff` as if a person had made it. Golden tests check this (§9).
- **Edits from outside.** The core watches the project's files and sends `files_changed` when one changes. Each `config.apply` carries the content hash of the file it was previewed against. If the file changed in between, the core refuses with `conflict`, and the desktop re-reads it and shows the new state. An edit made in another editor is never overwritten silently.
- **Undo.** Each `config.apply` is one step in an undo history kept by the core, per workspace and per session, and stored in `.embstudio/`. An undo is itself previewed and applied, with the same conflict check. Git remains the history that matters.

## 7. What it looks like: the views

The design is the developer's, within three principles:

- The RTOS is visible everywhere.
- Every number shown comes from the core, with its source one click away.
- A change shows its effect (diagnostics, budget) before it is applied.

| View | Shows and does | From |
| --- | --- | --- |
| **Start** | New project from a board or a template; recent projects | `project`, `board.list` |
| **Project** | The project's files, modules and board; its status (valid, generated, built) | `project.status` |
| **Problems** | Every diagnostic in one list (validation, generation, build), each linked to its file and line | `diagnostics` notifications |
| **Board** | The chip and the board: memory map, peripherals, the board's devices | `board.describe`, `devices.list` |
| **Pin map** | The package drawn from the SoC's pin data: each pin's function, conflicts highlighted; assign by click | `board.pins`, `config.set` |
| **Clock tree** | Oscillators, PLL and buses, with the frequencies the generator computed and the limits marked | `board.clocks` |
| **Devices and DMA** | Each device's class, binding fields, chip selects and DMA channels; add a part from a module | `devices.*` |
| **RTOS** | Threads (priority, stack, entry), objects, priority ceilings; a budget bar for RAM, flash and stack | `rtos.*` |
| **Memory and partitions** | Regions, flash partitions, and how the image uses them after a build | `board.memory`, the build report |
| **Build** | Output, size per component, warnings | `build.*` |
| **Flash and console** | Probe, flash, the serial console | `flash.*`, `monitor.*` |
| **Debugger** | Breakpoints, stepping, registers, memory, **RTOS threads and their states, stack high-water marks, devices and their lifecycle** | DAP; the RTOS's debug descriptor (A107) |
| **Crash report** | A decoded crash record: fault, thread, location, registers | `crash.decode` |
| **Evidence** | This board's hardware runs, passed or failed; the support matrix | `evidence.*` |
| **Settings** | Toolchains, probes, the core's log, preferences | `toolchain.*`, `.embstudio/` |
| **Trace and timing** (later) | Thread timeline, interrupt latency, power residency | The RTOS's trace (M4.3), timing tools |

**The editor** grows in three steps:

1. Syntax highlighting (KSyntaxHighlighting, MIT) and an "open in your editor" action.
2. C and C++ intelligence through a language-server client talking to clangd (completion, errors, go to definition), using the build's `compile_commands.json`.
3. Search, refactoring, git.

## 8. The plan: stages

Ten stages, each ending in something usable and demonstrated. S0 has started. Acceptance is checked in CI where possible. Where a check needs real hardware, CI runs it on EmbSIM and a person runs it on the board, with the result recorded as evidence.

| Stage | Delivers | Accepted when | Needs from the RTOS | Status |
| --- | --- | --- | --- | --- |
| **S0 Setup** | The EmbStudio repository, its licence files and contributor agreement, CI on Windows, Linux and macOS, a Qt application that starts and talks to a stub core | CI builds and tests on all three systems; `initialize` and `shutdown` succeed; the notices list is generated | Nothing | In progress |
| **S1 Core foundation** | `embstudio-core serve`, `project.open` and `validate`, `board.*` from `hw.json`, `generate.run` | Opening the RTOS's `nucleo_f446re` and `pico2_w` boards shows their model; validation reports the generator's errors with their locations | JSON diagnostics from `emb-hwgen`; a versioned `hw.json` | Not started |
| **S2 Read-only views** | Board, pin map, clock tree, devices, memory, RTOS, problems | For both boards, a golden test finds every value in the views equal to `hw.json` and to the generator's `board.md` and `build_report.md` | Existing outputs | Not started |
| **S3 Editing** | `config.preview`, `apply`, `undo`; pins, devices and threads editable; live diagnostics | A pin conflict is refused with the generator's message; an edit's `git diff` touches only the lines it must; generation stays clean | `system.yaml` (E3) for threads | Not started |
| **S4 Build, flash, console** | Toolchain discovery, CMake build, flash by OpenOCD and picotool, serial console | A project is created, built, flashed and seen running on the NUCLEO-F446RE and the Pico 2 W; in CI, on EmbSIM's `nucleo_f446re` | The CMake package (A91) | Not started |
| **S5 Debugging** | DAP sessions; the RTOS-aware views; crash decoding | Breakpoints and stepping on both boards; threads and their states shown; a crash record decoded | The debug descriptor (A107), crashdec | Not started |
| **S6 Modules and parts** | `modules.*`, the parts catalog, adding a part as a device | A BME280 added from the parts module is generated, built and read on a board | The module contract and lock file (E1); EmbLinkRTOS-parts | Not started |
| **S7 Evidence and trace** | The evidence view; the trace and timing views as the RTOS provides the data | The board's hardware runs listed; a trace shown | HIL records; the trace ring (M4.3) | Not started |
| **S8 The editor** | clangd integration, search, git | Daily work is possible without another editor | Nothing | Not started |
| **S9 Release** | Signed installers (Windows MSI, macOS DMG, Linux AppImage); updates; the first public release | Installs cleanly on a new machine and opens the examples | A tagged RTOS release | Not started |

**What the RTOS side provides, and when:**

| EmbStudio needs | Status in EmbLinkRTOS | First needed |
| --- | --- | --- |
| Machine-readable diagnostics from the generator (JSON) | To add, early | S1 |
| A version field in `hw.json` and a published schema for it | To add, early | S1 |
| `hw.json` and the generated outputs for each board | Exists | S1 |
| `emb-hwgen generate` and `check` | Exists | S1 |
| The CMake package for applications outside the tree | Exists (A91) | S4 |
| crashdec, the HIL runner's records, the support matrix | Exist | S5, S7 |
| The debug descriptor and `emb devices` | Exist (A107) | S5 |
| The `emb` CLI | M4.3 | S4 |
| `system.yaml` generation: threads, objects, `emb_system_init()` | Phase E3 | S3 |
| The module contract, the project manifest and the lock file | Phase E1 (R-004) | S6 |
| The trace ring and its format | M4.3 | S7 |
| `include:` in the board schema, if the views need it | On request | S3 |

Until the module contract and `system.yaml` exist, S1 to S3 work on what exists: the boards in the RTOS tree and their generated model. Threads become editable in S3 only once E3 lands; until then the RTOS view stays read-only. The core's project and module schemas are designed with the owner, and the RTOS adopts them in phase E1.

## 9. Engineering rules

- **Qt:** Qt 6.8 LTS, under the LGPL-3.0, **linked dynamically**, with no GPL-only module (Qt Charts, Qt Graphs, Qt Virtual Keyboard and Qt Creator's code are GPL or commercial). Check which 6.8 patch releases are published under the LGPL. If later LTS patches are commercial only, track the newest 6.x minor release instead. The installers ship Qt as replaceable shared libraries (frameworks on macOS), with the LGPL text and a link to Qt's sources, as the LGPL asks.
- **Widgets and Quick:** Qt Widgets for the frame, docking, tables and forms. Qt Quick, in `QQuickWidget`s, for the drawn views: pin map, clock tree, and later the trace timeline (§11).
- **Third-party libraries:** only under licences compatible with the open core: MIT, BSD, Apache-2.0, LGPL linked dynamically, MPL-2.0. **No GPL libraries** in the application. Some terminal and docking widgets are GPL, so check each one. GPL tools (GDB, OpenOCD) run as separate programs and are never linked. Toolchains are downloaded from their publishers, not bundled.
- **A notices list** of every component and its licence, C++ and Python alike, generated in CI and shown in the About box. CI fails on a licence that is not on the allowed list.
- **Tests:** Qt Test for the application; pytest for the core; JSON Schema checks of every message in both directions; golden projects (the RTOS's boards) whose generated outputs, YAML edits and builds are compared in CI; the GUI tested against a recorded core.
- **Reproducibility:** pinned dependencies, pinned toolchains with checksums, the lock file. The same project gives the same build in the IDE and on the command line.
- **Code:** clang-format for C++, ruff for Python; every commit signed off (`Signed-off-by`, the DCO).
- **Accessibility and languages:** keyboard navigation throughout; Qt's translation system from the start, even with only English at first.
- **Privacy:** no telemetry by default. Anything collected later is opt-in.

**Security.** Opening a project can run code: CMake scripts, module hooks, the project's pinned generator.

- **Trust.** A project opened for the first time is untrusted. The views read its files, but nothing in it runs (generate, build, flash, debug answer `untrusted`) until the person trusts it once with `project.trust`. Trust is remembered per path in the user's own settings, never in `.embstudio/`, so a project cannot ship its own trust.
- **Downloads.** Toolchains, and the bundled Python if §11 chooses one, are checked against SHA-256 sums pinned in the core's release. A mismatch is refused, never warned.
- **Modules** are fetched at the exact commits in the lock file, and a hash mismatch stops the build.
- **No listening sockets.** The core talks over standard input and output only. A DAP endpoint listens on the loopback address, with a one-time token.

## 10. Licence, ownership and the name

- **Licence:** Apache-2.0 for everything in the open core: the application, the core and the schemas. The repository carries `LICENSE` and a `NOTICE` file, as Apache-2.0 expects.
- **A commercial "Pro" tier** may come later. Which features it holds is the owner's decision. Candidates are advanced trace and timing analysis, power profiling, certification-report generation, and team features. The open core must stay complete for everyday work.
- **Ownership.** Every contributor, the EmbStudio developer included, signs the **contributor licence agreement** before their first contribution is merged. It lets the project offer the code under other terms later, the Pro tier included. A lawyer checks it before anyone signs; the Apache Software Foundation's individual agreement is a common model. A CLA bot on pull requests records who has signed.
- **The CLA and the DCO do different jobs.** The CLA grants the project rights to the code, once per contributor. The DCO sign-off on each commit says the author may submit it. Both stay.
- **Owner and developer.** They agree in writing on credit and on any future revenue from EmbStudio **before the developer's first merge in S0**, since most of the early code is the developer's.
- **The name.** "EmbStudio", "EmbLink" and "EmbLinkRTOS" are the project's names. A trademark policy says forks may use the code but not the names. Before the repository is announced in public, a trademark search checks "EmbStudio" against SEGGER Embedded Studio and others in the classes for software (class 9) and software services (class 42).

## 11. Questions settled on 2026-11-11

All ten questions are settled: the owner accepted each recommended answer on 2026-11-11, and §2 records them. Questions 1 to 7 came from v0.1, and 8 to 10 are new. This table keeps the reasoning.

| # | Question | Recommended answer | Why | Decision |
| --- | --- | --- | --- | --- |
| 1 | The docking framework | **Qt Advanced Docking System** (LGPL-2.1, linked dynamically) | Tabbed, floating and split docks, and saved layouts, which Qt's own `QDockWidget` lacks. KDDockWidgets is GPL or commercial. If ADS ever fails, fall back to `QDockWidget` | Decided |
| 2 | Where Qt Widgets and Qt Quick are used | Widgets for the frame, docks, tables, forms and the editor; Quick in `QQuickWidget` for the pin map, clock tree and trace timeline | Widgets are best at dense tool UIs and text. Quick is best at drawn, zoomable, animated views | Decided |
| 3 | Installers, updates, signing | Windows: MSI built with WiX, Authenticode-signed. macOS: a universal DMG, Developer ID signed and notarized. Linux: AppImage built with linuxdeploy. Updates: an in-app check against a signed release feed at first; Sparkle and WinSparkle (both MIT) later | Standard per platform. Signing needs paid certificates (an Apple developer account, a Windows code-signing certificate), which is the owner's cost to approve | Decided |
| 4 | How the core is packaged | **A bundled, relocatable CPython** (python-build-standalone builds) with the core in a private environment, not PyInstaller | The core runs the project's pinned `emb-hwgen` (§4), and that needs a real interpreter. A frozen PyInstaller program has none. A system Python varies by machine and breaks reproducibility. Each RTOS revision's tool dependencies go in an environment keyed by the lock file's hash | Decided |
| 5 | Which comes first after S2: debugging (S5) or modules (S6) | **S5, debugging** | What it needs from the RTOS exists today (A107, crashdec). S6 waits for the module contract (E1) | Decided |
| 6 | The Pro tier: which features, and when | Decide after S7. Until then, write nothing Pro-only, and keep trace, power and reporting as separable modules | By S7 it is clear what everyday work needs, and so what must stay open | Decided |
| 7 | The trademark check on "EmbStudio" | A search in classes 9 and 42 before the repository is announced, by a trademark attorney if the budget allows | A rename after release costs far more than one before | Decided |
| 8 | Supported platforms | Windows 10 22H2 and 11 (x64); macOS 13 or later (Apple silicon and Intel, one universal build); Ubuntu 22.04 and 24.04 (x64). Arm64 Windows and Linux later | Within Qt 6.8's supported platforms, and what CI's hosted runners provide | Decided |
| 9 | Where the repository lives | `EmbLinkOs/EmbStudio` on GitHub, beside the RTOS | One organisation and one CI system; issues can link across repositories | Decided |
| 10 | Versioning and RTOS compatibility | EmbStudio uses semantic versioning. Each release states the RTOS revisions it supports (§4), and the evidence view shows that range | Users must be able to tell whether a project will open before they open it | Decided |

## 12. Changes from v0.1

The review of v0.1 found 14 gaps. Four could have caused rework or a wrong build: the `emb` name clash, which generator runs, YAML round-tripping, and project trust. The rest were missing detail.

| Gap in v0.1 | Effect if left | Fix in v0.2 | Where |
| --- | --- | --- | --- |
| The core's server was `emb serve`, but the `emb` CLI belongs to EmbLinkRTOS | Two repositories shipping one command | The core is `embstudio-core`, with `serve` | §4 |
| Which `emb-hwgen` the core runs was not said | A bundled generator would differ from the project's pinned RTOS, and IDE builds would differ from CLI builds | The core runs the project's pinned RTOS tools, within a stated range | §4, §11 q4 |
| No rule for YAML comments and formatting on edit | IDE edits would rewrite whole files, with noisy diffs and lost comments | Round-trip editing (ruamel.yaml) and golden diff tests | §6, §9 |
| Opening a project could run its code with no consent | A cloned project runs CMake or module code on open | Project trust and the `untrusted` error | §9 |
| Edits made outside the IDE were not handled | An IDE edit overwrites a change made in another editor | A content hash on `apply` and the `conflict` error | §5, §6 |
| No undo | Editing views without undo | `config.undo` and `redo` | §5, §6 |
| No cancellation, lifecycle or error codes | Builds that can't be stopped; each side guessing at errors | `$/cancelRequest`, `shutdown`/`exit`, an error table | §5 |
| No schema for the protocol | The C++ and Python sides drift apart | JSON Schemas in `protocol/`, tested on both sides | §5 |
| No debug adapter was named | The developer would have to pick or write one | GDB's DAP interpreter behind a thin core proxy | §4 |
| No behaviour when the core crashes | A frozen or confusing UI | Read-only views and a restart that reopens the project | §4 |
| No build configurations | Debug and release builds set by hand | `CMakePresets.json`, shared with the CLI | §6 |
| Hardware checks in S4 and S5 can't run in CI | Acceptance claims with no evidence | EmbSIM in CI, and board runs recorded as evidence | §8 |
| Qt 6.8 LTS patches may be commercial only for LGPL users | Unpatched Qt in releases | A check, and a fallback to the newest 6.x minor | §9 |
| References (A76, A91, A107, M4.3, E1, E3) were unexplained | A new developer can't follow them | Added to §13 | §13 |

Smaller changes: S2's acceptance is now an automated golden test, each stage lists what it needs from the RTOS, the Problems and Settings views were added, and Python dependencies are in the notices list.

## 13. Terms used in the RTOS

| Term | Meaning |
| --- | --- |
| SoC description | A YAML file describing a chip: memory, peripherals, interrupts, clocks, pins (`soc/`) |
| Board description | A YAML file describing a board: its chip, oscillators, pins, console, devices, partitions (`boards/<vendor>/<board>/board.yaml`) |
| System description | `system.yaml`: what a product does with the board: threads, objects, partitions |
| `emb-hwgen` | The generator: validates the descriptions and writes `generated/`, including `hw.json` (the resolved model, the core's main input). Lives in `hw/gen/` |
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
| A*n* | An accepted amendment to the specs, listed in `docs/specs/AMENDMENTS-M*.md` |
| A76 | Boards outside the RTOS tree, found through `EMB_BOARD_ROOT` (SPEC-014) |
| A91 | Applications outside the tree: the RTOS's CMake package (`emb_add_executable()`) |
| A107 | The debug descriptor, version 1 (SPEC-015 §5), and `emb_gdb.py`, which lists threads and walks their stacks |
| M4.*n* | A milestone of the RTOS roadmap's phase M4 (`docs/architecture/07-roadmap.md`) |
| M4.3 | The milestone that brings the `emb` CLI and the trace ring (CTF) |
| E1 to E5 | The ecosystem phases of R-004. E1: the module contract, project manifest and lock file. E3: the configurator engine, generating `system.yaml`'s threads and objects with `emb_system_init()`. E2, E4 and E5 cover parts, the VS Code extension (now replaced by EmbStudio) and RTOS-aware debugging |

## 14. Where to read further, in EmbLinkRTOS

- `docs/specs/SPEC-014-hardware-description-schema.md`: the descriptions and the generator's outputs.
- `docs/specs/SPEC-016-device-and-driver-model.md` and `SPEC-017-driver-classes.md`: devices and classes.
- `docs/specs/SPEC-015-observability-formats.md` and `docs/engineering/debug-descriptor.md`: the debug descriptor and the crash record.
- `docs/specs/AMENDMENTS-M3.md`: A76, A91 and A107 in full.
- `hw/gen/`, `hw/schemas/`: the generator and its schemas.
- `boards/st/nucleo_f446re/`, `boards/rpi/pico2_w/`: two real boards and their `generated/` outputs.
- `tools/crashdec/`, `tools/descriptor/` (with `emb_gdb.py`, the base for the debugger's RTOS views), `tools/hil/`, `tools/support/`: the tools EmbStudio drives.
- `docs/research/R-004-ecosystem-addons-configurator-integrations.md`: modules, parts and the configurator. It is on the `docs/ecosystem-design` branch for now. This specification takes precedence where they differ on EmbStudio.
