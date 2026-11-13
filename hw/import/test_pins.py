# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""STM32 pin importer tests (HW-012, A85): synthetic open pin data files, the bonding masks,
the alternate functions of the described peripherals, and the comparison that never writes.
With EMB_PIN_DATA naming a checkout of STM32_open_pin_data, the F446RE and F446ZE are compared
with the curated package files."""
import contextlib
import io
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
import pins  # noqa: E402

MCU = """<?xml version="1.0" encoding="UTF-8"?>
<Mcu xmlns="http://dummy.com" RefName="STM32X1">
  <Pin Name="VBAT" Position="1" Type="Power"/>
  <Pin Name="PA2" Position="2" Type="I/O"><Signal Name="USART2_TX"/><Signal Name="TIM2_CH3"/>
    <Signal IOModes="Input,Output" Name="GPIO"/></Pin>
  <Pin Name="PA3" Position="3" Type="I/O"><Signal Name="USART2_RX"/></Pin>
  <Pin Name="PC14-OSC32_IN" Position="4" Type="I/O"><Signal Name="RCC_OSC32_IN"/></Pin>
  <Pin Name="PH0-OSC_IN" Position="5" Type="I/O"/>
  <Pin Name="NRST" Position="6" Type="Reset"/>
</Mcu>
"""
GPIO = """<?xml version="1.0" encoding="UTF-8"?>
<IP xmlns="http://dummy.com" Name="GPIO">
  <GPIO_Pin PortName="PA" Name="PA2">
    <PinSignal Name="TIM2_CH3"><SpecificParameter Name="GPIO_AF"><PossibleValue>GPIO_AF1_TIM2</PossibleValue></SpecificParameter></PinSignal>
    <PinSignal Name="USART2_TX"><SpecificParameter Name="GPIO_AF"><PossibleValue>GPIO_AF7_USART2</PossibleValue></SpecificParameter></PinSignal>
  </GPIO_Pin>
  <GPIO_Pin PortName="PA" Name="PA3">
    <PinSignal Name="USART2_RX"><SpecificParameter Name="GPIO_AF"><PossibleValue>GPIO_AF7_USART2</PossibleValue></SpecificParameter></PinSignal>
  </GPIO_Pin>
</IP>
"""


class Draft(unittest.TestCase):
    def setUp(self):
        self.d = Path(tempfile.mkdtemp(prefix="pins-"))
        self.addCleanup(shutil.rmtree, self.d)
        (self.d / "mcu.xml").write_text(MCU)
        (self.d / "gpio.xml").write_text(GPIO)

    def test_masks_and_functions(self):
        ref, ps = pins.read_mcu(self.d / "mcu.xml")
        self.assertEqual(ref, "STM32X1")
        self.assertEqual(sorted(ps), ["PA2", "PA3", "PC14", "PH0"])  # I/O pins only, suffixes dropped
        ports, table = pins.draft(ps, pins.read_gpio(self.d / "gpio.xml"), {"usart2"})
        self.assertEqual(ports, {"A": 0b1100, "C": 1 << 14, "H": 1})
        self.assertEqual(table, {"PA2": {"af7": "usart2_tx"}, "PA3": {"af7": "usart2_rx"}})  # not TIM2: undescribed

    def test_compare(self):
        ps = pins.read_mcu(self.d / "mcu.xml")[1]
        ports, table = pins.draft(ps, pins.read_gpio(self.d / "gpio.xml"), {"usart2"})
        existing = {"gpio": {"ports": {"A": 0b1100, "C": 1 << 14, "H": 1}},
                    "alternate_functions": {"PA2": {"af7": "usart2_tx"}}}
        self.assertEqual(pins.compare(ports, table, existing), ([], ["PA3.af7 usart2_rx"]))
        existing["alternate_functions"]["PA2"] = {"af8": "usart2_tx"}
        existing["gpio"]["ports"]["A"] = 0b0100
        diffs, _ = pins.compare(ports, table, existing)
        self.assertIn(("gpio.ports.A", "0x0004", "0x000C"), diffs)
        self.assertIn(("alternate_functions.PA2.af8", "usart2_tx", None), diffs)

    def test_refusals(self):
        (self.d / "bad.xml").write_text("<Mcu/>")
        with self.assertRaises(pins.PinError):
            pins.read_mcu(self.d / "bad.xml")
        with self.assertRaises(pins.PinError):
            pins.read_gpio(self.d / "bad.xml")

    def test_cli_never_writes(self):
        f = ROOT / "soc/st/stm32f4/stm32f446re.yaml"
        before = f.read_bytes()
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = pins.main(["--mcu", str(self.d / "mcu.xml"), "--gpio", str(self.d / "gpio.xml"),
                            "--soc", "st/stm32f4/stm32f446re.yaml", "--against", str(f)])
        self.assertEqual(rc, 0)
        self.assertEqual(f.read_bytes(), before)
        self.assertIn("difference(s)", out.getvalue())


DATA = os.environ.get("EMB_PIN_DATA")


@unittest.skipUnless(DATA, "set EMB_PIN_DATA to a checkout of STMicroelectronics/STM32_open_pin_data")
class F446Oracle(unittest.TestCase):
    def test_packages_agree(self):
        """The curated F446RE and F446ZE package files and the F446 die's UART table agree with
        ST's pin data. The pin data also lists functions the table leaves out (CTS, RTS, CK, MCO,
        the ZE's other routes), reported apart."""
        gpio = pins.read_gpio(Path(DATA) / "mcu/IP/GPIO-STM32F446_gpio_v1_0_Modes.xml")
        for pkg, mcu in (("re", "STM32F446R(C-E)Tx.xml"), ("ze", "STM32F446Z(C-E)Tx.xml")):
            with self.subTest(package=pkg):
                rel = f"st/stm32f4/stm32f446{pkg}.yaml"
                soc = pins.load.flatten(ROOT / "soc" / rel, ROOT).data
                periph = {p["name"] for p in soc["peripherals"] if p.get("compatible") != "st,stm32-gpio"}
                ports, table = pins.draft(pins.read_mcu(Path(DATA) / "mcu" / mcu)[1], gpio, periph)
                diffs, extra = pins.compare(ports, table, soc)
                self.assertEqual(diffs, [])
                self.assertTrue(extra)


if __name__ == "__main__":
    unittest.main()
