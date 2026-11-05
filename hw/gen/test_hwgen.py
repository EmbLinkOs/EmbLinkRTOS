# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""emb-hwgen tests: golden outputs and determinism (HW-003, HW-013), the clock solver, and one
negative description per validation rule, each checking the file, line and node path the
error names (HW-002, HW-011)."""
import json
import os
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
from emb_hwgen import HwError, facts, generate, load, model, schema  # noqa: E402

BOARD = "boards/st/nucleo_f446re"


class Root:
    """A scratch repository root holding copies of the descriptions a test edits."""

    def __init__(self):
        self.dir = Path(tempfile.mkdtemp(prefix="hwgen-"))
        shutil.copytree(ROOT / "soc", self.dir / "soc")
        (self.dir / "arch/cortex_m").mkdir(parents=True)
        shutil.copy(ROOT / "arch/cortex_m/arch.yaml", self.dir / "arch/cortex_m/arch.yaml")
        (self.dir / BOARD).mkdir(parents=True)
        shutil.copy(ROOT / BOARD / "board.yaml", self.dir / BOARD / "board.yaml")

    def close(self):
        shutil.rmtree(self.dir)

    def edit(self, rel, old, new):
        p = self.dir / rel
        text = p.read_text(encoding="utf-8")
        assert old in text, f"{old!r} not in {rel}"
        p.write_text(text.replace(old, new, 1), encoding="utf-8")

    def line_of(self, rel, needle):
        for n, line in enumerate((self.dir / rel).read_text(encoding="utf-8").splitlines(), 1):
            if needle in line:
                return n
        raise AssertionError(f"{needle!r} not in {rel}")

    def resolve(self, board=BOARD):
        return model.resolve(self.dir / board, self.dir)


class Negative(unittest.TestCase):
    """Each case breaks one rule and expects exactly that error, located."""

    def setUp(self):
        self.root = Root()

    def tearDown(self):
        self.root.close()

    def expect(self, file, line, path, message):
        with self.assertRaises(HwError) as cm:
            self.root.resolve()
        want = f"{file}:{line}: {path}: "
        hits = [e for e in cm.exception.errors if e.startswith(want) and re.search(message, e)]
        self.assertTrue(hits, f"expected '{want}...{message}', got:\n" + "\n".join(cm.exception.errors))

    def board_line(self, needle):
        return self.root.line_of(f"{BOARD}/board.yaml", needle)

    # schema (HW-002)
    def test_missing_required_key(self):
        self.root.edit(f"{BOARD}/board.yaml", "console: { device: usart2, baud: 115200 }\n", "")
        self.expect(f"{BOARD}/board.yaml", self.board_line("schema_version"), "(root)", "missing required key 'console'")

    def test_unknown_key(self):
        self.root.edit(f"{BOARD}/board.yaml", "console: { device: usart2, baud: 115200 }",
                       "console: { device: usart2, baud: 115200, parity: odd }")
        self.expect(f"{BOARD}/board.yaml", self.board_line("parity"), "console.parity", "unknown key")

    def test_wrong_type(self):
        self.root.edit(f"{BOARD}/board.yaml", "baud: 115200", "baud: fast")
        self.expect(f"{BOARD}/board.yaml", self.board_line("baud: fast"), "console.baud", "expected integer")

    def test_duplicate_yaml_key(self):
        self.root.edit(f"{BOARD}/board.yaml", "vendor: st\n", "vendor: st\nvendor: stm\n")
        self.expect(f"{BOARD}/board.yaml", self.board_line("vendor: stm"), "vendor", "duplicate key")

    def test_soc_schema_error_names_the_die_file(self):
        die = "soc/st/stm32f4/stm32f446.yaml"
        self.root.edit(die, "count: 97", "count: 0")
        self.expect(die, self.root.line_of(die, "count: 0"), "interrupts.count", "below the minimum 1")

    # references
    def test_unresolved_console_device(self):
        self.root.edit(f"{BOARD}/board.yaml", "device: usart2", "device: usart9")
        self.expect(f"{BOARD}/board.yaml", self.board_line("usart9"), "console.device", "no peripheral 'usart9'")

    def test_unresolved_pin_peripheral(self):
        self.root.edit(f"{BOARD}/board.yaml", "  usart2: { tx: PA2.af7, rx: PA3.af7 }",
                       "  usart2: { tx: PA2.af7, rx: PA3.af7 }\n  spi7: { sck: PA7.af5 }")
        self.expect(f"{BOARD}/board.yaml", self.board_line("spi7"), "pins.spi7", "no peripheral 'spi7'")

    def test_missing_soc_file(self):
        self.root.edit(f"{BOARD}/board.yaml", "stm32f446re.yaml", "stm32f499xx.yaml")
        self.expect(f"{BOARD}/board.yaml", self.board_line("soc:"), "soc", "no SoC description")

    def test_abstract_soc_refused(self):
        self.root.edit(f"{BOARD}/board.yaml", "stm32f446re.yaml", "stm32f446.yaml")
        self.expect(f"{BOARD}/board.yaml", self.board_line("soc:"), "soc", "abstract")

    def test_extends_cycle(self):
        self.root.edit("soc/st/stm32f4/stm32f4.yaml", "abstract: true", "abstract: true\nextends: stm32f446re.yaml")
        with self.assertRaises(HwError) as cm:
            self.root.resolve()
        self.assertRegex(cm.exception.errors[0], r"^soc/st/stm32f4/stm32f446re\.yaml:1: extends: cycle: ")

    # duplicate names
    def test_duplicate_led_name(self):
        self.root.edit(f"{BOARD}/board.yaml", "leds: [ { name: user, gpio: PA5, active: high, doc: LD2 } ]",
                       "leds: [ { name: user, gpio: PA5, active: high }, { name: user, gpio: PA6, active: high } ]")
        self.expect(f"{BOARD}/board.yaml", self.board_line("leds:"), "leds.user", "duplicate name")

    def test_duplicate_peripheral_name(self):
        die = "soc/st/stm32f4/stm32f446.yaml"
        self.root.edit(die, "  - { name: uart5,", "  - { name: uart4,")
        second = [n for n, line in enumerate((self.root.dir / die).read_text().splitlines(), 1) if "name: uart4" in line][-1]
        self.expect(die, second, "peripherals.uart4", "duplicate name")

    # memory, partitions
    def test_overlapping_memory_regions(self):
        die = "soc/st/stm32f4/stm32f446.yaml"
        self.root.edit(die, "base: 0x2001C000", "base: 0x2001B000")
        self.expect(die, self.root.line_of(die, "name: sram2"), "memory.regions.sram2", "overlaps sram1")

    def test_partition_off_sector_boundary(self):
        self.root.edit(f"{BOARD}/board.yaml", "- { name: app, base: 0x08000000, size: 512K }",
                       "- { name: app, base: 0x08000000, size: 20K }")
        self.expect(f"{BOARD}/board.yaml", self.board_line("name: app"), "flash_partitions.app", "erase-sector")

    def test_partition_outside_flash(self):
        self.root.edit(f"{BOARD}/board.yaml", "- { name: app, base: 0x08000000, size: 512K }",
                       "- { name: app, base: 0x08000000, size: 1M }")
        self.expect(f"{BOARD}/board.yaml", self.board_line("name: app"), "flash_partitions.app", "outside flash")

    def test_overlapping_partitions(self):
        self.root.edit(f"{BOARD}/board.yaml", "- { name: app, base: 0x08000000, size: 512K }",
                       "- { name: app, base: 0x08000000, size: 128K }\n  - { name: data, base: 0x08010000, size: 64K }")
        # 0x08010000 + 64K is sector 4 exactly: inside app (0 to 4 is 128K)
        self.expect(f"{BOARD}/board.yaml", self.board_line("name: data"), "flash_partitions.data", "overlaps app")

    # interrupts
    def test_duplicate_irq(self):
        die = "soc/st/stm32f4/stm32f446.yaml"
        self.root.edit(die, "number: 39", "number: 38")
        usart3 = [n for n, line in enumerate((self.root.dir / die).read_text().splitlines(), 1) if "number: 38" in line][-1]
        self.expect(die, usart3, "peripherals.usart3.irqs.global", "already usart2.global's")

    def test_irq_out_of_range(self):
        die = "soc/st/stm32f4/stm32f446.yaml"
        self.root.edit(die, "number: 71", "number: 120")
        self.expect(die, self.root.line_of(die, "number: 120"), "peripherals.usart6.irqs.global", "above the controller's 97")

    def test_test_irq_must_be_reserved(self):
        self.root.edit(f"{BOARD}/board.yaml", "test_irqs: [61, 62]", "test_irqs: [61, 38]")
        self.expect(f"{BOARD}/board.yaml", self.board_line("test_irqs:"), "test_irqs", "line 38 has a peripheral")

    # pins
    def test_pin_assigned_twice(self):
        self.root.edit(f"{BOARD}/board.yaml", "gpio: PA5", "gpio: PA2")
        self.expect(f"{BOARD}/board.yaml", self.board_line("name: user, gpio: PA2"), "leds.user", "already assigned to pins.usart2.tx")

    def test_pin_not_bonded(self):
        self.root.edit(f"{BOARD}/board.yaml", "usart2: { tx: PA2.af7, rx: PA3.af7 }", "usart3: { tx: PD8.af7, rx: PD9.af7 }")
        self.root.edit(f"{BOARD}/board.yaml", "device: usart2", "device: usart3")
        self.expect(f"{BOARD}/board.yaml", self.board_line("usart3: {"), "pins.usart3.tx", "PD8 is not bonded on the lqfp64")

    def test_pin_wrong_alternate_function(self):
        self.root.edit(f"{BOARD}/board.yaml", "tx: PA2.af7", "tx: PA2.af8")
        self.expect(f"{BOARD}/board.yaml", self.board_line("tx: PA2.af8"), "pins.usart2.tx", "PA2 af8 is not usart2_tx")

    def test_peripheral_pin_needs_af(self):
        self.root.edit(f"{BOARD}/board.yaml", "tx: PA2.af7", "tx: PA2")
        self.expect(f"{BOARD}/board.yaml", self.board_line("tx: PA2,"), "pins.usart2.tx", "needs its alternate function")

    def test_console_needs_tx_pin(self):
        self.root.edit(f"{BOARD}/board.yaml", "usart2: { tx: PA2.af7, rx: PA3.af7 }", "usart2: { rx: PA3.af7 }")
        self.expect(f"{BOARD}/board.yaml", self.board_line("device: usart2"), "console.device", "no tx pin")

    # clocks
    def test_bus_over_limit(self):
        self.root.edit(f"{BOARD}/board.yaml", "  pll: { m: 8, n: 360, p: 2, q: 7, r: 2 }\n", "")
        self.root.edit(f"{BOARD}/board.yaml", "cpu_hz: 180MHz", "cpu_hz: 200MHz")
        self.expect(f"{BOARD}/board.yaml", self.board_line("cpu_hz"), "clocks.cpu_hz", "exceeds the AHB limit of 180 MHz")

    def test_frequency_unreachable(self):
        self.root.edit(f"{BOARD}/board.yaml", "  pll: { m: 8, n: 360, p: 2, q: 7, r: 2 }\n", "")
        self.root.edit(f"{BOARD}/board.yaml", "cpu_hz: 180MHz", "cpu_hz: 179999999")
        self.expect(f"{BOARD}/board.yaml", self.board_line("cpu_hz"), "clocks.cpu_hz", "no pll setting reaches exactly")

    def test_pll_factor_out_of_range(self):
        self.root.edit(f"{BOARD}/board.yaml", "n: 360", "n: 500")
        self.expect(f"{BOARD}/board.yaml", self.board_line("n: 500"), "clocks.pll.n", "outside pll's n range 50 to 432")

    def test_pll_factors_give_other_frequency(self):
        self.root.edit(f"{BOARD}/board.yaml", "p: 2, q", "p: 4, q")
        self.expect(f"{BOARD}/board.yaml", self.board_line("pll:"), "clocks.pll", "give 90 MHz, not the requested 180 MHz")

    def test_oscillator_above_bypass_limit(self):
        self.root.edit(f"{BOARD}/board.yaml", "hse: { hz: 8MHz", "hse: { hz: 60MHz")
        self.expect(f"{BOARD}/board.yaml", self.board_line("hse: {"), "oscillators.hse.hz", "exceeds the SoC's 50 MHz")

    # port manifest, errata, defaults
    def test_arch_variant_mismatch(self):
        self.root.edit("soc/st/stm32f4/stm32f4.yaml", "variant: armv7em", "variant: armv6m")
        f = "soc/st/stm32f4/stm32f4.yaml"
        self.expect(f, self.root.line_of(f, "arch:"), "arch.variant", "no variant 'armv6m'")

    def test_arch_port_missing(self):
        self.root.edit("soc/st/stm32f4/stm32f4.yaml", "port: cortex_m", "port: xtensa")
        f = "soc/st/stm32f4/stm32f4.yaml"
        self.expect(f, self.root.line_of(f, "arch:"), "arch.port", "no port 'xtensa'")

    def test_errata_revision_unknown(self):
        f = "soc/st/stm32f4/stm32f446re.yaml"
        self.root.edit(f, "gpio:", "errata: [ { id: \"2.1.8\", revisions: [Z] } ]\ngpio:")
        self.expect(f, self.root.line_of(f, "errata:"), "errata.2.1.8", "revision\\(s\\) Z not in cpu.revisions")

    def test_default_overrides_hardware_fact(self):
        self.root.edit(f"{BOARD}/board.yaml", "CONFIG_EMB_TICK_NS: 1000000", "CONFIG_EMB_CM_CPU_HZ: 1000")
        self.expect(f"{BOARD}/board.yaml", self.board_line("defaults:"), "defaults.CONFIG_EMB_CM_CPU_HZ",
                    "comes from the hardware description")

    def test_board_name_matches_directory(self):
        self.root.edit(f"{BOARD}/board.yaml", "name: nucleo_f446re", "name: nucleo_f446rx")
        self.expect(f"{BOARD}/board.yaml", self.board_line("name: nucleo_f446rx"), "name", "differs from its directory")


class Outputs(unittest.TestCase):
    def test_committed_outputs_are_current(self):
        """HW-003: every committed generated/ directory is exactly what the descriptions give."""
        boards = sorted(p.parent for p in ROOT.glob("boards/*/*/generated"))
        self.assertGreaterEqual(len(boards), 2)
        for board in boards:
            files, _ = generate.generate(board, ROOT)
            gen = board / "generated"
            with self.subTest(board=board.name):
                self.assertEqual(sorted(files), sorted(p.name for p in gen.iterdir() if p.is_file()))
                for name, text in files.items():
                    self.assertEqual((gen / name).read_text(encoding="utf-8"), text, name)

    def test_second_board_on_the_soc_needs_no_c(self):
        """SPEC-014 §11, the M3a generator gate: the NUCLEO-F446ZE is a description only, built by the
        same SoC support and drivers as the NUCLEO-F446RE."""
        ze = ROOT / "boards/st/nucleo_f446ze"
        self.assertEqual(sorted(p.name for p in ze.iterdir() if not p.name.startswith(".")), ["board.yaml", "generated"])
        c_files = [p for p in (ze / "generated").iterdir() if p.suffix in (".c", ".h")]
        files, warnings = generate.generate(ze, ROOT)
        self.assertEqual(sorted(p.name for p in c_files), ["board_pins.c", "hw_config.h"])
        for p in c_files:
            self.assertEqual(p.read_text(encoding="utf-8"), files[p.name])  # generated, not edited
        re_cmake = (ROOT / BOARD / "generated/board.cmake").read_text(encoding="utf-8")
        self.assertIn('set(EMB_SOC_DIR "${CMAKE_SOURCE_DIR}/soc/st/stm32f4")', re_cmake)
        self.assertIn('set(EMB_SOC_DIR "${CMAKE_SOURCE_DIR}/soc/st/stm32f4")', files["board.cmake"])
        self.assertTrue(any("nucleo_f446ze/board.yaml" in w and "uncurated" in w for w in warnings))

    def test_second_board_solves_its_own_clock(self):
        data = json.loads(generate.generate(ROOT / "boards/st/nucleo_f446ze", ROOT)[0]["hw.json"])
        pll = data["derived"]["clock"]["pll"]
        self.assertEqual((pll["m"], pll["n"], pll["p"], pll["q"]), (4, 180, 2, 8))
        self.assertEqual(data["derived"]["console"]["device"], "usart3")
        self.assertEqual([p["pin"] for p in data["derived"]["pins"] if p["mode"] == "af"], ["PD8", "PD9"])

    def test_deterministic_and_location_independent(self):
        """HW-013: two runs agree byte for byte, and so does a copy of the inputs elsewhere."""
        a, _ = generate.generate(ROOT / BOARD, ROOT)
        b, _ = generate.generate(ROOT / BOARD, ROOT)
        self.assertEqual(a, b)
        root = Root()
        try:
            c, _ = generate.generate(root.dir / BOARD, root.dir)
        finally:
            root.close()
        self.assertEqual(a, c)

    def test_stamps(self):
        files, _ = generate.generate(ROOT / BOARD, ROOT)
        data = json.loads(files["hw.json"])
        self.assertEqual(data["generator"]["name"], "emb-hwgen")
        self.assertEqual(data["schema_version"], 0)
        paths = [i["path"] for i in data["inputs"]]
        self.assertEqual(paths, sorted(paths))
        self.assertIn(f"{BOARD}/board.yaml", paths)
        for name, text in files.items():
            self.assertNotRegex(text, r"\b20\d\d-\d\d-\d\d", f"{name} carries a date")
            for i in data["inputs"]:
                if name != "hw.json":
                    self.assertIn(i["sha256"], text, f"{name} lacks the stamp of {i['path']}")

    def test_hand_written_values_preserved(self):
        """The generated header keeps every value the hand-written one had (H2 swaps them)."""
        files, _ = generate.generate(ROOT / BOARD, ROOT)
        h = files["hw_config.h"]
        for macro, value in [("EMB_HW_BOARD", '"nucleo_f446re"'), ("EMB_HW_SOC", '"STM32F446RE"'),
                             ("EMB_HW_HSE_HZ", "8000000u"), ("EMB_HW_CPU_HZ", "180000000u"),
                             ("EMB_HW_APB1_HZ", "45000000u"), ("EMB_HW_CONSOLE_BAUD", "115200u"),
                             ("EMB_HW_USART2_BASE", "0x40004400u"), ("EMB_HW_LED_PIN", "5u"),
                             ("EMB_HW_TEST_IRQ", "61u"), ("EMB_HW_TEST_IRQ_HIGH", "62u")]:
            self.assertRegex(h, rf"#define {macro}\s+{re.escape(value)}\n")
        conf = files["board.conf"]
        for line in ("CONFIG_EMB_CM_CPU_HZ=180000000", "CONFIG_EMB_CM_IRQ_COUNT=97",
                     "CONFIG_EMB_CM_IRQ_PRIORITY_BITS=4", "CONFIG_EMB_TICK_NS=1000000"):
            self.assertIn(line + "\n", conf)

    def test_hw_json_matches_its_schema(self):
        files, _ = generate.generate(ROOT / BOARD, ROOT)
        self.assertEqual(schema.validate(json.loads(files["hw.json"]), schema.load_schema("hw")), [])

    def test_harness_metadata_fields(self):
        """HW-015: board, SoC, clock, timer source and probe hints are in hw.json."""
        data = json.loads(generate.generate(ROOT / BOARD, ROOT)[0]["hw.json"])
        self.assertEqual(data["board"]["name"], "nucleo_f446re")
        self.assertEqual(data["soc"]["part"], "STM32F446RE")
        self.assertEqual(data["derived"]["clock"]["hclk_hz"], 180_000_000)
        self.assertEqual(data["derived"]["timer"], {"source": "systick", "hz": 180_000_000})
        self.assertIn("openocd", data["board"]["debug_probe"])


class Facts(unittest.TestCase):
    """HW-001 (A69): no hardware facts outside the descriptions."""

    def test_repository_is_clean(self):
        self.assertEqual(facts.check(ROOT), [])

    def test_address_in_driver_c_is_found(self):
        root = Path(tempfile.mkdtemp(prefix="hwfacts-"))
        try:
            (root / "drivers/st").mkdir(parents=True)
            (root / "soc").mkdir()
            (root / "drivers/st/x.c").write_text("#define RCC 0x40023800u\n#define MASK 0x3Fu\n")
            self.assertEqual(facts.check(root), ["drivers/st/x.c:1: address-like constant 0x40023800u; "
                                                 "take it from hw_config.h (HW-001)"])
        finally:
            shutil.rmtree(root)

    def test_hand_written_file_in_generated_board_is_found(self):
        root = Path(tempfile.mkdtemp(prefix="hwfacts-"))
        try:
            (root / "boards/st/b/generated").mkdir(parents=True)
            (root / "boards/st/b/board.yaml").write_text("")
            (root / "boards/st/b/board.c").write_text("")
            (root / "soc").mkdir()
            (root / "drivers").mkdir()
            self.assertEqual(len(facts.check(root)), 1)
            self.assertTrue(facts.check(root)[0].startswith("boards/st/b/board.c: "))
        finally:
            shutil.rmtree(root)


class Clock(unittest.TestCase):
    def solve(self, cpu_hz, pll=None):
        root = Root()
        try:
            root.edit(f"{BOARD}/board.yaml", "  pll: { m: 8, n: 360, p: 2, q: 7, r: 2 }\n",
                      f"  pll: {pll}\n" if pll else "")
            root.edit(f"{BOARD}/board.yaml", "cpu_hz: 180MHz", f"cpu_hz: {cpu_hz}")
            return root.resolve()["clock"]
        finally:
            root.close()

    def test_pinned_factors_checked(self):
        c = self.solve("180MHz", "{ m: 8, n: 360, p: 2, q: 7, r: 2 }")
        self.assertEqual((c["pll"]["m"], c["pll"]["n"], c["pll"]["p"], c["pll"]["q"]), (8, 360, 2, 7))
        self.assertEqual((c["pclk1_div"], c["pclk2_div"], c["flash_wait_states"]), (4, 2, 5))
        self.assertEqual(c["voltage_scale"], {"name": "scale1", "vos": 3, "overdrive": True})

    def test_solved_180(self):
        c = self.solve("180MHz")
        # the highest VCO input first: 8 MHz / 4 = 2 MHz, x 180 = 360 MHz, / 2
        self.assertEqual((c["pll"]["m"], c["pll"]["n"], c["pll"]["p"]), (4, 180, 2))
        self.assertEqual(c["pll"]["q"], 8)  # 45 MHz, the closest to 48 without exceeding it

    def test_solved_168(self):
        c = self.solve("168MHz")
        self.assertEqual((c["pll"]["m"], c["pll"]["n"], c["pll"]["p"], c["pll"]["q"]), (4, 168, 2, 7))
        self.assertEqual(c["voltage_scale"], {"name": "scale1", "vos": 3, "overdrive": False})
        self.assertEqual(c["flash_wait_states"], 5)

    def test_solved_84(self):
        c = self.solve("84MHz")
        self.assertEqual((c["pll"]["m"], c["pll"]["n"], c["pll"]["p"]), (4, 84, 2))
        self.assertEqual((c["pclk1_div"], c["pclk2_div"]), (2, 1))
        self.assertEqual(c["flash_wait_states"], 2)
        self.assertEqual(c["voltage_scale"]["name"], "scale3")

    def test_direct_source_needs_no_pll(self):
        c = self.solve("8MHz")
        self.assertIsNone(c["pll"])
        self.assertEqual(c["flash_wait_states"], 0)


class Loader(unittest.TestCase):
    def test_units(self):
        self.assertEqual(load.normalize("512K"), 524288)
        self.assertEqual(load.normalize("1M"), 1 << 20)
        self.assertEqual(load.normalize("16MHz"), 16_000_000)
        self.assertEqual(load.normalize("32kHz"), 32_000)
        self.assertEqual(load.normalize("1.5MHz"), 1_500_000)
        self.assertEqual(load.normalize("PA2.af7"), "PA2.af7")
        self.assertEqual(load.normalize("board"), "board")

    def test_extends_merges_named_lists(self):
        m = load.flatten(ROOT / "soc/st/stm32f4/stm32f446re.yaml", ROOT)
        self.assertEqual(m.data["name"], "stm32f446re")
        self.assertNotIn("abstract", m.data)  # a file's own keys are not inherited
        self.assertEqual(m.data["clocks"]["sources"][0]["name"], "hsi")  # from the family base
        self.assertIn("buses", m.data["clocks"])  # merged with the die's clocks
        self.assertEqual([d.name for d in m.docs], ["soc/st/stm32f4/stm32f4.yaml", "soc/st/stm32f4/stm32f446.yaml",
                                                    "soc/st/stm32f4/stm32f446re.yaml"])

    def test_every_part_validates(self):
        for part in sorted((ROOT / "soc").rglob("*.yaml")):
            m = load.flatten(part, ROOT)
            if m.leaf.data.get("abstract"):
                continue
            with self.subTest(part=part.name):
                self.assertEqual(schema.validate(m.data, schema.load_schema("soc")), [])


class Schemas(unittest.TestCase):
    def test_subset_only(self):
        for kind in ("soc", "board", "hw"):
            schema.load_schema(kind)  # raises on a keyword outside the supported subset

    def test_bool_is_not_integer(self):
        self.assertTrue(schema.validate(True, {"type": "integer"}))
        self.assertFalse(schema.validate(3, {"type": "integer"}))

    @unittest.skipUnless(__import__("importlib").util.find_spec("jsonschema"), "jsonschema not installed")
    def test_agrees_with_reference_implementation(self):
        """A67: the in-tree validator and jsonschema accept and refuse the same documents."""
        import jsonschema

        cases = []
        for part in sorted((ROOT / "soc").rglob("*.yaml")):
            m = load.flatten(part, ROOT)
            if not m.leaf.data.get("abstract"):
                cases.append(("soc", m.data))
        board = load.load(ROOT / BOARD / "board.yaml", ROOT).data
        cases.append(("board", board))
        cases.append(("hw", json.loads(generate.generate(ROOT / BOARD, ROOT)[0]["hw.json"])))
        bad = json.loads(json.dumps(board))
        bad["console"]["baud"] = "fast"
        cases.append(("board", bad))
        bad = json.loads(json.dumps(board))
        bad["leds"][0]["gpio"] = "PA16"
        cases.append(("board", bad))
        bad = json.loads(json.dumps(board))
        bad["defaults"]["CONFIG_EMB_X"] = [1]
        cases.append(("board", bad))
        bad = json.loads(json.dumps(board))
        bad["extra"] = 1
        cases.append(("board", bad))
        for kind, doc in cases:
            s = schema.load_schema(kind)
            ours = not schema.validate(doc, s)
            theirs = jsonschema.Draft202012Validator(s).is_valid(doc)
            self.assertEqual(ours, theirs, f"{kind} document {doc.get('name')}: ours {ours}, jsonschema {theirs}")


if __name__ == "__main__":
    unittest.main()
