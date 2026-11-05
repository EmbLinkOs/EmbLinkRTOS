#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""crashdec: decode a crash record (SPEC-015 §4, OBS-016, A71 to A73).

    crashdec.py RECORD [--elf IMAGE.elf [--tool-prefix arm-none-eabi-] [--force]] [--json]

RECORD is a raw record file (the native port's $EMB_NATIVE_CRASH_FILE), a console log with
"EMB_CRASH <hex>" lines (every one is decoded), or "-" for standard input; --hex takes the
record as a hex string instead. With --elf, the record's build id must match the image's
emb_build_id (OBS-019; --force decodes anyway) and the pc is symbolized with
<prefix>addr2line. Versions 1 and 2 are known; a record of another version, a wrong length
or a failing CRC-32 is refused.

Exit status 0 when every record decodes (and matches), 1 on a build-id mismatch, 2 on a
malformed or unknown record or a usage error.
"""
import argparse
import json
import re
import struct
import subprocess
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / "tools" / "fault"))
import decode as api_decode  # noqa: E402  (the API ID decoder, tools/fault/decode.py)

MAGIC = b"EMBC"
RESET = {1: "reboot", 2: "halt"}
THREAD_NONE = 0xFFFF

# version: (length, field layout after the 8-byte header, excluding the CRC)
V1 = ("<20s8sQIBBHHBBHHIIIIIIBBH", ["build_id", "fw_version", "uptime_ticks", "reset_reason",
                                     "fault_class", "argument", "code", "api_id", "cpu_id",
                                     "partition_id", "thread_index", "thread_generation", "address",
                                     "pc", "lr_or_ra", "sp", "stack_base", "stack_size",
                                     "status_reg_count", "gpr_count", "flight_event_count"])
V2 = ("<4sIBBBBHHHHH", ["build_id", "uptime_ticks", "reset_reason", "fault_class", "argument",
                        "thread_index", "code", "api_id", "pc", "sp", "address"])
LAYOUTS = {1: (92, V1), 2: (34, V2)}


class RecordError(Exception):
    pass


def fault_classes(root=ROOT):
    text = (root / "include" / "emb" / "fault.h").read_text(encoding="utf-8")
    return {int(v): n.lower() for n, v in re.findall(r"EMB_FAULT_([A-Z_]+)\s*=\s*(\d+)", text)}


def parse(raw):
    """The record's fields, or RecordError naming what does not check."""
    if len(raw) < 12 or raw[:4] != MAGIC:
        raise RecordError("no crash record: the magic is not EMBC")
    version, length = struct.unpack_from("<HH", raw, 4)
    if version not in LAYOUTS:
        raise RecordError(f"record version {version} is unknown to this decoder (it knows 1 and 2)")
    want, (fmt, names) = LAYOUTS[version]
    if version == 1 and len(raw) >= 88:
        want += 4 * (raw[84] + raw[85])  # four bytes per register (A74)
    if length != want or len(raw) < length:
        raise RecordError(f"version {version} is {want} bytes; the record says {length} and has {len(raw)}")
    body = raw[:length - 4]
    crc = struct.unpack_from("<I", raw, length - 4)[0]
    if zlib.crc32(body) & 0xFFFFFFFF != crc:
        raise RecordError(f"CRC-32 {crc:#010x} does not match the record ({zlib.crc32(body) & 0xFFFFFFFF:#010x})")
    rec = dict(zip(names, struct.unpack_from(fmt, raw, 8)))
    rec["version"], rec["length"] = version, length
    if version == 2 and rec["thread_index"] == 0xFF:
        rec["thread_index"] = THREAD_NONE
    # version 2 is AVR's: avr-gcc's return address counts 16-bit words; addr2line wants bytes
    rec["pc_bytes"] = rec["pc"] * 2 if version == 2 else rec["pc"]
    rec["build_id"] = rec["build_id"].hex()
    count = rec.get("status_reg_count", 0) + rec.get("gpr_count", 0)
    rec["regs"] = list(struct.unpack_from(f"<{count}I", raw, 88)) if count else []
    if "fw_version" in rec:
        rec["fw_version"] = rec["fw_version"].rstrip(b"\0").decode("ascii", "replace")
    return rec


def records_from(data):
    """Raw record bytes from a binary file, a hex string or a console log."""
    if data[:4] == MAGIC:
        return [data]
    text = data.decode("utf-8", "replace")
    found = re.findall(r"EMB_CRASH ([0-9a-fA-F]+)", text)
    if not found and re.fullmatch(r"\s*[0-9a-fA-F]+\s*", text):
        found = [text.strip()]
    return [bytes.fromhex(h) for h in found]


# -------------------------------------------------------------------------------- ELF

def elf_symbol_bytes(path, name):
    """The bytes of a data symbol in an ELF file (no external tools), or None."""
    data = Path(path).read_bytes()
    if data[:4] != b"\x7fELF":
        return None
    cls, end = data[4], "<" if data[5] == 1 else ">"
    if cls == 1:
        shoff, = struct.unpack_from(end + "I", data, 0x20)
        shentsize, shnum = struct.unpack_from(end + "HH", data, 0x2E)
        sh = lambda i: struct.unpack_from(end + "IIIIIIIIII", data, shoff + i * shentsize)  # noqa: E731
        symfmt, symsize = end + "IIIBBH", 16
    else:
        shoff, = struct.unpack_from(end + "Q", data, 0x28)
        shentsize, shnum = struct.unpack_from(end + "HH", data, 0x3A)
        sh = lambda i: struct.unpack_from(end + "IIQQQQIIQQ", data, shoff + i * shentsize)  # noqa: E731
        symfmt, symsize = end + "IBBHQQ", 24
    sections = [sh(i) for i in range(shnum)]
    for s in sections:
        if s[1] != 2:  # SHT_SYMTAB
            continue
        strtab = sections[s[6]]
        for off in range(s[4], s[4] + s[5], symsize):
            fields = struct.unpack_from(symfmt, data, off)
            if cls == 1:
                st_name, st_value, st_size, _, _, st_shndx = fields
            else:
                st_name, _, _, st_shndx, st_value, st_size = fields
            nm_off = strtab[4] + st_name
            sym = data[nm_off:data.index(b"\0", nm_off)].decode()
            if sym != name or st_shndx == 0 or st_shndx >= len(sections):
                continue
            sec = sections[st_shndx]
            addr, offset = sec[3], sec[4]
            return data[offset + st_value - addr: offset + st_value - addr + st_size]
    return None


# The ports' register order (emb_arch_crash_regs, A74).
REG_NAMES = {
    "cortex_m": (["cfsr", "hfsr", "mmfar", "bfar"], ["r0", "r1", "r2", "r3", "r12", "lr", "pc", "xpsr"]),
    "riscv": (["mcause", "mtval", "mepc", "mstatus"], ["ra", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"]),
}
CFSR = {0: "IACCVIOL", 1: "DACCVIOL", 3: "MUNSTKERR", 4: "MSTKERR", 5: "MLSPERR", 7: "MMARVALID",
        8: "IBUSERR", 9: "PRECISERR", 10: "IMPRECISERR", 11: "UNSTKERR", 12: "STKERR", 13: "LSPERR",
        15: "BFARVALID", 16: "UNDEFINSTR", 17: "INVSTATE", 18: "INVPC", 19: "NOCP", 20: "STKOF", 24: "UNALIGNED",
        25: "DIVBYZERO"}
HFSR = {1: "VECTTBL", 30: "FORCED", 31: "DEBUGEVT"}
MCAUSE = {0: "instruction address misaligned", 1: "instruction access fault", 2: "illegal instruction",
          3: "breakpoint", 4: "load address misaligned", 5: "load access fault",
          6: "store address misaligned", 7: "store access fault", 8: "environment call from U-mode",
          11: "environment call from M-mode"}
ELF_MACHINE = {40: "cortex_m", 243: "riscv"}


def elf_arch(path):
    data = Path(path).read_bytes()[:20]
    if data[:4] != b"\x7fELF":
        return None
    return ELF_MACHINE.get(struct.unpack_from("<H" if data[5] == 1 else ">H", data, 18)[0])


def name_regs(rec, arch):
    """The registers with the port's names, and the status bits spelled out."""
    status, gprs = REG_NAMES.get(arch, ([], []))
    ns = rec.get("status_reg_count", 0)
    out = []
    for i, v in enumerate(rec["regs"]):
        names, j = (status, i) if i < ns else (gprs, i - ns)
        name = names[j] if j < len(names) else (f"status{j}" if i < ns else f"gpr{j}")
        note = ""
        if arch == "cortex_m" and name in ("cfsr", "hfsr"):
            bits = [n for b, n in sorted((CFSR if name == "cfsr" else HFSR).items()) if v >> b & 1]
            note = " ".join(bits)
        elif arch == "riscv" and name == "mcause":
            note = MCAUSE.get(v, "interrupt" if v >> 31 else "unknown")
        out.append({"name": name, "value": v, "note": note})
    return out


def symbolize(elf, pc, prefix):
    try:
        out = subprocess.run([f"{prefix}addr2line", "-f", "-e", str(elf), f"{pc:#x}"],
                             capture_output=True, text=True, check=True).stdout.split("\n")
    except (OSError, subprocess.CalledProcessError):
        return None
    return {"function": out[0].strip(), "location": out[1].strip() if len(out) > 1 else "??"}


# -------------------------------------------------------------------------------- output

def describe(rec, classes, api_map):
    rec = dict(rec)
    rec["fault_class_name"] = classes.get(rec["fault_class"], "unknown")
    rec["api_name"] = (api_decode.lookup(api_map, rec["api_id"]) or "unknown") if rec["api_id"] else None
    rec["reset_reason_name"] = RESET.get(rec["reset_reason"], "unknown")
    return rec


def text(rec):
    lines = [f"crash record v{rec['version']} ({rec['length']} bytes), build {rec['build_id']}"
             + (f", firmware {rec['fw_version']}" if rec.get("fw_version") else "")]
    lines.append(f"  fault      {rec['fault_class_name']} (class {rec['fault_class']}), code {rec['code']:#06x}")
    if rec["api_name"]:
        lines.append(f"  api        {rec['api_name']} (ID {rec['api_id']:#06x}), argument {rec['argument']}")
    thread = "none (ISR or pre-kernel)" if rec["thread_index"] == THREAD_NONE else str(rec["thread_index"])
    lines.append(f"  thread     {thread}")
    lines.append(f"  uptime     {rec['uptime_ticks']} ticks; then {rec['reset_reason_name']}")
    sym = rec.get("symbol")
    where = f" in {sym['function']} at {sym['location']}" if sym else ""
    byte_pc = f" (byte address {rec['pc_bytes']:#x})" if rec["pc_bytes"] != rec["pc"] else ""
    lines.append(f"  pc         {rec['pc']:#x}{byte_pc}{where}")
    lines.append(f"  sp         {rec['sp']:#x}; fault address {rec['address']:#x}")
    if rec.get("lr_or_ra"):
        lines.append(f"  lr/ra      {rec['lr_or_ra']:#x}")
    for r in rec.get("named_regs", []):
        lines.append(f"  {r['name']:<10} {r['value']:#010x}" + (f"  {r['note']}" if r["note"] else ""))
    if rec.get("build_match") is False:
        lines.append("  WARNING    the record's build id does not match the ELF (decoded with --force)")
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("record", nargs="?", help="a record file, a console log, or - for standard input")
    ap.add_argument("--hex", help="the record as a hex string")
    ap.add_argument("--elf", help="the image that wrote the record")
    ap.add_argument("--tool-prefix", default="", help="binutils prefix for addr2line, e.g. arm-none-eabi-")
    ap.add_argument("--force", action="store_true", help="decode a record whose build id does not match")
    ap.add_argument("--json", action="store_true", help="JSON output, one object per record")
    ap.add_argument("--arch", choices=sorted(REG_NAMES), help="name the registers for this port (default: from --elf)")
    args = ap.parse_args(argv)
    if args.hex:
        data = args.hex.encode()
    elif args.record == "-":
        data = sys.stdin.buffer.read()
    elif args.record:
        data = Path(args.record).read_bytes()
    else:
        ap.error("give a RECORD or --hex")
    raws = records_from(data)
    if not raws:
        print("crashdec: no crash record in the input", file=sys.stderr)
        return 2
    classes = fault_classes()
    api_map = json.loads((ROOT / "tools" / "fault" / "api-ids.json").read_text(encoding="utf-8"))
    elf_id = elf_symbol_bytes(args.elf, "emb_build_id") if args.elf else None
    arch = args.arch or (elf_arch(args.elf) if args.elf else None)
    status, out = 0, []
    for raw in raws:
        try:
            rec = describe(parse(raw), classes, api_map)
        except RecordError as e:
            print(f"crashdec: {e}", file=sys.stderr)
            status = 2
            continue
        if args.elf:
            if elf_id is None:
                print(f"crashdec: {args.elf} has no emb_build_id (not an ELF, or the record is off)", file=sys.stderr)
            else:
                rec["build_match"] = elf_id.hex().startswith(rec["build_id"])
                if not rec["build_match"] and not args.force:
                    print(f"crashdec: the record is from build {rec['build_id']}, the ELF is {elf_id.hex()}; "
                          "refusing (--force decodes anyway)", file=sys.stderr)
                    status = max(status, 1)
                    continue
            if rec["pc"]:
                rec["symbol"] = symbolize(args.elf, rec["pc_bytes"], args.tool_prefix)
        rec["named_regs"] = name_regs(rec, arch)
        out.append(rec)
    if args.json:
        print(json.dumps(out, indent=2, sort_keys=True))
    else:
        print("\n\n".join(text(r) for r in out))
    return status


if __name__ == "__main__":
    sys.exit(main())
