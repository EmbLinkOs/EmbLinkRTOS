# SPEC-014 - Hardware Description Schema v0 and the Generator

**Status:** Accepted 2026-10-07 by the project owner ("do all"), with the defaults of §13. Specification work item 13 of the roadmap (07 §3).
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
| **Schema** | JSON Schema (draft 2020-12) files in `hw/schemas/` that validate the three description kinds; `schema_version: 0` until 1.0 |
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
  - { name: bootloader, base: 0x08000000, size: 32K }
  - { name: slot0, base: 0x08008000, size: 480K }
  - { name: slot1, base: 0x08080000, size: 480K }
  - { name: storage, base: 0x080F8000, size: 32K }
leds: [ { name: green, gpio: PD12 }, { name: orange, gpio: PD13 }, { name: red, gpio: PD14 }, { name: blue, gpio: PD15 } ]
buttons: [ { name: user, gpio: PA0, active: high } ]
debug_probe: { openocd: "board/stm32f4discovery.cfg", pyocd_target: stm32f407vgtx }
defaults: { CONFIG_EMB_PROFILE: base, CONFIG_EMB_TICK_NS: 1000000, CONFIG_EMB_CONSOLE: usart2 }
```

Pins are `P<port><pin>.af<n>` for alternate functions or `P<port><pin>` for GPIO; the generator checks each against the SoC's GPIO ports and against other assignments (no pin twice).

## 5. System schema

```yaml
schema_version: 0
kind: system
name: sensor_node
board: st/stm32f407g_disc1.yaml
profile: isolated                      # tiny | base | isolated | multicore | native
config: { CONFIG_EMB_PRIORITY_COUNT: 32, CONFIG_EMB_LOG_LEVEL_DEFAULT: info }
partitions: [ ... ]                    # SPEC-010 §2
kernel_partition:
  threads: [ { name: main, entry: app_main, priority: 10, stack: 2K } ]
  objects: [ { name: cmds, type: msgq, item_size: 16, capacity: 8 } ]
ceiling_mutexes:                       # SPEC-005 §3.4, ADR-030
  - { name: bus_lock, users: [kernel_partition.main, sensors.acq] }
shares: { window_ms: 100 }             # ADR-029 partition shares; per-partition percent in partitions[]
trace: { ring_kb: 8, classes: [sched, sync, irq] }
log: { mode: deferred, transport: itm }
images:                                # ADR-008 slots per board partition
  - { name: app, slot: slot0, cores: [0] }
```

The system file is where the generator learns every statically declared thread and object, so it can size kstores, build capability tables, compute ceilings, and order the init table (SPEC-009 §5.2, SPEC-010 §3).

## 6. Generated outputs

| Output | Content | Consumer | Spec |
|---|---|---|---|
| `hw_config.h` | constants: core count, region bases and sizes, IRQ numbers and encoded priorities for generic levels, vector names, clock frequencies, pin encodings | everything | SPEC-002 §8 |
| `soc_devices.c` | `const` device instance table with config structs, init order from `deps` and `clock`/`bus` references (DRV-001), vector bindings, DMA bindings | drivers, kernel init | 04 §2 |
| `soc_clocks.c` | the clock tree configuration sequence that reaches the requested frequencies, with the computed PLL factors and bus dividers, checked against `max_hz` | SoC startup | 04 §1.3 |
| `board_pins.c` | pin configuration table | board init | |
| `regions.ld`, `embld.opts` | linker script and `embld` option set from one region model, per-partition sections, retained and `NOLOAD` sections, kstore placement | linker | HW-006, SPEC-010 §3 |
| `partitions.c` | partition table, region sets in the port's encoding, descriptors with kstore addresses, device grants | kernel | SPEC-010 |
| `caps_init.c` | initial capability tables | kernel | SPEC-009 §4.1 |
| `ceilings.h` | static ceiling values per declared mutex | kernel, application | SPEC-005 §3.4 |
| `init_order.c` | ordered init table entries for system-declared objects (`EMB_INIT_AFTER`) | kernel start | SPEC-009 §5.2 |
| `Kconfig.board`, `Kconfig.system` | defaults and forced values | configuration | |
| `board.md` | reference page: pins, devices, partitions, memory map, interrupts | documentation | |
| `hw.json` | the normalized, flattened, resolved model with provenance and hashes | EmbStudio, EmbDebug, HIL runner, the benchmark harness metadata | HW-005 |
| `build_report.md` | region budget per partition, kstore sizes, MPU constraint results, clock results, warnings (uncurated nodes) | reviewer | SPEC-010 §4.3 |
| `trace_metadata/` | CTF metadata for the configured trace event set | tracing tools | SPEC-015 |

Two generated artifacts come from other tools in the same pipeline: `<emb/storage.h>` from the layout probe (SPEC-009 §5.1) and the syscall stubs and table from the API annotations (SPEC-010 §5.3); CMake orders them.

## 7. Validation beyond the schema

The generator refuses the build, naming file and node (HW-002), on: unresolved reference; duplicate name within a section; overlapping memory regions or flash partitions; a region or partition not aligned as its attributes or the protection hardware require; an IRQ number used twice or above `interrupts.count`; a pin assigned twice or to a function the SoC's GPIO does not offer; a clock DAG cycle or a bus above `max_hz`; a device dependency cycle; a partition constraint violation (SPEC-010 §4.3); a ceiling mutex whose user is not a declared thread; a port manifest mismatch (`arch` says MPU but the port lacks `mpu`); an errata entry whose revision is not in the SoC's list; a `safety` profile with an uncurated node.

## 8. Importers

- **SVD.** `embsvd --emit-emb-yaml` (an output mode added to EmbCC's parser, 09 §9, HW-004) produces a SoC draft: peripherals with base, size, and IRQ numbers, the memory map, the NVIC parameters, and `imported_from` provenance; `compatible` strings are filled from a vendor mapping table (`hw/import/compat/<vendor>.yaml`) where known and marked `todo` otherwise; clocks, DMA request maps, flash geometry, power states, and errata are not in SVD and start as `todo` sections. The author curates and sets `curated: true` per node.
- **DeviceTree.** `emb-dtsimport` (Python, using `dtc` to flatten) reads a Zephyr or Linux board `.dts` and produces a board draft: pins from pinctrl nodes, devices with `compatible`, bus and address, console from `chosen`, flash partitions, LEDs and buttons; SoC-level `.dtsi` content is used only to resolve references, never to replace the SVD-derived SoC file.
- Imports are one-shot drafts: re-import produces a diff for the author, never an overwrite of curated nodes.

## 9. Determinism and reproducibility

Outputs are sorted by name, carry no timestamps, and embed the generator version, the schema version, and the SHA-256 of each input file; CI regenerates and fails on any difference with the committed outputs (HW-003); `hw.json` is the single artifact other tools consume, so they never parse YAML themselves.

## 10. Versioning and migration

`schema_version: 0` is unstable until 1.0: breaking changes ship with `emb-hwgen migrate` that rewrites descriptions in place and reports what it could not migrate. From 1.0 the schema follows the API versioning policy (SPEC-001 §11): additive changes within a major, a migration tool across majors.

## 11. Worked flow: a new board on a supported SoC

1. `emb board new --soc st/stm32f4/stm32f407 --from-dts nucleo_f446re.dts` produces `boards/st/nucleo_f446re.yaml` with provenance and `todo` markers.
2. The author fixes the oscillator, console, and LED, marks nodes curated, and commits.
3. `emb build -b nucleo_f446re` regenerates everything; `build_report.md` shows the region budget; no C is written (01 §6 success criterion).

## 12. Profiles and the tiny target

The tiny profile uses the same pipeline: the ATmega328P SoC file is 60 lines, the Uno board file 15 (SPEC-012 §11), and the generator's AVR outputs are `hw_config.h`, `soc_devices.c` (small), the `embld` option set and a GNU ld script, `board.md`, and `hw.json`. Tables that would cost SRAM under EmbCC are emitted with `EMB_FLASH_CONST` (SPEC-012 §8).

## 13. Decisions taken at acceptance (2026-10-07)

1. Three description kinds (SoC, board, system) with one schema language (JSON Schema 2020-12 over YAML 1.2), `extends` inheritance, cross-file references, and recorded provenance with `curated` marks.
2. The system description is the single source for statically declared partitions, threads, objects, ceilings, shares, and images.
3. The generator is one Python tool emitting every output of §6 deterministically with input hashes; CI regenerates and compares.
4. Semantic validation (§7) is part of the generator and fails the build naming file and node; the `safety` profile attribute refuses uncurated nodes.
5. The SVD importer is an output mode of EmbCC's `embsvd`; the DeviceTree importer flattens with `dtc`; both produce one-shot drafts and diffs, never overwrites.
6. `hw.json` is the only artifact downstream tools consume.
7. Schema v0 is unstable until 1.0 with a migration tool; thereafter it follows the API versioning policy.
