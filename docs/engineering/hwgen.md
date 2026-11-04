# The hardware generator (emb-hwgen)

`hw/gen/emb-hwgen.py` reads a board description and the SoC description it names. It validates both and writes the board's `generated/` directory: the header, tables, linker script and build fragments that would otherwise be typed by hand ([SPEC-014](../specs/SPEC-014-hardware-description-schema.md), [HW](../requirements/HW.md)). This page covers the first increment ([A66 to A68](../specs/AMENDMENTS-M3.md)), which describes the STM32F446 and the NUCLEO-F446RE.

## Use

```sh
python3 -I hw/gen/emb-hwgen.py validate --board boards/st/nucleo_f446re   # check only
python3 -I hw/gen/emb-hwgen.py generate --board boards/st/nucleo_f446re   # rewrite generated/
python3 -I hw/gen/emb-hwgen.py check                                      # CI: every generated/ is current
```

The generator needs Python 3 and PyYAML, the configuration tool's one dependency.

- Exit status 2 means a description error. Every error is printed as `file:line: node.path: message`, and all errors found are reported, sorted.
- `check` exits 1 when a committed output differs from what the descriptions give, and prints the diff.

Never edit `generated/` by hand (HW-003). Change the YAML and regenerate.

## Files

| Path | Kind | Content |
|---|---|---|
| `soc/st/stm32f4/stm32f4.yaml` | family base (`abstract`) | port binding, core, clock-tree limits, PLL ranges, prescalers, flash latency |
| `soc/st/stm32f4/stm32f446.yaml` | die (`abstract`) | memory regions, NVIC size and free lines, buses with their limits, regulator scales, peripherals, serial alternate functions, flash sectors |
| `soc/st/stm32f4/stm32f446re.yaml`, `stm32f446ze.yaml` | part | package and bonded pins |
| `boards/st/nucleo_f446re/board.yaml` | board | oscillators, CPU frequency, pins, console, LED, button, flash partitions, test interrupt lines, probe hints, defaults |
| `hw/schemas/*.schema.json` | JSON Schema 2020-12 | the SoC and board grammar, and the shape of `hw.json` |

A part file `extends` its die, which extends the family. Mappings merge key by key, and lists of named nodes merge by name. The flattened part is what the schema checks.

Numbers accept `0x` hexadecimal, sizes `K`, `M` and `G` (powers of two), and frequencies `Hz`, `kHz`, `MHz` and `GHz`.

Register base addresses, interrupt numbers, clock gates and pin functions come from the SoC file. The board file states only what the PCB fixes.

## Outputs

| File | Consumer | Content |
|---|---|---|
| `hw_config.h` | board and driver C | identity, clock tree, memory regions, NVIC size, every peripheral's base, interrupt lines and clock gate, the console, LEDs and buttons, the test framework's interrupt lines |
| `board_pins.c` | board initialization | the pins to configure: port, number, mode and alternate function |
| `regions.ld` | linker | FLASH from the `app` partition, RAM from the contiguous writable regions (SRAM1 + SRAM2), the kernel's table brackets |
| `board.conf` | configuration | the port's hardware symbols (CPU clock, IRQ count, priority bits), then the board's defaults |
| `board.cmake` | CMake | port, variant, default profile, CPU flags, the SoC family's board support directory and include paths, EmbSIM target, input hashes |
| `CMakeLists.txt` | CMake | hands the build to the SoC family's board support |
| `hw.json` | tools (HW-005) | the flattened descriptions and everything derived, validated by `hw.schema.json` |
| `board.md`, `build_report.md` | reviewers | board reference; clock results against the limits, image layout, warnings |

Each output is sorted and carries no timestamp. It names the generator version, the schema version and the SHA-256 of every input: the board, the SoC chain and the port manifest (HW-013).

### The clock solver

- **Factors in the board file.** When the board gives PLL factors, the solver checks them against the PLL's ranges and VCO limits, and checks that they give exactly `cpu_hz`. The NUCLEO-F446RE pins the factors it was brought up with: M 8, N 360, P 2, Q 7, R 2.
- **No factors.** The search is deterministic:
  1. M ascending, so the highest VCO input comes first, which RM0390 recommends for jitter.
  2. Then N ascending, then P ascending. The first exact match wins.
  3. Q is the divider closest to 48 MHz without exceeding it.
- **Buses.** Each APB divider is the smallest that keeps its bus within its limit.
- **Flash.** Wait states follow the SoC's table.
- **Regulator.** The scale is the lowest-power one that covers HCLK without over-drive. Failing that, the solver uses the first that covers it with over-drive.

## Building a generated board

A board described in YAML holds `board.yaml` and `generated/` and nothing else ([A69](../specs/AMENDMENTS-M3.md)):

- **Finding the board.** Configuration finds the board through `generated/board.cmake`, and `generated/` becomes the board directory.
- **Staleness.** Each input's SHA-256 is compared with the one `board.cmake` records. A mismatch stops configuration with the `generate` command to run, and editing a description reconfigures.
- **The build.** `generated/CMakeLists.txt` hands the build to the family's board support, `soc/st/stm32f4/CMakeLists.txt`. It compiles four things into `emb_board`:
  - `board.c`, the `emb_board.h` interface for every STM32F4 board;
  - the drivers in `drivers/st/`: clock-tree bring-up (`stm32f4_rcc.c`), the pin table and toggling (`stm32f4_gpio.c`), and the polled console (`stm32_usart.c`);
  - the board's `board_pins.c`;
  - the board's `hw_config.h`, which the other three compile against.

  It then links `regions.ld`.

The drivers keep register offsets and bit layouts. Every base address, gate, pin, divider and frequency comes from `hw_config.h`. `emb-hwgen.py check` enforces both halves of HW-001:
- a generated board's directory holds no other file;
- C under `soc/` and `drivers/` contains no hexadecimal literal of seven or more digits.

## Validation rules

Each rule has a negative test in `hw/gen/test_hwgen.py` that asserts the file, the line and the node path of its error.

| Rule | Example message |
|---|---|
| Schema: missing or unknown key, wrong type, value out of range or pattern | `board.yaml:24: console.parity: unknown key` |
| Duplicate YAML key; duplicate name in a named section | `stm32f446.yaml:60: peripherals.uart4: duplicate name` |
| Unresolved reference (SoC file, peripheral, bus, memory region, port) | `console.device: unresolved reference: the SoC has no peripheral 'usart9'` |
| Abstract SoC file used by a board; `extends` cycle or missing file | `soc: ... is an abstract family or die file; name a part` |
| Overlapping memory regions or peripheral blocks | `memory.regions.sram2: overlaps sram1` |
| Flash partition outside flash, overlapping, or not on erase-sector boundaries | `flash_partitions.app: does not start and end on erase-sector boundaries` |
| Interrupt line duplicated, above the controller's count or listed as reserved; test lines not reserved | `peripherals.usart3.irqs.global: line 38 is already usart2.global's` |
| Pin assigned twice, not bonded on the package, without or with the wrong alternate function | `pins.usart2.tx: PA2 af8 is not usart2_tx (PA2 offers af7 usart2_tx)` |
| Oscillator out of the SoC's range; frequency unreachable; PLL factor out of range; bus over its limit | `clocks.cpu_hz: 200 MHz exceeds the AHB limit of 180 MHz` |
| Port manifest: no such port or variant, cycle counter the port lacks | `arch.variant: port cortex_m has no variant 'armv6m'` |
| Errata naming a revision not in `cpu.revisions` | `errata.2.1.8: revision(s) Z not in cpu.revisions` |
| A board default that overrides a hardware fact; a profile other than tiny or base | `defaults.CONFIG_EMB_CM_CPU_HZ: comes from the hardware description` |

## Verification

```sh
python3 -I -m unittest discover -s hw/gen -p 'test_*.py' -v
python3 -I hw/gen/emb-hwgen.py check
```

The tests cover:
- the committed outputs (HW-003);
- byte-identical output across two runs and across a copy of the inputs elsewhere (HW-013);
- the stamps;
- the values the hand-written NUCLEO-F446RE files carry;
- `hw.json` against its schema, and the fields the benchmark harness needs (HW-015);
- the solver at 180, 168, 84 and 8 MHz;
- unit parsing and inheritance.

The `Facts` cases cover the HW-001 check. With `jsonschema` installed, as in CI's lint job, the tests also check that the in-tree validator agrees with it (A67).

## Limits of this increment

- **Not compared register by register.** The NUCLEO-F446RE build from `generated/` writes the same register values as the hand-written `board.c` it replaced, checked in review and by the EmbSIM suites. EmbSIM's image mode has no register dump, so the two were not compared register by register. Pins are now configured one at a time.
- **Not generated:** the system description, `soc_devices.c`, `embld.opts`, the importers, trace metadata and `migrate` ([A68](../specs/AMENDMENTS-M3.md)).
- **Other boards:** the Uno, the MPS2 boards, RV32 virt and the ESP32-C3 have no descriptions in this schema yet.
- **No hardware check.** The STM32F446ZE package file is `curated: false`, because no physical board has checked it.
