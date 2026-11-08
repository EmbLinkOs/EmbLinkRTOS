#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""An image's complete flash and RAM accounting, reconciled with its sections (05 §5, KRN-MEM-009).

    memory_report.py build/controller/controller.elf.map --elf build/controller/controller.elf \\
                     [--ldscript regions.ld] [--nm arm-none-eabi-nm] [--readelf arm-none-eabi-readelf]
                     [--json out.json]

It reads GNU ld's map and LLD's. GNU ld's map lists the memory regions; LLD's does not, so for an
LLD map they come from the linker script's MEMORY block: --ldscript, or else the script the
link command names with -T in the build's build.ninja, next to the map.

footprint.py attributes each input section to the component that contributed it. This report
adds what that leaves out and checks that the parts add up:

- **Regions.** For each memory region of the linker map, what the image's allocated sections
  occupy in it. The load image of .data counts in flash as well as in RAM, and the area the
  linker script keeps above the stacks for the crash record (__emb_retained_start to
  __emb_retained_end) counts in RAM.
- **Flash.** Per component, from the map, against the flash sections' total. The map gives
  merged string constants (.rodata.str*) their size before merging, so they are not in the
  components; the difference holds them after merging, the vector table's alignment and the
  linker's own tables.
- **RAM.** Per purpose, from the image's symbols: the interrupt stack, the thread stacks, the
  thread control blocks, the queues' storage, then each remaining symbol by its component,
  against the RAM sections' total. The difference is alignment padding.

Exit status 0, 1 when the parts and the totals disagree by more than the tolerance (64 bytes per
region), 2 on a usage error.
"""
import argparse
import json
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import footprint  # noqa: E402  (parse_map, component)

REGION = re.compile(r"^(\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)")
TOLERANCE = 64
PURPOSES = [
    ("interrupt stack", re.compile(r"^embn_cm_isr_stack$|^embk_isr_stack|_isr_stack$")),
    ("thread stacks", re.compile(r"(_stack_?|^idle_stack)$")),
    ("thread control blocks", re.compile(r"_thread_storage_$|^embk_idle_thread$")),
    ("kernel objects and queue rings", re.compile(r"_(ring|storage)_$")),
]


MEMORY_LINE = re.compile(r"^\s*(\w+)\s*(?:\([^)]*\))?\s*:\s*ORIGIN\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*,"
                         r"\s*LENGTH\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*([KM]?)\s*$")
SCALE = {"": 1, "K": 1024, "M": 1024 * 1024}


def script_regions(script, seen=None):
    """The regions of a linker script's MEMORY block, following INCLUDE."""
    seen = set() if seen is None else seen
    script = Path(script).resolve()
    if script in seen:
        return []
    seen.add(script)
    text = re.sub(r"/\*.*?\*/", "", script.read_text(encoding="utf-8", errors="replace"), flags=re.S)
    out = []
    for inc in re.findall(r"^\s*INCLUDE\s+(\S+)", text, re.M):
        out += script_regions(script.parent / inc.strip('"'), seen)
    block = re.search(r"\bMEMORY\s*\{(.*?)\}", text, re.S)
    for line in (block.group(1).splitlines() if block else []):
        m = MEMORY_LINE.match(line)
        if m:
            out.append({"name": m.group(1), "origin": int(m.group(2), 0),
                        "length": int(m.group(3), 0) * SCALE[m.group(4)]})
    return out


def link_script(map_path):
    """The linker script the link command names (-T), from build.ninja beside the map or above it."""
    for d in [Path(map_path).resolve().parent, *Path(map_path).resolve().parents]:
        ninja = d / "build.ninja"
        if ninja.is_file():
            m = re.search(r"(?:-T\s*|-Wl,-T,)(\S+\.ld)\b", ninja.read_text(encoding="utf-8", errors="replace"))
            return Path(m.group(1)) if m else None
    return None


def regions(map_path, ldscript=None):
    text = Path(map_path).read_text(encoding="utf-8", errors="replace")
    if footprint.is_lld(text):
        script = ldscript or link_script(map_path)
        if not script:
            raise OSError(f"{map_path}: LLD's map has no memory regions; give the linker script with --ldscript")
        return script_regions(script)
    out, inside = [], False
    for line in text.splitlines():
        if line.startswith("Memory Configuration"):
            inside = True
            continue
        if line.startswith("Linker script and memory map"):
            break
        m = REGION.match(line) if inside else None
        if m and m.group(1) != "*default*":
            out.append({"name": m.group(1), "origin": int(m.group(2), 16), "length": int(m.group(3), 16)})
    return out


def sections(elf, readelf):
    """The allocated sections: name, address, size, NOBITS, and the load address of .data."""
    text = subprocess.run([readelf, "-S", "-W", str(elf)], capture_output=True, text=True, check=True).stdout
    out = []
    for line in text.splitlines():
        m = re.match(r"^\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+\S+\s+(\S*)", line)
        if m and "A" in m.group(6):
            out.append({"name": m.group(1), "nobits": m.group(2) == "NOBITS", "addr": int(m.group(3), 16),
                        "size": int(m.group(5), 16)})
    seg = subprocess.run([readelf, "-l", "-W", str(elf)], capture_output=True, text=True, check=True).stdout
    loads = []
    for line in seg.splitlines():
        m = re.match(r"^\s*LOAD\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)", line)
        if m:
            loads.append({"vaddr": int(m.group(1), 16), "paddr": int(m.group(2), 16),
                          "filesz": int(m.group(3), 16)})
    for s in out:
        s["lma"] = s["addr"]
        for ld in loads:
            if not s["nobits"] and ld["vaddr"] <= s["addr"] < ld["vaddr"] + max(ld["filesz"], 1):
                s["lma"] = ld["paddr"] + (s["addr"] - ld["vaddr"])
    return out


def symbols(elf, nm):
    """{name: (address, size, type)} of the image's sized symbols, and every symbol's address."""
    text = subprocess.run([nm, "-S", str(elf)], capture_output=True, text=True, check=True).stdout
    sized, addrs = {}, {}
    for line in text.splitlines():
        p = line.split()
        if len(p) == 4:
            sized[p[3]] = (int(p[0], 16), int(p[1], 16), p[2])
            addrs[p[3]] = int(p[0], 16)
        elif len(p) == 3:
            addrs[p[2]] = int(p[0], 16)
    return sized, addrs


def owners(map_path):
    """{symbol: component} from the map's input-section lines (.bss.<name>, .data.<name>)."""
    text = Path(map_path).read_text(encoding="utf-8", errors="replace")
    if footprint.is_lld(text):
        return {m.group(1): footprint.component(obj) for sec, _, obj in footprint.lld_inputs(text)
                for m in [re.match(r"^\.(?:bss|data|noinit)\.(\S+)$", sec)] if m}
    out, pending = {}, None
    sec_re = re.compile(r"^\s(\.(?:bss|data|noinit)\.(\S+?))(?:\s+0x[0-9a-fA-F]+\s+0x[0-9a-fA-F]+\s+(\S+))?\s*$")
    cont = re.compile(r"^\s+0x[0-9a-fA-F]+\s+0x[0-9a-fA-F]+\s+(\S+)\s*$")
    for line in Path(map_path).read_text(encoding="utf-8", errors="replace").splitlines():
        m = sec_re.match(line)
        if m:
            if m.group(3):
                out[m.group(2)] = footprint.component(m.group(3))
                pending = None
            else:
                pending = m.group(2)
            continue
        m = cont.match(line)
        if m and pending:
            out[pending] = footprint.component(m.group(1))
            pending = None
    return out


def in_region(addr, reg):
    return reg["origin"] <= addr < reg["origin"] + reg["length"]


def build_report(map_path, elf, nm, readelf, ldscript=None):
    regs = regions(map_path, ldscript)
    if not regs:
        raise OSError(f"{map_path}: no memory regions found")
    secs = sections(elf, readelf)
    sized, addrs = symbols(elf, nm)
    flash = next((r for r in regs if "x" in r["name"].lower() or r["name"].upper().startswith("FLASH")), regs[0])
    ram = next((r for r in regs if r["name"].upper().startswith("RAM")), regs[-1])
    retained = 0
    if "__emb_retained_start" in addrs and "__emb_retained_end" in addrs:
        retained = addrs["__emb_retained_end"] - addrs["__emb_retained_start"]
    flash_secs = [s for s in secs if in_region(s["lma"], flash) and not s["nobits"]]
    ram_secs = [s for s in secs if in_region(s["addr"], ram)]
    flash_used = sum(s["size"] for s in flash_secs)
    ram_used = sum(s["size"] for s in ram_secs) + retained

    merged = {}
    comps = footprint.parse_map(map_path, merged)
    flash_parts = {c: v["text"] + v["data"] for c, v in comps.items() if v["text"] + v["data"]}
    flash_rest = flash_used - sum(flash_parts.values())

    owner = owners(map_path)
    purposes = defaultdict(lambda: {"bytes": 0, "symbols": []})
    for name, (addr, size, _) in sorted(sized.items(), key=lambda kv: -kv[1][1]):
        if not in_region(addr, ram) or size == 0:
            continue
        label = next((p for p, rx in PURPOSES if rx.search(name)), None)
        if label is None:
            comp = owner.get(name, "other")
            label = {"app": "application state"}.get(comp, comp if comp.startswith("kernel") else
                                                     ("port state" if comp == "arch" else
                                                      ("board and drivers state" if comp in ("board", "drivers") else
                                                       "runtime and other")))
            if label.startswith("kernel"):
                label = "kernel state"
        purposes[label]["bytes"] += size
        purposes[label]["symbols"].append({"name": name, "bytes": size})
    if retained:
        purposes["retained crash record area"]["bytes"] += retained
    ram_rest = ram_used - sum(p["bytes"] for p in purposes.values())
    problems = []
    if flash_rest < 0 or flash_rest > sum(merged.values()) + 1024:  # strings, vector table, linker tables
        problems.append(f"flash: the components leave {flash_rest} bytes unexplained")
    if abs(ram_rest) > TOLERANCE:
        problems.append(f"RAM: the symbols leave {ram_rest} bytes unexplained")
    return {
        "regions": [{"name": flash["name"], "length": flash["length"], "used": flash_used,
                     "free": flash["length"] - flash_used},
                    {"name": ram["name"], "length": ram["length"], "used": ram_used,
                     "free": ram["length"] - ram_used}],
        "sections": [{"name": s["name"], "addr": s["addr"], "lma": s["lma"], "size": s["size"],
                      "nobits": s["nobits"]} for s in secs],
        "flash": {"components": dict(sorted(flash_parts.items(), key=lambda kv: -kv[1])),
                  "unattributed": flash_rest,
                  "strings_before_merging": sum(v for k, v in merged.items() if k != "<linker>"),
                  "strings_after_merging": merged.get("<linker>", 0),
                  "total": flash_used},
        "ram": {"purposes": {k: {"bytes": v["bytes"], "symbols": v["symbols"][:12]}
                             for k, v in sorted(purposes.items(), key=lambda kv: -kv[1]["bytes"])},
                "padding": ram_rest, "total": ram_used},
        "problems": problems,
    }


def text(rep):
    out = ["| Region | Size | Used | Free |", "|---|---:|---:|---:|"]
    for r in rep["regions"]:
        out.append(f"| {r['name']} | {r['length']:,} | {r['used']:,} ({100 * r['used'] / r['length']:.1f}%) | {r['free']:,} |")
    out += ["", "| Flash, by component | Bytes |", "|---|---:|"]
    for c, b in rep["flash"]["components"].items():
        out.append(f"| {c} | {b:,} |")
    out.append(f"| string constants after merging, vector table alignment, linker tables "
               f"| {rep['flash']['unattributed']:,} |")
    out.append(f"| **total** | **{rep['flash']['total']:,}** |")
    if rep["flash"].get("strings_after_merging"):
        out.append(f"\nString constants, as the linker merged them: {rep['flash']['strings_after_merging']:,} bytes.")
    else:
        out.append(f"\nString constants before the linker merged them: {rep['flash']['strings_before_merging']:,} bytes.")
    out += ["", "| RAM, by purpose | Bytes |", "|---|---:|"]
    for p, v in rep["ram"]["purposes"].items():
        out.append(f"| {p} | {v['bytes']:,} |")
    out.append(f"| alignment padding | {rep['ram']['padding']:,} |")
    out.append(f"| **total** | **{rep['ram']['total']:,}** |")
    for p in rep["problems"]:
        out.append(f"PROBLEM: {p}")
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("map")
    ap.add_argument("--elf", required=True)
    ap.add_argument("--nm", default="arm-none-eabi-nm")
    ap.add_argument("--readelf", default="arm-none-eabi-readelf")
    ap.add_argument("--json")
    ap.add_argument("--ldscript", help="the linker script with the MEMORY block (for an LLD map)")
    args = ap.parse_args(argv)
    try:
        rep = build_report(args.map, args.elf, args.nm, args.readelf, args.ldscript)
    except (OSError, subprocess.CalledProcessError, StopIteration, IndexError) as e:
        print(f"memory_report: {e}", file=sys.stderr)
        return 2
    print(text(rep))
    if args.json:
        Path(args.json).write_text(json.dumps(rep, indent=2) + "\n", encoding="utf-8")
    return 1 if rep["problems"] else 0


if __name__ == "__main__":
    sys.exit(main())
