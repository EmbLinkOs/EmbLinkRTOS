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
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return f"0x{v:08X}u" if expr.rsplit(".", 1)[-1] in HEX_KEYS else f"{v}u"
    if isinstance(v, str):
        return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'
    raise TypeError(v)


def resolve(board, soc, clk, bindings, err):
    """The board's devices in initialization order: EARLY first, each level a topological
    sort of its dependencies in which ties keep the description's order. `err(path, msg)`
    reports against the board."""
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

    clock_devs = [n for n in names if n in devs and devs[n]["binding"]["class"] == "clock"]
    for n, d in devs.items():
        e, at = d["entry"], f"devices.{n}"
        deps = list(e.get("deps", []))
        if "bus" in e:
            deps.append(e["bus"])
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

    out = []
    for n in order:
        d = devs[n]
        b, e = d["binding"], d["entry"]
        ctx = {"device": {k: v for k, v in e.items() if k not in ("deps", "doc")},
               "board": board.data}
        if d["peripheral"] is not None:
            ctx["peripheral"] = _peripheral_view(d["peripheral"], soc.data, clk)
        if "bus" in e:
            ctx["device"]["bus"] = {"device": e["bus"]}
        fields = []
        for field, expr in b.get("fields", {}).items():
            v = _lookup(ctx, expr)
            if v is None or isinstance(v, (list,)) or (isinstance(v, dict) and "device" not in v):
                err(f"devices.{n}", f"the {d['compatible']} binding's field {field} reads {expr}, which this device does not have")
                continue
            fields.append({"field": field, "expr": expr, "value": v, "c": c_value(v, expr)})
        out.append({
            "name": n,
            "compatible": d["compatible"],
            "peripheral": d["peripheral"]["name"] if d["peripheral"] is not None else None,
            "class": b["class"],
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
