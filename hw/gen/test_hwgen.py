# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

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
UNO = "boards/arduino/arduino_uno"
PICO = "boards/rpi/pico2_w"
# the NUCLEO-F446RE's partitions, as its description lists them: the tests replace them whole
NUCLEO_PARTITIONS = ("- { name: app, base: 0x08000000, size: 384K }        # sectors 0 to 6\n"
                     "  - { name: storage, base: 0x08060000, size: 128K }    # sector 7")


class Root:
    """A scratch repository root holding copies of the descriptions a test edits."""

    def __init__(self, boards=(BOARD,)):
        self.dir = Path(tempfile.mkdtemp(prefix="hwgen-"))
        shutil.copytree(ROOT / "soc", self.dir / "soc")
        (self.dir / "drivers").mkdir()
        shutil.copy(ROOT / "drivers" / "bindings.yaml", self.dir / "drivers" / "bindings.yaml")
        for port in ("cortex_m", "avr"):
            (self.dir / "arch" / port).mkdir(parents=True)
            shutil.copy(ROOT / "arch" / port / "arch.yaml", self.dir / "arch" / port / "arch.yaml")
        for board in boards:
            (self.dir / board).mkdir(parents=True)
            shutil.copy(ROOT / board / "board.yaml", self.dir / board / "board.yaml")

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
        self.root.edit(f"{BOARD}/board.yaml", NUCLEO_PARTITIONS,
                       "- { name: app, base: 0x08000000, size: 20K }")
        self.expect(f"{BOARD}/board.yaml", self.board_line("name: app"), "flash_partitions.app", "erase-sector")

    def test_app_partition_must_start_where_the_core_boots(self):
        """A82: a Cortex-M core takes its vector table from the boot region's start; an image
        elsewhere (MCUboot's slot 0) would need a bootloader."""
        self.root.edit(f"{BOARD}/board.yaml", NUCLEO_PARTITIONS,
                       "- { name: boot, base: 0x08000000, size: 128K, reserved: true }\n  - { name: app, base: 0x08020000, size: 384K }")
        self.expect(f"{BOARD}/board.yaml", self.board_line("name: app, base: 0x08020000"), "flash_partitions.app",
                    "the core boots from 0x08000000")

    def test_partition_outside_flash(self):
        self.root.edit(f"{BOARD}/board.yaml", NUCLEO_PARTITIONS,
                       "- { name: app, base: 0x08000000, size: 1M }")
        self.expect(f"{BOARD}/board.yaml", self.board_line("name: app"), "flash_partitions.app", "outside flash")

    def test_overlapping_partitions(self):
        self.root.edit(f"{BOARD}/board.yaml", NUCLEO_PARTITIONS,
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
        # Armv8-M Baseline: a variant the Cortex-M port does not have (Armv6-M it has, A86)
        self.root.edit("soc/st/stm32f4/stm32f4.yaml", "variant: armv7em", "variant: armv8m-base")
        f = "soc/st/stm32f4/stm32f4.yaml"
        self.expect(f, self.root.line_of(f, "arch:"), "arch.variant", "no variant 'armv8m-base'")

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


class ChipSelects(unittest.TestCase):
    """devices[].cs (SPEC-017 §5, A116): an SPI device's chip selects, each a line of one of the
    board's GPIO devices, named in soc_devices.h and set up deasserted by the pin table."""

    def setUp(self):
        self.root = Root()
        self.addCleanup(self.root.close)
        self.board = f"{BOARD}/board.yaml"

    def files(self):
        files, _ = generate.generate(self.root.dir / BOARD, self.root.dir)
        return files

    def expect(self, needle, path, message):
        with self.assertRaises(HwError) as cm:
            self.root.resolve()
        want = f"{self.board}:{self.root.line_of(self.board, needle)}: {path}: "
        hits = [e for e in cm.exception.errors if e.startswith(want) and re.search(message, e)]
        self.assertTrue(hits, f"expected '{want}...{message}', got:\n" + "\n".join(cm.exception.errors))

    def test_active_low_by_default(self):
        files = self.files()
        self.assertIn("#define EMB_SOC_SPI2_CS0 {&emb_dev_gpiob, 12u, 1u, {0u, 0u}} /* PB12 */", files["soc_devices.h"])
        self.assertIn("{EMB_STM32_PORT(B), 12u, EMB_STM32_PIN_OUTPUT, 0u, 1u, 0u},", files["board_pins.c"])  # starts high
        self.assertIn("set(EMB_BOARD_SPI2_CS_COUNT 1)", files["board.cmake"])

    def test_active_high(self):
        self.root.edit(self.board, "cs: [PB12]", "cs: [{ pin: PB12, active: high }]")
        files = self.files()
        self.assertIn("#define EMB_SOC_SPI2_CS0 {&emb_dev_gpiob, 12u, 0u, {0u, 0u}} /* PB12 */", files["soc_devices.h"])
        self.assertIn("{EMB_STM32_PORT(B), 12u, EMB_STM32_PIN_OUTPUT, 0u, 0u, 0u},", files["board_pins.c"])  # starts low

    def test_needs_the_ports_device(self):
        self.root.edit(self.board, '  - { name: gpiob, doc: "SPI2\'s chip select, PB12" }\n', "")
        self.expect("cs: [PB12]", "devices.spi2.cs[0]", "PB12 is a line of gpiob, which the board does not list")

    def test_only_spi_devices(self):
        self.root.edit(self.board, "{ name: uart4,", "{ name: uart4, cs: [PB1],")
        self.expect("name: uart4", "devices.uart4.cs", "not an SPI controller or a device on one")

    def test_a_device_on_the_bus_names_its_own(self):
        """SPEC-014 §4's example: a target device on an SPI controller names its chip select."""
        self.root.edit(self.board, "  - { name: uart4,", "  - { name: nor, compatible: \"acme,flash\", bus: spi2, cs: PB1 }\n  - { name: uart4,")
        (self.root.dir / "drivers" / "bindings.yaml").write_text(
            (self.root.dir / "drivers" / "bindings.yaml").read_text() +
            "  acme,flash: { class: flash, level: kernel, sources: [drivers/acme/flash.c], header: flash.h, api: flash_api, config: flash_config_t }\n",
            encoding="utf-8")
        files = self.files()
        self.assertIn("#define EMB_SOC_NOR_CS {&emb_dev_gpiob, 1u, 1u, {0u, 0u}} /* PB1 */", files["soc_devices.h"])
        self.root.edit(self.board, "bus: spi2, cs: PB1 }", "bus: spi2, cs: [PB1, PB2] }")
        self.expect("name: nor", "devices.nor.cs", "a device on an SPI bus has one chip select")

    def test_a_pin_has_one_owner(self):
        self.root.edit(self.board, "cs: [PB12]", "cs: [PB13]")  # SPI2's clock
        self.expect("cs: [PB13]", "devices.spi2.cs[0]", "PB13 is already assigned")


class DmaChannels(unittest.TestCase):
    """devices[].dma and peripherals[].dma_requests (A119): each request a board asks for gets
    a channel no other device has, named in the configuration and soc_devices.h."""

    def setUp(self):
        self.root = Root()
        self.addCleanup(self.root.close)
        self.board = f"{BOARD}/board.yaml"

    def errors(self):
        with self.assertRaises(HwError) as cm:
            self.root.resolve()
        return cm.exception.errors

    def test_assigned_and_named(self):
        files, _ = generate.generate(self.root.dir / BOARD, self.root.dir)
        self.assertIn(".dma_tx = {&emb_dev_dma1, 4u, 0u, {0u, 0u}}, /* dma.tx */", files["soc_devices.c"])
        self.assertIn("#define EMB_SOC_SPI2_DMA_RX {&emb_dev_dma1, 3u, 0u, {0u, 0u}}", files["soc_devices.h"])
        self.assertIn("set(EMB_BOARD_SPI2_DMA rx tx)", files["board.cmake"])
        self.assertIn("{0x20000000u, 0x0001C000u}, /* sram1 */", files["soc_devices.c"])  # where DMA reaches

    def test_a_stream_two_devices_want(self):
        # UART4's transmit is on DMA1 stream 4 alone, which SPI2's transmit took first
        self.root.edit(self.board, "{ name: uart4,", "{ name: uart4, dma: [tx],")
        self.assertTrue(any("devices.uart4.dma" in e and "every channel it can use is taken" in e
                            for e in self.errors()), self.errors())

    def test_a_controller_the_board_lacks(self):
        self.root.edit(self.board, '  - { name: dma1, doc: "DMA1: SPI2\'s transfers, and the console\'s writes" }\n', "")
        self.assertTrue(any("devices.spi2.dma" in e and "does not list in devices" in e for e in self.errors()))
        self.assertTrue(any("devices.usart2.dma" in e for e in self.errors()))

    def test_a_request_the_device_lacks(self):
        self.root.edit(self.board, "dma: [tx, rx],", "dma: [tx, sideways],")
        self.assertTrue(any("has no DMA request 'sideways'" in e for e in self.errors()))

    def test_a_stream_the_controller_lacks(self):
        self.root.edit("soc/st/stm32f4/stm32f446.yaml", "dma_requests: { tx: dma1.s4c0, rx: dma1.s3c0 }",
                       "dma_requests: { tx: dma1.s9c0, rx: dma1.s3c0 }")
        self.assertTrue(any("peripherals.spi2.dma_requests.tx" in e and "8 streams" in e for e in self.errors()))


class FlashGeometry(unittest.TestCase):
    """A flash device's geometry (A122): the SoC's sectors, listed or of one size up to the
    board's chip size, in its configuration; the partitions on it, EMB_FLASH_AREA_<NAME>; and the
    RAM code section the RP2350's flash routines run from."""

    def setUp(self):
        self.root = Root(boards=(BOARD, PICO))
        self.addCleanup(self.root.close)

    def errors(self, board):
        with self.assertRaises(HwError) as cm:
            self.root.resolve(board)
        return cm.exception.errors

    def test_listed_sectors_and_partitions(self):
        files, _ = generate.generate(self.root.dir / BOARD, self.root.dir)
        c, h = files["soc_devices.c"], files["soc_devices.h"]
        self.assertIn("static const uint32_t flash_sectors[8] = {0x4000u, 0x4000u, 0x4000u, 0x4000u, "
                      "0x10000u, 0x20000u, 0x20000u, 0x20000u};", c)
        self.assertIn(".geometry = {0x08000000u, 0x00080000u, flash_sectors, 0u, 8u, 1u, 0xFFu, {0u, 0u, 0u}},", c)
        self.assertIn('const emb_flash_area_t emb_flash_area_storage = {"storage", &emb_dev_flash, 0x00060000u, '
                      "0x00020000u};", c)
        self.assertIn("#define EMB_FLASH_AREA_STORAGE (&emb_flash_area_storage)", h)
        self.assertIn("#include <emb/flash_area.h>", h)
        self.assertIn("*(.ramfunc .ramfunc.*)", files["regions.ld"])

    def test_uniform_sectors_up_to_the_chip(self):
        files, _ = generate.generate(self.root.dir / PICO, self.root.dir)
        c = files["soc_devices.c"]
        self.assertIn(".geometry = {0x10000000u, 0x00400000u, NULL, 4096u, 1024u, 256u, 0xFFu, {0u, 0u, 0u}},", c)
        self.assertIn('{"storage", &emb_dev_flash, 0x003F0000u, 0x00010000u};', c)

    def test_listed_sectors_cover_the_region(self):
        self.root.edit(f"{BOARD}/board.yaml", "{ name: flash, peripheral: flash_if,", "{ name: flash, peripheral: flash_if, size: 256K,")
        self.assertTrue(any("devices.flash.size" in e and "whole region" in e for e in self.errors(BOARD)))

    def test_a_size_of_whole_sectors(self):
        self.root.edit(f"{PICO}/board.yaml", "peripheral: qmi, size: 4M,", "peripheral: qmi, size: 4097K,")
        self.assertTrue(any("devices.flash.size" in e and "4096-byte sectors" in e for e in self.errors(PICO)))

    def test_a_size_beyond_the_window(self):
        self.root.edit(f"{PICO}/board.yaml", "peripheral: qmi, size: 4M,", "peripheral: qmi, size: 32M,")
        self.assertTrue(any("devices.flash.size" in e and "within its region xip" in e for e in self.errors(PICO)))


class PinLines(unittest.TestCase):
    """A binding's pins.<signal> (A118): the GPIO line a device's pin is on, in its
    configuration and as EMB_SOC_<DEVICE>_<SIGNAL>, for an I2C bus's recovery."""

    def setUp(self):
        self.root = Root()
        self.addCleanup(self.root.close)
        self.board = f"{BOARD}/board.yaml"

    def test_lines_in_the_configuration_and_the_header(self):
        files, _ = generate.generate(self.root.dir / BOARD, self.root.dir)
        self.assertIn(".scl = {&emb_dev_gpiob, 8u, 0u, {0u, 0u}}, /* pins.scl */", files["soc_devices.c"])
        self.assertIn("#define EMB_SOC_I2C2_SDA {&emb_dev_gpiob, 3u, 0u, {0u, 0u}} /* PB3 */", files["soc_devices.h"])

    def test_a_port_without_its_gpio_device(self):
        self.root.edit(self.board, "  - { name: gpiob, doc: \"SPI2's chip select, PB12\" }\n", "")
        with self.assertRaises(HwError) as cm:
            self.root.resolve()
        self.assertTrue(any("devices.i2c1" in e and "reads pins.scl" in e for e in cm.exception.errors),
                        "\n".join(cm.exception.errors))


class Bank(unittest.TestCase):
    """A79: a SoC with one numbered GPIO bank (the RP2350): GPIO<n>.f<k> pins, the bank's line
    count, its function selects, and the RP2350 pin table."""

    def setUp(self):
        self.root = Root(boards=(PICO, BOARD))
        self.addCleanup(self.root.close)

    def expect(self, board, file, needle, path, message):
        with self.assertRaises(HwError) as cm:
            self.root.resolve(board)
        want = f"{file}:{self.root.line_of(file, needle)}: {path}: "
        hits = [e for e in cm.exception.errors if e.startswith(want) and re.search(message, e)]
        self.assertTrue(hits, f"expected '{want}...{message}', got:\n" + "\n".join(cm.exception.errors))

    def test_pin_table(self):
        files, _ = generate.generate(ROOT / PICO, ROOT)
        self.assertIn("{0u, 2u, 1u, EMB_RP2350_PULL_RESET, EMB_RP2350_OUT_NONE},", files["board_pins.c"])  # UART0 TX
        self.assertIn("{1u, 2u, 1u, EMB_RP2350_PULL_UP, EMB_RP2350_OUT_NONE},", files["board_pins.c"])  # RX idles high
        # SPI1's chip select: an SIO output, its input on so it reads back, starting deasserted
        self.assertIn("{13u, 5u, 1u, EMB_RP2350_PULL_RESET, EMB_RP2350_OUT_HIGH},", files["board_pins.c"])
        self.assertIn("#define EMB_SOC_SPI1_CS0 {&emb_dev_io_bank0, 13u, 1u, {0u, 0u}} /* GPIO13 */",
                      files["soc_devices.h"])
        self.assertIn("set(EMB_BOARD_FLASH_IMAGE uf2)", files["board.cmake"])
        self.assertIn("set(EMB_BOARD_UF2_FAMILY 0xE48BFF59)", files["board.cmake"])
        self.assertIn("0x10210142u, /* item IMAGE_TYPE (0x42), one word: 0x1021 */", files["boot_block.c"])

    def test_line_beyond_the_bank(self):
        f = f"{PICO}/board.yaml"
        self.root.edit(f, "tx: GPIO0.f2", "tx: GPIO30.f2")
        self.expect(PICO, f, "tx: GPIO30.f2", "pins.uart0.tx", "GPIO30 is not bonded on the qfn60 package")

    def test_wrong_function_select(self):
        f = f"{PICO}/board.yaml"
        self.root.edit(f, "tx: GPIO0.f2", "tx: GPIO0.f11")
        self.expect(PICO, f, "tx: GPIO0.f11", "pins.uart0.tx", r"GPIO0 f11 is not uart0_tx \(GPIO0 offers f2 uart0_tx\)")

    def test_function_select_needed(self):
        f = f"{PICO}/board.yaml"
        self.root.edit(f, "tx: GPIO0.f2", "tx: GPIO0")
        self.expect(PICO, f, "tx: GPIO0,", "pins.uart0.tx", r"needs its alternate function: GPIO0\.f<n>")

    def test_pin_styles_do_not_mix(self):
        f = f"{PICO}/board.yaml"
        self.root.edit(f, "tx: GPIO0.f2", "tx: PA2.af7")
        self.expect(PICO, f, "tx: PA2.af7", "pins.uart0.tx", "names its pins GPIO<n>")
        g = f"{BOARD}/board.yaml"
        self.root.edit(g, "tx: PA2.af7", "tx: GPIO2.f7")
        self.expect(BOARD, g, "tx: GPIO2.f7", "pins.usart2.tx", "names its pins P<port><n>")

    def test_bank_soc_rules(self):
        die = "soc/rpi/rp2350/rp2350.yaml"
        self.root.edit(die, "  GPIO29: { f2: uart0_rx }", "  GPIO31: { f2: uart0_rx }")
        self.root.edit(die, "  GPIO28: { f2: uart0_tx }", "  GPIO28: { af2: uart0_tx }")
        with self.assertRaises(HwError) as cm:
            self.root.resolve(PICO)
        text = "\n".join(cm.exception.errors)
        self.assertIn("alternate_functions.GPIO31: beyond the bank's 30 lines", text)
        self.assertIn("alternate_functions.GPIO28.af2: not a function select (f0 to f31)", text)

    def test_ports_and_lines_exclude_each_other(self):
        part = "soc/rpi/rp2350/rp2350a.yaml"
        self.root.edit(part, "gpio: { lines: 30, controller: io_bank0 }", "gpio: { lines: 30, controller: io_bank0, ports: { A: 1 } }")
        with self.assertRaises(HwError):
            self.root.resolve(PICO)


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
        self.assertEqual(sorted(p.name for p in c_files), ["board_pins.c", "hw_config.h", "soc_devices.c", "soc_devices.h"])
        for p in c_files:
            self.assertEqual(p.read_text(encoding="utf-8"), files[p.name])  # generated, not edited
        re_cmake = (ROOT / BOARD / "generated/board.cmake").read_text(encoding="utf-8")
        self.assertIn('set(EMB_SOC_DIR "${EMB_BASE}/soc/st/stm32f4")', re_cmake)
        self.assertIn('set(EMB_SOC_DIR "${EMB_BASE}/soc/st/stm32f4")', files["board.cmake"])
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
        for board in sorted(p.parent for p in ROOT.glob("boards/*/*/generated")):
            files, _ = generate.generate(board, ROOT)
            with self.subTest(board=board.name):
                self.assertEqual(schema.validate(json.loads(files["hw.json"]), schema.load_schema("hw")), [])

    def test_harness_metadata_fields(self):
        """HW-015: board, SoC, clock, timer source and probe hints are in hw.json."""
        data = json.loads(generate.generate(ROOT / BOARD, ROOT)[0]["hw.json"])
        self.assertEqual(data["board"]["name"], "nucleo_f446re")
        self.assertEqual(data["soc"]["part"], "STM32F446RE")
        self.assertEqual(data["derived"]["clock"]["hclk_hz"], 180_000_000)
        self.assertEqual(data["derived"]["timer"], {"source": "systick", "hz": 180_000_000})
        self.assertIn("openocd", data["board"]["debug_probe"])


BINDINGS = """\
schema_version: 0
kind: bindings
bindings:
  st,stm32f4-rcc: { class: clock, level: early, sources: [drivers/st/rcc_dev.c], header: rcc.h,
                    api: rcc_api, config: rcc_config_t, fields: { base: peripheral.base } }
  st,stm32-gpio: { class: gpio, level: early, sources: [drivers/st/gpio_dev.c], header: gpio.h, api: gpio_api,
                   config: gpio_config_t,
                   fields: { base: peripheral.base, gate: peripheral.clock.bit, enable: peripheral.clock.enable_offset } }
  st,stm32-usart: { class: uart, level: early, sources: [drivers/st/usart_dev.c, drivers/st/common.c],
                    include: drivers/st/include, header: usart.h, api: usart_api.dev, config: usart_config_t,
                    data: usart_data_t,
                    fields: { base: peripheral.base, clock_hz: peripheral.clock_hz, irq: peripheral.irqs.global,
                              baud: device.baud } }
  st,stm32f4-iwdg: { class: watchdog, level: kernel, sources: [drivers/st/iwdg_dev.c, drivers/st/common.c],
                     header: iwdg.h, api: iwdg_api, config: iwdg_config_t, fields: { base: peripheral.base } }
  st,stm32-tim: { class: clock, level: kernel, sources: [drivers/st/unused_tim.c], header: tim.h, api: tim_api,
                  config: tim_config_t }
  acme,part: { class: gpio, level: kernel, sources: [drivers/acme/part.c], header: part.h, api: part_api,
               config: part_config_t, fields: { bus: device.bus, address: device.address } }
"""

# the board's devices, out of dependency order on purpose: the generator sorts them
DEVICES = """devices:
  - { name: wdt, peripheral: iwdg }
  - { name: sensor, compatible: "acme,part", bus: console, address: 0x76 }
  - { name: console, peripheral: usart2, baud: 115200 }
  - { name: gpioa }
  - { name: rcc }
"""


class Devices(unittest.TestCase):
    """SPEC-016 §4 and §8 (DRV-001, DRV-009): devices bound through drivers/bindings.yaml, in
    initialization order, with only their drivers in the image."""

    def setUp(self):
        self.root = Root()
        (self.root.dir / "drivers" / "bindings.yaml").write_text(BINDINGS, encoding="utf-8")
        board = self.root.dir / BOARD / "board.yaml"
        text = re.sub(r"^devices:\n(?:  - .*\n)+", "", board.read_text(encoding="utf-8"), flags=re.M)
        text = re.sub(r"^  # [^\n]*\n  loopbacks:\n(?:    - .*\n)+|^  loopbacks:\n(?:    - .*\n)+", "", text, flags=re.M)
        board.write_text(text.replace("leds:", DEVICES + "leds:", 1), encoding="utf-8")

    def tearDown(self):
        self.root.close()

    def expect(self, message, file=f"{BOARD}/board.yaml", needle=None, path=None):
        with self.assertRaises(HwError) as cm:
            self.root.resolve()
        want = ""
        if needle is not None:
            want = f"{file}:{self.root.line_of(file, needle)}: {path}: "
        hits = [e for e in cm.exception.errors if e.startswith(want) and re.search(message, e)]
        self.assertTrue(hits, f"expected '{want}...{message}', got:\n" + "\n".join(cm.exception.errors))

    def files(self):
        return generate.generate(self.root.dir / BOARD, self.root.dir)[0]

    def test_initialization_order(self):
        """DRV-001: the clock tree first, then what it gates; EARLY before KERNEL; a part after
        its bus; ties in the description's order."""
        devs = self.root.resolve()["devices"]
        self.assertEqual([d["name"] for d in devs], ["rcc", "console", "gpioa", "wdt", "sensor"])
        by = {d["name"]: d for d in devs}
        self.assertEqual(by["console"]["deps"], ["rcc"])
        self.assertEqual(by["sensor"]["deps"], ["console"])
        self.assertEqual(by["wdt"]["deps"], [])  # the IWDG has no clock gate
        self.assertEqual([d["level"] for d in devs], ["early"] * 3 + ["kernel"] * 2)

    def test_fields_come_from_the_description(self):
        by = {d["name"]: {f["field"]: f["c"] for f in d["fields"]} for d in self.root.resolve()["devices"]}
        self.assertEqual(by["console"], {"base": "0x40004400u", "clock_hz": "45000000u", "irq": "38u", "baud": "115200u"})
        self.assertEqual(by["gpioa"], {"base": "0x40020000u", "gate": "0u", "enable": "0x00000030u"})
        self.assertEqual(by["sensor"], {"bus": "&emb_dev_console", "address": "0x00000076u"})

    def test_outputs(self):
        files = self.files()
        c, h, cmake = files["soc_devices.c"], files["soc_devices.h"], files["board.cmake"]
        self.assertIn("const emb_device_table_t emb_board_device_table = {devices, 5u, 3u};", c)
        self.assertIn("static const emb_device_t *const sensor_deps[] = {&emb_dev_console};", c)
        self.assertIn("    .api = &usart_api.dev,\n", c)
        self.assertIn("static usart_data_t console_data;", c)
        self.assertIn("    .class_id = EMB_DRV_CLASS_WATCHDOG,\n", c)
        table = c[c.index("static const emb_device_t *const devices[]"):]
        self.assertLess(table.index("&emb_dev_rcc,"), table.index("&emb_dev_console,"))
        self.assertIn("extern const emb_device_t emb_dev_sensor;", h)
        self.assertIn("#define EMB_SOC_DEVICE_COUNT 5u", h)
        # DRV-009: the image builds only the drivers its devices use, each once
        self.assertEqual(cmake.count("drivers/st/common.c"), 1)
        self.assertNotIn("unused_tim.c", cmake)
        self.assertIn('"${EMB_BASE}/drivers/st/include"', cmake)
        self.assertIn("drivers/bindings.yaml", c)  # an input, stamped
        data = json.loads(files["hw.json"])
        self.assertEqual([d["name"] for d in data["derived"]["devices"]], ["rcc", "console", "gpioa", "wdt", "sensor"])
        self.assertEqual(schema.validate(data, schema.load_schema("hw")), [])
        self.assertIn("| sensor | `acme,part` | gpio | KERNEL | console |", files["board.md"])

    def test_deterministic(self):
        self.assertEqual(self.files(), self.files())

    def test_buffers(self):
        """A binding's buffers: sized by a device property or the binding's default, emitted as
        RAM beside the configuration, which names them and their sizes."""
        self.root.edit("drivers/bindings.yaml", "data: usart_data_t,",
                       "data: usart_data_t, buffers: { rx: { size: device.rx_buffer, default: 64 } },")
        c = self.files()["soc_devices.c"]
        self.assertIn("static uint8_t console_rx[64];", c)
        self.assertIn("    .rx = console_rx,", c)
        self.assertIn("    .rx_size = 64u,", c)
        self.root.edit(f"{BOARD}/board.yaml", "peripheral: usart2, baud: 115200 }",
                       "peripheral: usart2, baud: 115200, rx_buffer: 256 }")
        self.assertIn("static uint8_t console_rx[256];", self.files()["soc_devices.c"])
        self.root.edit(f"{BOARD}/board.yaml", "rx_buffer: 256", "rx_buffer: 70000")
        self.expect(r"buffer rx needs a size of 1 to 65535", needle="name: console", path="devices.console")

    def test_loopbacks(self):
        """simulation.loopbacks: a UART or SPI device, wired by two ports or looped inside."""
        self.root.edit(f"{BOARD}/board.yaml", "simulation:\n", "simulation:\n  loopbacks: [ { device: console, ports: [A0, A1] } ]\n")
        self.assertIn('set(EMB_BOARD_EMBSIM_UART_LOOP console "A0,A1")', self.files()["board.cmake"])
        self.root.edit(f"{BOARD}/board.yaml", "{ device: console, ports: [A0, A1] }", "{ device: console, internal: true }")
        self.assertIn("set(EMB_BOARD_UART_LOOP_INTERNAL console)", self.files()["board.cmake"])
        self.root.edit(f"{BOARD}/board.yaml", "{ device: console, internal: true }", "{ device: rcc, internal: true }")
        self.expect(r"rcc is not a UART, SPI or I2C device", needle="device: rcc", path="simulation.loopbacks[0]")

    def test_loopback_names_one_way(self):
        self.root.edit(f"{BOARD}/board.yaml", "simulation:\n", "simulation:\n  loopbacks: [ { device: console } ]\n")
        self.expect(r"two ports or internal: true", needle="device: console }", path="simulation.loopbacks[0]")

    def test_presence_and_console_macros(self):
        h = self.files()["soc_devices.h"]
        self.assertIn("#define EMB_SOC_HAS_SENSOR  1", h)
        self.assertIn("#define EMB_SOC_HAS_CONSOLE 1", h)
        self.assertIn("#define EMB_SOC_CONSOLE_DEVICE (&emb_dev_console)", h)  # the device on usart2

    def test_board_without_devices(self):
        """Every board defines its device table, so a board's devices can never be left out by
        the linker; without devices it is empty, builds no driver and does not read the
        bindings."""
        self.root.edit(f"{BOARD}/board.yaml", DEVICES, "")
        files = self.files()
        self.assertIn("const emb_device_table_t emb_board_device_table = {NULL, 0u, 0u};", files["soc_devices.c"])
        self.assertIn("#define EMB_SOC_DEVICE_COUNT 0u", files["soc_devices.h"])
        cmake = files["board.cmake"]
        sources = cmake[cmake.index("set(EMB_BOARD_DRIVER_SOURCES"):]
        self.assertEqual(sources[:sources.index(")")].count('"'), 2)  # soc_devices.c alone
        self.assertNotIn("EMB_BOARD_DRIVER_INCLUDE_DIRS", cmake)
        self.assertNotIn("bindings.yaml", cmake)

    def test_unknown_compatible(self):
        """DRV-009: a compatible no driver binds is refused."""
        self.root.edit(f"{BOARD}/board.yaml", '"acme,part"', '"acme,other"')
        self.expect(r"no driver binds acme,other", needle="name: sensor", path="devices.sensor")

    def test_peripheral_without_binding(self):
        self.root.edit(f"{BOARD}/board.yaml", "{ name: rcc }", "{ name: rcc }\n  - { name: adc1 }")
        self.expect(r"no driver binds st,stm32f4-adc", needle="name: adc1", path="devices.adc1")

    def test_cycle(self):
        self.root.edit(f"{BOARD}/board.yaml", "{ name: gpioa }", "{ name: gpioa, deps: [console] }")
        self.root.edit(f"{BOARD}/board.yaml", "peripheral: usart2, baud: 115200 }",
                       "peripheral: usart2, baud: 115200, deps: [gpioa] }")
        self.expect(r"dependency cycle: console -> gpioa -> console", needle="name: console", path="devices.console")

    def test_early_on_kernel(self):
        self.root.edit(f"{BOARD}/board.yaml", "{ name: gpioa }", "{ name: gpioa, deps: [wdt] }")
        self.expect(r"an EARLY device cannot depend on the KERNEL device wdt", needle="name: gpioa", path="devices.gpioa")

    def test_unresolved_dependency(self):
        self.root.edit(f"{BOARD}/board.yaml", "bus: console", "bus: i2c9")
        self.expect(r"no device 'i2c9'", needle="name: sensor", path="devices.sensor")

    def test_unresolved_peripheral(self):
        self.root.edit(f"{BOARD}/board.yaml", "peripheral: iwdg", "peripheral: wwdg")
        self.expect(r"the SoC has no peripheral 'wwdg'", needle="name: wdt", path="devices.wdt")

    def test_peripheral_used_twice(self):
        self.root.edit(f"{BOARD}/board.yaml", "{ name: rcc }", "{ name: rcc }\n  - { name: usart2 }")
        self.expect(r"usart2 is already the device console", needle="name: usart2 }", path="devices.usart2")

    def test_peripheral_and_compatible(self):
        self.root.edit(f"{BOARD}/board.yaml", "peripheral: iwdg", 'peripheral: iwdg, compatible: "acme,part"')
        self.expect(r"a SoC peripheral or a part", needle="name: wdt", path="devices.wdt")

    def test_missing_field(self):
        self.root.edit(f"{BOARD}/board.yaml", "peripheral: usart2, baud: 115200", "peripheral: usart2")
        self.expect(r"field baud reads device.baud, which this device does not have", needle="name: console",
                    path="devices.console")

    def test_duplicate_device(self):
        self.root.edit(f"{BOARD}/board.yaml", "{ name: rcc }", "{ name: rcc }\n  - { name: rcc }")
        self.expect(r"duplicate")

    def test_binding_schema(self):
        self.root.edit("drivers/bindings.yaml", "class: watchdog", "class: watchdogs")
        self.expect(r"must be one of", file="drivers/bindings.yaml", needle="class: watchdogs",
                    path="bindings.st,stm32f4-iwdg.class")


class OutOfTree(unittest.TestCase):
    """A76: a project's own board, kept outside the tree under an EMB_BOARD_ROOT, uses the tree's
    SoC descriptions; its outputs name its own files from its directory ('@board/'), never the
    machine's paths."""

    def project(self, name="acme_m4"):
        d = Path(tempfile.mkdtemp(prefix="hwgen-project-"))
        self.addCleanup(shutil.rmtree, d)
        board = d / "boards" / "acme" / name
        board.mkdir(parents=True)
        text = (ROOT / "boards/arm/mps2_an386/board.yaml").read_text(encoding="utf-8")
        (board / "board.yaml").write_text(text.replace("name: mps2_an386", f"name: {name}"), encoding="utf-8")
        return d, board

    def test_inputs_are_stamped_from_the_board(self):
        d, board = self.project()
        files, _ = generate.generate(board, ROOT)
        cmake = files["board.cmake"]
        self.assertRegex(cmake, r'"@board/board\.yaml=[0-9a-f]{64}"')
        self.assertIn('"soc/arm/mps2/an386.yaml=', cmake)
        self.assertIn("generate --board <path to>/acme_m4", cmake)
        for name, text in files.items():
            with self.subTest(file=name):
                self.assertNotIn(str(d), text)
                self.assertNotIn(str(d.resolve()), text)

    def test_location_independent(self):
        """HW-013 for a board outside the tree: two projects in two places give the same bytes."""
        _, a = self.project()
        _, b = self.project()
        self.assertEqual(generate.generate(a, ROOT)[0], generate.generate(b, ROOT)[0])

    def test_terminal_messages_keep_the_real_path(self):
        d, board = self.project()
        _, warnings = generate.generate(board, ROOT)
        self.assertEqual(warnings, [])  # mps2_an386 is curated: no uncurated warning
        (board / "board.yaml").write_text((board / "board.yaml").read_text().replace("curated: true", "curated: false"))
        files, warnings = generate.generate(board, ROOT)
        self.assertTrue(any(str((board / "board.yaml").resolve()) in w for w in warnings), warnings)
        self.assertIn("@board/board.yaml:1: (root): uncurated description", files["hw.json"])

    def soc_project(self):
        """A board root that also holds its own SoC family (A84): a copy of the MPS2's."""
        d, board = self.project()
        shutil.copytree(ROOT / "soc/arm/mps2", d / "soc/acme/mps2x")
        for f in (d / "soc/acme/mps2x").glob("*.yaml"):
            f.write_text(f.read_text().replace("vendor: arm", "vendor: acme").replace("family: mps2", "family: mps2x"))
        y = board / "board.yaml"
        y.write_text(y.read_text().replace("soc: arm/mps2/an386.yaml", "soc: acme/mps2x/an386.yaml"))
        return d, board

    def test_soc_in_the_board_root(self):
        d, board = self.soc_project()
        files, _ = generate.generate(board, ROOT)
        self.assertIn('"@board/../../../soc/acme/mps2x/an386.yaml=', files["board.cmake"])
        self.assertIn('get_filename_component(EMB_SOC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../soc/acme/mps2x" ABSOLUTE)',
                      files["board.cmake"])
        for text in files.values():
            self.assertNotIn(str(d.resolve()), text)

    def test_soc_in_both_places_refused(self):
        d, board = self.soc_project()
        shutil.copytree(ROOT / "soc/arm/mps2", d / "soc/arm/mps2")
        y = board / "board.yaml"
        y.write_text(y.read_text().replace("soc: acme/mps2x/an386.yaml", "soc: arm/mps2/an386.yaml"))
        with self.assertRaises(HwError) as cm:
            generate.generate(board, ROOT)
        self.assertTrue(any("is both in the tree and in the board root" in e for e in cm.exception.errors))

    def test_facts_check_on_a_board_root(self):
        d, board = self.soc_project()
        c = d / "soc/acme/mps2x/board.c"
        c.write_text(c.read_text() + "\n#define STRAY 0x40004000u\n")
        self.assertTrue(any("board.c" in p and "0x40004000u" in p for p in facts.check(d)))

    def test_check_board_root(self):
        d, board = self.project()
        tool = [sys.executable, "-I", str(HERE / "emb-hwgen.py")]
        import subprocess
        run = lambda *a: subprocess.run(tool + list(a), capture_output=True, text=True)  # noqa: E731
        self.assertEqual(run("generate", "--board", str(board)).returncode, 0)
        r = run("check", "--board-root", str(d))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("1 board(s), 0 stale file(s)", r.stdout)
        (board / "board.yaml").write_text((board / "board.yaml").read_text().replace("baud: 115200", "baud: 57600"))
        r = run("check", "--board-root", str(d))
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        r = run("check", "--board-root", str(board))
        self.assertEqual(r.returncode, 2)
        self.assertIn("has no boards/ directory", r.stderr)


class Avr(unittest.TestCase):
    """The ATmega328P and the Arduino Uno: separate address spaces, vectors, fixed-function pins,
    page-sized erase units, and HW-006's second linker output."""

    def files(self):
        return generate.generate(ROOT / UNO, ROOT)[0]

    def test_linker_outputs_agree(self):
        """HW-006: regions.ld and embld.opts describe the same text and data regions."""
        f = self.files()
        memory = re.findall(r"^\s*(text|data)\s+\([^)]*\)\s*:\s*ORIGIN = (0x[0-9a-f]+), LENGTH = (\d+)$",
                            f["regions.ld"], re.M)
        mem = {name: (int(origin, 16), int(length)) for name, origin, length in memory}
        opts = dict(line.split("=", 1) for line in f["embld.opts"].splitlines() if line.startswith("-"))
        self.assertEqual(int(opts["-Ttext"], 16), mem["text"][0])
        self.assertEqual(int(opts["--rom-limit"]), mem["text"][1])
        self.assertEqual(int(opts["-Tdata"], 16), mem["data"][0])
        self.assertEqual(mem["text"], (0, 32256))  # the app partition, below Optiboot
        self.assertEqual(mem["data"], (0x800100, 2048))

    def test_embcc_toolchain_uses_the_generated_options(self):
        """The EmbCC AVR toolchain file passes embld exactly the generated option set."""
        opts = [line for line in self.files()["embld.opts"].splitlines() if line.startswith("-")]
        toolchain = (ROOT / "cmake/toolchains/embcc-avr.cmake").read_text(encoding="utf-8")
        for o in opts:
            self.assertIn(f"-Wl,{o}", toolchain)

    def test_hand_written_values_preserved(self):
        h = self.files()["hw_config.h"]
        vectors = ["RESET", "INT0", "INT1", "PCINT0", "PCINT1", "PCINT2", "WDT", "TIMER2_COMPA", "TIMER2_COMPB",
                   "TIMER2_OVF", "TIMER1_CAPT", "TIMER1_COMPA", "TIMER1_COMPB", "TIMER1_OVF", "TIMER0_COMPA",
                   "TIMER0_COMPB", "TIMER0_OVF", "SPI_STC", "USART_RX", "USART_UDRE", "USART_TX", "ADC",
                   "EE_READY", "ANALOG_COMP", "TWI", "SPM_READY"]
        for n, name in enumerate(vectors):
            self.assertRegex(h, rf"#define EMB_VECTOR_{name}\s+{n}\n")
        for macro, value in [("EMB_HW_BOARD", '"arduino_uno"'), ("EMB_HW_CPU_HZ", "16000000u"),
                             ("EMB_HW_SRAM_BASE", "0x00000100u"), ("EMB_HW_SRAM_SIZE", "2048u"),
                             ("EMB_HW_CONSOLE_BAUD", "115200u"), ("EMB_HW_LED_PIN", "5u")]:
            self.assertRegex(h, rf"#define {macro}\s+{re.escape(value)}\n")
        conf = self.files()["board.conf"]
        for line in ('CONFIG_EMB_AVR_MCU="atmega328p"', "CONFIG_EMB_AVR_F_CPU=16000000", "CONFIG_EMB_TICK_NS=1000000"):
            self.assertIn(line + "\n", conf)
        cmake = self.files()["board.cmake"]
        self.assertIn("set(EMB_BOARD_DEFAULT_PROFILE tiny)", cmake)
        self.assertIn("set(EMB_BOARD_QEMU_ARGS -M arduino-uno -icount shift=6,sleep=off -bios)", cmake)

    def test_derived_model(self):
        data = json.loads(self.files()["hw.json"])
        d = data["derived"]
        self.assertIsNone(d["clock"]["pll"])
        self.assertEqual(d["timer"], {"source": "timer1", "hz": 16_000_000})
        self.assertEqual([(p["pin"], p["mode"]) for p in d["pins"]], [("PB5", "output"), ("PD0", "fixed"), ("PD1", "fixed")])
        self.assertEqual([(p["name"], p["reserved"], p["pages"]) for p in d["partitions"]],
                         [("app", False, [0, 251]), ("bootloader", True, [252, 255])])

    def setUp(self):
        self.root = Root(boards=(UNO,))

    def tearDown(self):
        self.root.close()

    def expect(self, file, line, path, message):
        with self.assertRaises(HwError) as cm:
            self.root.resolve(UNO)
        want = f"{file}:{line}: {path}: "
        hits = [e for e in cm.exception.errors if e.startswith(want) and re.search(message, e)]
        self.assertTrue(hits, f"expected '{want}...{message}', got:\n" + "\n".join(cm.exception.errors))

    def test_fixed_pin_with_another_signal(self):
        b = f"{UNO}/board.yaml"
        self.root.edit(b, "usart0: { tx: PD1, rx: PD0 }", "usart0: { tx: PD0, rx: PD1 }")
        self.expect(b, self.root.line_of(b, "usart0: {"), "pins.usart0.tx", "PD0 needs its alternate function")

    def test_partition_off_page_boundary(self):
        b = f"{UNO}/board.yaml"
        self.root.edit(b, "size: 32256 }", "size: 32250 }")
        self.expect(b, self.root.line_of(b, "name: app"), "flash_partitions.app", "erase-sector boundaries")

    def test_vectors_must_be_complete(self):
        f = "soc/microchip/atmega/atmega328p.yaml"
        self.root.edit(f, "    - { name: spm_ready, number: 25 }\n", "")
        # a list's line is its first item's
        self.expect(f, self.root.line_of(f, "name: reset"), "interrupts.vectors", "lists all 26 vectors; 25 are listed")

    def test_peripheral_line_must_be_a_vector(self):
        f = "soc/microchip/atmega/atmega328p.yaml"
        self.root.edit(f, "count: 26", "count: 30")
        self.root.edit(f, "{ name: ovf, number: 13 }", "{ name: ovf, number: 27 }")
        self.expect(f, self.root.line_of(f, "number: 27"), "peripherals.timer1.irqs.ovf", "not in interrupts.vectors")

    def test_spaces_overlap_only_within_one(self):
        f = "soc/microchip/atmega/atmega328p.yaml"
        self.root.edit(f, "{ name: sram, base: 0x0100, size: 2K, attrs: [r, w], space: data }",
                       "{ name: sram, base: 0x0100, size: 2K, attrs: [r, w], space: program }")
        self.expect(f, self.root.line_of(f, "name: sram"), "memory.regions.sram", "overlaps flash")

    def test_nvic_needs_priority_bits(self):
        root = Root()
        try:
            root.edit("soc/st/stm32f4/stm32f4.yaml", "  priority_bits: 4\n", "")
            with self.assertRaises(HwError) as cm:
                root.resolve()
            self.assertTrue(any(": interrupts: an NVIC needs priority_bits" in e for e in cm.exception.errors))
        finally:
            root.close()


class QemuBoards(unittest.TestCase):
    """The MPS2 boards and RV32 virt from their descriptions (A75): the values the hand-written
    files had, a console without pins, the CLINT, one test interrupt line."""

    def files(self, board):
        return generate.generate(ROOT / board, ROOT)[0]

    def test_mps2(self):
        for board, soc in (("boards/arm/mps2_an385", "Cortex-M3"), ("boards/arm/mps2_an386", "Cortex-M4F")):
            with self.subTest(board=board):
                f = self.files(board)
                for macro, value in [("EMB_HW_SOC", f'"{soc}"'), ("EMB_HW_CPU_HZ", "25000000u"),
                                     ("EMB_HW_UART0_BASE", "0x40004000u"), ("EMB_HW_TEST_IRQ", "30u"),
                                     ("EMB_HW_TEST_IRQ_HIGH", "31u"), ("EMB_HW_IRQ_PRIORITY_BITS", "3u")]:
                    self.assertRegex(f["hw_config.h"], rf"#define {macro}\s+{re.escape(value)}\n")
                for line in ("CONFIG_EMB_CM_CPU_HZ=25000000", "CONFIG_EMB_CM_IRQ_COUNT=32",
                             "CONFIG_EMB_CM_IRQ_PRIORITY_BITS=3", "CONFIG_EMB_TICK_NS=1000000"):
                    self.assertIn(line + "\n", f["board.conf"])
                self.assertIn("FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 4M", f["regions.ld"])
                self.assertNotIn("board_pins.c", f)

    def test_riscv32_virt(self):
        f = self.files("boards/qemu/riscv32_virt")
        for line in ("CONFIG_EMB_RV_CLINT_BASE=0x02000000", "CONFIG_EMB_RV_IRQ_COUNT=8",
                     "CONFIG_EMB_RV_MTIME_HZ=10000000"):
            self.assertIn(line + "\n", f["board.conf"])  # the hex symbol spelled as before: same hash
        self.assertRegex(f["hw_config.h"], r"#define EMB_HW_TEST_IRQ\s+0u\n")
        self.assertNotIn("EMB_HW_TEST_IRQ_HIGH", f["hw_config.h"])
        self.assertRegex(f["hw_config.h"], r"#define EMB_HW_TEST_BASE\s+0x00100000u\n")
        self.assertIn("RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 16M", f["regions.ld"])
        self.assertIn("ENTRY(embn_rv_reset)", f["regions.ld"])


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


class PostDividers(unittest.TestCase):
    """A PLL with two post-dividers (the RP2350's PLL_SYS): P is their product (A101)."""
    SOC = {"clocks": {"sources": [{"name": "xosc", "hz": "board", "min_hz": 1_000_000, "max_hz": 50_000_000}],
                      "plls": [{"name": "pll_sys", "inputs": ["xosc"], "m": [1, 63], "n": [16, 320],
                                "postdiv": [1, 7], "vco_in_hz": [5_000_000, 800_000_000],
                                "vco_out_hz": [750_000_000, 1_600_000_000]}],
                      "buses": [{"name": "sys", "from": "hclk", "max_hz": 150_000_000}]}}

    def solve(self, cpu_hz, pll=None):
        from emb_hwgen import clock
        board = {"oscillators": {"xosc": {"hz": 12_000_000, "source": "external_crystal"}},
                 "clocks": {"cpu_hz": cpu_hz, "source": "xosc"}}
        if pll:
            board["clocks"]["pll"] = pll
        problems = []
        out = clock.solve(self.SOC, board, lambda where, what: problems.append((where, what)))
        return out, problems

    def test_pico_sdk_factors_checked_and_split(self):
        c, problems = self.solve(150_000_000, {"m": 1, "n": 125, "p": 10})
        self.assertEqual(problems, [])
        self.assertEqual((c["pll"]["postdiv1"], c["pll"]["postdiv2"], c["pll"]["vco_hz"]), (5, 2, 1_500_000_000))

    def test_search_finds_a_split(self):
        c, problems = self.solve(150_000_000)
        self.assertEqual(problems, [])
        pll = c["pll"]
        self.assertEqual(pll["vco_hz"] // (pll["postdiv1"] * pll["postdiv2"]), 150_000_000)
        self.assertGreaterEqual(pll["postdiv1"], pll["postdiv2"])

    def test_p_must_be_a_product_of_two_postdividers(self):
        _, problems = self.solve(125_000_000, {"m": 1, "n": 125, "p": 11})  # 11 is prime, above 7
        self.assertIn("clocks.pll.p", [w for w, _ in problems])

    def test_above_the_bus_limit_refused(self):
        _, problems = self.solve(200_000_000, {"m": 1, "n": 100, "p": 6})
        self.assertIn("clocks.cpu_hz", [w for w, _ in problems])


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
        for kind in ("soc", "board", "hw", "bindings"):
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
        # SPEC-016 §8: the bindings, a board with devices and its hw.json, and two refusals
        cases.append(("bindings", load.load(ROOT / "drivers" / "bindings.yaml", ROOT).data))
        mps2 = load.load(ROOT / "boards/arm/mps2_an386/board.yaml", ROOT).data
        cases.append(("board", mps2))
        cases.append(("hw", json.loads(generate.generate(ROOT / "boards/arm/mps2_an386", ROOT)[0]["hw.json"])))
        bad = json.loads(json.dumps(mps2))
        bad["devices"][0]["name"] = "Uart-0"
        cases.append(("board", bad))
        bad = json.loads(json.dumps(mps2))
        bad["devices"][0]["props"] = [1]
        cases.append(("board", bad))
        for kind, doc in cases:
            s = schema.load_schema(kind)
            ours = not schema.validate(doc, s)
            theirs = jsonschema.Draft202012Validator(s).is_valid(doc)
            self.assertEqual(ours, theirs, f"{kind} document {doc.get('name')}: ours {ours}, jsonschema {theirs}")


if __name__ == "__main__":
    unittest.main()
