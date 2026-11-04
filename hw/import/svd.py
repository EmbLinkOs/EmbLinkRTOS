#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The SVD importer (SPEC-014 §8, HW-004, HW-012, A77): a SoC description draft from a CMSIS-SVD
file, through EmbCC's embsvd parser.

    svd.py DEVICE.svd --vendor st --family stm32f4 --flash 0x08000000:512K --ram 0x20000000:128K -o draft.yaml
    svd.py DEVICE.svd ... --against soc/st/stm32f4/stm32f446.yaml     # what the SVD says differently

The draft holds what an SVD states: the CPU, the interrupt controller's count, priority bits and
the lines no peripheral uses, and each peripheral's base, size, interrupts and (from the vendor
table in compat/) its `compatible`, GPIO port and clock gate. Memory comes from --flash and
--ram. What an SVD does not state (clocks, pins and alternate functions, flash geometry, errata,
packages) is listed as `todo` at the top of the draft. A draft is `abstract` and `curated: false`:
a person checks it against the reference manual, splits it into family, die and part files, and
marks what they checked as curated.

--against compares the draft with an existing description (with its `extends` chain) and prints
every difference, matching peripherals by base address. It never writes a description
(HW-012): the author applies what the SVD gets right. Exit status 0, or 2 on an error.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / "hw" / "gen"))
from emb_hwgen import HwError, load, schema  # noqa: E402

VERSION = "0.1.0"
PPB = 0xE0000000  # Cortex-M private peripheral bus: architecture, not the SoC's peripherals

# SVD cpu name -> (arch variant, core, core with FPU, -mcpu, FPU kind for SP, for DP)
CORES = {
    "CM0": ("armv6m", "cortex-m0", None, "cortex-m0", None, None),
    "CM0PLUS": ("armv6m", "cortex-m0plus", None, "cortex-m0plus", None, None),
    "CM0+": ("armv6m", "cortex-m0plus", None, "cortex-m0plus", None, None),
    "CM1": ("armv6m", "cortex-m1", None, "cortex-m1", None, None),
    "CM3": ("armv7m", "cortex-m3", None, "cortex-m3", None, None),
    "CM4": ("armv7em", "cortex-m4", "cortex-m4f", "cortex-m4", "fpv4-sp-d16", "fpv4-sp-d16"),
    "CM7": ("armv7em", "cortex-m7", "cortex-m7f", "cortex-m7", "fpv5-sp-d16", "fpv5-d16"),
    "CM23": ("armv8m-base", "cortex-m23", None, "cortex-m23", None, None),
    "CM33": ("armv8m-main", "cortex-m33", "cortex-m33f", "cortex-m33", "fpv5-sp-d16", "fpv5-sp-d16"),
    "CM35P": ("armv8m-main", "cortex-m35p", "cortex-m35pf", "cortex-m35p", "fpv5-sp-d16", "fpv5-sp-d16"),
    "CM55": ("armv8.1m-main", "cortex-m55", "cortex-m55f", "cortex-m55", "fpv5-d16", "fpv5-d16"),
}

TODO = [
    "clocks: oscillators, PLLs, prescalers, buses (max_hz, from) and flash latency; the SVD has none",
    "memory: check --flash and --ram against the reference manual, and add any other region",
    "alternate_functions and the package files' gpio.ports: pins are not in an SVD",
    "flash: geometry (sectors or pages)",
    "errata, debug, and cpu.mpu.regions (the SVD says only that an MPU is present)",
    "compatible: every `svd,<group>` binds no driver; map it in hw/import/compat/<vendor>.yaml",
]


class ImportError_(Exception):
    pass


def ident(text):
    s = re.sub(r"[^a-z0-9_-]", "_", text.strip().lower())
    return s if re.match(r"^[a-z]", s) else "p_" + s


def parse_region(text, what):
    m = re.match(r"^(0x[0-9a-fA-F]+|[0-9]+):([0-9]+[KMG]?|0x[0-9a-fA-F]+)$", text or "")
    if not m:
        raise ImportError_(f"--{what} wants ORIGIN:LENGTH, as 0x08000000:512K")
    return int(m.group(1), 0), load.normalize(m.group(2)) if not m.group(2).startswith("0x") else int(m.group(2), 16)


def run_embsvd(svd, embsvd):
    exe = embsvd if os.path.sep in embsvd else shutil.which(embsvd)
    if not exe:
        raise ImportError_(f"{embsvd} not found: install EmbCC, or give --json with embsvd's output")
    with tempfile.TemporaryDirectory() as d:
        out = Path(d) / "svd.json"
        r = subprocess.run([exe, str(svd), "--json", str(out)], capture_output=True, text=True)
        if r.returncode != 0:
            raise ImportError_(f"embsvd failed on {svd}: {(r.stderr or r.stdout).strip()}")
        return json.loads(out.read_text(encoding="utf-8"))


def load_table(vendor):
    import yaml
    p = HERE / "compat" / f"{vendor}.yaml"
    if not p.is_file():
        return {"rules": [], "clock_gates": None}
    t = yaml.safe_load(p.read_text(encoding="utf-8")) or {}
    return {"rules": t.get("rules") or [], "clock_gates": t.get("clock_gates")}


def match_rule(table, periph, family):
    for rule in table["rules"]:
        if "family" in rule and rule["family"] != family:
            continue
        if "name" in rule and rule["name"] != periph["name"]:
            continue
        if "group" in rule and rule["group"] != (periph.get("groupName") or ""):
            continue
        return rule
    return None


def clock_gates(svd, table):
    """{PERIPHERAL name: (bus, bit, register offset)} from the vendor's enable registers."""
    spec = table.get("clock_gates")
    if not spec:
        return {}, {}
    owner = next((p for p in svd["peripherals"] if p["name"] == spec["peripheral"]), None)
    if owner is None:
        return {}, {}
    gates, buses = {}, {}
    for reg in owner.get("registers") or []:
        m = re.match(spec["register"], reg["name"])
        if not m:
            continue
        bus = m.group(1).lower()
        buses[bus] = reg["offset"]
        for f in reg.get("fields") or []:
            if f["name"].endswith("EN") and f["bitWidth"] == 1:
                gates[f["name"][:-2]] = (bus, f["bitOffset"])
    return gates, buses


def irq_name(periph_name, irq):
    n = irq["name"].strip()
    if n.lower() == periph_name.lower():
        return "global"
    low = n.lower()
    pre = periph_name.lower() + "_"
    if low.startswith(pre) and re.match(r"^[a-z]", low[len(pre):]):
        low = low[len(pre):]
    return ident(low)


def draft(svd, vendor, family, name, flash, ram, source, digest):
    cpu = svd.get("cpu") or {}
    core_key = (cpu.get("name") or "").upper()
    if core_key not in CORES:
        raise ImportError_(f"cpu {cpu.get('name')!r}: not a Cortex-M core this importer knows ({', '.join(sorted(CORES))})")
    variant, core, core_f, mcpu, fpu_sp, fpu_dp = CORES[core_key]
    has_fpu = bool(cpu.get("fpuPresent")) and core_f is not None
    out_cpu = {"core": core_f if has_fpu else core}
    cflags = [f"-mcpu={mcpu}"]
    if has_fpu:
        out_cpu["fpu"] = fpu_dp if cpu.get("fpuDP") else fpu_sp
        cflags += [f"-mfpu={out_cpu['fpu']}", "-mfloat-abi=hard"]
    else:
        cflags += ["-mfloat-abi=soft"]
    if cpu.get("mpuPresent"):
        out_cpu["mpu"] = {"model": "armv8m" if variant.startswith("armv8") else ("armv6m" if variant == "armv6m" else "armv7m"),
                          "regions": 8}
    if variant != "armv6m" and core_key not in ("CM23",):
        out_cpu["cycle_counter"] = "dwt"
    out_cpu["cflags"] = cflags

    table = load_table(vendor)
    gates, buses = clock_gates(svd, table)
    peripherals, skipped, used = [], [], {}
    for p in sorted(svd["peripherals"], key=lambda p: (p["baseAddress"], p["name"])):
        if p["baseAddress"] >= PPB:
            skipped.append(p["name"])
            continue
        blocks = p.get("addressBlocks") or []
        size = max((b["offset"] + b["size"] for b in blocks), default=0x400)
        rule = match_rule(table, p, family)
        group = p.get("groupName") or p["name"]
        node = {"name": ident(rule["rename"]) if rule and rule.get("rename") else ident(p["name"]),
                "compatible": rule["compatible"] if rule else "svd," + re.sub(r"[^a-z0-9-]", "-", group.lower()).strip("-"),
                "base": p["baseAddress"], "size": size}
        if rule and rule.get("port"):
            m = re.match(rule["port"], p["name"])
            if m:
                node["port"] = m.group(1)
        if p["name"] in gates:
            node["clock"] = {"bus": gates[p["name"]][0], "bit": gates[p["name"]][1]}
        irqs, seen = [], set()
        for i in sorted(p.get("interrupts") or [], key=lambda i: i["value"]):
            if i["value"] in seen:
                continue
            seen.add(i["value"])
            irqs.append({"name": irq_name(p["name"], i), "number": i["value"]})
            used.setdefault(i["value"], node["name"])
        # interrupt names must be unique in a peripheral
        names = [i["name"] for i in irqs]
        for i in irqs:
            if names.count(i["name"]) > 1:
                i["name"] = f"{i['name']}_{i['number']}"
        if irqs:
            node["irqs"] = irqs
        peripherals.append(node)
    lines = sorted({i["value"] for i in svd.get("interrupts") or []} | set(used))
    count = cpu.get("deviceNumInterrupts") or ((max(lines) + 1) if lines else 1)
    interrupts = {"controller": "nvic", "count": count}
    if cpu.get("nvicPrioBits"):
        interrupts["priority_bits"] = cpu["nvicPrioBits"]
    interrupts["systick"] = not cpu.get("vendorSystickConfig", False)
    reserved = [n for n in range(count) if n not in lines]
    if reserved:
        interrupts["reserved"] = reserved
    regions = []
    if flash:
        regions.append({"name": "flash", "base": flash[0], "size": flash[1], "attrs": ["r", "x", "boot"]})
    if ram:
        regions.append({"name": "sram", "base": ram[0], "size": ram[1], "attrs": ["r", "w", "x", "dma"]})
    if not regions:
        raise ImportError_("an SVD states no memory: give --flash and --ram (ORIGIN:LENGTH)")
    names = [p["name"] for p in peripherals]
    for p in peripherals:  # a renamed or clashing name stays unique
        if names.count(p["name"]) > 1 or p["name"] in [r["name"] for r in regions]:
            p["name"] = f"{p['name']}_{p['base']:08x}"
    doc = {
        "schema_version": 0, "kind": "soc", "name": name, "vendor": vendor, "family": family,
        "abstract": True, "curated": False,
        "imported_from": {"tool": f"hw/import/svd.py {VERSION} (embsvd)", "source": source, "hash": f"sha256:{digest}"},
        "arch": {"port": "cortex_m", "variant": variant},
        "cpu": out_cpu,
        "memory": {"regions": regions},
        "interrupts": interrupts,
        "peripherals": peripherals,
    }
    notes = {"skipped": skipped, "buses": buses}
    return doc, notes


# ------------------------------------------------------------------------------ YAML out

def _scalar(k, v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        if k in ("base",):
            return f"0x{v:08X}"
        if k == "size" or k == "enable_offset":
            if v % (1 << 20) == 0:
                return f"{v >> 20}M"
            if v % 1024 == 0 and v >= 1024 and k == "size":
                return f"{v >> 10}K" if v >= 64 * 1024 else f"0x{v:X}"
            return f"0x{v:X}"
        return str(v)
    s = str(v)
    if re.match(r"^[A-Za-z0-9_.+/-]+$", s) and not re.match(r"^(true|false|null|yes|no|on|off|[0-9].*)$", s, re.I):
        return s
    return json.dumps(s)


def _flow(k, v):
    if isinstance(v, dict):
        return "{ " + ", ".join(f"{kk}: {_flow(kk, vv)}" for kk, vv in v.items()) + " }"
    if isinstance(v, list):
        return "[" + ", ".join(_flow(k, x) for x in v) + "]"
    return _scalar(k, v)


def to_yaml(doc, notes, svd_name):
    out = ["# SPDX-License-Identifier: Apache-2.0",
           f"# A draft SoC description imported from {svd_name} by hw/import/svd.py (SPEC-014 §8).",
           "# Not curated: check every fact against the reference manual and the datasheet, split the",
           "# file into family, die and part files, and mark what you checked `curated: true`.",
           "# todo:"]
    out += [f"#   - {t}" for t in TODO]
    if notes["buses"]:
        out.append("#   - the SVD's clock-enable registers, for clocks.buses' enable_offset: "
                   + ", ".join(f"{b} 0x{o:X}" for b, o in sorted(notes["buses"].items())))
    if notes["skipped"]:
        out.append("# Left out, as architecture rather than SoC (private peripheral bus): " + ", ".join(notes["skipped"]))
    for k in ("schema_version", "kind", "name", "vendor", "family", "abstract", "curated"):
        out.append(f"{k}: {_scalar(k, doc[k])}")
    out.append(f"imported_from: {_flow('imported_from', doc['imported_from'])}")
    out.append(f"arch: {_flow('arch', doc['arch'])}")
    out.append("cpu:")
    for k, v in doc["cpu"].items():
        out.append(f"  {k}: {_flow(k, v)}")
    out.append("memory:\n  regions:")
    for r in doc["memory"]["regions"]:
        out.append(f"    - {_flow('', r)}")
    out.append("interrupts:")
    for k, v in doc["interrupts"].items():
        out.append(f"  {k}: {_flow(k, v)}")
    out.append("peripherals:")
    for p in doc["peripherals"]:
        out.append(f"  - {_flow('', p)}")
    return "\n".join(out) + "\n"


# ------------------------------------------------------------------------------ compare

def compare(doc, existing):
    """Differences between the draft and a resolved description: [(path, theirs, ours)]."""
    diffs = []

    def d(path, a, b):
        if a != b:
            diffs.append((path, a, b))

    for k in ("variant",):
        d(f"arch.{k}", existing.get("arch", {}).get(k), doc["arch"][k])
    ecpu = existing.get("cpu", {})
    for k in ("core", "fpu"):
        d(f"cpu.{k}", ecpu.get(k), doc["cpu"].get(k))
    d("cpu.mpu", "present" if ecpu.get("mpu") else None, "present" if doc["cpu"].get("mpu") else None)
    eint = existing.get("interrupts", {})
    for k in ("controller", "count", "priority_bits", "systick"):
        d(f"interrupts.{k}", eint.get(k), doc["interrupts"].get(k))
    d("interrupts.reserved", sorted(eint.get("reserved", [])), doc["interrupts"].get("reserved", []))
    ours = {p["base"]: p for p in doc["peripherals"]}
    only_ours = set(ours)
    for p in existing.get("peripherals", []):
        q = ours.get(p["base"])
        at = f"peripherals.{p['name']}"
        if q is None:
            diffs.append((at, f"at 0x{p['base']:08X}", "no peripheral at that base"))
            continue
        only_ours.discard(p["base"])
        for k in ("name", "compatible", "size", "port", "clock"):
            d(f"{at}.{k}", p.get(k), q.get(k))
        d(f"{at}.irqs", sorted((i["number"], i["name"]) for i in p.get("irqs", [])),
          sorted((i["number"], i["name"]) for i in q.get("irqs", [])))
    extra = sorted(ours[b]["name"] for b in only_ours)
    return diffs, extra


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("svd", help="the CMSIS-SVD file")
    ap.add_argument("--vendor", required=True, help="the vendor directory name under soc/ (st, nordic, ...)")
    ap.add_argument("--family", required=True, help="the family directory name (stm32f4, ...)")
    ap.add_argument("--name", help="the description's name (default: the SVD device name, lower case)")
    ap.add_argument("--flash", help="ORIGIN:LENGTH of the flash, as 0x08000000:512K")
    ap.add_argument("--ram", help="ORIGIN:LENGTH of the RAM, as 0x20000000:128K")
    ap.add_argument("-o", "--out", help="write the draft here (default: standard output)")
    ap.add_argument("--against", help="compare with this existing description instead of writing a draft")
    ap.add_argument("--json", help="embsvd's --json output for the SVD, instead of running embsvd")
    ap.add_argument("--embsvd", default="embsvd", help="the embsvd to run (default: embsvd on PATH)")
    a = ap.parse_args(argv)
    try:
        svd_path = Path(a.svd)
        digest = hashlib.sha256(svd_path.read_bytes()).hexdigest()
        svd = json.loads(Path(a.json).read_text(encoding="utf-8")) if a.json else run_embsvd(svd_path, a.embsvd)
        if svd.get("schema") != 1:
            raise ImportError_(f"embsvd JSON schema {svd.get('schema')!r}: this importer reads schema 1")
        name = a.name or ident(svd.get("device", {}).get("name") or svd_path.stem)
        flash = parse_region(a.flash, "flash") if a.flash else None
        ram = parse_region(a.ram, "ram") if a.ram else None
        doc, notes = draft(svd, a.vendor, a.family, name, flash, ram, svd_path.name, digest)
        problems = schema.validate(doc, schema.load_schema("soc"))
        if problems:
            raise ImportError_("the draft does not validate (an importer bug): "
                               + "; ".join(f"{p}: {m}" for p, m in problems))
        if a.against:
            existing = load.flatten(Path(a.against), ROOT).data
            diffs, extra = compare(doc, existing)
            print(f"svd.py: {svd_path.name} against {a.against}: {len(diffs)} difference(s); "
                  f"the description is not changed")
            for path, theirs, ours in diffs:
                print(f"  {path}: description {theirs!r}, SVD {ours!r}")
            if extra:
                print(f"  only in the SVD ({len(extra)}): {', '.join(extra)}")
            return 0
        text = to_yaml(doc, notes, svd_path.name)
        if a.out:
            out = Path(a.out)
            if out.exists() and "curated: true" in out.read_text(encoding="utf-8"):
                raise ImportError_(f"{out} holds curated nodes: an import never overwrites them (HW-012); "
                                   "use --against to see what differs")
            out.write_text(text, encoding="utf-8")
            print(f"svd.py: wrote {out} ({len(doc['peripherals'])} peripherals, curated: false)")
        else:
            sys.stdout.write(text)
        return 0
    except (ImportError_, HwError, OSError, json.JSONDecodeError) as e:
        msg = "\n".join(e.errors) if isinstance(e, HwError) else str(e)
        print(f"svd.py: {msg}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
