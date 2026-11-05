#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The DeviceTree board importer (SPEC-014 §8, HW-004, HW-012, A82): a board description draft
from a Zephyr board's .dts, preprocessed by the C preprocessor and flattened by dtc.

    dts.py BOARD.dts -I zephyr/dts -I zephyr/dts/arm -I zephyr/include ... --soc st/stm32f4/stm32f446re.yaml -o board.yaml
    dts.py BOARD.dts ... --soc ... --against boards/st/nucleo_f446re/board.yaml   # differences only
    dts.py --flat FLAT.dts --soc ...                                               # dtc's output, given

The draft holds the board facts a Zephyr board states, for STM32 parts in this increment:
- the enabled oscillators (HSE in bypass is an external clock);
- the system clock and the PLL factors;
- the console (`chosen` zephyr,console), its baud rate and its pinctrl pins as P<port><n>.af<k>;
- the gpio-leds and gpio-keys, with their polarity;
- the flash partitions. The code partition becomes `app`, the rest reserved.
Zephyr's clock and partition settings are its own choices; the draft says so. What a .dts does
not hold (test lines, the debug probe, simulation) is listed as `todo`. A draft is
`curated: false`. --against prints every difference with an existing board and never writes it.
Exit status 0, or 2 on an error.
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
from emb_hwgen import load, schema  # noqa: E402

VERSION = "0.1.0"
OSCILLATORS = {"clk_hse": "hse", "clk_lse": "lse"}  # STM32 clock node labels -> our oscillator names


class DtsError(Exception):
    pass


# ------------------------------------------------------------------------------ dtc's output

# names may hold commas (zephyr,console); between values a comma is a token of its own
TOKEN = re.compile(r'\s*(?:(/\*.*?\*/)|(//[^\n]*)|("(?:[^"\\]|\\.)*")|(<[^>]*>)|(\[[^\]]*\])|([{};=])|([^\s{};="<\[]+))', re.S)


class Node:
    def __init__(self, name, path, labels):
        self.name, self.path, self.labels = name, path, labels
        self.props, self.children = {}, []

    def get(self, key, default=None):
        return self.props.get(key, default)


def tokens(text):
    pos, out = 0, []
    while pos < len(text):
        m = TOKEN.match(text, pos)
        if not m or m.end() == pos:
            if text[pos:].strip() == "":
                break
            raise DtsError(f"cannot read the flattened tree at offset {pos}")
        pos = m.end()
        if m.group(1) or m.group(2):
            continue
        out.append(next(g for g in m.groups()[2:] if g is not None))
    return out


def value(parts):
    """A property's value: a list of cells (ints and &refs), strings, or bytes."""
    out = []
    for p in parts:
        if p == ",":
            continue
        if p.startswith('"'):
            out.append(json.loads(p))
        elif p.startswith("<"):
            out.append([c if c.startswith("&") else int(c, 0) for c in p[1:-1].split()])
        elif p.startswith("["):
            out.append(bytes.fromhex(p[1:-1].replace(" ", "")))
        else:
            out.append(p)
    return out


def parse(text):
    """The tree dtc -O dts writes: (root node, {label: node}, {path: node})."""
    toks = tokens(text)
    labels, paths = {}, {}
    i = 0
    while i < len(toks) and toks[i] != "/":
        i += 1
    if i >= len(toks):
        raise DtsError("no root node")
    root = Node("/", "/", [])
    paths["/"] = root
    i += 2  # '/' '{'

    def body(node, i):
        while i < len(toks):
            t = toks[i]
            if t == "}":
                return i + 2  # '}' ';'
            ls = []
            while t.endswith(":"):
                ls.append(t[:-1])
                i += 1
                t = toks[i]
            if toks[i + 1] == "{":
                path = (node.path.rstrip("/") + "/" + t)
                child = Node(t, path, ls)
                node.children.append(child)
                paths[path] = child
                for lb in ls:
                    labels[lb] = child
                i = body(child, i + 2)
            elif toks[i + 1] == ";":
                node.props[t] = True
                i += 2
            elif toks[i + 1] == "=":
                j = i + 2
                while toks[j] != ";":
                    j += 1
                node.props[t] = value(toks[i + 2:j])
                i = j + 1
            else:
                raise DtsError(f"unexpected {toks[i + 1]!r} after {t!r} in {node.path}")
        raise DtsError("unterminated node")

    body(root, i)
    return root, labels, paths


def flatten(dts, includes, cpp=None, dtc="dtc"):
    """Preprocess with the C preprocessor (as Zephyr does) and flatten with dtc."""
    cc = cpp or shutil.which("clang") or shutil.which("gcc") or shutil.which("cpp")
    if not cc or not shutil.which(dtc):
        raise DtsError("the importer needs a C preprocessor (clang or gcc) and dtc on PATH, or --flat")
    with tempfile.TemporaryDirectory() as d:
        pp = Path(d) / "board.pp.dts"
        args = [cc, "-E", "-P", "-nostdinc", "-undef", "-D__DTS__", "-x", "assembler-with-cpp"]
        args += [x for inc in includes for x in ("-I", str(inc))] + ["-I", str(Path(dts).parent), str(dts), "-o", str(pp)]
        r = subprocess.run(args, capture_output=True, text=True)
        if r.returncode != 0:
            raise DtsError("preprocessing failed: " + (r.stderr.strip().splitlines() or ["?"])[-1])
        r = subprocess.run([dtc, "-q", "-I", "dts", "-O", "dts", str(pp)], capture_output=True, text=True)
        if r.returncode != 0:
            raise DtsError("dtc failed: " + r.stderr.strip())
        return r.stdout


# ------------------------------------------------------------------------------ the draft

def cells(node, key):
    v = node.get(key)
    return v[0] if v and isinstance(v[0], list) else None


def okay(node):
    s = node.get("status")
    return s is None or s == ["okay"]


def ref(token, labels):
    return labels.get(token[1:]) if isinstance(token, str) and token.startswith("&") else None


def gpio_pin(spec, labels):
    """<&gpioa 5 flags> -> ('PA5', active)."""
    ctl = ref(spec[0], labels)
    m = re.match(r"^gpio([a-k])$", (ctl.labels[0] if ctl and ctl.labels else ""))
    if not m:
        raise DtsError(f"a GPIO line on {spec[0]}, not an STM32 GPIO port")
    return f"P{m.group(1).upper()}{spec[1]}", "low" if spec[2] & 1 else "high"


def pinmux(v):
    """Zephyr's STM32_PINMUX: mode in bits 0-4, line 5-8, port 9-13 (stm32-pinctrl.h)."""
    mode, line, port = v & 0x1F, (v >> 5) & 0xF, (v >> 9) & 0x1F
    if mode > 15:
        return None  # analog or plain GPIO: not a peripheral's pin
    return f"P{chr(ord('A') + port)}{line}.af{mode}"


def ident(text):
    s = re.sub(r"[^a-z0-9_-]", "_", text.strip().lower())
    return s if re.match(r"^[a-z]", s) else "n_" + s


def draft(flat, name, soc, source, digest, reserved=()):
    root, labels, paths = parse(flat)
    todo, warnings = [], []
    compat = (root.get("compatible") or ["?"])[0]
    vendor = compat.split(",")[0] if "," in compat else "todo"
    doc = {"schema_version": 0, "kind": "board", "name": name, "vendor": vendor, "soc": soc, "curated": False,
           "imported_from": {"tool": f"hw/import/dts.py {VERSION} (dtc)", "source": source, "hash": f"sha256:{digest}"}}
    if root.get("model"):
        doc["doc"] = f"Imported from Zephyr's description: {root.get('model')[0]}."
    # oscillators
    osc = {}
    for label, ours in OSCILLATORS.items():
        n = labels.get(label)
        if n is None:
            continue
        hz = cells(n, "clock-frequency")
        if not okay(n):
            todo.append(f"oscillators.{ours}: {label} is disabled in the .dts; add it if the board fits one")
            continue
        if hz:
            osc[ours] = {"hz": hz[0], "source": "external_clock" if n.get("hse-bypass") else "external_crystal"}
    if osc:
        doc["oscillators"] = osc
    # clocks
    rcc = labels.get("rcc")
    pll = labels.get("pll")
    clocks = {}
    if rcc is not None and cells(rcc, "clock-frequency"):
        clocks["cpu_hz"] = cells(rcc, "clock-frequency")[0]
        src = ref((cells(rcc, "clocks") or [None])[0], labels)
        if src is pll and pll is not None:
            psrc = ref((cells(pll, "clocks") or [None])[0], labels)
            clocks["source"] = OSCILLATORS.get(psrc.labels[0] if psrc and psrc.labels else "", "todo")
            factors = {k: cells(pll, p)[0] for k, p in (("m", "div-m"), ("n", "mul-n"), ("p", "div-p"), ("q", "div-q"),
                                                       ("r", "div-r")) if cells(pll, p)}
            if {"m", "n", "p"} <= set(factors):
                clocks["pll"] = factors
        elif src is not None and src.labels:
            clocks["source"] = OSCILLATORS.get(src.labels[0], "todo")
        todo.append("clocks: Zephyr's frequency and PLL factors are its own choice; keep them or ask for another")
    doc["clocks"] = clocks or {"cpu_hz": 1, "source": "todo"}
    # console and its pins
    chosen = paths.get("/chosen")
    con_path = (chosen.get("zephyr,console") or [None])[0] if chosen else None
    con = paths.get(con_path) if con_path else None
    if con is None or not con.labels:
        raise DtsError("no console: the .dts names no chosen zephyr,console")
    dev = ident(con.labels[0])
    baud = (cells(con, "current-speed") or [115200])[0]
    pins = {}
    for r in cells(con, "pinctrl-0") or []:
        p = ref(r, labels)
        if p is None:
            continue
        pm = pinmux((cells(p, "pinmux") or [0x11])[0])
        m = re.match(rf"^{re.escape(dev)}_([a-z0-9]+)_p[a-k][0-9]+$", p.name)
        if pm and m:
            pins[m.group(1)] = pm
    if pins:
        doc["pins"] = {dev: dict(sorted(pins.items()))}
    doc["console"] = {"device": dev, "baud": baud}
    # LEDs and buttons
    for kind, compatible in (("leds", "gpio-leds"), ("buttons", "gpio-keys")):
        lines = []
        for n in paths.values():
            if (n.get("compatible") or [None])[0] != compatible or not okay(n):
                continue
            for c in n.children:
                g = cells(c, "gpios")
                if not g or not okay(c):
                    continue
                pin, active = gpio_pin(g, labels)
                line = {"name": ident(c.labels[0] if c.labels else c.name), "gpio": pin, "active": active}
                if c.get("label"):
                    line["doc"] = c.get("label")[0]
                lines.append(line)
        if lines:
            doc[kind] = lines
    # flash partitions. An image runs from the flash's start: Zephyr's code partition elsewhere
    # (MCUboot's slot 0) needs a bootloader the RTOS does not have, so the draft takes the whole
    # flash and lists Zephyr's layout as todo.
    code = (chosen.get("zephyr,code-partition") or [None])[0] if chosen else None
    parts, layout = [], []
    for n in paths.values():
        if n.name != "partitions":
            continue
        flash = paths.get(n.path.rsplit("/", 1)[0])
        freg = cells(flash, "reg") if flash else None
        base = freg[0] if freg else 0
        for c in n.children:
            reg = cells(c, "reg")
            if not reg:
                continue
            label = (c.get("label") or [c.labels[0] if c.labels else c.name])[0]
            layout.append(f"{label} 0x{base + reg[0]:08X}+0x{reg[1]:X}")
            if c.path == code and reg[0] == 0:
                parts.append({"name": "app", "base": base, "size": reg[1]})
        if not parts and freg and len(freg) >= 2:
            parts.append({"name": "app", "base": base, "size": freg[1]})
    if parts:
        doc["flash_partitions"] = parts
    if layout:
        todo.append("flash_partitions: the app is the whole flash; Zephyr's own layout, for a bootloader: "
                    + ", ".join(layout))
    if len(reserved) >= 2:
        doc["test_irqs"] = list(reserved[:2])
        todo.append(f"test_irqs: the SoC's first two reserved lines ({reserved[0]}, {reserved[1]}); any free lines will do")
    todo.append("debug_probe, simulation and defaults: a .dts has none")
    others = sorted(ident(n.labels[0]) for n in paths.values()
                    if n.labels and okay(n) and cells(n, "pinctrl-0") and n is not con)
    if others:
        todo.append("other enabled peripherals with pins, not imported: " + ", ".join(others))
    return doc, todo


# ------------------------------------------------------------------------------ out and compare

class _Hex(int):
    pass


def to_yaml(doc, todo, source):
    import yaml

    class Dumper(yaml.SafeDumper):
        pass
    Dumper.add_representer(_Hex, lambda d, v: d.represent_scalar("tag:yaml.org,2002:int", f"0x{int(v):08X}"))
    for p in doc.get("flash_partitions", []):
        p["base"] = _Hex(p["base"])
    head = ["# SPDX-License-Identifier: Apache-2.0",
            f"# A draft board description imported from {source} by hw/import/dts.py (SPEC-014 §8).",
            "# Not curated: check every fact against the board's manual and schematic, and mark the file",
            "# `curated: true` once checked.", "# todo:"] + [f"#   - {t}" for t in todo]
    body = yaml.dump(doc, Dumper=Dumper, sort_keys=False, default_flow_style=None, width=100)
    return "\n".join(head) + "\n" + body


def compare(doc, existing):
    diffs = []

    def d(path, a, b):
        if a != b:
            diffs.append((path, a, b))

    for name in sorted(set(existing.get("oscillators", {})) | set(doc.get("oscillators", {}))):
        d(f"oscillators.{name}", existing.get("oscillators", {}).get(name), doc.get("oscillators", {}).get(name))
    for k in ("cpu_hz", "source", "pll"):
        theirs = existing.get("clocks", {}).get(k)
        ours = doc.get("clocks", {}).get(k)
        if k == "pll" and theirs and ours:
            theirs = {x: theirs[x] for x in ours}  # the factors the .dts states
        d(f"clocks.{k}", theirs, ours)
    d("console", existing.get("console"), doc.get("console"))
    dev = doc["console"]["device"]
    d(f"pins.{dev}", existing.get("pins", {}).get(dev), doc.get("pins", {}).get(dev))
    for kind in ("leds", "buttons"):  # matched by line: names are each project's own
        key = lambda xs: sorted((x["gpio"], x["active"]) for x in xs)  # noqa: E731
        d(kind, key(existing.get(kind, [])), key(doc.get(kind, [])))
    key = lambda xs: [(p["name"], p["base"], p["size"]) for p in xs]  # noqa: E731
    d("flash_partitions", key(existing.get("flash_partitions", [])), key(doc.get("flash_partitions", [])))
    d("test_irqs", existing.get("test_irqs"), doc.get("test_irqs"))
    return diffs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dts", nargs="?", help="the board's .dts")
    ap.add_argument("-I", dest="includes", action="append", default=[], help="an include directory (repeatable)")
    ap.add_argument("--flat", help="dtc's flattened output, instead of preprocessing a .dts")
    ap.add_argument("--soc", required=True, help="the SoC description the board names, relative to soc/")
    ap.add_argument("--name", help="the board's name (default: the .dts file's stem)")
    ap.add_argument("-o", "--out", help="write the draft here (default: standard output)")
    ap.add_argument("--against", help="compare with this existing board description instead of writing")
    a = ap.parse_args(argv)
    try:
        if a.flat:
            src = Path(a.flat)
            flat = src.read_text(encoding="utf-8")
        elif a.dts:
            src = Path(a.dts)
            flat = flatten(src, a.includes)
        else:
            raise DtsError("give a .dts, or --flat")
        if not (ROOT / "soc" / a.soc).is_file():
            raise DtsError(f"no SoC description soc/{a.soc}")
        reserved = load.flatten(ROOT / "soc" / a.soc, ROOT).data.get("interrupts", {}).get("reserved", [])
        name = a.name or ident(src.stem.split(".")[0])
        doc, todo = draft(flat, name, a.soc, src.name, hashlib.sha256(src.read_bytes()).hexdigest(), reserved)
        problems = schema.validate(doc, schema.load_schema("board"))
        if problems:
            raise DtsError("the draft does not validate: " + "; ".join(f"{p}: {m}" for p, m in problems))
        if a.against:
            existing = load.load(Path(a.against), ROOT).data
            diffs = compare(doc, existing)
            print(f"dts.py: {src.name} against {a.against}: {len(diffs)} difference(s); the description is not changed")
            for path, theirs, ours in diffs:
                print(f"  {path}: description {theirs!r}, .dts {ours!r}")
            return 0
        text = to_yaml(doc, todo, src.name)
        if a.out:
            out = Path(a.out)
            if out.exists():
                import yaml
                if (yaml.safe_load(out.read_text(encoding="utf-8")) or {}).get("curated") is True:
                    raise DtsError(f"{out} is curated: an import never overwrites it (HW-012); use --against")
            out.write_text(text, encoding="utf-8")
            print(f"dts.py: wrote {out} (curated: false)")
        else:
            sys.stdout.write(text)
        return 0
    except (DtsError, OSError) as e:
        print(f"dts.py: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
