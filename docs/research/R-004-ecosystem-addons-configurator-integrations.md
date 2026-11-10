# R-004 The ecosystem: add-ons, the configurator, and integrations

**Status:** draft for the project owner's design session, 2026-11-10. Not normative: the decisions it proposes become ADRs (§11) once the session settles them.

**The question.** The owner wants three things beyond the kernel:
1. A separate add-on with drivers for external parts (sensors, memories, displays) and application code built from them.
2. Code generation like Microchip's MCC or ST's CubeMX, but our own and only for EmbLinkRTOS: a user picks the board, the parts and the features, and gets a project.
3. EmbLinkRTOS working with the platforms and tools developers already use.

This record proposes how many repositories there should be and what each holds, how they link, what the configurator is, and which integrations come first.

## 1. What already exists to build on

| Piece | Where | What it gives |
|---|---|---|
| Hardware as data | SPEC-014: SoC, board and system descriptions in YAML, validated by JSON Schema | One source of truth the configurator can read and write |
| The generator | `hw/gen/` (`emb-hwgen`) | Resolved models, `hw_config.h`, `soc_devices.c/.h`, the linker script, `hw.json`, reports; deterministic and checked |
| Driver bindings | `drivers/bindings.yaml` (M4.1) | A compatible (`vendor,part`) names its driver, class and configuration fields |
| Board devices | board `devices:`, with `bus:`, `cs:` and `dma:` (SPEC-014 §4) | A part on a bus is already a device of the description |
| Boards outside the tree | `EMB_BOARD_ROOT` (A76) | The model for everything else outside the tree |
| Applications outside the tree | the CMake package, `find_package(EmbLinkRTOS)` (A91) | A user's project builds against an installed RTOS |
| The system description | `system.yaml` (SPEC-014 §5): threads, objects, partitions | Specified, not yet generated: the configurator's core |
| Debug and trace data | the debug descriptor (A107), `emb_gdb.py`, CTF planned (M4.3) | What debuggers and trace tools read |

So the configurator is mostly a matter of finishing `system.yaml`'s generation and letting descriptions, bindings and drivers come from outside the tree.

## 2. Decision 1: how many repositories

| Option | Layout | For | Against |
|---|---|---|---|
| A | One repository: the kernel, on-chip drivers and every part driver | One place; one CI | Every sensor driver lands in the kernel's evidence and review scope; the safety story gets diluted; contributions to parts wait on kernel review |
| B | The RTOS, plus one add-on holding parts, templates **and** the generator for them | One add-on to install | Two generators that drift apart. The add-on's generation would need kernel facts (storage sizes, stack bounds, init order) it can only reach through internals |
| C | The RTOS with the generation **engine**, plus a parts repository, plus an editor extension repository later | Each changes at its own pace; generation is tested with the kernel it generates for; parts can be contributed quickly | Three repositories to keep in step: needs a version contract (§3) |
| D | Many repositories, one per vendor or family of parts | Fine-grained ownership | Too much for one maintainer; users must find and pin many |

**Proposed: C, introduced over time.**

1. **`EmbLinkRTOS`** (exists): the kernel, the on-chip drivers, the driver classes, the generator **including the configurator engine**, the schemas and the `emb` CLI. Generated code is kernel code in all but name: objects, stacks, priority ceilings, init order. It must be versioned and tested with the kernel, and covered by the same evidence pipeline.
2. **`EmbLinkRTOS-parts`** (new): drivers for external parts and their bindings; application **blocks** (§5) and project templates; examples; EmbSIM wiring for the parts it models; its own CI and support matrix. It grows fast, takes community contributions with a lighter review, and stays outside the kernel's evidence and certification scope.
3. **`EmbLinkRTOS-vscode`** (later): the editor extension, the configurator's graphical face. It is TypeScript, released through the marketplace at its own pace, and holds no logic of its own: it drives the CLI and shows what the generator reports.

To a user it still feels like one product with an add-on: they install the RTOS, add the parts module, and the configurator offers its parts and blocks.

## 3. Decision 2: how they link, the module contract

**A module** is any directory with an `emb-module.yaml` at its root. The parts repository is a module, a user's own project can be one, and a vendor or a company could publish one.

```yaml
schema_version: 0
kind: module
name: emblink-parts
version: 0.1.0
requires: { emblinkrtos: ">=0.3, <0.4" }  # refused outside this range
provides:
  bindings: bindings.yaml   # compatibles for its parts
  drivers: drivers/          # the sources its bindings name
  include: include/
  boards: boards/            # optional: boards in A76's layout
  blocks: blocks/            # application blocks (section 5)
  templates: templates/      # project templates for `emb new`
  kconfig: Kconfig           # options for its parts and blocks
  embsim: embsim/            # wiring of EmbSIM's part models, for its CI
```

**A project manifest**, `emb-project.yaml` in the user's application, pins the RTOS and the modules:

```yaml
schema_version: 0
kind: project
rtos: { url: https://github.com/EmbLinkOs/EmbLinkRTOS, rev: v0.3.0 }
modules:
  - { name: emblink-parts, url: https://github.com/EmbLinkOs/EmbLinkRTOS-parts, rev: v0.1.0 }
  - { name: mine, path: ./parts }   # a module of the project's own
board: st/nucleo_f446re
system: system.yaml
```

**How the pieces meet:**
- **Fetching.** `emb fetch` clones each entry at its revision into `.emb/` and writes `emb-project.lock` with the exact commits and hashes, so a build is reproducible. Users who prefer it can use git submodules or CMake's FetchContent instead; the lock file is the contract either way.
- **The generator** takes a list of module roots (`--module`, CMake `EMB_MODULES`). Bindings merge in order: the RTOS's first, then each module's. A module may not redefine a compatible the RTOS or another module defines; that is an error, never a silent override. Boards come through A76's search path, extended to modules' `boards/`.
- **The build.** The generator already writes the driver sources a board's devices need (`EMB_BOARD_DRIVER_SOURCES`). Module drivers join that list, so only the parts a system uses are compiled.
- **Kconfig.** Each module's `Kconfig` is sourced under a "Modules" menu.
- **Versions.** The generator checks `requires` against the RTOS's version and refuses a mismatch with a message naming both. The parts repository's CI builds and runs against the RTOS's latest release and its main branch.

**The rules that keep modules safe:**
- A module uses only the public API: `include/emb/` and the driver-class interfaces. It never includes `kernel/include/embk/`. The generator and CI check this.
- The driver-class interfaces (`emb_drv_*_api_t`) are today `@stable no`. Before the parts repository depends on them, the classes it uses must be frozen, with the same rule as the 1.0 API: they only grow (§10, question 7).
- A module cannot change kernel behaviour. Its Kconfig options are its own, prefixed with its name.

## 4. The configurator: our MCC

**What MCC and CubeMX do well:** one place to choose the part, the pins, the clocks and the peripherals; validation as you go; a project that builds at once.

**What users dislike:** code regenerated over their edits, or kept apart by `USER CODE BEGIN` comments; generated code that is hard to read and review; lock-in to one IDE; configuration hidden in a binary project file.

**Proposed design:**
- **One source of truth: YAML, in the user's repository.** The board description holds what is on the board, soldered or wired, including external parts:
  ```yaml
  devices:
    - { name: env, compatible: "bosch,bme280", bus: i2c1, address: 0x76 }
  ```
  `system.yaml` holds what the product does: threads, objects, partitions, and the blocks (§5) it uses:
  ```yaml
  blocks:
    - { use: periodic_sampler, part: env, period: 1s, to: samples }
  kernel_partition:
    threads: [ { name: main, entry: app_main, priority: 10, stack: 2K } ]
    objects: [ { name: samples, type: msgq, item_size: 16, capacity: 8 } ]
  ```
- **Generated data and calls, not generated logic.** The generator writes tables (device configurations, thread and object descriptors, block parameters) and one `emb_system_init()` that creates the objects and starts the threads and blocks in order. The logic lives in ordinary C in the RTOS or the module, written once, reviewed, MISRA-checked and tested. Generated files stay in `generated/`, are never edited, and have no "user code" regions: a user's code calls the handles the generator names.
- **Checked at generation:**
  - stacks against the bounds the stack tool measures;
  - priority ceilings;
  - the RAM and flash budget;
  - pin and bus-address conflicts;
  - clock limits;
  - a block's needs (a part with the channel it samples).

  A system that does not fit is refused, with the reason, before anything is compiled.
- **The configurator's report:** a page of what was chosen and what it costs (RAM, flash, stack per thread, interrupt priorities), from the same data as `build_report.md`.
- **Three ways to use it, one engine:**
  1. the CLI: `emb new`, `emb add part`, `emb add block`, `emb gen`, `emb check`;
  2. a terminal menu for Kconfig options;
  3. the VS Code extension: a pin map, the device tree, the thread table and the budget. It edits the YAML through the CLI and keeps no state of its own.

## 5. Parts and blocks

**A sensor class.** Part drivers need a class to implement, as on-chip drivers do. SPEC-017 has none for sensors. Proposed: a **sensor class** in the RTOS, with these properties:
- A sensor has channels: temperature, humidity, pressure, acceleration on three axes, and so on.
- A reading is a request with a completion, like the other classes.
- Values are fixed point in micro-units: µ°C, µPa, µm/s². No floating point is needed, which keeps the tiny profile possible.
- Each channel has its range and resolution as capabilities.

It is the RTOS's interface, versioned with the kernel; the part drivers that implement it live in the parts repository. Other part kinds use the classes that exist:
- an SPI NOR flash (W25Q64) is a flash-class device;
- an EEPROM (24LC256) is a flash-class device with a byte write unit;
- an RTC module (DS3231) is an RTC-class device.

Displays and inputs get classes when a real application needs them.

**Blocks** are the application building blocks the configurator offers. A block is a C module with a fixed interface (a const configuration struct, an init, and a thread or a timer callback), plus a YAML description of its parameters and needs. For example:
- `periodic_sampler`: read a sensor every period into a queue;
- `data_logger`: append records with a CRC and a timestamp to a flash partition;
- `watchdog_guard`: check in each thread of the system with the FLT-004 service;
- `uart_console`: a command shell.

The field node (M4.6) is the first application built from blocks, which proves the idea on hardware.

## 6. Integrations with other platforms and tools

| Kind | Targets | Approach | Priority |
|---|---|---|---|
| Build systems | CMake (every IDE that reads it) | Exists (A91); add presets for user projects | P1, small |
| | PlatformIO | A framework package wrapping the CMake package | P2 |
| | Arduino-style libraries | Only if users ask: the model differs (no RTOS ownership of `main`) | P3 |
| | Zephyr's `west` | Not proposed: we have our own manifest (§3) | — |
| IDEs | VS Code | The extension (§2): build, flash, monitor, configurator views | P1 |
| | Keil MDK, IAR, STM32CubeIDE and Eclipse | **CMSIS-Packs**: one format opens all of them | P2 |
| | CLion | Works through CMake already | — |
| Debugging | OpenOCD, pyOCD | RTOS awareness (threads, their stacks and states in the debugger), from the debug descriptor (A107) | P1 |
| | GDB | `emb_gdb.py` exists; grow it | P1 |
| | SEGGER J-Link and Ozone | An RTOS plugin from the same descriptor | P2 |
| Trace | SEGGER SystemView, Percepio Tracealyzer | An exporter of the trace ring (M4.3) to their formats | P1 |
| | Trace Compass | CTF 1.8 (M4.3, Q12) | P1, planned |
| Simulators | QEMU, EmbSIM | Exist in CI | — |
| | Renode, Wokwi | Board files for demos in the browser and in CI | P2 |
| Vendor code | STM32 HAL and LL, pico-sdk, ESP-IDF components | A guide to using vendor code beside the RTOS (what it may touch: clocks, interrupts), with one example each | P2 |
| Middleware | lwIP, mbedTLS or wolfSSL, TinyUSB, FatFs, littlefs | The OS abstraction contract (04 §11), each as a module (§3) | M6+ |
| API compatibility | CMSIS-RTOS2 | M4.7 (planned) | P1, planned |
| | A FreeRTOS API shim | Eases migration; a module | P3 |
| | A POSIX subset | 04 §12 | P3 |
| Languages | C++ wrappers, Rust bindings | Modules over the public C API | P3 |
| Updates | MCUboot, then mcumgr/SMP | M4.5, then a module | planned |

**The rule for every integration:** it uses the same artifacts the project already publishes: the CMake package, `hw.json`, the debug descriptor, the trace format and the CLI. No tool gets a private path into the RTOS. Each integration has a check in CI where the tool can run there.

## 7. Evidence and support for modules

The parts repository keeps the RTOS's discipline at its own level:
- **Tiers per part:** 1 when an archived run with the real part passed, 2 when EmbSIM's model passed, 3 when it only builds.
- **Its own support matrix,** generated from its own archived runs, like the RTOS's.
- **Its CI** builds every part for every board family it claims, and runs the EmbSIM models.

The RTOS's own claims (requirements, evidence, any future certification scope) never extend to modules. A product that uses a module carries that module's evidence, not the kernel's.

## 8. Licensing and governance

The proposal is Apache-2.0 for the parts repository and the extension, the same as the RTOS, with the same DCO sign-off. Parts take a lighter review than the kernel: a driver, its binding, a test on EmbSIM or a part, and a tier.

**The owner's intent changes this.** "Separate, and only for our RTOS" can mean a separate open project, or a commercial add-on (§10, question 9). The repository layout and the module contract above work either way.

## 9. Where it goes in the roadmap

The configurator was already a phase after M4.2 (decided 2026-11-09). Proposed: widen it to an **ecosystem phase, E**, in steps that each deliver something usable:

| Step | Repository | Delivers |
|---|---|---|
| E1 | RTOS | The module contract: `emb-module.yaml`, `emb-project.yaml` and its lock, the generator's module search paths, `emb fetch`; checks that modules use only the public API |
| E2 | RTOS, parts | The sensor class (SPEC-017 gains a section). The parts repository starts with three parts EmbSIM models and the bench has (BME280, 24LC256, W25Q64), at tier 2 then tier 1 |
| E3 | RTOS | The configurator engine: `system.yaml`'s threads, objects, parts and blocks generated, with `emb_system_init()`, the checks and the report; the first blocks; `emb new` from a template |
| E4 | extension | VS Code v0: build, flash and monitor, then read-only views of the pins, devices, threads and budget; then editing through the CLI |
| E5 | RTOS, modules | RTOS-aware debugging (OpenOCD, pyOCD), the SystemView or Tracealyzer exporter, CMSIS-Packs, PlatformIO |

It depends on parts of M4:
- the `emb` CLI and the trace ring of M4.3 come before E1 and E5;
- the field node of M4.6 is the natural first user of E2 and E3.

A possible order: finish M4.2 (entropy), then M4.3, then E1 to E3, then M4.4 to M4.8 with E4 and E5 beside them. The owner decides.

## 10. Questions for the session

1. **Repositories.** Three as proposed (§2), or another split?
2. **Names.** `EmbLinkRTOS-parts` and `EmbLinkRTOS-vscode`, or others?
3. **Parts in the board or the system?** As proposed: what is wired is the board's (`devices:`), and what the product does with it is the system's (`blocks:`).
4. **Generated code.** Data tables and init calls only, with the logic in reviewed C (§4)? Or do some blocks need generated logic?
5. **The sensor class.** Fixed-point micro-units with requests and completions (§5)? Which channels first?
6. **Integrations.** Is the P1 list in §6 the right first set?
7. **Freezing the driver classes.** The parts repository needs stable class interfaces. Freeze the classes it uses at E2?
8. **Roadmap.** Where does phase E go: after M4.3 as suggested, or another order?
9. **Open or commercial?** Is the add-on open source like the RTOS, or a separate product (§8)?
10. **EmbStudio.** The architecture names an EmbStudio (06, M6+). Is the VS Code extension its first form, or a separate thing?

## 11. ADRs this would become

| ADR | Decision |
|---|---|
| ADR-060 | The repositories and what each holds (§2) |
| ADR-061 | The module contract and the project manifest (§3) |
| ADR-062 | The configurator: YAML as the single source; generated data and calls, never edited (§4) |
| ADR-063 | The sensor class (§5), with its SPEC-017 section |
| ADR-064 | The integration policy: public artifacts only, CI checks, priorities (§6) |
