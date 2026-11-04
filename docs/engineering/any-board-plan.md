# Implementation plan: any board (ADR-051)

**Status:** working plan, 2026-11-04. It implements [ADR-051](../architecture/06-decision-records.md), which is proposed and awaiting the owner's review. Each step is one stacked PR with its own amendment. A step is done only when its verification has run; this page records the result.

ADR-051 grows support in three layers. A **core port** is written once per CPU family. A **SoC description** is drafted from vendor data and then curated. A **board** is a `board.yaml` only. The steps below fill each layer where it is thinnest, in the order the hardware on the bench needs them:

- the classic ESP32, from 2026-11-05;
- the NUCLEO-F446RE and the Raspberry Pi Pico 2 W, from 2026-11-07.

Every step that needs no hardware is verified on QEMU or EmbSIM first, and a board then raises its tier.

| # | Step | Layer | Verification | Status |
|---|---|---|---|---|
| 1 | Boards outside the tree (`EMB_BOARD_ROOT`, A76) | board | `OutOfTree` tests; an out-of-tree copy of the AN386 passes its 21 tests under QEMU | done (#61) |
| 2 | SVD importer (HW-004, HW-012, A77) | SoC | `stm32f446.yaml` draft vs the curated file; re-import keeps curated nodes | done: agrees except the PVD line ([hwgen.md](hwgen.md#importing-a-soc-from-its-svd)) |
| 3 | Armv8-M Mainline (Cortex-M33) in the Cortex-M port | core | full suite on QEMU `mps2-an505` (checked, release, tiny) | done: 21, 21 and 15 of 15 pass (A78) |
| 3b | PSPLIM stack limits on Armv8-M Mainline | core | a thread overrunning its stack takes a STKOF fault, recorded, on QEMU | done: `test_stack_limit`, which fails with PSPLIM disabled (A80) |
| 4 | RP2350 SoC and Pico 2 W board | SoC, board | builds from its description (tier 3); on 2026-11-07, HIL (tier 1) | tier 3 done (A79); HIL waits for the board |
| 5 | Support tiers from evidence | all | the matrix lists every board with its tier and the outcome that earned it | done: [support matrix](support-matrix.md), 1/8/1 boards at tiers 1/2/3 (A81) |
| 6 | DeviceTree board importer (`hw/import/dts.py`) | board | Zephyr's `nucleo_f446re` DTS draft vs the curated `board.yaml` | done: agrees on every board fact; the draft passes 19/19 on EmbSIM (A82) |
| 7 | STM32 pin importer (ST open pin data) | SoC | package files and alternate functions vs the curated F446RE and F446ZE files | |
| 8 | SoC descriptions outside the tree (`EMB_SOC_ROOT`) | SoC | an out-of-tree SoC copy builds and runs a board on QEMU | |
| 9 | Armv6-M (Cortex-M0/M0+) and Cortex-M7 | core | suite on QEMU `microbit` and `mps2-an500` | Cortex-M7 done: 21/21 checked and release (A83); Armv6-M next |
| 10 | Xtensa LX6 core port (classic ESP32) | core | after the bring-up probe (#60) runs on the board | linker decided: embld with scripts (ADR-052, EmbCC #174) |

## 1. Boards outside the tree (done)

Covered in [hwgen.md](hwgen.md#a-board-outside-the-tree) and A76.

## 2. SVD importer

SPEC-014 §8 sets the design: EmbCC's `embsvd` parses the SVD (`embsvd DEVICE.svd --json`), and `hw/import/svd.py` turns its JSON into a SoC draft.

- **What the SVD gives:**
  - the CPU (core, FPU, MPU, NVIC priority bits);
  - every peripheral with its base, the size of its address block, and its interrupts;
  - the interrupt count. Lines no peripheral uses become `reserved`.
- **What it does not give, marked `todo`:** clocks, memory regions (unless `--flash` and `--ram` are given, as `embsvd` takes them), pins and alternate functions, flash geometry, and errata.
- **`compatible` strings** come from `hw/import/compat/<vendor>.yaml`, which maps SVD peripheral names or groups to strings. A name not in the map gets `todo`.
- **Clock gates.** For ST, a gate is found by matching a peripheral to an `RCC` `*ENR` register field named `<PERIPHERAL>EN`, which gives the bus and the bit.
- **Provenance.** The draft carries `imported_from: { tool, source, hash }` and `curated: false`.
- **Re-import (HW-012).** Importing against an existing file never overwrites a node marked `curated: true`. It prints a diff of what the SVD says differently.
- **Oracle.** The F446 die file was written by hand from the SVD and the reference manual. Every fact the importer produces for it must equal the curated value, or be explained in the test.
- **Result (2026-11-04).** The draft from ST's SVD v2.0 agrees with the curated die on every fact both hold, except that the SVD gives the PVD interrupt to EXTI and the curated file gives it to PWR. The older SVD 1.1 gets ten facts wrong, and the comparison reports each one.
- **Input.** The SVD comes from the vendor at a pinned hash and is not committed. Tests use a small synthetic SVD written for them. The F446 comparison runs when the file is present, and its result is recorded here.

## 3. Cortex-M33 (Armv8-M Mainline)

- **Port.** `arch/cortex_m` gains variant `armv8m-main` (the importer's spelling, A77):
  - PSPLIM and MSPLIM as hardware stack limits, wired to the checked stack guard;
  - the Armv8-M exception return values;
  - no TrustZone: the port runs in the Secure state with no non-secure image, as QEMU starts it.
- **SoC and board.** `soc/arm/mps2/an505.yaml` and the `mps2_an505` board, from their descriptions.
- **Verification.** Presets `cm33-an505`, `cm33-an505-release` and `cm33-an505-tiny` run the suite in CI. Running the RP2350's Arm side needs this step first.
- **Result (2026-11-04).** Every image passes on QEMU 11.1, with no change to the port's code: the Secure-state Armv8-M Mainline exception model matches Armv7E-M's where the port touches it. PSPLIM moved to step 3b, because it adds a field to every thread's control block and needs its own overflow test.

## 4. RP2350 and the Pico 2 W

- **SoC.** The SVD importer drafts the RP2350 from the `pico-sdk` SVD (BSD-3-Clause), and a person curates it against the datasheet:
  - SRAM at 0x20000000;
  - the boot ROM's image definition;
  - XOSC at 12 MHz, and the PLL;
  - UART0 on GPIO0 and GPIO1, through the IO bank.
- **Board support.** Family board support in `soc/rpi/rp2350/`: clocks, resets, the UART, and the boot block the ROM looks for.
- **Image.** `embpack` writes the UF2 (`rp2350-arm-s`).
- **Board.** The Pico 2 W `board.yaml`.
- **Verification.** Tier 3 until 2026-11-07: it builds, and its image passes the boot-block check. On the board, the HIL runner raises it to tier 1. The radio chip (CYW43439) is out of scope.
- **Result (2026-11-04).** The SVD importer drafted the die. Curation set the block alias spans, the spare interrupts and the UART function selects. The generator learned numbered GPIO banks and the boot block. The 19 test images and the samples build, each with the IMAGE_DEF at 0x10000140, and their UF2 files match `embpack`'s byte for byte.
- **For the HIL run.** The console is UART0 on GPIO0 and GPIO1 (pins 1 and 2), not the USB port, so the run needs a 3.3 V USB-serial adapter: the NUCLEO's ST-LINK virtual COM port, or any CP2102 or FT232 adapter. The image is flashed by copying its UF2 to the boot ROM's drive: hold BOOTSEL while plugging in. To run the suite without a press per image, the board's exit could reboot into BOOTSEL through the ROM (the next step, once one image is seen running).

## 5. Support tiers from evidence

`tools/support/matrix.py` derives each board's tier and writes `docs/engineering/support-matrix.md`:

| Tier | Evidence |
|---|---|
| 1 | an archived hardware run report (`docs/engineering/hil-*.md`) naming the board, the build and the result |
| 2 | a CI job running the suite on the board's emulator |
| 3 | the board builds |

A board's tier is never declared in its description. CI checks that the page is current.

## 6. DeviceTree board importer

`hw/import/dts.py` drafts a `board.yaml` from a Zephyr or Linux board `.dts`, flattened by `dtc`:

- pins from the `pinctrl` nodes;
- the console from `chosen`;
- the LEDs, buttons and flash partitions;
- the oscillators from the clock nodes.

The SoC-level `.dtsi` content only resolves references (SPEC-014 §8). Zephyr's board files are Apache-2.0, and its catalog is the largest source of board facts. The importer is how a board the project has never seen gets a draft in minutes.

## 7. STM32 pin importer

ST's open pin data (BSD-3-Clause) lists, for each package, the bonded pins and every pin's alternate functions. The importer writes the package file's `gpio.ports` masks and the die file's `alternate_functions`, which steps 2 and 6 cannot give.

## 8. SoC descriptions outside the tree

A board root may hold `soc/<vendor>/<family>/` as well: descriptions plus the family's board support. The board's `soc` key is then looked up there first, and `board.cmake` names that directory instead of the tree's.

## 9. Armv6-M and Cortex-M7

- **Armv6-M.** PRIMASK critical sections, since it has no BASEPRI, and the reduced Thumb instruction set in the switch. Verified on QEMU's `microbit` (Cortex-M0, nRF51).
- **Cortex-M7.** Variant flags, and the cache's clean and invalidate kept out of the kernel. Verified on QEMU's `mps2-an500`.

## 10. Xtensa LX6

Starts after the bring-up probe from #60 runs on the board. It needs two things first:

- the owner's decision on the linker: Xtensa linker-script support in `embld`, or another linker;
- the windowed-register spill in the context switch.

Before the board, the core can be brought up on QEMU's `sim` machine (de212 core), as EmbCC's Xtensa tests run.

## Rules for every step

- Commits are signed off by the owner, and carry no other trailer.
- Each step adds an amendment (A77 and up), its tests, and its STATUS and CHANGELOG lines.
- A claim of support names the evidence behind it (SIM-003: emulator timing is never evidence).
