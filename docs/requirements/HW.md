# HW - Hardware Description and Generator

Group `HW`. Design: `docs/specs/SPEC-014-hardware-description-schema.md`; `docs/architecture/04-platform-architecture.md` §1 (ADR-004, LOCKED). Related groups: KRN-PART (partition tables), KRN-CAP (initial capability tables), KRN-SYNC (static ceilings), KRN-OBJ (init table ordering), DRV (device tables, init order), BLD (generated files in the build), OBS (trace metadata), PORT (manifest cross-check).

HW-001 to 006 originate in `docs/architecture/04-platform-architecture.md` §1.3 (004 and 006 refined for EmbCC). All are restated here as the authoritative copy. New requirements start at 007.

---

### HW-001  Single source for hardware facts
**Statement.** Every peripheral instance, interrupt binding, clock setting, pin assignment, memory region, and flash partition shall originate in the hardware description.
**Rationale.** No hand-written board C for things the description knows (01 §6).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: CI grep for register addresses and IRQ numbers in `boards/` and `soc/` C sources; review of generated outputs.
**Trace.** SPEC-014 §3 to §6; ADR-004

### HW-002  Schema validation with named errors
**Statement.** The hardware model shall be validated against a published schema before generation; validation errors shall name the file and node.
**Rationale.** Authors fix descriptions, not generator stack traces.
**Status.** Accepted 2026-10-07
**Verification.** Generator tests with malformed inputs; the error message names file and node path.
**Trace.** SPEC-014 §1, §7

### HW-003  Reproducible generated files
**Statement.** Generated files shall be reproducible and never edited by hand.
**Rationale.** BLD-004; drift between description and code is the failure this pipeline prevents.
**Status.** Accepted 2026-10-07
**Verification.** CI regenerates and diffs against the committed outputs.
**Trace.** SPEC-014 §9

### HW-004  SVD and DeviceTree importers
**Statement.** Importers for CMSIS-SVD and DeviceTree source shall exist to bootstrap SoC and board descriptions; the SVD importer builds on EmbCC's `embsvd` parser (09 §9).
**Rationale.** Authors curate rather than transcribe.
**Status.** Accepted 2026-10-07
**Verification.** Importer tests on the STM32F407 SVD and a Zephyr board DTS; the drafts validate and carry provenance.
**Trace.** SPEC-014 §8

### HW-005  Normalized JSON export
**Statement.** The normalized model shall be exported in a stable JSON form for tooling.
**Rationale.** EmbStudio, EmbDebug, the HIL runner, and the harness consume one artifact and never parse YAML.
**Status.** Accepted 2026-10-07
**Verification.** Schema for `hw.json`; consumer tests.
**Trace.** SPEC-014 §6, §9

### HW-006  Two linker outputs from one region model
**Statement.** From the same region model, the generator shall emit both a GNU ld linker script and an `embld` option set (`-Ttext`, `-Tdata`, `--rom-limit`), because `embld` accepts linker scripts on ARM and RISC-V but not on AVR (09 §7); the two outputs shall be tested for agreement.
**Rationale.** EmbCC on AVR must link the same layout as avr-gcc.
**Status.** Accepted 2026-10-07
**Verification.** Generator test comparing the memory maps produced by both outputs for the Uno board.
**Trace.** SPEC-014 §6; SPEC-012 §8

### HW-007  Three description kinds and common conventions
**Statement.** Descriptions shall be YAML 1.2 files of kind `soc`, `board`, or `system` with a `schema_version`, validated by JSON Schema 2020-12, supporting unit suffixes, cross-file references, `extends` inheritance, provenance, `curated` marks, and `doc` text.
**Rationale.** One grammar for every hardware fact; DeviceTree's good ideas without its macro layer (R-002 §1).
**Status.** Accepted 2026-10-07
**Verification.** Schema files exist for the three kinds; conformance of the shipped descriptions.
**Trace.** SPEC-014 §1, §2

### HW-008  SoC and board content
**Statement.** A SoC description shall carry `arch`, `cpu`, `memory`, `interrupts`, and `peripherals` (clocks, DMA, flash, power, errata, debug optional) and shall reference a port manifest; a board description shall carry the SoC reference, oscillators, pins, devices, console, flash partitions, LEDs, buttons, probe hints, and defaults.
**Rationale.** SPEC-014 §3, §4 fix what the generator can rely on.
**Status.** Accepted 2026-10-07
**Verification.** Schema required fields; the STM32F407, RP2350, and ATmega328P descriptions validate.
**Trace.** SPEC-014 §3, §4

### HW-009  System description as the static source
**Statement.** The system description shall declare the profile, configuration overrides, partitions, kernel-partition threads and objects, ceiling mutexes with users, shares, trace and log configuration, and images; the generator shall derive kstore sizes, capability tables, ceilings, and init ordering from it.
**Rationale.** SPEC-009 §4.1, §5.2; SPEC-010 §2; SPEC-005 §3.4.
**Status.** Accepted 2026-10-07
**Verification.** Generator tests for a two-partition system; the isolated reference configuration is one.
**Trace.** SPEC-014 §5

### HW-010  Generated outputs
**Statement.** The generator shall emit `hw_config.h`, `soc_devices.c`, `soc_clocks.c`, `board_pins.c`, `regions.ld` and `embld.opts`, `partitions.c`, `caps_init.c`, `ceilings.h`, `init_order.c`, `Kconfig.board` and `Kconfig.system`, `board.md`, `hw.json`, `build_report.md`, and the CTF trace metadata, each as SPEC-014 §6 describes.
**Rationale.** The complete list is what downstream specifications depend on.
**Status.** Accepted 2026-10-07
**Verification.** Generator golden-file tests per output.
**Trace.** SPEC-014 §6

### HW-011  Semantic validation
**Statement.** The generator shall fail the build, naming file and node, on unresolved references, duplicate names, overlapping or misaligned regions or partitions, duplicate or out-of-range interrupts, pin conflicts, clock cycles or bus overspeed, dependency cycles, partition hardware constraint violations, undeclared ceiling users, port manifest mismatches, inapplicable errata, and uncurated nodes under the `safety` attribute.
**Rationale.** Every rule here was a runtime bug somewhere else.
**Status.** Accepted 2026-10-07
**Verification.** One negative generator test per rule.
**Trace.** SPEC-014 §7

### HW-012  Importers produce drafts, never overwrites
**Statement.** Importers shall produce drafts with `imported_from` provenance and `todo` markers; re-import shall produce a diff for the author and shall never overwrite curated nodes.
**Rationale.** Curation is the value; tooling must not destroy it.
**Status.** Accepted 2026-10-07
**Verification.** Importer tests: re-import after curation leaves curated nodes intact and reports differences.
**Trace.** SPEC-014 §8

### HW-013  Determinism and stamps
**Statement.** Generated outputs shall be sorted, free of timestamps, and stamped with the generator version, schema version, and input hashes; `hw.json` shall carry the same stamps.
**Rationale.** HW-003 made checkable.
**Status.** Accepted 2026-10-07
**Verification.** Two generator runs produce identical outputs; stamp fields present.
**Trace.** SPEC-014 §9

### HW-014  Schema versioning and migration
**Statement.** Schema v0 is unstable until 1.0 and ships a migration tool for breaking changes; from 1.0 the schema shall follow the API versioning policy.
**Rationale.** Descriptions are long-lived assets of users.
**Status.** Accepted 2026-10-07
**Verification.** Migration test on a v0 fixture at each breaking change.
**Trace.** SPEC-014 §10; SPEC-001 §11

### HW-015  Benchmark and tooling metadata
**Statement.** `hw.json` shall provide the board, SoC revision, clock, and timer-source fields the benchmark metadata record of 05 §4.4 requires, and the probe hints the HIL runner needs.
**Rationale.** One source for the numbers that make measurements comparable (R-003 §6).
**Status.** Accepted 2026-10-07
**Verification.** Harness metadata test reads them from `hw.json`.
**Trace.** SPEC-014 §4, §6; TEST-012
