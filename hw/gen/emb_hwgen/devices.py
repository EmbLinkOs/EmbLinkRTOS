# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
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
CLASSES = ("uart", "spi", "i2c", "dma", "watchdog", "flash", "rtc", "entropy", "clock", "gpio")
COMPATIBLE = re.compile(r"^[a-z0-9-]+,[a-z0-9-]+$")
# the keys of a board device that are not its properties
DEVICE_KEYS = ("name", "peripheral", "compatible", "bus", "deps", "level", "doc")


class Bindings:
    """drivers/bindings.yaml in the tree, and the board root's own (A84), merged: a
    compatible is bound once."""

    def __init__(self, files, err_at):
        self.table = {}  # compatible -> (binding, model, origin)
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
            for compat, b in m.data["bindings"].items():
                where = f"bindings.{compat}"
                if not COMPATIBLE.match(compat):
                    err_at(m, where, f"{compat!r} is not a compatible: vendor,name")
                elif compat in self.table:
                    err_at(m, where, f"{compat} is already bound in {self.table[compat][1].leaf.name}")
                else:
                    self.table[compat] = (b, m, origin)


def _peripheral_view(p, soc, clk):
    """What a binding's `peripheral.*` fields read: the SoC node, its interrupts by name, its
    clock gate with the bus's enable register, and the frequency it is clocked at."""
    view = {k: v for k, v in p.items() if k not in ("irqs", "clock", "doc", "curated")}
    view["irqs"] = {i["name"]: i["number"] for i in p.get("irqs", [])}
    gate = p.get("clock")
    if gate is not None:
        bus = next((b for b in soc.get("clocks", {}).get("buses", []) if b["name"] == gate["bus"]), {})
        view["clock"] = {"bus": gate["bus"], "bit": gate["bit"]}
        if "enable_offset" in bus:
            view["clock"]["enable_offset"] = bus["enable_offset"]
    if clk is not None:
        view["clock_hz"] = clk["bus_hz"].get(gate["bus"], clk["hclk_hz"]) if gate else clk["hclk_hz"]
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
    if isinstance(v, dict) and "line" in v:  # a GPIO line: an emb_drv_gpio_line_t initializer
        return f"{{&emb_dev_{v['gpio']}, {v['line']}u, 0u, {{0u, 0u}}}}"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return f"0x{v:08X}u" if expr.rsplit(".", 1)[-1] in HEX_KEYS else f"{v}u"
    if isinstance(v, str):
        return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'
    raise TypeError(v)


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
        bound = bindings.table.get(compat)
        if bound is None:
            err(at, f"no driver binds {compat} (drivers/bindings.yaml)")
            continue
        b, _, origin = bound
        devs[e["name"]] = {"index": i, "entry": e, "peripheral": p, "compatible": compat, "binding": b,
                           "origin": origin, "level": e.get("level", b["level"])}

    _dma_assign(devs, sorted(devs, key=lambda n: devs[n]["index"]), err)
    clock_devs = [n for n in names if n in devs and devs[n]["binding"]["class"] == "clock"]
    for n, d in devs.items():
        e, at = d["entry"], f"devices.{n}"
        deps = list(e.get("deps", []))
        if "bus" in e:
            deps.append(e["bus"])
        deps += sorted({c["dma"] for c in d["dma"].values()})  # its DMA controllers first
        if d["peripheral"] is not None and d["peripheral"].get("clock") and d["binding"]["class"] != "clock":
            deps += clock_devs  # the clock tree first: it gates this one (SPEC-016 §4)
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
        if d["peripheral"] is not None:
            ctx["peripheral"] = _peripheral_view(d["peripheral"], soc.data, clk)
        if "bus" in e:
            ctx["device"]["bus"] = {"device": e["bus"]}
        fields = []
        for field, expr in b.get("fields", {}).items():
            v = _lookup(ctx, expr)
            if v is None and expr.startswith("dma."):
                v = {"dma": None}  # a request the board gives no channel: none
            if v is None and expr.startswith("pins."):
                err(f"devices.{n}", f"the {d['compatible']} binding's field {field} reads {expr}: the board gives "
                                    f"{n} no such pin, or its port has no GPIO device in devices:")
                continue
            if v is None or isinstance(v, (list,)) or (isinstance(v, dict) and not ({"device", "line", "dma"} & set(v))):
                err(f"devices.{n}", f"the {d['compatible']} binding's field {field} reads {expr}, which this device does not have")
                continue
            fields.append({"field": field, "expr": expr, "value": v, "c": c_value(v, expr)})
        buffers = []
        for bname, spec in b.get("buffers", {}).items():
            size = _lookup(ctx, spec["size"])
            if size is None:
                size = spec.get("default")
            if not isinstance(size, int) or isinstance(size, bool) or not 1 <= size <= 65535:
                err(f"devices.{n}", f"the {d['compatible']} binding's buffer {bname} needs a size of 1 to 65535 "
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
