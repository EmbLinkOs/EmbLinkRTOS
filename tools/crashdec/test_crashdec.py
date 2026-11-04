# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""crashdec tests: both record versions, every refusal, log extraction, and a record the
kernel itself wrote (native test_crash), which ties the C serializer to the decoder."""
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import crashdec  # noqa: E402

# crash_record_round_trip's sample, as native-gcc printed it (version 1, 92 bytes)
KERNEL_V1 = ("454d424301005c001b36f06945ce27faa5ddd7d1948407becf061e47302e322e30000000000000000000000001"
             "000000050202012900000003000000341200007856000000000000bc7a000000000000000000000000000025e2d95e")


def v1(**f):
    body = b"EMBC" + struct.pack("<HH", 1, 92) + struct.pack(
        "<20s8sQIBBHHBBHHIIIIIIBBH", f.get("build_id", b"\x11" * 20), b"0.2.0", f.get("uptime", 7), 1,
        f.get("fault_class", 11), f.get("argument", 0), f.get("code", 0x5EED), f.get("api_id", 0), 0, 0,
        f.get("thread", 2), 0, 0, f.get("pc", 0x8000123), 0, 0x20001000, 0, 0, 0, 0, 0)
    return body + struct.pack("<I", zlib.crc32(body))


def v2(**f):
    body = b"EMBC" + struct.pack("<HH", 2, 34) + struct.pack(
        "<4sIBBBBHHHHH", b"\xaa\xbb\xcc\xdd", 9, 2, f.get("fault_class", 3), 1, f.get("thread", 0xFF),
        2, f.get("api_id", 0x0012), 0x1234, 0x08F0, 0)
    return body + struct.pack("<I", zlib.crc32(body))


class Decode(unittest.TestCase):
    def test_version_1(self):
        r = crashdec.parse(v1())
        self.assertEqual((r["version"], r["length"], r["fault_class"], r["code"], r["thread_index"], r["pc"]),
                         (1, 92, 11, 0x5EED, 2, 0x8000123))
        self.assertEqual(r["fw_version"], "0.2.0")
        self.assertEqual(r["build_id"], "11" * 20)

    def test_version_2(self):
        r = crashdec.parse(v2())
        self.assertEqual((r["version"], r["length"], r["build_id"], r["uptime_ticks"]), (2, 34, "aabbccdd", 9))
        self.assertEqual(r["thread_index"], crashdec.THREAD_NONE)  # 0xFF: no thread
        self.assertEqual((r["api_id"], r["argument"], r["pc"], r["sp"]), (0x0012, 1, 0x1234, 0x08F0))
        self.assertEqual(r["pc_bytes"], 0x2468)  # AVR's word address, in bytes for addr2line

    def test_a_record_the_kernel_wrote(self):
        """The C serializer and this decoder agree on the layout and the CRC-32."""
        r = crashdec.parse(bytes.fromhex(KERNEL_V1))
        self.assertEqual((r["fault_class"], r["argument"], r["code"], r["api_id"], r["thread_index"]),
                         (5, 2, 0x0102, 0x0029, 3))
        self.assertEqual((r["address"], r["pc"], r["sp"], r["reset_reason"]), (0x1234, 0x5678, 0x7ABC, 1))

    def test_refusals(self):
        bad_crc = bytearray(v1())
        bad_crc[50] ^= 1
        bad_version = bytearray(v1())
        bad_version[4] = 9
        short = v1()[:60]
        for raw, why in ((bytes(bad_crc), "CRC-32"), (bytes(bad_version), "version 9 is unknown"),
                         (short, "92 bytes"), (b"XXXX" + v1()[4:], "magic")):
            with self.subTest(why=why), self.assertRaisesRegex(crashdec.RecordError, why):
                crashdec.parse(raw)

    def test_records_from_a_console_log(self):
        log = f"TEST a\nEMB_CRASH {v1().hex()}\nCASE a outcome=passed\nEMB_CRASH {v2().hex()}\n".encode()
        self.assertEqual([crashdec.parse(r)["version"] for r in crashdec.records_from(log)], [1, 2])
        self.assertEqual(crashdec.records_from(v2()), [v2()])  # a raw file
        self.assertEqual(crashdec.records_from(v2().hex().encode()), [v2()])  # a bare hex string

    def test_names(self):
        rec = crashdec.describe(crashdec.parse(bytes.fromhex(KERNEL_V1)), crashdec.fault_classes(),
                                json.loads((crashdec.ROOT / "tools/fault/api-ids.json").read_text()))
        self.assertEqual(rec["fault_class_name"], "api_lifecycle")
        self.assertEqual(rec["api_name"], "emb_sem_give")
        self.assertEqual(rec["reset_reason_name"], "reboot")


class Cli(unittest.TestCase):
    def run_cli(self, *args):
        return subprocess.run([sys.executable, "-I", str(HERE / "crashdec.py"), *args], capture_output=True, text=True)

    def test_text_and_json(self):
        r = self.run_cli("--hex", KERNEL_V1)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("api_lifecycle (class 5), code 0x0102", r.stdout)
        self.assertIn("emb_sem_give (ID 0x0029), argument 2", r.stdout)
        r = self.run_cli("--json", "--hex", KERNEL_V1)
        self.assertEqual(json.loads(r.stdout)[0]["code"], 0x0102)

    def test_malformed_input_exits_2(self):
        r = self.run_cli("--hex", KERNEL_V1[:-2] + "00")
        self.assertEqual(r.returncode, 2)
        self.assertIn("CRC-32", r.stderr)

    def test_raw_file(self):
        with tempfile.NamedTemporaryFile(delete=False) as f:
            f.write(v2() + b"\0" * 94)  # the native file holds the whole retained buffer
        try:
            r = self.run_cli(f.name)
        finally:
            os.unlink(f.name)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("crash record v2 (34 bytes), build aabbccdd", r.stdout)


class Elf(unittest.TestCase):
    def elf32(self, symbol_bytes, name=b"emb_build_id"):
        """A minimal ELF32 with one PROGBITS section holding the symbol, a symtab and a strtab."""
        shstr = b"\0.data\0.symtab\0.strtab\0.shstrtab\0"
        strtab = b"\0" + name + b"\0"
        data = symbol_bytes
        addr = 0x08001000
        sym = struct.pack("<IIIBBH", 0, 0, 0, 0, 0, 0) + struct.pack("<IIIBBH", 1, addr, len(data), 0x11, 0, 1)
        off_data = 52
        off_sym = off_data + len(data)
        off_str = off_sym + len(sym)
        off_shstr = off_str + len(strtab)
        off_sh = off_shstr + len(shstr)
        hdr = b"\x7fELF" + bytes([1, 1, 1]) + b"\0" * 9 + struct.pack(
            "<HHIIIIIHHHHHH", 2, 40, 1, 0, 0, off_sh, 0, 52, 0, 0, 40, 5, 4)
        sh = [struct.pack("<IIIIIIIIII", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
              struct.pack("<IIIIIIIIII", 1, 1, 2, addr, off_data, len(data), 0, 0, 4, 0),
              struct.pack("<IIIIIIIIII", 7, 2, 0, 0, off_sym, len(sym), 3, 1, 4, 16),
              struct.pack("<IIIIIIIIII", 15, 3, 0, 0, off_str, len(strtab), 0, 0, 1, 0),
              struct.pack("<IIIIIIIIII", 23, 3, 0, 0, off_shstr, len(shstr), 0, 0, 1, 0)]
        return hdr + data + sym + strtab + shstr + b"".join(sh)

    def test_build_id_match_and_mismatch(self):
        with tempfile.TemporaryDirectory() as d:
            good = Path(d) / "good.elf"
            good.write_bytes(self.elf32(b"\x11" * 20))
            other = Path(d) / "other.elf"
            other.write_bytes(self.elf32(b"\x22" * 20))
            self.assertEqual(crashdec.elf_symbol_bytes(good, "emb_build_id"), b"\x11" * 20)
            raw = Path(d) / "rec.bin"
            raw.write_bytes(v1(pc=0))
            ok = subprocess.run([sys.executable, "-I", str(HERE / "crashdec.py"), str(raw), "--elf", str(good)],
                                capture_output=True, text=True)
            self.assertEqual(ok.returncode, 0, ok.stderr)
            bad = subprocess.run([sys.executable, "-I", str(HERE / "crashdec.py"), str(raw), "--elf", str(other)],
                                 capture_output=True, text=True)
            self.assertEqual(bad.returncode, 1)
            self.assertIn("refusing", bad.stderr)
            forced = subprocess.run([sys.executable, "-I", str(HERE / "crashdec.py"), str(raw), "--elf", str(other),
                                     "--force"], capture_output=True, text=True)
            self.assertIn("does not match the ELF", forced.stdout)


if __name__ == "__main__":
    unittest.main()
