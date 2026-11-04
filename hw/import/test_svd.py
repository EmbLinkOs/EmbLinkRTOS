# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""SVD importer tests (HW-004, HW-012, A77): the draft from a synthetic embsvd export, its
validity and YAML, the comparison that never writes, and the refusals. With embsvd on PATH a
synthetic SVD runs end to end; with EMB_SVD_STM32F446 naming ST's STM32F446 SVD (v2.0, from
modm-io/cmsis-svd-stm32) the draft is compared with the curated stm32f446.yaml."""
import contextlib
import io
import json
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "hw" / "gen"))
import svd  # noqa: E402
from emb_hwgen import load, schema  # noqa: E402


def field(name, bit):
    return {"name": name, "bitOffset": bit, "bitWidth": 1}


def periph(name, group, base, irqs=(), size=0x400, registers=()):
    return {"name": name, "groupName": group, "baseAddress": base, "derivedFrom": None,
            "addressBlocks": [{"offset": 0, "size": size, "usage": "registers"}],
            "interrupts": [{"name": n, "value": v, "description": ""} for n, v in irqs],
            "registers": list(registers)}


def export(**cpu):
    """A synthetic embsvd --json export of a small STM32-like part."""
    c = {"name": "CM4", "fpuPresent": True, "fpuDP": False, "mpuPresent": True, "nvicPrioBits": 4,
         "vendorSystickConfig": False, "deviceNumInterrupts": None}
    c.update(cpu)
    rcc_regs = [{"name": "AHB1ENR", "offset": 0x30, "fields": [field("GPIOAEN", 0), field("GPIOBEN", 1)]},
                {"name": "APB1ENR", "offset": 0x40, "fields": [field("USART2EN", 17), field("PWREN", 28)]},
                {"name": "APB1LPENR", "offset": 0x60, "fields": [field("USART2LPEN", 17)]}]
    ps = [periph("USART2", "USART", 0x40004400, [("USART2", 38), ("USART2", 38)]),
          periph("PWR", "PWR", 0x40007000),
          periph("GPIOA", "GPIO", 0x40020000),
          periph("GPIOB", "GPIO", 0x40020400),
          periph("RCC", "RCC", 0x40023800, [("RCC", 5)], registers=rcc_regs),
          periph("FLASH", "FLASH", 0x40023C00, [("FLASH", 4)]),
          periph("TIM1", "TIM", 0x40010000, [("TIM1_BRK_TIM9", 24), ("TIM1_UP_TIM10", 25)]),
          periph("EXTI", "EXTI", 0x40013C00, [("PVD", 1), ("EXTI0", 6)]),
          periph("NVIC", "NVIC", 0xE000E100),
          periph("UART7", "UART", 0x40007800, [("UART7_IRQ", 30)])]
    ints = sorted({(i["name"], i["value"]) for p in ps for i in p["interrupts"]}, key=lambda t: t[1])
    return {"schema": 1, "generator": "embsvd", "device": {"name": "STM32X1"}, "cpu": c, "memories": [],
            "interrupts": [{"name": n, "value": v} for n, v in ints], "peripherals": ps}


def make(svd_json=None, **kw):
    args = dict(vendor="st", family="stm32f4", name="stm32x1", flash=(0x08000000, 512 * 1024),
                ram=(0x20000000, 128 * 1024), source="x.svd", digest="00")
    args.update(kw)
    return svd.draft(svd_json or export(), **args)


class Draft(unittest.TestCase):
    def setUp(self):
        self.doc, self.notes = make()
        self.p = {p["name"]: p for p in self.doc["peripherals"]}

    def test_validates_and_is_a_draft(self):
        self.assertEqual(schema.validate(self.doc, schema.load_schema("soc")), [])
        self.assertIs(self.doc["abstract"], True)
        self.assertIs(self.doc["curated"], False)
        self.assertEqual(self.doc["imported_from"]["source"], "x.svd")

    def test_cpu(self):
        self.assertEqual(self.doc["arch"], {"port": "cortex_m", "variant": "armv7em"})
        cpu = self.doc["cpu"]
        self.assertEqual((cpu["core"], cpu["fpu"], cpu["mpu"]["model"]), ("cortex-m4f", "fpv4-sp-d16", "armv7m"))
        self.assertEqual(cpu["cflags"], ["-mcpu=cortex-m4", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"])
        doc, _ = make(export(name="CM0PLUS", fpuPresent=False, mpuPresent=False, nvicPrioBits=2))
        self.assertEqual(doc["arch"]["variant"], "armv6m")
        self.assertEqual(doc["cpu"], {"core": "cortex-m0plus", "cflags": ["-mcpu=cortex-m0plus", "-mfloat-abi=soft"]})
        doc, _ = make(export(name="CM33"))
        self.assertEqual((doc["arch"]["variant"], doc["cpu"]["core"]), ("armv8m-main", "cortex-m33f"))

    def test_interrupts(self):
        i = self.doc["interrupts"]
        self.assertEqual((i["controller"], i["count"], i["priority_bits"], i["systick"]), ("nvic", 39, 4, True))
        used = {1, 4, 5, 6, 24, 25, 30, 38}
        self.assertEqual(i["reserved"], [n for n in range(39) if n not in used])
        doc, _ = make(export(deviceNumInterrupts=42))
        self.assertEqual(doc["interrupts"]["count"], 42)

    def test_peripherals(self):
        p = self.p
        self.assertNotIn("nvic", p)  # the private peripheral bus is architecture
        self.assertEqual(self.notes["skipped"], ["NVIC"])
        self.assertEqual(p["usart2"], {"name": "usart2", "compatible": "st,stm32-usart", "base": 0x40004400, "size": 0x400,
                                       "clock": {"bus": "apb1", "bit": 17}, "irqs": [{"name": "global", "number": 38}]})
        self.assertEqual((p["gpiob"]["port"], p["gpiob"]["clock"]), ("B", {"bus": "ahb1", "bit": 1}))
        self.assertEqual(p["flash_if"]["compatible"], "st,stm32f4-flash")  # renamed by the family rule
        self.assertEqual(p["tim1"]["compatible"], "svd,tim")  # unmapped: binds no driver
        self.assertEqual([i["name"] for i in p["tim1"]["irqs"]], ["brk_tim9", "up_tim10"])
        self.assertEqual(p["uart7"]["irqs"], [{"name": "global", "number": 30}])  # <PERIPHERAL>_IRQ
        self.assertEqual(self.notes["buses"], {"ahb1": 0x30, "apb1": 0x40})  # not the low-power register

    def test_family_rule_applies_to_its_family_only(self):
        doc, _ = make(family="stm32g4")
        names = {p["name"]: p for p in doc["peripherals"]}
        # without the rename, the peripheral's name would clash with the flash region's
        self.assertEqual(names["flash_40023c00"]["compatible"], "svd,flash")
        self.assertNotIn("flash_if", names)

    def test_yaml_reads_back_as_the_draft(self):
        text = svd.to_yaml(self.doc, self.notes, "x.svd")
        d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, d)
        f = Path(d) / "draft.yaml"
        f.write_text(text, encoding="utf-8")
        self.assertEqual(load.load(f, ROOT).data, self.doc)
        self.assertIn("# todo:", text)

    def test_refusals(self):
        with self.assertRaisesRegex(svd.ImportError_, "not a Cortex-M core"):
            make(export(name="RV32"))
        with self.assertRaisesRegex(svd.ImportError_, "give --flash and --ram"):
            make(flash=None, ram=None)
        with self.assertRaisesRegex(svd.ImportError_, "ORIGIN:LENGTH"):
            svd.parse_region("0x08000000", "flash")
        self.assertEqual(svd.parse_region("0x08000000:512K", "flash"), (0x08000000, 512 * 1024))


class Cli(unittest.TestCase):
    def setUp(self):
        self.d = Path(tempfile.mkdtemp(prefix="svd-"))
        self.addCleanup(shutil.rmtree, self.d)
        (self.d / "x.svd").write_text("<device/>", encoding="utf-8")
        (self.d / "x.json").write_text(json.dumps(export()), encoding="utf-8")

    def run_main(self, *extra):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = svd.main([str(self.d / "x.svd"), "--json", str(self.d / "x.json"), "--vendor", "st",
                           "--family", "stm32f4", "--flash", "0x08000000:512K", "--ram", "0x20000000:128K"]
                          + list(extra))
        return rc, out.getvalue(), err.getvalue()

    def test_against_reports_and_never_writes(self):
        """HW-012: re-import after curation leaves the curated file intact and reports differences."""
        doc, notes = make()
        doc["curated"] = True
        doc["interrupts"]["priority_bits"] = 3            # a curated fact the SVD contradicts
        doc["peripherals"] = [p for p in doc["peripherals"] if p["name"] != "tim1"]
        f = self.d / "curated.yaml"
        f.write_text(svd.to_yaml(doc, notes, "x.svd"), encoding="utf-8")
        before = f.read_bytes()
        rc, out, _ = self.run_main("--against", str(f))
        self.assertEqual(rc, 0)
        self.assertEqual(f.read_bytes(), before)
        self.assertIn("1 difference(s)", out)
        self.assertIn("interrupts.priority_bits: description 3, SVD 4", out)
        self.assertIn("only in the SVD (1): tim1", out)

    def test_out_refuses_a_curated_file(self):
        f = self.d / "curated.yaml"
        f.write_text("curated: true\n", encoding="utf-8")
        rc, _, err = self.run_main("-o", str(f))
        self.assertEqual(rc, 2)
        self.assertIn("never overwrites", err)
        self.assertEqual(f.read_text(), "curated: true\n")
        rc, out, _ = self.run_main("-o", str(self.d / "draft.yaml"))
        self.assertEqual(rc, 0, out)
        self.assertIn("curated: false", (self.d / "draft.yaml").read_text())
        # a draft is rewritten (its header mentions `curated: true`, as prose); a curated node is not
        rc, out, _ = self.run_main("-o", str(self.d / "draft.yaml"))
        self.assertEqual(rc, 0, out)
        f.write_text("peripherals:\n  - { name: uart0, curated: true }\n", encoding="utf-8")
        rc, _, err = self.run_main("-o", str(f))
        self.assertEqual(rc, 2)

    def test_other_embsvd_schema_refused(self):
        (self.d / "x.json").write_text(json.dumps({**export(), "schema": 2}), encoding="utf-8")
        rc, _, err = self.run_main()
        self.assertEqual((rc, "schema 2" in err), (2, True))


SVD_XML = """<?xml version="1.0" encoding="utf-8"?>
<device schemaVersion="1.1">
  <name>TESTCHIP</name><version>1.0</version><description>test</description>
  <cpu><name>CM3</name><revision>r2p1</revision><endian>little</endian><mpuPresent>true</mpuPresent>
    <fpuPresent>false</fpuPresent><nvicPrioBits>3</nvicPrioBits><vendorSystickConfig>false</vendorSystickConfig></cpu>
  <addressUnitBits>8</addressUnitBits><width>32</width><size>32</size>
  <peripherals>
    <peripheral><name>USART1</name><groupName>USART</groupName><baseAddress>0x40013800</baseAddress>
      <addressBlock><offset>0</offset><size>0x400</size><usage>registers</usage></addressBlock>
      <interrupt><name>USART1</name><value>37</value></interrupt>
      <registers><register><name>SR</name><addressOffset>0</addressOffset></register></registers>
    </peripheral>
    <peripheral derivedFrom="USART1"><name>USART2</name><baseAddress>0x40004400</baseAddress>
      <interrupt><name>USART2</name><value>38</value></interrupt>
    </peripheral>
  </peripherals>
</device>
"""


@unittest.skipUnless(shutil.which("embsvd"), "embsvd (EmbCC) is not on PATH")
class WithEmbsvd(unittest.TestCase):
    def test_synthetic_svd_end_to_end(self):
        d = Path(tempfile.mkdtemp(prefix="svd-"))
        self.addCleanup(shutil.rmtree, d)
        (d / "t.svd").write_text(SVD_XML, encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()):
            rc = svd.main([str(d / "t.svd"), "--vendor", "st", "--family", "stm32f1", "--flash", "0x08000000:64K",
                           "--ram", "0x20000000:20K", "-o", str(d / "t.yaml")])
        self.assertEqual(rc, 0)
        doc = load.load(d / "t.yaml", ROOT).data
        self.assertEqual((doc["name"], doc["arch"]["variant"], doc["cpu"]["core"]), ("testchip", "armv7m", "cortex-m3"))
        self.assertEqual(doc["interrupts"]["count"], 39)
        usart2 = next(p for p in doc["peripherals"] if p["name"] == "usart2")
        self.assertEqual((usart2["compatible"], usart2["size"], usart2["irqs"]), ("st,stm32-usart", 0x400,
                                                                                    [{"name": "global", "number": 38}]))


F446 = os.environ.get("EMB_SVD_STM32F446")


@unittest.skipUnless(F446 and shutil.which("embsvd"), "set EMB_SVD_STM32F446 to ST's STM32F446.svd, with embsvd on PATH")
class Stm32f446Oracle(unittest.TestCase):
    def test_draft_agrees_with_the_curated_die(self):
        """The curated F446 die was written by hand from the SVD and RM0390: the draft agrees on
        every fact both hold, except where the PVD interrupt (line 1) sits. The SVD gives it to
        EXTI ("PVD through EXTI line detection"), the curated file to PWR, whose supply monitor
        raises it."""
        data = svd.run_embsvd(Path(F446), "embsvd")
        doc, _ = svd.draft(data, "st", "stm32f4", "stm32f446", (0x08000000, 512 * 1024), (0x20000000, 128 * 1024),
                           "STM32F446.svd", "00")
        existing = load.flatten(ROOT / "soc/st/stm32f4/stm32f446.yaml", ROOT).data
        diffs, extra = svd.compare(doc, existing)
        self.assertEqual(diffs, [("peripherals.pwr.irqs", [(1, "pvd")], [])])
        self.assertIn("usart1", [p["name"] for p in doc["peripherals"]])
        self.assertGreater(len(extra), 40)


if __name__ == "__main__":
    unittest.main()
