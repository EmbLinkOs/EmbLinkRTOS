# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Resolution and semantic validation (SPEC-014 §7, HW-011): a board and its SoC become one
resolved model, or every problem found is reported as 'file:line: node.path: message'."""
import re
from pathlib import Path

from . import HwError, clock, load, schema

PIN = re.compile(r"^P([A-K])(1[0-5]|[0-9])(?:\.af(1[0-5]|[0-9]))?$")
# a SoC with one numbered bank (gpio.lines, A79): GPIO<n>, with function select <k> as .f<k>
PIN_BANK = re.compile(r"^GPIO([1-5][0-9]|6[0-3]|[0-9])(?:\.f([12][0-9]|3[01]|[0-9]))?$")

# Sections whose items are named and must be unique (SPEC-014 §7: duplicate name).
SOC_NAMED = ("memory.regions", "peripherals", "clocks.sources", "clocks.plls", "clocks.buses", "clocks.voltage_scales")
BOARD_NAMED = ("leds", "buttons", "flash_partitions")


def _get(data, dotted):
    cur = data
    for part in dotted.split("."):
        if not isinstance(cur, dict) or part not in cur:
            return None
        cur = cur[part]
    return cur


def parse_pin(text):
    m = PIN.match(text)
    if m:
        return {"port": m.group(1), "number": int(m.group(2)), "af": int(m.group(3)) if m.group(3) else None,
                "name": f"P{m.group(1)}{m.group(2)}", "bank": False}
    m = PIN_BANK.match(text)
    if m:
        return {"port": "", "number": int(m.group(1)), "af": int(m.group(2)) if m.group(2) else None,
                "name": f"GPIO{m.group(1)}", "bank": True}
    return None


def af_key(pin, n):
    """The alternate-function key of a pin's description: af<n> on a ported SoC, f<n> on a bank."""
    return f"f{n}" if pin["bank"] else f"af{n}"


class Resolver:
    def __init__(self, root):
        self.root = Path(root)
        self.errors = []
        self.warnings = []

    def err(self, model, path, msg):
        self.errors.append(f"{model.at(path)}: {msg}")

    # -------------------------------------------------------------- entry point

    def resolve(self, board_dir):
        self.board_dir = Path(board_dir)
        board_path = Path(board_dir) / "board.yaml"
        board = load.flatten(board_path, self.root)
        self._schema(board, "board")
        self._duplicates(board, BOARD_NAMED)
        if self.errors:
            raise HwError(self.errors)
        if board.data["name"] != Path(board_dir).name:
            self.err(board, "name", f"{board.data['name']!r} differs from its directory {Path(board_dir).name!r}")

        soc_path = self.root / "soc" / board.data["soc"]
        if not soc_path.is_file():
            self.err(board, "soc", f"no SoC description {load.rel(soc_path, self.root)}")
            raise HwError(self.errors)
        soc = load.flatten(soc_path, self.root)
        if soc.data.get("kind") != "soc":
            self.err(board, "soc", f"{load.rel(soc_path, self.root)} is a {soc.data.get('kind')!r} description, not a SoC")
            raise HwError(self.errors)
        if soc.leaf.data.get("abstract"):
            self.err(board, "soc", f"{load.rel(soc_path, self.root)} is an abstract family or die file; name a part")
            raise HwError(self.errors)
        self._schema(soc, "soc")
        self._duplicates(soc, SOC_NAMED)
        if self.errors:
            raise HwError(self.errors)

        manifest = self._arch(soc)
        self._soc_rules(soc)
        pins = self._pins(board, soc)
        console = self._console(board, soc)
        test_irqs = self._test_irqs(board, soc)
        clk = clock.solve(soc.data, board.data, lambda p, m: self.err(board, p, m))
        partitions = self._partitions(board, soc)
        linker = self._linker(board, soc)
        kconfig, profile = self._kconfig(board, soc, clk)
        if self.errors:
            raise HwError(self.errors)
        self._curation(board, soc)

        if console is not None and clk is not None:
            gate = console["clock"]
            console["clock_hz"] = clk["bus_hz"][gate["bus"]] if gate else clk["hclk_hz"]
        docs = soc.docs + board.docs + ([manifest] if manifest else [])
        return {
            "board": {k: v for k, v in board.data.items()},
            "soc": {k: v for k, v in soc.data.items() if k not in ("extends", "abstract")},
            "clock": clk,
            "pins": pins,
            "console": console,
            "test_irqs": test_irqs,
            "partitions": partitions,
            "linker": linker,
            "kconfig": kconfig,
            "profile": profile,
            "timer": self._timer(soc, clk),
            "inputs": sorted(({"path": load.stamp(d.path, self.root, self.board_dir), "sha256": d.sha256} for d in docs), key=lambda i: i["path"]),
            "warnings": sorted(self.warnings),
        }

    # -------------------------------------------------------------- schema and names

    def _schema(self, model, kind):
        for path, msg in schema.validate(model.data, schema.load_schema(kind)):
            self.err(model, path, msg)

    def _duplicates(self, model, sections):
        # each file on its own (a part may override a die node by name) and the flattened result
        for doc in model.docs:
            for sec in sections:
                items = _get(doc.data, sec)
                seen = set()
                for i, item in enumerate(items if isinstance(items, list) else []):
                    if isinstance(item, dict) and isinstance(item.get("name"), str):
                        if item["name"] in seen:
                            line = doc.lines.get(f"{sec}[{i}]", (doc.name, 1))[1]
                            self.errors.append(f"{doc.name}:{line}: {sec}.{item['name']}: duplicate name")
                        seen.add(item["name"])

    # -------------------------------------------------------------- SoC

    def _arch(self, soc):
        port = soc.data["arch"]["port"]
        variant = soc.data["arch"]["variant"]
        path = self.root / "arch" / port / "arch.yaml"
        if not path.is_file():
            self.err(soc, "arch.port", f"no port {port!r} (arch/{port}/arch.yaml)")
            return None
        man = load.load(path, self.root)
        variants = man.data.get("variants", [])
        if variant not in variants:
            self.err(soc, "arch.variant", f"port {port} has no variant {variant!r} ({', '.join(variants)})")
        feats = man.data.get("features", {})
        cc = feats.get("cycle_counter", False)
        if "cycle_counter" in soc.data["cpu"] and not (variant in cc if isinstance(cc, list) else cc):
            self.err(soc, "cpu.cycle_counter", f"port {port} has no cycle counter for {variant} ({man.name})")
        if "mpu" in soc.data["cpu"] and feats.get("mpu", "none") == "none":
            self.warnings.append(f"{soc.at('cpu.mpu')}: the {port} port does not use the MPU yet")
        return man

    def _soc_rules(self, soc):
        d = soc.data
        # address spaces are separate (AVR: program, data, eeprom); the default is one space
        regions = sorted(d["memory"]["regions"], key=lambda r: (r.get("space", "main"), r["base"], r["name"]))
        for a, b in zip(regions, regions[1:]):
            if a.get("space", "main") == b.get("space", "main") and a["base"] + a["size"] > b["base"]:
                self.err(soc, f"memory.regions.{b['name']}", f"overlaps {a['name']} ({a['base']:#010x} + {a['size']:#x})")
        for r in regions:
            if r["base"] + r["size"] > 1 << 32:
                self.err(soc, f"memory.regions.{r['name']}", "extends past the 32-bit address space")

        buses = {b["name"] for b in d.get("clocks", {}).get("buses", [])}
        count = d["interrupts"]["count"]
        reserved = set(d["interrupts"].get("reserved", []))
        for n in sorted(reserved):
            if n >= count:
                self.err(soc, "interrupts.reserved", f"line {n} is above the controller's {count} lines")
        ctl = d["interrupts"]["controller"]
        if ctl == "nvic" and "priority_bits" not in d["interrupts"]:
            self.err(soc, "interrupts", "an NVIC needs priority_bits")
        vectors = {}
        for v in d["interrupts"].get("vectors", []):
            at = f"interrupts.vectors.{v['name']}"
            if v["number"] >= count:
                self.err(soc, at, f"vector {v['number']} is above the controller's {count}")
            elif v["number"] in vectors:
                self.err(soc, at, f"vector {v['number']} is already {vectors[v['number']]}")
            else:
                vectors[v["number"]] = v["name"]
        if ctl == "avr" and len(vectors) != count:
            self.err(soc, "interrupts.vectors", f"an AVR lists all {count} vectors; {len(vectors)} are listed")
        irq_owner = {}
        periph = sorted(d["peripherals"], key=lambda p: (p.get("space", "main"), p["base"], p["name"]))
        for a, b in zip(periph, periph[1:]):
            if a.get("space", "main") == b.get("space", "main") and a["base"] + a["size"] > b["base"]:
                self.err(soc, f"peripherals.{b['name']}.base", f"overlaps {a['name']} ({a['base']:#010x} + {a['size']:#x})")
        for p in sorted(d["peripherals"], key=lambda p: p["name"]):
            where = f"peripherals.{p['name']}"
            clk = p.get("clock")
            if clk is not None and clk["bus"] not in buses:
                self.err(soc, f"{where}.clock.bus", f"unresolved reference: no bus {clk['bus']!r}")
            for irq in p.get("irqs", []):
                n = irq["number"]
                at = f"{where}.irqs.{irq['name']}"
                if n >= count:
                    self.err(soc, at, f"line {n} is above the controller's {count} lines")
                elif n in reserved:
                    self.err(soc, at, f"line {n} is listed in interrupts.reserved")
                elif n in irq_owner:
                    self.err(soc, at, f"line {n} is already {irq_owner[n]}'s")
                elif vectors and n not in vectors:
                    self.err(soc, at, f"line {n} is not in interrupts.vectors")
                else:
                    irq_owner[n] = f"{p['name']}.{irq['name']}"

        ports = d.get("gpio", {}).get("ports", {})
        gpio_ports = {p["port"]: p["name"] for p in d["peripherals"] if "port" in p}
        for port in sorted(ports):
            if port not in gpio_ports:
                self.err(soc, f"gpio.ports.{port}", f"no peripheral is GPIO port {port}")
        bank = "lines" in d.get("gpio", {})
        if bank and d["gpio"]["controller"] not in {p["name"] for p in d["peripherals"]}:
            self.err(soc, "gpio.controller", f"unresolved reference: no peripheral {d['gpio']['controller']!r}")
        for pin, afs in sorted(d.get("alternate_functions", {}).items()):
            parsed = parse_pin(pin)
            if parsed is None or parsed["af"] is not None or parsed["bank"] != bank:
                self.err(soc, f"alternate_functions.{pin}", "not a pin name (" + ("GPIO<n>" if bank else "P<port><n>") + ")")
                continue
            if bank and parsed["number"] >= d["gpio"]["lines"]:
                self.err(soc, f"alternate_functions.{pin}", f"beyond the bank's {d['gpio']['lines']} lines")
            for af in sorted(afs):
                ok = r"f([12][0-9]|3[01]|[0-9])" if bank else r"af(1[0-5]|[0-9])|fixed"
                if not re.fullmatch(ok, af):
                    self.err(soc, f"alternate_functions.{pin}.{af}",
                             "not a function select (f0 to f31)" if bank else "not an alternate function (af0 to af15, or fixed)")

        fl = d.get("flash")
        if fl is not None:
            region = next((r for r in d["memory"]["regions"] if r["name"] == fl["region"]), None)
            if region is None:
                self.err(soc, "flash.region", f"unresolved reference: no memory region {fl['region']!r}")
            elif "sectors" in fl["geometry"] and sum(fl["geometry"]["sectors"]) != region["size"]:
                self.err(soc, "flash.geometry.sectors",
                         f"the sectors add up to {sum(fl['geometry']['sectors'])} bytes, region {region['name']} is {region['size']}")

        revisions = set(d["cpu"].get("revisions", []))
        for e in d.get("errata", []):
            bad = sorted(set(e["revisions"]) - revisions)
            if bad:
                self.err(soc, f"errata.{e['id']}", f"revision(s) {', '.join(bad)} not in cpu.revisions")

    # -------------------------------------------------------------- board

    def _bonded(self, soc, pin):
        gpio = soc.data.get("gpio", {})
        if pin["bank"]:
            return "lines" in gpio and pin["number"] < gpio["lines"]
        mask = gpio.get("ports", {}).get(pin["port"])
        return mask is not None and (mask >> pin["number"]) & 1 == 1

    def _port_peripheral(self, soc, pin):
        if pin["bank"]:
            name = soc.data.get("gpio", {}).get("controller")
            return next((p for p in soc.data["peripherals"] if p["name"] == name), None)
        return next((p for p in soc.data["peripherals"] if p.get("port") == pin["port"]), None)

    def _pins(self, board, soc):
        d = board.data
        periph = {p["name"]: p for p in soc.data["peripherals"]}
        afs = soc.data.get("alternate_functions", {})
        owner = {}
        out = []

        def claim(text, at, mode, signal=None):
            pin = parse_pin(text)
            bank = "lines" in soc.data.get("gpio", {})
            if pin["bank"] != bank:
                self.err(board, at, f"{pin['name']}: the {soc.data['name']} names its pins "
                         + ("GPIO<n>" if bank else "P<port><n>"))
                return
            if not self._bonded(soc, pin):
                self.err(board, at, f"{pin['name']} is not bonded on the {soc.data.get('package', soc.data['name'])} package")
                return
            if pin["name"] in owner:
                self.err(board, at, f"{pin['name']} is already assigned to {owner[pin['name']]}")
                return
            owner[pin["name"]] = at
            gpio = self._port_peripheral(soc, pin)
            if gpio is None:
                return  # reported by the SoC rules: gpio.ports names a port no peripheral drives
            entry = {"pin": pin["name"], "port": pin["port"], "number": pin["number"], "mode": mode, "owner": at,
                     "gpio": gpio["name"]}
            if mode == "af":
                offered = afs.get(pin["name"], {})
                if pin["af"] is None and offered.get("fixed") == signal:
                    entry.update({"mode": "fixed", "signal": signal})  # taken over when the peripheral is on
                    out.append(entry)
                    return
                if pin["af"] is None:
                    self.err(board, at, f"{pin['name']} needs its alternate function: {pin['name']}.{af_key(pin, '<n>')}")
                    return
                got = offered.get(af_key(pin, pin["af"]))
                if got != signal:
                    have = ", ".join(f"{k} {v}" for k, v in sorted(offered.items())) or "no listed function"
                    self.err(board, at, f"{pin['name']} {af_key(pin, pin['af'])} is not {signal} ({pin['name']} offers {have})")
                    return
                entry.update({"af": pin["af"], "signal": signal})
            elif pin["af"] is not None:
                self.err(board, at, f"a GPIO line takes no alternate function: {pin['name']}")
                return
            out.append(entry)

        for dev in sorted(d.get("pins", {})):
            if dev not in periph:
                self.err(board, f"pins.{dev}", f"unresolved reference: the SoC has no peripheral {dev!r}")
                continue
            for sig in sorted(d["pins"][dev]):
                claim(d["pins"][dev][sig], f"pins.{dev}.{sig}", "af", f"{dev}_{sig}")
        for kind, mode in (("leds", "output"), ("buttons", "input")):
            for line in d.get(kind, []):
                claim(line["gpio"], f"{kind}.{line['name']}", mode)
        return sorted(out, key=lambda e: (e["port"], e["number"]))

    def _console(self, board, soc):
        dev = board.data["console"]["device"]
        p = next((x for x in soc.data["peripherals"] if x["name"] == dev), None)
        if p is None:
            self.err(board, "console.device", f"unresolved reference: the SoC has no peripheral {dev!r}")
            return None
        if "gpio" in soc.data and "tx" not in board.data.get("pins", {}).get(dev, {}):
            # a SoC with pin muxing needs the pin; a fixed device (an emulator's UART) has none
            self.err(board, "console.device", f"{dev} has no tx pin in pins.{dev}")
            return None
        return {"device": dev, "baud": board.data["console"]["baud"], "base": p["base"], "clock": p.get("clock"),
                "irqs": p.get("irqs", [])}

    def _test_irqs(self, board, soc):
        irqs = board.data.get("test_irqs")
        if irqs is None:
            return None
        count = soc.data["interrupts"]["count"]
        reserved = set(soc.data["interrupts"].get("reserved", []))
        if len(irqs) == 2 and irqs[0] == irqs[1]:
            self.err(board, "test_irqs", f"the two lines must differ (both {irqs[0]})")
        for n in irqs:
            if n >= count:
                self.err(board, "test_irqs", f"line {n} is above the controller's {count} lines")
            elif n not in reserved:
                self.err(board, "test_irqs", f"line {n} has a peripheral behind it; use one of interrupts.reserved")
        return {"low": irqs[0], "high": irqs[1] if len(irqs) == 2 else None}

    def _partitions(self, board, soc):
        parts = board.data.get("flash_partitions", [])
        fl = soc.data.get("flash")
        region = None
        if fl is not None:
            region = next((r for r in soc.data["memory"]["regions"] if r["name"] == fl["region"]), None)
        if region is None:
            region = next((r for r in soc.data["memory"]["regions"] if "boot" in r["attrs"]), None)
        if parts and region is None:
            self.err(board, "flash_partitions", "the SoC describes no flash region")
            return []
        bounds = []
        if fl is not None:
            addr = region["base"]
            page = fl["geometry"].get("page_size")
            for size in fl["geometry"].get("sectors") or [page] * (region["size"] // page):
                bounds.append((addr, addr + size))
                addr += size
        starts = {b[0] for b in bounds}
        ends = {b[1] for b in bounds}
        out = []
        for p in sorted(parts, key=lambda p: (p["base"], p["name"])):
            at = f"flash_partitions.{p['name']}"
            end = p["base"] + p["size"]
            if p["base"] < region["base"] or end > region["base"] + region["size"]:
                self.err(board, at, f"{p['base']:#010x} + {p['size']:#x} is outside flash "
                         f"({region['base']:#010x} + {region['size']:#x})")
                continue
            if bounds and (p["base"] not in starts or end not in ends):
                self.err(board, at, "does not start and end on erase-sector boundaries; "
                         "independently written partitions must not share a sector")
                continue
            units = [i for i, b in enumerate(bounds) if b[0] >= p["base"] and b[1] <= end]
            entry = {"name": p["name"], "base": p["base"], "size": p["size"], "reserved": bool(p.get("reserved", False))}
            if fl is not None and "sectors" in fl["geometry"]:
                entry["sectors"] = units
            elif fl is not None:
                entry["pages"] = [units[0], units[-1]] if units else []
            out.append(entry)
        for a, b in zip(out, out[1:]):
            if a["base"] + a["size"] > b["base"]:
                self.err(board, f"flash_partitions.{b['name']}", f"overlaps {a['name']}")
        return out

    def _linker(self, board, soc):
        regions = soc.data["memory"]["regions"]
        flash = next((r for r in regions if "boot" in r["attrs"]), None)
        if flash is None:
            self.err(soc, "memory.regions", "no region has the boot attribute")
            return None
        app = next((p for p in board.data.get("flash_partitions", []) if p["name"] == "app"), None)
        if board.data.get("flash_partitions") and app is None:
            self.err(board, "flash_partitions", "no partition named app: the image's place is unknown")
            return None
        code = {"origin": app["base"], "length": app["size"]} if app else {"origin": flash["base"], "length": flash["size"]}
        ram = sorted((r for r in regions if "w" in r["attrs"] and not {"retained", "tcm"} & set(r["attrs"])),
                     key=lambda r: r["base"])
        if not ram:
            self.err(soc, "memory.regions", "no writable, non-retained region for RAM")
            return None
        # contiguous regions from the lowest one become one linker RAM (SRAM1 + SRAM2 on the F446)
        merged = [ram[0]]
        end = ram[0]["base"] + ram[0]["size"]
        for r in ram[1:]:
            if r["base"] != end:
                break
            merged.append(r)
            end += r["size"]
        return {"flash": code,
                "ram": {"origin": merged[0]["base"], "length": end - merged[0]["base"], "regions": [r["name"] for r in merged]}}

    def _kconfig(self, board, soc, clk):
        """The board's configuration fragment: what the port needs from the hardware, then the
        board's own defaults (CONFIG_EMB_PROFILE goes to board.cmake instead)."""
        derived = {}
        port = soc.data["arch"]["port"]
        if port == "cortex_m" and clk is not None:
            derived = {
                "CONFIG_EMB_CM_CPU_HZ": clk["hclk_hz"],
                "CONFIG_EMB_CM_IRQ_COUNT": soc.data["interrupts"]["count"],
                "CONFIG_EMB_CM_IRQ_PRIORITY_BITS": soc.data["interrupts"].get("priority_bits"),
            }
        elif port == "riscv" and clk is not None:
            clint = next((p for p in soc.data["peripherals"] if p["compatible"] == "riscv,clint0"), None)
            timebase = next((s for s in soc.data.get("clocks", {}).get("sources", []) if s["name"] == "timebase"), None)
            derived = {"CONFIG_EMB_RV_IRQ_COUNT": soc.data["interrupts"]["count"]}
            if clint is not None:
                derived["CONFIG_EMB_RV_CLINT_BASE"] = f"0x{clint['base']:08X}"  # a hex symbol, spelled as one
            if timebase is not None:
                derived["CONFIG_EMB_RV_MTIME_HZ"] = timebase["hz"]
        elif port == "avr" and clk is not None:
            derived = {"CONFIG_EMB_AVR_MCU": f'"{soc.data["cpu"].get("mcu", soc.data["arch"]["variant"])}"',
                       "CONFIG_EMB_AVR_F_CPU": clk["hclk_hz"]}
        out = dict(derived)
        profile = "base"
        for key, value in sorted(board.data.get("defaults", {}).items()):
            at = f"defaults.{key}"
            if not re.fullmatch(r"CONFIG_EMB_[A-Z0-9_]+", key):
                self.err(board, at, "not a CONFIG_EMB_ symbol")
            elif key in derived:
                self.err(board, at, f"comes from the hardware description ({derived[key]}); remove it")
            elif key == "CONFIG_EMB_PROFILE":
                if value not in ("tiny", "base"):
                    self.err(board, at, f"{value!r} is not a reference profile (tiny or base)")
                profile = value
            else:
                out[key] = value
        return dict(sorted(out.items())), profile

    def _timer(self, soc, clk):
        """The kernel tick's source, as the port drives it (HW-015): SysTick on Cortex-M, Timer1 on
        the AVR port (SPEC-012)."""
        port = soc.data["arch"]["port"]
        if port == "cortex_m" and soc.data["interrupts"].get("systick"):
            return {"source": "systick", "hz": clk["hclk_hz"]}
        if port == "avr" and any(p["name"] == "timer1" for p in soc.data["peripherals"]):
            return {"source": "timer1", "hz": clk["hclk_hz"]}
        return None

    def _curation(self, board, soc):
        for doc in (soc.leaf, board.leaf):
            if not doc.data.get("curated", False):
                self.warnings.append(f"{doc.name}:1: (root): uncurated description (curated: false or absent)")
        for p in soc.data["peripherals"]:
            if p.get("curated") is False:
                self.warnings.append(f"{soc.at('peripherals.' + p['name'])}: uncurated node")


def resolve(board_dir, root):
    return Resolver(root).resolve(board_dir)
