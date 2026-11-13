# SPEC-014 - Hardware Description Schema v0 and the Generator

**Implementation and release scope:** [Project status](../STATUS.md) records the implemented subset; [roadmap](../architecture/07-roadmap.md) defines release gates. Acceptance of this design does not establish implementation, verification, or support. Reconciled 2026-10-14.

**Status:** Accepted 2026-10-07 by the project owner ("do all"), with the defaults of §13. Specification work item 13 of the roadmap (07 §3).
**Implementation:** first increment for the STM32F446 and the NUCLEO-F446RE ([hardware generator](../engineering/hwgen.md)); its paths, validator and output subset are [A66 to A68](AMENDMENTS-M3.md); boards outside the tree are [A76](AMENDMENTS-M3.md), the SVD importer [A77](AMENDMENTS-M3.md).
**Requirements:** `docs/requirements/HW.md` (HW-001 to 006 restated; new from 007).
**Builds on:** 04 §1 (pipeline, model, outputs; ADR-004 LOCKED), §2 (device instances, init order), 03 §9.1 (regions); SPEC-009 §4.1, §5 (capability tables, storage probe), SPEC-010 §2 to §4 (partition description and constraints), SPEC-011 §2 (port manifest), SPEC-012 §11 (a board file), SPEC-005 §3.4 (static ceilings), SPEC-002 §8 (generic interrupt levels), 09 §7, §9 (`embld` outputs, `embsvd`); HW-006.
**Research:** R-002 §1: Devicetree's "macrobatics" is the most repeated Zephyr complaint; R-001 §10: 19,000 lines of macros in Zephyr's devicetree layer. The schema below keeps DeviceTree's good ideas (`compatible`, references, hierarchy) in plain YAML validated by JSON Schema and turns them into plain C tables with a generator, so no macro layer exists.

---

## 1. Scope and terms

| Term | Meaning |
|---|---|
| **SoC description** | `soc/<vendor>/<family>/<part>.yaml`: everything the silicon fixes |
| **Board description** | `boards/<vendor>/<board>.yaml`: everything the PCB fixes |
| **System description** | `<app>/system.yaml`: everything the product fixes: profile, configuration, partitions, threads, objects, ceilings, shares, images |
| **Schema** | JSON Schema (draft 2020-12) files in `hw/schemas/` that validate the three description kinds; `schema_version: 0` until the hardware schema is independently released stable |
| **Generator** | `hw/gen/` (`emb-hwgen`, Python 3): validates, resolves, checks, and emits the outputs of §6 deterministically |
| **Importer** | `hw/import/`: produces curated-draft descriptions from CMSIS-SVD (`embsvd` extension) and DeviceTree source |
| **Compatible** | A `vendor,device` string binding a node to a driver, as in DeviceTree |

## 2. Common conventions

- Files are YAML 1.2; keys are `snake_case`; every file starts with `schema_version` and `kind: soc | board | system`.
- Numbers accept `0x` hexadecimal and the suffixes `K`, `M`, `G` (powers of two for sizes), `kHz`, `MHz` for frequencies; the generator normalizes to integers.
- References are `ref: <path>` where the path is `<section>.<name>` within the same file or `<file>:<section>.<name>` across files (`soc:peripherals.usart1`); every reference is resolved and checked.
- `extends: <file>` lets a part inherit a family base (`stm32f4.yaml`) and override or add nodes; the generator flattens inheritance before validation.
- Provenance: `imported_from: { tool, source, hash }` is recorded by importers and preserved; `curated: true` marks a node a human has reviewed. The generator warns on uncurated nodes and the `safety` profile attribute refuses them (ADR-035).
- Every node may carry `doc:` free text, emitted into `board.md`.

## 3. SoC schema

```yaml
schema_version: 0
kind: soc
name: stm32f407
vendor: st
family: stm32f4
extends: stm32f4.yaml
arch: { port: cortex_m, variant: armv7m }          # must match arch/<port>/arch.yaml
cpu:
  core: cortex-m4f
  revision: r0p1
  fpu: fpv4-sp-d16
  mpu: { model: armv7m, regions: 8 }
  cache: none
  trustzone: false
  cores: [ { id: 0, role: main } ]
  cycle_counter: dwt
memory:
  regions:
    - { name: flash,  base: 0x08000000, size: 1M,   attrs: [r, x] }
    - { name: sram1,  base: 0x20000000, size: 112K, attrs: [r, w, dma] }
    - { name: sram2,  base: 0x2001C000, size: 16K,  attrs: [r, w, dma] }
    - { name: ccm,    base: 0x10000000, size: 64K,  attrs: [r, w, tcm] }          # no DMA
    - { name: bkpsram, base: 0x40024000, size: 4K,  attrs: [r, w, retained] }
clocks:
  sources: [ { name: hsi, hz: 16MHz }, { name: hse, hz: board }, { name: lsi, hz: 32kHz } ]
  plls: [ { name: pll, input: [hsi, hse], m: [2, 63], n: [50, 432], p: [2, 4, 6, 8], q: [2, 15] } ]
  buses: [ { name: ahb1, max_hz: 168MHz }, { name: apb1, max_hz: 42MHz }, { name: apb2, max_hz: 84MHz } ]
  gates: { usart1: apb2, usart2: apb1, gpioa: ahb1 }
interrupts:
  controller: nvic
  count: 82
  priority_bits: 4
  systick: true
peripherals:
  - { name: usart1, compatible: "st,stm32-usart", base: 0x40011000, size: 0x400, irqs: [ { name: global, number: 37 } ],
      clock: usart1, dma_requests: { tx: dma2.s7c4, rx: dma2.s2c4 }, reset: apb2.usart1 }
  - { name: gpioa,  compatible: "st,stm32-gpio",  base: 0x40020000, size: 0x400, clock: gpioa, pins: 16 }
  - { name: tim2,   compatible: "st,stm32-timer", base: 0x40000000, size: 0x400, irqs: [ { name: global, number: 28 } ],
      clock: tim2, features: [32bit, lowpower: false] }
dma:
  controllers: [ { name: dma2, base: 0x40026400, streams: 8, channels: 8, irqs: [56, 57, 58, 59, 60, 68, 69, 70] } ]
flash:
  geometry: { sectors: [16K, 16K, 16K, 16K, 64K, 128K, 128K, 128K, 128K, 128K, 128K, 128K], write_granularity: 1 }
power:
  states: [ { name: sleep, exit_latency_us: 1 }, { name: stop, exit_latency_us: 15, loses: [clocks] }, { name: standby, exit_latency_us: 400, loses: [sram, clocks] } ]
errata:
  - { id: "2.1.8", revisions: [r0p1], workaround: usart_rxne_clear }
debug: { swd: true, jtag: true, dwt: true, itm: true }
```

DMA (A119): a DMA controller is a peripheral with a `dma` key, `{kind, channels, max_count, memory}`. A `fixed` controller serves each request on the streams its table lists, a channel select picking it (the STM32F4); a `dreq` controller serves any request on any channel, paced by a request number (the RP2350). A peripheral's `dma_requests` names its request lines: `{tx: dma2.s7c4, rx: [dma2.s2c4, dma2.s5c4]}` on a fixed controller, the streams it may use, or `{tx: dma.dreq26}` on a dreq one. The `dma: controllers:` section above is that form's predecessor.

Required sections: `arch`, `cpu`, `memory`, `interrupts`, `peripherals`; the rest are optional with empty defaults. The `arch` line binds the SoC to a port manifest so the generator can check that the cpu's protection model, cycle counter, and nesting match the port's feature flags (PORT-001).

## 4. Board schema

```yaml
schema_version: 0
kind: board
name: stm32f407g_disc1
vendor: st
soc: st/stm32f4/stm32f407.yaml
oscillators: { hse: { hz: 8MHz, source: external_crystal }, lse: { hz: 32768, source: external_crystal } }
pins:
  usart2: { tx: PA2.af7, rx: PA3.af7 }
  spi1:   { sck: PA5.af5, miso: PA6.af5, mosi: PA7.af5 }
devices:
  - { name: accel, compatible: "st,lis3dsh", bus: spi1, cs: PE3, irq: PE0, power: always }
  - { name: audio_dac, compatible: "cirrus,cs43l22", bus: i2c1, address: 0x4A, reset: PD4 }
console: { device: usart2, baud: 115200 }
flash_partitions:
  - { name: bootloader, base: 0x08000000, size: 128K }
  - { name: slot0, base: 0x08020000, size: 384K }
  - { name: slot1, base: 0x08080000, size: 384K }
  - { name: storage, base: 0x080E0000, size: 128K }
leds: [ { name: green, gpio: PD12 }, { name: orange, gpio: PD13 }, { name: red, gpio: PD14 }, { name: blue, gpio: PD15 } ]
buttons: [ { name: user, gpio: PA0, active: high } ]
debug_probe: { openocd: "board/stm32f4discovery.cfg", pyocd_target: stm32f407vgtx }
defaults: { CONFIG_EMB_PROFILE: base, CONFIG_EMB_TICK_NS: 1000000, CONFIG_EMB_CONSOLE: usart2 }
```

The illustrative flash layout uses STM32F407 erase-sector boundaries: independent updates/storage must not share an erase unit. A nonoverlapping byte range alone is insufficient. Generation validates actual erase geometry and update trailer/alignment requirements before advertising A/B support.

Pins are `P<port><pin>.af<n>` for alternate functions or `P<port><pin>` for GPIO; the generator checks each against the SoC's GPIO ports and against other assignments (no pin twice).

A device's `dma` lists the request lines to give channels, `dma: [tx, rx]` (A119): the generator gives each a channel of one of the board's DMA devices that no other device has (on a fixed controller, the first free stream its table lists; on a dreq one, the lowest free channel), refuses a board that runs out, writes the channel into the device's configuration (a binding's `dma.<request>`) and names it `EMB_SOC_<DEVICE>_DMA_<REQUEST>`. A request with no channel is none, so a driver runs it without DMA. The regions marked `dma` become `emb_board_dma_regions`, where the DMA class takes buffers.

A flash device (A122) takes its geometry from the SoC's `flash:`, its region and its sectors, listed or all of `page_size`, and its `write_granularity`. Where the board's chip is smaller than the region it is mapped in, as the RP2350's 16 MiB XIP window holds the Pico 2 W's 4 MiB, the device gives the chip's size, `{ name: flash, peripheral: qmi, size: 4M }`, a whole number of sectors; a flash whose sectors are listed is its whole region. The generator writes the geometry into the device's configuration (a binding's `flash.geometry`). Each of `flash_partitions` on a flash device becomes an `emb_flash_area_t` in `soc_devices.c`, its offset from the device's start, named `EMB_FLASH_AREA_<NAME>` in `soc_devices.h`.

A device's `cs` is a chip select (A116): a GPIO pin, active low unless written `{pin: PE3, active: high}`. A device on an SPI bus names its own, as `accel` does, and the generator names it `EMB_SOC_ACCEL_CS`; an SPI controller may list spare ones for the application, `EMB_SOC_SPI1_CS0` and on. Each must be a line of one of the board's GPIO devices, and the pin table sets it up as an output, deasserted, at boot.

## 5. System schema

*Rewritten by [A125](AMENDMENTS-M4.md), accepted by the project owner on 2026-11-13; it replaces the sketch this section held. Partitions, shares, trace, log and images join the schema with the phases that implement them (M3b, M4.3, M4.5).*

The system description, `system.yaml` beside the application's sources, says what a product does with its board: its threads, its kernel objects, and the settings it wants from the board's devices. The board description keeps what the PCB fixes. Two products on one board differ only in their system descriptions.

```yaml
schema_version: 0
kind: system
name: controller
boards: [st/nucleo_f446re, st/nucleo_f446ze]   # <vendor>/<board> of the tree, or a directory; board: for one
conf: [controller.conf]          # the application's Kconfig fragments, read for the checks below
includes: [controller.h]         # headers the generated sources need (item types, entry functions)
threads:
  - { name: control_thread, entry: control, priority: 6, stack: 1K }
  - { name: command_thread, entry: command, priority: 4, stack: 1K }
  - { name: telemetry_thread, entry: telemetry, priority: 2, stack: 1K, autostart: true }
objects:
  - { name: samples, type: msgq, item: sample_t, capacity: 8 }
  - { name: rx_bytes, type: msgq, item_size: 1, capacity: 64 }
  - { name: console, type: mutex, protocol: inherit }
  - { name: bus_lock, type: mutex, protocol: ceiling, users: [control_thread, command_thread] }
  - { name: ready, type: sem, initial: 0, max: 1 }
devices:
  usart2: { baud: 115200, rx_buffer: 128 }   # the product's settings for the board's devices
spi_targets:
  - { name: flash, bus: spi2, hz: 8MHz, mode: 0, cs: 0 }
i2c_targets:
  - { name: env, bus: i2c1, address: 0x76, speed: fast }
watchdog: { device: iwdg, timeout: 500ms }
```

**Threads** (`EMB_THREAD_DEFINE`): `name`, `entry` (a C function `void entry(void *arg)`), `priority`, `stack` (bytes, `K` allowed), and optionally `arg` (a C expression), `autostart` (default true) and `flags` (the `EMB_THREAD_*` flag names). **Objects**, one per static definition macro: `mutex` (`protocol`: `none`, `inherit` or `ceiling`; a ceiling mutex names its `users`, threads of this file, and the generator sets its ceiling to the highest of their priorities), `sem` (`initial`, `max`), `cond`, `event` (`initial`), `msgq` (`item`, a C type taken with `sizeof`, or `item_size` in bytes; `capacity`), `timer` (`callback`, `arg`) and `workq` (`priority`, `stack`). Every name is a C identifier, unique in the file, and becomes the handle's name.

**Devices:** each key names a device of the board's description, and its value sets the options of that device's binding, the fields the binding fills from the device (`device.<option>`, SPEC-016 §8: today a UART's `baud` and `rx_buffer`, the RP2350 TRNG's `sample_count`). The value replaces the board's for this product. What the board fixes cannot be set here: the device's compatible, its pins, chip selects, bus, address and DMA channels. A value is checked as the board's is, by its binding, and a baud rate also against the hardware: the peripheral's clock must divide to it within 2 %.

**Bus targets and the watchdog** are not settings of a device but what the application passes to the class's calls (SPEC-017): an SPI target's clock, mode and chip select travel with each transfer, an I2C target's address and speed too, and a watchdog's timeout with `emb_drv_wdt_start()`. The system names them, the generator checks them, and they become constants the application passes:
- `spi_targets`: `name`, `bus` (an SPI controller device), `hz`, `mode` (0 to 3), `cs` (an index into the controller's chip selects in the board description, or absent for none) and `bits` (default 8); a `const emb_drv_spi_target_t`. Checked: the clock within what the controller's dividers make from its peripheral clock, and one target per chip select.
- `i2c_targets`: `name`, `bus` (an I2C controller device), `address` (7 bits, 0x08 to 0x77), `speed` (`standard`, `fast` or `fast_plus`); a `const emb_drv_i2c_target_t`. Checked: the address unique on its bus, and the speed one the controller's binding lists.
- `watchdog`: `device` (a watchdog class device) and `timeout`; `EMB_SYSTEM_WATCHDOG_TIMEOUT_MS`, checked within the driver's range.

A binding states the limits these checks use, with the figures its driver enforces (`limits:` in `drivers/bindings.yaml`: an SPI controller's divider range, an I2C controller's speeds, a watchdog's timeout range).

**Boards:** `boards:` lists the boards the product runs on, or `board:` names its one board. The system is checked against each, and an error found on one of several names it. `profile:` chooses the kernel profile where the board's default is not the product's.

**Kconfig options stay in their fragments** (`.conf`), which `conf:` names so the generator can resolve them with the board's defaults and check against them; the build uses the same fragments.

**Threads and objects may also be defined in C** with the same macros. The generator does not see them: their names are the linker's to keep apart, and the checks after the link (below) cover them.

### 5.1 What the generator writes

`emb-hwgen generate --system <dir>/system.yaml` resolves each board with the system and writes `generated/<board>/` beside `system.yaml`, committed with it and kept current by `emb-hwgen check` (HW-003), as a board's outputs are. The build takes the system with `EMB_SYSTEM` and one of its boards with `EMB_BOARD`, the first by default:

| Output | Content |
|---|---|
| the board's outputs (§6) | rendered again with the product's device settings; the application builds against these, not the board's |
| `system.h` | the handles (`extern emb_thread_t control_thread;` and so on), the entry functions' declarations, the targets (`extern const emb_drv_spi_target_t flash;`) and the watchdog's timeout, for the application's sources |
| `system.c` | one static definition macro per thread and object, with the computed ceilings, and the targets' constants; the kernel creates the objects from its initialization table (SPEC-001 §6.4) |
| `system.json` | the resolved system: every thread, object and device setting, each with where it came from (system or board), for tools such as EmbLink Studio |
| `system.md` | the system's reference page: threads by priority with their stacks, objects, ceilings, device settings and their origin |

§6's `ceilings.h` and `init_order.c` are not written yet: `system.c` holds the computed ceilings, and the kernel's initialization table orders the objects. Generated files hold data and macro calls only: the code they set in motion is the kernel's and the drivers'. They are never edited, and have no user regions.

### 5.2 Checks

At generation, beside the board's (§7), the generator refuses, naming file, line and node (HW-002): a name that is not a C identifier or is used twice; a priority outside `CONFIG_EMB_PRIORITY_COUNT`, or shared between threads where the profile needs unique priorities (tiny); an object type its Kconfig option leaves out; a ceiling mutex user that is not a thread of the file; a device the board does not have, or an option its binding does not take; a device setting the hardware cannot meet; a target on a device that is not a controller of its class, outside its binding's limits, sharing a chip select or an I2C address; a watchdog timeout outside its driver's range; a stack below the port's minimum (`EMB_THREAD_STACK_MIN`).

After the link, the build compares each thread's `stack` with the worst case the stack tool computes from the image (`tools/footprint/stack_bound.py`), from the thread's entry and the roots the application lists for calls the call graph cannot see (`stack-roots.json`). A stack below its worst case **fails the build**, naming the thread, both figures and the deepest call chain. The memory report covers the image as linked, C definitions included.

### 5.3 Tasks, editable outputs and the layout

*Added by [A126](AMENDMENTS-M4.md), accepted by the project owner on 2026-11-13. Where it differs from §5.1, it replaces it.*

**Tasks.** A system's `tasks:` declares threads written as state machines ([SPEC-020](SPEC-020-tasks.md)): each task has its model (event or periodic), states, events and handlers, and the generator writes their tables and threads.

**Every generated file is editable.** The generator keeps what it last wrote, its *baseline*, in `generated/.base/`, committed with the outputs. On regeneration, each file is handled as follows:

| The generated text | The file on disk | What happens |
|---|---|---|
| unchanged since the baseline | anything | untouched: the edits stay |
| changed | as the baseline (not edited) | rewritten |
| changed | edited | three-way merge of the baseline, the file and the new text: the edits and the change both kept |
| changed | edited on the same lines | conflict markers in the file; `generate` reports it, and `check` fails until it is resolved |

There are no `USER CODE` regions. `check` verifies that the baseline is what the descriptions give now, and that no conflict is left. The tree's own boards are checked strictly in CI (`check --strict`): their files must equal the baseline, since a board's facts belong in its description.

**The layout.** An application's files:

```text
my_app/
  system.yaml
  CMakeLists.txt              the application's build: generated, editable
  src/
    main.c                    generated, editable: the board and kernel start, the tasks' start
    tasks/<task>.c, .h        generated, editable: each task's handlers
  generated/
    .base/                    the baseline: what the generator last wrote (do not edit)
    <board>/
      config/                 board.conf
      include/                system.h, tasks.h, hw_config.h, soc_devices.h
      src/                    system.c, tasks.c, soc_devices.c, board_pins.c, boot_block.c
      linker/                 regions.ld, embld.opts
      build/                  board.cmake, CMakeLists.txt
      docs/                   board.md, system.md, build_report.md, hw.json, system.json
```

- **Shared files:** `main.c`, the task files and `CMakeLists.txt` serve every board.
- **Per board:** `generated/<board>/` holds each board's own outputs.
- **Boards:** a board's own `generated/` has the same six folders (`boards/<vendor>/<board>/generated/config/` and so on).

## 6. Generated outputs

| Output | Content | Consumer | Spec |
|---|---|---|---|
| `hw_config.h` | constants: core count, region bases and sizes, IRQ numbers and encoded priorities for generic levels, vector names, clock frequencies, pin encodings | everything | SPEC-002 §8 |
| `soc_devices.c` | `const` device instance table with config structs, init order from `deps` and `clock`/`bus` references (DRV-001), vector bindings, DMA bindings, flash geometries and the flash partitions (A122) | drivers, kernel init | 04 §2 |
| `soc_clocks.c` | the clock tree configuration sequence that reaches the requested frequencies, with the computed PLL factors and bus dividers, checked against `max_hz` | SoC startup | 04 §1.3 |
| `board_pins.c` | pin configuration table | board init | |
| `regions.ld`, `embld.opts` | linker script and `embld` option set from one region model, per-partition sections, retained and `NOLOAD` sections, code that runs from RAM (`.ramfunc`, A122), kstore placement | linker | HW-006, SPEC-010 §3 |
| `partitions.c` | partition table, region sets in the port's encoding, descriptors with kstore addresses, device grants | kernel | SPEC-010 |
| `caps_init.c` | initial capability tables | kernel | SPEC-009 §4.1 |
| `ceilings.h` | static ceiling values per declared mutex | kernel, application | SPEC-005 §3.4 |
| `init_order.c` | ordered init table entries for system-declared objects (`EMB_INIT_AFTER`) | kernel start | SPEC-009 §5.2 |
| `Kconfig.board`, `Kconfig.system` | defaults and forced values | configuration | |
| `board.md` | reference page: pins, devices, partitions, memory map, interrupts | documentation | |
| `hw.json` | the normalized, flattened, resolved model with provenance and hashes | EmbLink Studio, EmbDebug, HIL runner, the benchmark harness metadata | HW-005 |
| `build_report.md` | region budget per partition, kstore sizes, MPU constraint results, clock results, warnings (uncurated nodes) | reviewer | SPEC-010 §4.3 |
| `trace_metadata/` | CTF metadata for the configured trace event set | tracing tools | SPEC-015 |

Two generated artifacts come from other tools in the same pipeline: `<emb/storage.h>` from the layout probe (SPEC-009 §5.1) and the syscall stubs and table from the API annotations (SPEC-010 §5.3); CMake orders them.

## 7. Validation beyond the schema

The generator refuses the build, naming file and node (HW-002), on: unresolved reference; duplicate name within a section; overlapping memory regions or flash partitions, independently writable flash partitions sharing erase units or violating write/update alignment; a region or partition not aligned as its attributes or the protection hardware require; an IRQ number used twice or above `interrupts.count`; a pin assigned twice or to a function the SoC's GPIO does not offer; a clock DAG cycle or a bus above `max_hz`; a device dependency cycle; a partition constraint violation (SPEC-010 §4.3); a ceiling mutex whose user is not a declared thread; a port manifest mismatch (`arch` says MPU but the port lacks `mpu`); an errata entry whose revision is not in the SoC's list; a `safety` profile with an uncurated node.

## 8. Importers

- **SVD.** EmbCC's parser now exports nested clusters/arrays and structured `embsvd --json` (09 §9). The RTOS `--emit-emb-yaml` mode/adapter remains a planned HW-004 deliverable: it must turn that input into a SoC draft with peripheral base/size/IRQ, memory map, NVIC parameters and `imported_from` provenance. `compatible` strings come from a vendor mapping table (`hw/import/compat/<vendor>.yaml`) where known, else `todo`; clocks, DMA maps, flash geometry, power and errata remain curated `todo` fields. JSON export alone does not implement the RTOS schema or its provenance/curation rules.
- **DeviceTree.** `emb-dtsimport` (Python, using `dtc` to flatten) reads a Zephyr or Linux board `.dts` and produces a board draft: pins from pinctrl nodes, devices with `compatible`, bus and address, console from `chosen`, flash partitions, LEDs and buttons; SoC-level `.dtsi` content is used only to resolve references, never to replace the SVD-derived SoC file.
- Imports are one-shot drafts: re-import produces a diff for the author, never an overwrite of curated nodes.

## 9. Determinism and reproducibility

Outputs are sorted by name, carry no timestamps, and embed the generator version, the schema version, and the SHA-256 of each input file; CI regenerates and fails on any difference with the committed outputs (HW-003); `hw.json` is the single artifact other tools consume, so they never parse YAML themselves.

## 10. Versioning and migration

`schema_version: 0` remains unstable until its own stable schema release: breaking changes ship with `emb-hwgen migrate` that rewrites descriptions in place and reports what it could not migrate. From the stable schema release it follows the API versioning policy (SPEC-001 §11): additive changes within a major, a migration tool across majors.

## 11. Target flow: a new board on a supported SoC

These CLI/importer/generator operations are planned. The example uses the same F407 SoC; moving from F407 to F446 is a different SoC description even when drivers can be shared.

1. `emb board new --soc st/stm32f4/stm32f407 --from-dts custom_f407.dts` produces `boards/custom/custom_f407.yaml` with provenance and `todo` markers.
2. The author fixes the oscillator, console, and LED, marks nodes curated, and commits.
3. `emb build -b custom_f407` regenerates everything; `build_report.md` shows the region budget; no C is written (01 §6 success criterion).

## 12. Profiles and the tiny target

The tiny profile uses the same pipeline: the ATmega328P SoC file is 60 lines, the Uno board file 15 (SPEC-012 §11), and the generator's AVR outputs are `hw_config.h`, `soc_devices.c` (small), the `embld` option set and a GNU ld script, `board.md`, and `hw.json`. Tables that would cost SRAM under EmbCC are emitted with `EMB_FLASH_CONST` (SPEC-012 §8).

## 13. Decisions taken at acceptance (2026-10-07)

1. Three description kinds (SoC, board, system) with one schema language (JSON Schema 2020-12 over YAML 1.2), `extends` inheritance, cross-file references, and recorded provenance with `curated` marks.
2. The system description is the single source for statically declared partitions, threads, objects, ceilings, shares, and images.
3. The generator is one Python tool emitting every output of §6 deterministically with input hashes; CI regenerates and compares.
4. Semantic validation (§7) is part of the generator and fails the build naming file and node; the `safety` profile attribute refuses uncurated nodes.
5. The SVD importer is an output mode of EmbCC's `embsvd`; the DeviceTree importer flattens with `dtc`; both produce one-shot drafts and diffs, never overwrites.
6. `hw.json` is the only artifact downstream tools consume.
7. Schema v0 is unstable until its own stable release with a migration tool; thereafter it follows the API versioning policy.
