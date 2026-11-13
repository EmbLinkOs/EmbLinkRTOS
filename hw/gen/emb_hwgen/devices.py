# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""The board's devices (SPEC-016 §4 and §8; DRV-001, DRV-009): each bound to its driver
through drivers/bindings.yaml, each configuration field taken from the description, and all
of them in initialization order.

A board device is a SoC peripheral (`- {name: uart0}`, or `peripheral:` when the device's
name differs) or a part outside the SoC, named by its `compatible`. Its other keys are its
properties, which a binding's fields may read.
"""
import re

from . import load, schema

LEVELS = ("early", "kernel")
# EMB_DRV_CLASS_* in <emb/device.h>, numbered from 1 in this order (0 is no class)
CLASSES = ("uart", "spi", "i2c", "dma", "watchdog", "flash", "rtc", "entropy", "clock", "gpio", "counter", "pwm", "adc")
COMPATIBLE = re.compile(r"^[a-z0-9-]+,[a-z0-9-]+$")
# the keys of a board device that are not its properties
DEVICE_KEYS = ("name", "peripheral", "compatible", "class", "bus", "deps", "level", "doc")


class Bindings:
    """drivers/bindings.yaml in the tree, and the board root's own (A84), merged: a
    compatible is bound once for each class. A peripheral that more than one class can drive
    (a timer: a counter or PWM, A129) has a binding per class, keyed `compatible:class`, and a
    board device names the one it wants with `class:`."""

    def __init__(self, files, err_at):
        self.table = {}  # compatible -> {class: (binding, model, origin)}
        self.models = []
        for path, origin in files:
            m = load.flatten(path, path.parent.parent)
            ok = True
            for p, msg in schema.validate(m.data, schema.load_schema("bindings")):
                err_at(m, p, msg)
                ok = False
            if not ok:
                continue
            self.models.append(m)
            for key, b in m.data["bindings"].items():
                where = f"bindings.{key}"
                compat, _, cls = key.partition(":")
                if not COMPATIBLE.match(compat):
                    err_at(m, where, f"{compat!r} is not a compatible: vendor,name")
                elif cls and cls != b["class"]:
                    err_at(m, where, f"the key names the class {cls}, the binding {b['class']}")
                elif b["class"] in self.table.get(compat, {}):
                    err_at(m, where, f"{compat} is already bound for the {b['class']} class in "
                                     f"{self.table[compat][b['class']][1].leaf.name}")
                else:
                    self.table.setdefault(compat, {})[b["class"]] = (b, m, origin)

    def lookup(self, compat, cls=None):
        """(binding, model, origin) for a compatible, or the reason there is none: a device whose
        compatible several classes drive names its class."""
        by_class = self.table.get(compat)
        if not by_class:
            return None, f"no driver binds {compat} (drivers/bindings.yaml)"
        if cls is not None:
            if cls not in by_class:
                return None, f"no {cls} driver binds {compat}: it has {', '.join(sorted(by_class))}"
            return by_class[cls], None
        if len(by_class) > 1:
            return None, f"{compat} has drivers for {' and '.join(sorted(by_class))}: say which with class:"
        return next(iter(by_class.values())), None


def _peripheral_view(p, soc, clk):
    """What a binding's `peripheral.*` fields read: the SoC node, its interrupts by name, its
    clock gate with the bus's enable register, and the frequency it is clocked at: its bus's, a
    timer's on its bus (timer_clock_hz), or the PLL's it names in clocked_by."""
    view = {k: v for k, v in p.items() if k not in ("irqs", "clock", "doc", "curated")}
    view["irqs"] = {i["name"]: i["number"] for i in p.get("irqs", [])}
    index = re.search(r"(\d+)$", p["name"])
    if index:  # its number among its kind: timer1's 1, a PWM slice's (A129)
        view["index"] = int(index.group(1))
    gate = p.get("clock")
    if gate is not None:
        bus = next((b for b in soc.get("clocks", {}).get("buses", []) if b["name"] == gate["bus"]), {})
        view["clock"] = {"bus": gate["bus"], "bit": gate["bit"]}
        if "enable_offset" in bus:
            view["clock"]["enable_offset"] = bus["enable_offset"]
    if clk is not None:
        view["clock_hz"] = clk["bus_hz"].get(gate["bus"], clk["hclk_hz"]) if gate else clk["hclk_hz"]
        timer = clk.get("bus_timer_hz", {})
        view["timer_clock_hz"] = timer.get(gate["bus"], view["clock_hz"]) if gate else view["clock_hz"]
        fixed = clk.get("fixed_plls", {}).get(p.get("clocked_by"))
        if fixed is not None:  # a kernel clock of its own, a PLL's fixed output (A129)
            view["clock_hz"] = view["timer_clock_hz"] = fixed["hz"]
    return view


def _lookup(ctx, expr):
    cur = ctx
    for part in expr.split("."):
        if not isinstance(cur, dict) or part not in cur:
            return None
        cur = cur[part]
    return cur


# the description keys whose values are addresses or register offsets, written in hexadecimal
HEX_KEYS = ("base", "enable_offset", "address")


def c_value(v, expr=""):
    """A field's value as C: an integer (hexadecimal for an address or offset), a boolean, a
    string literal, or a device reference."""
    if isinstance(v, dict) and "device" in v:
        return f"&emb_dev_{v['device']}"
    if isinstance(v, dict) and "dma" in v:  # a DMA channel: an emb_drv_dma_chan_t initializer
        if v["dma"] is None:
            return "{NULL, 0u, 0u, {0u, 0u}}"
        return f"{{&emb_dev_{v['dma']}, {v['stream']}u, {v['request']}u, {{0u, 0u}}}}"
    if isinstance(v, dict) and "flash_geometry" in v:  # an emb_drv_flash_geometry_t initializer
        g = v["flash_geometry"]
        sectors = f"{g['name']}_sectors" if g["sectors"] else "NULL"
        return (f"{{0x{g['base']:08X}u, 0x{g['size']:08X}u, {sectors}, {g['sector_size']}u, {g['count']}u, "
                f"{g['write_unit']}u, 0x{g['erased']:02X}u, {{0u, 0u, 0u}}}}")
    if isinstance(v, dict) and "line" in v:  # a GPIO line: an emb_drv_gpio_line_t initializer
        return f"{{&emb_dev_{v['gpio']}, {v['line']}u, 0u, {{0u, 0u}}}}"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return f"0x{v:08X}u" if expr.rsplit(".", 1)[-1] in HEX_KEYS else f"{v}u"
    if isinstance(v, str):
        return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'
    raise TypeError(v)


def _flash_geometry(name, entry, soc, err):
    """What a flash device's `flash.geometry` field reads: the SoC's flash region and geometry
    (SPEC-014 §3), the size the board's chip has where it is smaller than the window (`size:`
    on the device), its sectors listed or all of the page size."""
    fl = soc.get("flash")
    if fl is None:
        err(f"devices.{name}", "a flash device needs the SoC's flash: description (region and geometry)")
        return None
    region = next((r for r in soc["memory"]["regions"] if r["name"] == fl["region"]), None)
    if region is None:
        return None  # the SoC's own check reports it
    g = fl["geometry"]
    size = entry.get("size", region["size"])
    if not isinstance(size, int) or isinstance(size, bool) or not 0 < size <= region["size"]:
        err(f"devices.{name}.size", f"the flash's size must be within its region {region['name']} ({region['size']} bytes)")
        return None
    if "sectors" in g:
        if size != region["size"]:
            err(f"devices.{name}.size", "a flash whose sectors are listed is its whole region")
            return None
        sectors, sector_size, count = list(g["sectors"]), 0, len(g["sectors"])
    else:
        if size % g["page_size"]:
            err(f"devices.{name}.size", f"{size} bytes is not a whole number of {g['page_size']}-byte sectors")
            return None
        sectors, sector_size, count = None, g["page_size"], size // g["page_size"]
    if count > 65535:
        err(f"devices.{name}", f"{count} sectors: the geometry counts at most 65535")
        return None
    return {"flash_geometry": {"name": name, "base": region["base"], "size": size, "sectors": sectors,
                               "sector_size": sector_size, "count": count,
                               "write_unit": g.get("write_granularity", 1), "erased": 0xFF}}


def _pin_lines(pins, periph, gpio_devs):
    """A peripheral's pins, by signal, as the GPIO lines that carry them: what a binding's
    pins.<signal> fields read (an I2C bus's SCL and SDA, for its recovery). A pin on a port the
    board lists no GPIO device for has no line."""
    out = {}
    for p in pins or []:
        owner = p["owner"].split(".")
        if len(owner) == 3 and owner[0] == "pins" and owner[1] == periph and p["gpio"] in gpio_devs:
            out[owner[2]] = {"gpio": gpio_devs[p["gpio"]], "line": p["number"], "pin": p["pin"]}
    return out


def _dma_options(text):
    """A request's streams, as (controller, stream or None, request number): s<stream>c<channel
    select> on a fixed controller, dreq<number> on a dreq one."""
    out = []
    for opt in (text if isinstance(text, list) else [text]):
        ctrl, _, sel = opt.partition(".")
        m = re.fullmatch(r"s(\d+)c(\d+)", sel)
        if m:
            out.append((ctrl, int(m.group(1)), int(m.group(2))))
        else:
            out.append((ctrl, None, int(sel[len("dreq"):])))
    return out


def _dma_assign(devs, order, err):
    """Each device's DMA request lines that its board entry asks for (dma: [tx, rx]), given a
    channel of a board DMA device no other device has: on a fixed controller the first free
    stream its table lists, on a dreq controller the lowest free channel (A119)."""
    ctrl_dev = {d["peripheral"]["name"]: n for n, d in devs.items()
                if d["peripheral"] is not None and d["binding"]["class"] == "dma"}
    taken = {}  # (controller device, channel) -> the device and request that has it
    for n in order:
        d = devs[n]
        d["dma"] = {}
        asked = d["entry"].get("dma", [])
        offered = (d["peripheral"] or {}).get("dma_requests", {})
        for req in asked:
            at = f"devices.{n}.dma"
            if req not in offered:
                err(at, f"{n} has no DMA request {req!r}" + (f" (it has {', '.join(sorted(offered))})" if offered else ""))
                continue
            got = None
            for ctrl, stream, number in _dma_options(offered[req]):
                if ctrl not in ctrl_dev:
                    continue
                cdev = ctrl_dev[ctrl]
                spec = devs[cdev]["peripheral"]["dma"]
                if stream is None:  # dreq: any free channel
                    free = [c for c in range(spec["channels"]) if (cdev, c) not in taken]
                    if free:
                        got = (cdev, free[0], number)
                        break
                elif (cdev, stream) not in taken:
                    got = (cdev, stream, number)
                    break
            if got is None:
                ctrls = sorted({c for c, _, _ in _dma_options(offered[req])})
                if not any(c in ctrl_dev for c in ctrls):
                    err(at, f"{n}'s {req} is on {', '.join(ctrls)}, which the board does not list in devices:")
                else:
                    err(at, f"{n}'s {req}: every channel it can use is taken ("
                            + ", ".join(f"{taken[k][0]}.{taken[k][1]}" for k in sorted(taken) if k[0] in ctrls or devs[k[0]]['peripheral']['name'] in ctrls) + ")")
                continue
            taken[(got[0], got[1])] = (n, req)
            d["dma"][req] = {"dma": got[0], "stream": got[1], "request": got[2]}


def resolve(board, soc, clk, bindings, err, pins=None):
    """The board's devices in initialization order: EARLY first, each level a topological
    sort of its dependencies in which ties keep the description's order. `err(path, msg)`
    reports against the board; `pins` are the board's resolved pins, for pins.<signal>."""
    entries = board.data.get("devices", [])
    if not entries:
        return []
    periph = {p["name"]: p for p in soc.data["peripherals"]}
    names = [e["name"] for e in entries]
    devs = {}
    used = {}
    for i, e in enumerate(entries):
        at = f"devices.{e['name']}"
        if "compatible" in e and "peripheral" in e:
            err(at, "a device is a SoC peripheral or a part named by its compatible, not both")
            continue
        p = None
        if "compatible" in e:
            compat = e["compatible"]
        else:
            pname = e.get("peripheral", e["name"])
            p = periph.get(pname)
            if p is None:
                err(at, f"unresolved reference: the SoC has no peripheral {pname!r}")
                continue
            if pname in used:
                err(at, f"{pname} is already the device {used[pname]}")
                continue
            used[pname] = e["name"]
            compat = p["compatible"]
        bound, why = bindings.lookup(compat, e.get("class"))
        if bound is None:
            err(at, why)
            continue
        b, _, origin = bound
        takes = {}
        for key, want in sorted(b.get("takes", {}).items()):  # its other peripherals (A129)
            tname = e.get(key)
            if not isinstance(tname, str):
                err(at, f"the {compat} binding takes a {want} peripheral as {key}: name one ({key}: <peripheral>)")
                continue
            tp = periph.get(tname)
            if tp is None:
                err(f"{at}.{key}", f"unresolved reference: the SoC has no peripheral {tname!r}")
            elif tp["compatible"] != want:
                err(f"{at}.{key}", f"{tname} is a {tp['compatible']}, not the {want} the {compat} binding takes")
            elif tname in used:
                err(f"{at}.{key}", f"{tname} is already the device {used[tname]}")
            else:
                used[tname] = f"{e['name']} (its {key})"
                takes[key] = tp
        devs[e["name"]] = {"index": i, "entry": e, "peripheral": p, "compatible": compat, "binding": b,
                           "origin": origin, "level": e.get("level", b["level"]), "takes": takes}

    _dma_assign(devs, sorted(devs, key=lambda n: devs[n]["index"]), err)
    clock_devs = [n for n in names if n in devs and devs[n]["binding"]["class"] == "clock"]
    for n, d in devs.items():
        e, at = d["entry"], f"devices.{n}"
        deps = list(e.get("deps", []))
        if "bus" in e:
            deps.append(e["bus"])
        deps += sorted({c["dma"] for c in d["dma"].values()})  # its DMA controllers first
        gated = [x for x in [d["peripheral"]] + list(d["takes"].values()) if x is not None and x.get("clock")]
        if gated and d["binding"]["class"] != "clock":
            deps += clock_devs  # the clock tree first: it gates this one, or what it takes (SPEC-016 §4)
        d["deps"] = []
        for dep in deps:
            if dep not in names:
                err(at, f"unresolved reference: no device {dep!r}")
            elif dep == n:
                err(at, "a device cannot depend on itself")
            elif dep in devs and dep not in d["deps"]:
                if d["level"] == "early" and devs[dep]["level"] == "kernel":
                    err(at, f"an EARLY device cannot depend on the KERNEL device {dep}")
                    continue
                d["deps"].append(dep)

    order = []
    done = set()  # sorted, or left on a cycle already reported
    for level in LEVELS:
        pending = sorted((n for n, d in devs.items() if d["level"] == level), key=lambda n: devs[n]["index"])
        while pending:
            ready = [n for n in pending if all(x in done for x in devs[n]["deps"])]
            if not ready:
                cycle = _cycle(pending, devs)
                err(f"devices.{cycle[0]}", "dependency cycle: " + " -> ".join(cycle + [cycle[0]]))
                done.update(pending)
                break
            n = ready[0]  # the first in the description's order
            order.append(n)
            done.add(n)
            pending.remove(n)

    # each GPIO controller's device on this board, by its peripheral
    gpio_devs = {d["peripheral"]["name"]: n for n, d in devs.items()
                 if d["peripheral"] is not None and d["binding"]["class"] == "gpio"}
    out = []
    for n in order:
        d = devs[n]
        b, e = d["binding"], d["entry"]
        ctx = {"device": {k: v for k, v in e.items() if k not in ("deps", "doc")},
               "board": board.data, "soc": soc.data}
        if d["peripheral"] is not None:
            ctx["pins"] = _pin_lines(pins, d["peripheral"]["name"], gpio_devs)
        ctx["dma"] = d["dma"]
        if b["class"] == "flash":
            ctx["flash"] = {"geometry": _flash_geometry(n, e, soc.data, err)}
        if d["peripheral"] is not None:
            ctx["peripheral"] = _peripheral_view(d["peripheral"], soc.data, clk)
        ctx["takes"] = {k: _peripheral_view(tp, soc.data, clk) for k, tp in d["takes"].items()}
        if "bus" in e:
            ctx["device"]["bus"] = {"device": e["bus"]}
        fields = []
        for field, spec in b.get("fields", {}).items():
            expr, default = (spec, None) if isinstance(spec, str) else (spec["from"], spec["default"])
            v = _lookup(ctx, expr)
            defaulted = v is None and default is not None
            if defaulted:
                v = default  # the device does not give it (A129)
            if v is None and expr.startswith("dma."):
                v = {"dma": None}  # a request the board gives no channel: none
            if v is None and expr.startswith("pins."):
                err(f"devices.{n}", f"the {d['compatible']} binding's field {field} reads {expr}: the board gives "
                                    f"{n} no such pin, or its port has no GPIO device in devices:")
                continue
            if v is None or isinstance(v, (list,)) or (isinstance(v, dict) and not ({"device", "line", "dma", "flash_geometry"} & set(v))):
                err(f"devices.{n}", f"the {d['compatible']} binding's field {field} reads {expr}, which this device does not have")
                continue
            fields.append({"field": field, "expr": expr, "value": v, "c": c_value(v, expr)}
                          | ({"default": True} if defaulted else {}))
        buffers = []
        for bname, spec in b.get("buffers", {}).items():
            size = _lookup(ctx, spec["size"])
            if size is None:
                size = spec.get("default")
            if not isinstance(size, int) or isinstance(size, bool) or not 1 <= size <= 65535:
                # at the option itself where the device gives it: a system's setting names its own line
                at = f"devices.{n}.{spec['size'][len('device.'):]}" if spec["size"].startswith("device.") else f"devices.{n}"
                err(at, f"the {d['compatible']} binding's buffer {bname} needs a size of 1 to 65535 "
                                    f"bytes from {spec['size']}" + ("" if "default" in spec else ", which has no default"))
                continue
            buffers.append({"name": bname, "size": size})
            fields.append({"field": bname, "expr": f"its {bname} buffer", "value": f"{n}_{bname}", "c": f"{n}_{bname}"})
            fields.append({"field": f"{bname}_size", "expr": spec["size"], "value": size, "c": f"{size}u"})
        out.append({
            "name": n,
            "compatible": d["compatible"],
            "peripheral": d["peripheral"]["name"] if d["peripheral"] is not None else None,
            "class": b["class"],
            "dma": d.get("dma", {}),
            "class_id": CLASSES.index(b["class"]) + 1,
            "level": d["level"],
            "deps": d["deps"],
            "api": b["api"],
            "config": b["config"],
            "data": b.get("data"),
            "header": b["header"],
            "include": b.get("include"),
            "sources": b["sources"],
            "origin": d["origin"],
            "fields": fields,
            "buffers": buffers,
            "props": {k: v for k, v in e.items() if k not in DEVICE_KEYS},
            # what a system description may set (A125): the options the binding reads from the
            # device, and the limits its driver states for targets and timeouts
            "options": sorted({x[len("device."):].split(".")[0] for x in
                               [s if isinstance(s, str) else s["from"] for s in b.get("fields", {}).values()]
                               + [v["size"] for v in b.get("buffers", {}).values()]
                               if x.startswith("device.") and x != "device.bus"}),
            "limits": b.get("limits", {}),
        })
    return out


def _cycle(pending, devs):
    """One dependency cycle among the devices left unsorted, in dependency order."""
    left = set(pending)
    start = pending[0]
    path, seen = [], {}
    n = start
    while n not in seen:
        seen[n] = len(path)
        path.append(n)
        n = next(x for x in devs[n]["deps"] if x in left)
    return path[seen[n]:]
