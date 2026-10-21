# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Real configuration pipeline and independent Cortex-M encoding expectations."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BOARDS = {
    "native": ("native/native", "native", "base", "posix"),
    "avr": ("arduino/arduino_uno", "avr", "tiny", "atmega328p"),
    "riscv": ("qemu/riscv32_virt", "riscv", "base", "rv32imac"),
    "cm3": ("arm/mps2_an385", "cortex_m", "base", "armv7m"),
    "cm4": ("arm/mps2_an386", "cortex_m", "base", "armv7em"),
    "f446": ("st/nucleo_f446re", "cortex_m", "base", "armv7em"),
}


class IRQConfiguration(unittest.TestCase):
    def configure(self, board, settings=(), succeeds=True):
        directory, arch, profile, variant = BOARDS[board]
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)/"config.json"
            args = [sys.executable, "-I", str(ROOT/"tools/kconfig/embconfig.py"),
                    "--kconfig", str(ROOT/"Kconfig"), "--manifest", str(ROOT/f"arch/{arch}/arch.yaml"),
                    "--variant", variant, "--conf", str(ROOT/f"boards/{directory}/board.conf"),
                    "--conf", str(ROOT/f"configs/{profile}.conf"), "--out-json", str(output)]
            for setting in settings:
                args += ["--set", setting]
            result = subprocess.run(args, text=True, capture_output=True, check=False)
            self.assertEqual(result.returncode == 0, succeeds, result.stdout + result.stderr)
            return (json.loads(output.read_text())["symbols"] if succeeds else result.stderr)

    @staticmethod
    def value(symbols, name):
        return int(symbols["CONFIG_" + name]["value"])

    def test_fixed_ports_have_one_explicit_level(self):
        for board in ("native", "avr", "riscv"):
            with self.subTest(board=board):
                symbols = self.configure(board)
                self.assertEqual(self.value(symbols, "EMB_IRQ_KERNEL_LEVEL"), 0)
                self.assertEqual(self.value(symbols, "EMB_IRQ_LEVEL_MAX"), 0)
                error = self.configure(board, ["EMB_IRQ_KERNEL_LEVEL=1"], False)
                self.assertIn("must equal 0", error)

    def test_reference_boards_derive_limits_and_timer_default(self):
        for board, maximum in (("cm3", 5), ("cm4", 5), ("f446", 12)):
            with self.subTest(board=board):
                symbols = self.configure(board)
                for name in ("EMB_IRQ_KERNEL_LEVEL", "EMB_IRQ_LEVEL_MAX", "EMB_CM_TIMER_LEVEL"):
                    self.assertEqual(self.value(symbols, name), maximum)

    def test_width_and_threshold_determine_range(self):
        for bits in range(3, 8):
            step = 1 << (8 - bits)
            with self.subTest(bits=bits):
                symbols = self.configure("cm3", [f"EMB_CM_IRQ_PRIORITY_BITS={bits}",
                                                  f"EMB_CM_KERNEL_BASEPRI={step}"])
                self.assertEqual(self.value(symbols, "EMB_IRQ_KERNEL_LEVEL"), (1 << bits) - 3)
        symbols = self.configure("f446", ["EMB_CM_KERNEL_BASEPRI=64"])
        self.assertEqual(self.value(symbols, "EMB_IRQ_KERNEL_LEVEL"), 10)
        symbols = self.configure("cm3", ["EMB_CM_KERNEL_BASEPRI=192", "EMB_CM_IRQ_PRIORITY=192"])
        self.assertEqual(self.value(symbols, "EMB_IRQ_KERNEL_LEVEL"), 0)

    def test_invalid_width_or_threshold_is_refused(self):
        for setting in ("EMB_CM_IRQ_PRIORITY_BITS=2", "EMB_CM_IRQ_PRIORITY_BITS=8",
                        "EMB_CM_KERNEL_BASEPRI=0", "EMB_CM_KERNEL_BASEPRI=16",
                        "EMB_CM_KERNEL_BASEPRI=33", "EMB_CM_KERNEL_BASEPRI=224"):
            with self.subTest(setting=setting):
                self.assertIn("CONFIG_EMB_CM_", self.configure("cm3", [setting], False))

    def test_default_priority_must_be_aligned_masked_and_not_pendsv(self):
        for value in (0, 16, 129, 224, 255):
            with self.subTest(value=value):
                error = self.configure("cm3", [f"EMB_CM_IRQ_PRIORITY={value}"], False)
                self.assertIn("CONFIG_EMB_CM_IRQ_PRIORITY", error)

    def test_derived_limits_cannot_be_overridden_inconsistently(self):
        for name in ("EMB_IRQ_KERNEL_LEVEL", "EMB_IRQ_LEVEL_MAX"):
            self.assertIn("must equal 12", self.configure("f446", [f"{name}=3"], False))
        symbols = self.configure("f446", ["EMB_IRQ_KERNEL_LEVEL=12", "EMB_IRQ_LEVEL_MAX=12"])
        self.assertEqual(self.value(symbols, "EMB_IRQ_LEVEL_MAX"), 12)

    def test_timer_level_can_be_selected_within_domain(self):
        symbols = self.configure("f446", ["EMB_CM_TIMER_LEVEL=0"])
        self.assertEqual(self.value(symbols, "EMB_CM_TIMER_LEVEL"), 0)
        self.assertIn("EMB_CM_TIMER_LEVEL", self.configure("f446", ["EMB_CM_TIMER_LEVEL=13"], False))

    def test_unimplemented_raw_class_is_refused(self):
        self.assertIn("raw kernel-independent vectors", self.configure("f446", ["EMB_IRQ_ZERO_LATENCY=y"], False))

    def test_encoding_matches_known_f446_values(self):
        symbols = self.configure("f446")
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            config = directory/"emb/config.h"
            config.parent.mkdir()
            config.write_text("\n".join(f"#define {name} {record['value']}"
                                        for name, record in symbols.items()
                                        if record["type"] in ("int", "hex")) + "\n")
            source = directory/"mapping.c"
            source.write_text('''#include <emb_irq_priority.h>
#include <assert.h>
_Static_assert(CONFIG_EMB_IRQ_KERNEL_LEVEL == 12, "F446 domain");
int main(void) {
    assert(embn_cm_irq_priority(0) == 0xe0);
    assert(embn_cm_irq_priority(1) == 0xd0);
    assert(embn_cm_irq_priority(6) == 0x80);
    assert(embn_cm_irq_priority(12) == 0x20);
    return 0;
}
''')
            executable = directory/"mapping"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(directory),
                            "-I", str(ROOT/"arch/cortex_m/include"), str(source), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
