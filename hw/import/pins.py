#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""The STM32 pin importer (SPEC-014 §8, HW-012, A85): a package's bonded pins and its pins'
alternate functions, from ST's open pin data (github.com/STMicroelectronics/STM32_open_pin_data,
BSD-3-Clause).

    pins.py --mcu 'mcu/STM32F446R(C-E)Tx.xml' --gpio mcu/IP/GPIO-STM32F446_gpio_v1_0_Modes.xml \\
            --soc st/stm32f4/stm32f446re.yaml
    pins.py ... --against soc/st/stm32f4/stm32f446re.yaml     # differences only; writes nothing

The MCU file lists the package's pins and each pin's signals; the GPIO modes file gives each
signal's alternate-function number (GPIO_AF<n>_...). The draft is a package file's
`gpio.ports` (one bonding mask per port) and `alternate_functions`, for the signals of the
peripherals the SoC description (--soc) has, so only what a driver can use. --against compares
both with the description (through its extends chain) and prints the differences. A function
the description lacks is reported apart, since a curated table may list only some. Exit status
0, or 2 on an error.
"""
import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / "hw" / "gen"))
from emb_hwgen import HwError, load  # noqa: E402

PIN = re.compile(r"^P([A-K])(1[0-5]|[0-9])(?:[-_].*)?$")


class PinError(Exception):
    pass


def _strip(tag):
    return tag.split("}", 1)[-1]


def read_mcu(path):
    """{pin: [signal]} for the package's I/O pins, and the MCU's reference name."""
    root = ET.parse(path).getroot()
    pins = {}
    for el in root.iter():
        if _strip(el.tag) != "Pin" or el.get("Type") != "I/O":
            continue
        m = PIN.match(el.get("Name", ""))
        if not m:
            continue
        name = f"P{m.group(1)}{m.group(2)}"
        pins[name] = [s.get("Name") for s in el if _strip(s.tag) == "Signal" and s.get("Name") != "GPIO"]
    if not pins:
        raise PinError(f"{path}: no I/O pins (not an open pin data MCU file?)")
    return root.get("RefName", "?"), pins


def read_gpio(path):
    """{(pin, signal): af number} from a GPIO modes file."""
    root = ET.parse(path).getroot()
    out = {}
    for pin in root.iter():
        if _strip(pin.tag) != "GPIO_Pin":
            continue
        m = PIN.match(pin.get("Name", ""))
        if not m:
            continue
        name = f"P{m.group(1)}{m.group(2)}"
        for sig in pin:
            if _strip(sig.tag) != "PinSignal":
                continue
            for v in sig.iter():
                if _strip(v.tag) == "PossibleValue":
                    af = re.match(r"^GPIO_AF(\d+)_", v.text or "")
                    if af:
                        out[(name, sig.get("Name"))] = int(af.group(1))
    if not out:
        raise PinError(f"{path}: no alternate functions (not a GPIO modes file?)")
    return out


def draft(pins, afs, peripherals):
    """(gpio.ports masks, alternate_functions) for the signals of the named peripherals."""
    ports = {}
    for name in pins:
        port, n = name[1], int(name[2:])
        ports[port] = ports.get(port, 0) | (1 << n)
    table = {}
    for name in sorted(pins, key=lambda p: (p[1], int(p[2:]))):
        for sig in pins[name]:
            periph = sig.split("_", 1)[0].lower()
            if periph not in peripherals or (name, sig) not in afs:
                continue
            table.setdefault(name, {})[f"af{afs[(name, sig)]}"] = sig.lower()
    return dict(sorted(ports.items())), table


def compare(ports, table, existing):
    """(differences, functions only in the pin data)."""
    diffs, extra = [], []
    have = existing.get("gpio", {}).get("ports", {})
    for port in sorted(set(have) | set(ports)):
        a, b = have.get(port), ports.get(port)
        if a != b:
            diffs.append((f"gpio.ports.{port}", None if a is None else f"0x{a:04X}", None if b is None else f"0x{b:04X}"))
    cur = existing.get("alternate_functions", {})
    for pin in sorted(set(cur) | set(table)):
        for af in sorted(set(cur.get(pin, {})) | set(table.get(pin, {}))):
            a, b = cur.get(pin, {}).get(af), table.get(pin, {}).get(af)
            if a == b:
                continue
            if a is None:
                extra.append(f"{pin}.{af} {b}")
            elif pin in table or pin[1] in ports and (ports[pin[1]] >> int(pin[2:])) & 1:
                diffs.append((f"alternate_functions.{pin}.{af}", a, b))
    return diffs, extra


def yaml_out(ref, ports, table, source):
    out = [f"# Imported from ST's open pin data for {ref} ({source}) by hw/import/pins.py; not curated.",
           "gpio:", "  ports: { " + ", ".join(f"{p}: 0x{m:04X}" for p, m in ports.items()) + " }",
           "alternate_functions:"]
    for pin, fs in table.items():
        out.append(f"  {pin}: {{ " + ", ".join(f"{k}: {v}" for k, v in sorted(fs.items(), key=lambda kv: int(kv[0][2:])))
                   + " }")
    return "\n".join(out) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mcu", required=True, help="the open pin data MCU file of one package")
    ap.add_argument("--gpio", required=True, help="the family's GPIO modes file (mcu/IP/GPIO-..._Modes.xml)")
    ap.add_argument("--soc", required=True, help="the SoC description whose peripherals select the signals (relative to soc/)")
    ap.add_argument("--against", help="compare with this description instead of printing a draft")
    a = ap.parse_args(argv)
    try:
        soc = load.flatten(ROOT / "soc" / a.soc, ROOT).data
        peripherals = {p["name"] for p in soc.get("peripherals", []) if p.get("compatible") != "st,stm32-gpio"}
        ref, pins = read_mcu(a.mcu)
        ports, table = draft(pins, read_gpio(a.gpio), peripherals)
        if a.against:
            existing = load.flatten(Path(a.against), ROOT).data
            diffs, extra = compare(ports, table, existing)
            print(f"pins.py: {ref} against {a.against}: {len(diffs)} difference(s); the description is not changed")
            for path, theirs, ours in diffs:
                print(f"  {path}: description {theirs!r}, pin data {ours!r}")
            if extra:
                print(f"  only in the pin data ({len(extra)}): {', '.join(extra)}")
            return 0
        sys.stdout.write(yaml_out(ref, ports, table, Path(a.mcu).name))
        return 0
    except (PinError, HwError, OSError, ET.ParseError) as e:
        msg = "\n".join(e.errors) if isinstance(e, HwError) else str(e)
        print(f"pins.py: {msg}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
