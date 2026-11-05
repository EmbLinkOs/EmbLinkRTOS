# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""DeviceTree board importer tests (HW-004, HW-012, A82): a synthetic flattened tree, as dtc
writes it, through to a draft that validates. With EMB_ZEPHYR and EMB_HAL_STM32 naming
checkouts of Zephyr and its hal_stm32 module, Zephyr's NUCLEO-F446RE is compared with the
curated board."""
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
sys.path.insert(0, str(ROOT / "hw" / "gen"))
import dts  # noqa: E402
from emb_hwgen import load, schema  # noqa: E402

FLAT = """/dts-v1/;

/ {
	#address-cells = <0x01>;
	compatible = "acme,widget-board";
	model = "Acme Widget";

	chosen {
		zephyr,console = "/soc/serial@40004400";
		zephyr,code-partition = "/soc/flash-controller@40023c00/flash@8000000/partitions/partition@20000";
	};

	soc {
		flash-controller@40023c00 {
			flash0: flash@8000000 {
				reg = <0x8000000 0x80000>;

				partitions {
					boot_partition: partition@0 {
						label = "mcuboot";
						reg = <0x00 0x8000>;
					};

					slot0_partition: partition@20000 {
						label = "image-0";
						reg = <0x20000 0x20000>;
					};
				};
			};
		};

		pinctrl@40020000 {
			usart2_rx_pa3: usart2_rx_pa3 {
				pinmux = <0x67>;
			};

			usart2_tx_pa2: usart2_tx_pa2 {
				pinmux = <0x47>;
				bias-pull-up;
			};

			analog_pa4: analog_pa4 {
				pinmux = <0x90>;
			};

			gpioa: gpio@40020000 {
				gpio-controller;
				phandle = <0x01>;
			};

			gpioc: gpio@40020800 {
				gpio-controller;
				phandle = <0x02>;
			};
		};

		usart2: serial@40004400 {
			compatible = "st,stm32-usart", "st,stm32-uart";
			status = "okay";
			pinctrl-0 = <&usart2_tx_pa2 &usart2_rx_pa3>;
			current-speed = <0x1c200>;
		};

		usart1: serial@40011000 {
			status = "okay";
			pinctrl-0 = <&analog_pa4>;
		};
	};

	clocks {
		clk_hse: clk-hse {
			compatible = "st,stm32-hse-clock";
			status = "okay";
			hse-bypass;
			clock-frequency = <0x7a1200>;
		};

		clk_lse: clk-lse {
			compatible = "fixed-clock";
			clock-frequency = <0x8000>;
			status = "disabled";
		};

		pll: pll {
			div-m = <0x08>;
			mul-n = <0x180>;
			div-p = <0x04>;
			div-q = <0x08>;
			clocks = <&clk_hse>;
			status = "okay";
		};
	};

	rcc: rcc@40023800 {
		clocks = <&pll>;
		clock-frequency = <0x5b8d800>;
	};

	leds {
		compatible = "gpio-leds";

		green_led: led_0 {
			gpios = <&gpioa 0x05 0x00>;
			label = "User LD2";
		};
	};

	gpio_keys {
		compatible = "gpio-keys";

		user_button: button {
			gpios = <&gpioc 0x0d 0x01>;
		};
	};
};
"""


def make(flat=FLAT, reserved=(61, 62)):
    return dts.draft(flat, "widget", "st/stm32f4/stm32f446re.yaml", "widget.dts", "00", list(reserved))


class Parse(unittest.TestCase):
    def test_tree_labels_and_values(self):
        root, labels, paths = dts.parse(FLAT)
        self.assertEqual(root.get("compatible"), ["acme,widget-board"])
        self.assertIs(labels["usart2"], paths["/soc/serial@40004400"])
        self.assertEqual(labels["usart2"].get("pinctrl-0"), [["&usart2_tx_pa2", "&usart2_rx_pa3"]])
        self.assertIs(labels["clk_hse"].get("hse-bypass"), True)
        self.assertEqual(paths["/chosen"].get("zephyr,console"), ["/soc/serial@40004400"])

    def test_pinmux(self):
        self.assertEqual(dts.pinmux(0x47), "PA2.af7")
        self.assertEqual(dts.pinmux((2 << 9) | (13 << 5) | 0), "PC13.af0")
        self.assertIsNone(dts.pinmux(0x90))  # analog: not a peripheral's pin


class Draft(unittest.TestCase):
    def setUp(self):
        self.doc, self.todo = make()

    def test_validates(self):
        self.assertEqual(schema.validate(self.doc, schema.load_schema("board")), [])
        self.assertIs(self.doc["curated"], False)
        self.assertEqual(self.doc["vendor"], "acme")

    def test_facts(self):
        d = self.doc
        self.assertEqual(d["oscillators"], {"hse": {"hz": 8000000, "source": "external_clock"}})
        self.assertEqual(d["clocks"], {"cpu_hz": 96000000, "source": "hse", "pll": {"m": 8, "n": 384, "p": 4, "q": 8}})
        self.assertEqual(d["console"], {"device": "usart2", "baud": 115200})
        self.assertEqual(d["pins"], {"usart2": {"rx": "PA3.af7", "tx": "PA2.af7"}})
        self.assertEqual(d["leds"], [{"name": "green_led", "gpio": "PA5", "active": "high", "doc": "User LD2"}])
        self.assertEqual(d["buttons"], [{"name": "user_button", "gpio": "PC13", "active": "low"}])
        self.assertEqual(d["test_irqs"], [61, 62])

    def test_a_bootloader_layout_becomes_the_whole_flash(self):
        """The code partition is MCUboot's slot 0: without a bootloader the image must start where
        the core boots, so the app is the whole flash and Zephyr's layout is a todo."""
        self.assertEqual(self.doc["flash_partitions"], [{"name": "app", "base": 0x08000000, "size": 0x80000}])
        self.assertTrue(any("image-0 0x08020000+0x20000" in t for t in self.todo))
        flat = FLAT.replace('partitions/partition@20000"', 'partitions/partition@0"')
        doc, _ = make(flat)
        self.assertEqual(doc["flash_partitions"], [{"name": "app", "base": 0x08000000, "size": 0x8000}])

    def test_todo_names_what_was_not_imported(self):
        text = "\n".join(self.todo)
        self.assertIn("oscillators.lse: clk_lse is disabled", text)
        self.assertIn("not imported: usart1", text)

    def test_yaml_reads_back(self):
        d = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, d)
        (d / "board.yaml").write_text(dts.to_yaml(self.doc, self.todo, "widget.dts"))
        back = load.load(d / "board.yaml", ROOT).data
        self.assertEqual(back["flash_partitions"][0]["base"], 0x08000000)
        self.assertEqual(back["pins"], self.doc["pins"])

    def test_refusals(self):
        with self.assertRaisesRegex(dts.DtsError, "no console"):
            make(FLAT.replace('zephyr,console = "/soc/serial@40004400";', ""))
        with self.assertRaisesRegex(dts.DtsError, "not an STM32 GPIO port"):
            make(FLAT.replace("gpios = <&gpioa 0x05 0x00>;", "gpios = <&usart2 0x05 0x00>;"))


class Cli(unittest.TestCase):
    def setUp(self):
        self.d = Path(tempfile.mkdtemp(prefix="dts-"))
        self.addCleanup(shutil.rmtree, self.d)
        (self.d / "widget.flat.dts").write_text(FLAT)

    def run_main(self, *extra):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = dts.main(["--flat", str(self.d / "widget.flat.dts"), "--soc", "st/stm32f4/stm32f446re.yaml"] + list(extra))
        return rc, out.getvalue(), err.getvalue()

    def test_against_reports_and_never_writes(self):
        f = ROOT / "boards/st/nucleo_f446re/board.yaml"
        before = f.read_bytes()
        rc, out, _ = self.run_main("--against", str(f))
        self.assertEqual(rc, 0)
        self.assertEqual(f.read_bytes(), before)
        self.assertIn("clocks.cpu_hz: description 180000000, .dts 96000000", out)
        self.assertNotIn("console", out)  # the same console and pins

    def test_out_refuses_a_curated_board(self):
        cur = self.d / "board.yaml"
        cur.write_text("curated: true\n")
        rc, _, err = self.run_main("-o", str(cur))
        self.assertEqual((rc, "never overwrites" in err), (2, True))
        rc, _, _ = self.run_main("-o", str(self.d / "draft.yaml"), "--name", "widget")
        self.assertEqual(rc, 0)
        self.assertIn("curated: false", (self.d / "draft.yaml").read_text())


ZEPHYR, HAL = os.environ.get("EMB_ZEPHYR"), os.environ.get("EMB_HAL_STM32")


@unittest.skipUnless(ZEPHYR and HAL and shutil.which("dtc"), "set EMB_ZEPHYR and EMB_HAL_STM32 (checkouts), with dtc on PATH")
class ZephyrOracle(unittest.TestCase):
    def test_nucleo_f446re(self):
        """Zephyr's NUCLEO-F446RE agrees with the curated board on every board fact: the HSE in
        bypass, the console and its pins, the LED and the button, the flash and the test lines.
        It differs on Zephyr's own choices, the clock (96 MHz) and its PLL, and on the LSE,
        which Zephyr leaves disabled though the board fits the crystal (X2)."""
        z = Path(ZEPHYR)
        flat = dts.flatten(z / "boards/st/nucleo_f446re/nucleo_f446re.dts",
                           [z / "dts", z / "dts/arm", z / "dts/common", z / "include", z / "include/zephyr",
                            Path(HAL) / "dts"])
        reserved = load.flatten(ROOT / "soc/st/stm32f4/stm32f446re.yaml", ROOT).data["interrupts"]["reserved"]
        doc, _ = dts.draft(flat, "nucleo_f446re", "st/stm32f4/stm32f446re.yaml", "nucleo_f446re.dts", "00", reserved)
        existing = load.load(ROOT / "boards/st/nucleo_f446re/board.yaml", ROOT).data
        self.assertEqual([p for p, _, _ in dts.compare(doc, existing)], ["oscillators.lse", "clocks.cpu_hz", "clocks.pll"])


if __name__ == "__main__":
    unittest.main()
