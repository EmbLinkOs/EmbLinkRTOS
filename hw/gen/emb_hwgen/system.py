# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The system description (SPEC-014 §5, A125): what a product does with its board.

A system names its board, its Kconfig fragments, its threads and kernel objects, its devices'
settings, its SPI and I2C targets and its watchdog. It resolves with its board: the board's
model, with the system's device settings over its own (model.Resolver), and the system's own
part, checked against the resolved configuration and the bindings' limits. Every problem is
reported as 'file:line: node.path: message' (HW-002), located in the system description.
"""
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from . import HwError, load, model, schema

# Each object type, its static definition macro's Kconfig option, and the keys it takes.
OBJECTS = {
    "mutex": ("CONFIG_EMB_MUTEX", {"protocol", "users"}),
    "sem": ("CONFIG_EMB_SEM", {"initial", "max"}),
    "cond": ("CONFIG_EMB_COND", set()),
    "event": ("CONFIG_EMB_EVENT", {"initial"}),
    "msgq": ("CONFIG_EMB_MSGQ", {"item", "item_size", "capacity"}),
    "timer": ("CONFIG_EMB_TIMER", {"callback", "arg"}),
    "workq": ("CONFIG_EMB_WORKQ", {"priority", "stack"}),
}
COMMON = {"name", "type", "doc"}
SPEEDS = {"standard": 100000, "fast": 400000, "fast_plus": 1000000}
_DURATION = re.compile(r"^(\d+)(ms|s)$")
BAUD_TOLERANCE = 0.02  # a UART's rate within 2 % of the one asked (A125)


def duration_ms(value):
    if isinstance(value, int):
        return value
    m = _DURATION.match(value)
    return int(m.group(1)) * (1000 if m.group(2) == "s" else 1)


def boards(system_path, root):
    """The board references a system description names, in order: boards:, or board: alone."""
    m = load.flatten(Path(system_path), root)
    errors = [f"{m.at(p)}: {msg}" for p, msg in schema.validate(m.data, schema.load_schema("system"))]
    if ("board" in m.data) == ("boards" in m.data):
        errors.append(f"{m.at('(root)')}: name the product's board (board:) or its boards (boards:), one of them")
    if errors:
        raise HwError(errors)
    refs = m.data["boards"] if "boards" in m.data else [m.data["board"]]
    if len(set(refs)) != len(refs):
        raise HwError([f"{m.at('boards')}: a board named twice"])
    return refs


class System:
    def __init__(self, root, board_ref, several):
        self.root = Path(root)
        self.errors = []
        self.board_ref = board_ref
        self.several = several  # with several boards, each error says which one it is about

    def err(self, path, msg):
        where = f" (on {Path(self.board_ref).name})" if self.several else ""
        self.errors.append(f"{self.model.at(path)}: {msg}{where}")

    # -------------------------------------------------------------- entry point

    def resolve(self, system_path):
        system_path = Path(system_path)
        self.dir = system_path.parent
        self.model = load.flatten(system_path, self.root)
        for path, msg in schema.validate(self.model.data, schema.load_schema("system")):
            self.err(path, msg)
        if self.errors:
            raise HwError(self.errors)
        s = self.model.data
        at = "boards" if "boards" in s else "board"
        board_dir = self._board_dir(self.board_ref, at)
        if board_dir is None:
            raise HwError(self.errors)
        # the board's own device values, before the system's settings replace them
        self.board_devices = {d["name"]: dict(d) for d in
                              load.flatten(board_dir / "board.yaml", self.root).data.get("devices", [])}
        r = model.resolve(board_dir, self.root, self.model)
        r["board_dir"] = load.rel(board_dir, self.root)
        # the outputs' regeneration hint: its path in the tree, or a placeholder outside it (HW-013)
        r["system_path"] = (load.rel(system_path, self.root) if load.inside(system_path, self.root)
                            else f"<path to>/{system_path.resolve().parent.name}/system.yaml")
        if s.get("profile"):
            r["profile"] = s["profile"]
        cfg = self._config(r, board_dir)
        threads = self._threads(cfg)
        objects = self._objects(cfg, threads)
        devices = {d["name"]: d for d in r["devices"]}
        self._baud(r, devices)
        spi = self._spi_targets(r, devices)
        i2c = self._i2c_targets(devices)
        wdog = self._watchdog(devices)
        self._names(threads, objects, spi, i2c)
        if self.errors:
            raise HwError(self.errors)
        r["system"] = {
            "name": s["name"],
            "boards": [Path(b).name for b in boards(self.model.leaf.path, self.root)],  # in order: the first is the build's default
            "path": load.rel(system_path, self.root),
            "dir": load.rel(self.dir, self.root),
            "board_dir": load.rel(board_dir, self.root),
            "includes": s.get("includes", []),
            "conf": s.get("conf", []),
            "threads": threads,
            "objects": objects,
            "devices": self._settings(r),
            "spi_targets": spi,
            "i2c_targets": i2c,
            "watchdog": wdog,
            "config": {k: cfg[k] for k in sorted(cfg) if k in _CONFIG_SHOWN},
        }
        stamps = [system_path] + [self.dir / c for c in s.get("conf", [])]
        r["inputs"] = sorted(r["inputs"] + [{"path": load.stamp(p, self.root, self.dir),
                                             "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in stamps],
                             key=lambda i: i["path"])
        return r

    def _board_dir(self, ref, at):
        """<vendor>/<board> in the tree, or a directory relative to the system description."""
        for cand in (self.root / "boards" / ref, self.dir / ref):
            if (cand / "board.yaml").is_file():
                return cand
        self.err(at, f"no board {ref!r}: neither boards/{ref}/ in the tree nor {ref}/ beside this file "
                     "holds a board.yaml")
        return None

    # -------------------------------------------------------------- the configuration

    def _config(self, r, board_dir):
        """The configuration the build resolves (cmake/emb_config.cmake): the board's fragment,
        the profile's, then the application's, through tools/kconfig/embconfig.py."""
        from . import emit
        port = r["soc"]["arch"]["port"]
        variant = r["soc"]["arch"].get("variant", "")
        confs = []
        for i, c in enumerate(self.model.data.get("conf", [])):
            p = self.dir / c
            if not p.is_file():
                self.err(f"conf[{i}]", f"no such file {c} beside the system description")
            confs.append(p)
        profile = self.root / "configs" / f"{r['profile']}.conf"
        if not profile.is_file():
            self.err("profile", f"no configs/{r['profile']}.conf")
        if self.errors:
            raise HwError(self.errors)
        with tempfile.TemporaryDirectory(prefix="emb-system-") as tmp:
            board_conf = Path(tmp) / "board.conf"
            board_conf.write_text(emit.board_conf(r), encoding="utf-8")
            out = Path(tmp) / "config.json"
            cmd = [sys.executable, "-I", str(self.root / "tools/kconfig/embconfig.py"), "--kconfig", str(self.root / "Kconfig"),
                   "--manifest", str(self.root / "arch" / port / "arch.yaml"), "--variant", variant,
                   "--host-word-bits", "64" if port == "native" else "32",
                   "--conf", str(board_conf), "--conf", str(profile)]
            for c in confs:
                cmd += ["--conf", str(c)]
            cmd += ["--out-json", str(out)]
            p = subprocess.run(cmd, capture_output=True, text=True, cwd=self.root, env=dict(os.environ))
            if p.returncode != 0:
                lines = [l for l in (p.stderr + p.stdout).splitlines() if l.strip()]
                self.err("conf", "the configuration does not resolve: " + ("; ".join(lines[-3:]) or "embconfig failed"))
                raise HwError(self.errors)
            symbols = json.loads(out.read_text(encoding="utf-8"))["symbols"]
        return {k: v["value"] for k, v in symbols.items()}

    # -------------------------------------------------------------- threads and objects

    def _priority(self, cfg, at, prio):
        """1 to CONFIG_EMB_PRIORITY_COUNT - 1, larger higher; 0 is the idle level (SPEC-008)."""
        count = cfg.get("CONFIG_EMB_PRIORITY_COUNT")
        if prio < 1:
            self.err(at, "priority 0 is the idle level, not assignable: application priorities start at 1")
        elif isinstance(count, int) and prio >= count:
            self.err(at, f"priority {prio} is outside 1 to {count - 1} (CONFIG_EMB_PRIORITY_COUNT {count})")

    def _stack(self, cfg, at, stack):
        least = cfg.get("CONFIG_EMB_ARCH_STACK_MIN")
        if isinstance(least, int) and stack < least:
            self.err(at, f"{stack} bytes is below the port's minimum stack of {least} (EMB_THREAD_STACK_MIN)")

    def _threads(self, cfg):
        out = []
        prios = {}
        unique = cfg.get("CONFIG_EMB_PROFILE_TINY") is True
        for i, t in enumerate(self.model.data.get("threads", [])):
            at = f"threads.{t['name']}"
            self._priority(cfg, f"{at}.priority", t["priority"])
            self._stack(cfg, f"{at}.stack", t["stack"])
            if unique and t["priority"] in prios:
                self.err(f"{at}.priority", f"priority {t['priority']} is {prios[t['priority']]}'s: the tiny profile "
                                           "gives each thread a priority of its own")
            prios.setdefault(t["priority"], t["name"])
            flags = [f"EMB_THREAD_{f.upper()}" for f in t.get("flags", [])]
            if not t.get("autostart", True):
                flags.append("EMB_THREAD_NO_AUTOSTART")
            out.append({"name": t["name"], "entry": t["entry"], "arg": t.get("arg", "NULL"), "priority": t["priority"],
                        "stack": t["stack"], "autostart": t.get("autostart", True), "flags": flags,
                        "doc": t.get("doc", "")})
        return out

    def _objects(self, cfg, threads):
        prio = {t["name"]: t["priority"] for t in threads}
        out = []
        for o in self.model.data.get("objects", []):
            at = f"objects.{o['name']}"
            option, keys = OBJECTS[o["type"]]
            if cfg.get(option) is not True:
                self.err(f"{at}.type", f"a {o['type']} needs {option}, which the configuration leaves out")
            for k in sorted(set(o) - COMMON - keys):
                self.err(f"{at}.{k}", f"a {o['type']} takes no {k!r}")
            rec = {"name": o["name"], "type": o["type"], "doc": o.get("doc", "")}
            t = o["type"]
            if t == "mutex":
                proto = o.get("protocol", "inherit")
                rec["protocol"] = proto
                if proto == "inherit" and cfg.get("CONFIG_EMB_MUTEX_INHERIT") is not True:
                    self.err(f"{at}.protocol", "priority inheritance needs CONFIG_EMB_MUTEX_INHERIT")
                if proto == "ceiling":
                    users = o.get("users")
                    if not users:
                        self.err(at, "a ceiling mutex names its users, the threads that lock it")
                        users = []
                    for u in users:
                        if u not in prio:
                            self.err(f"{at}.users", f"{u!r} is not a thread of this system description")
                    rec["users"] = users
                    rec["ceiling"] = max((prio[u] for u in users if u in prio), default=0)
                elif "users" in o:
                    self.err(f"{at}.users", "only a ceiling mutex names its users")
            elif t == "sem":
                rec["initial"], rec["max"] = o.get("initial", 0), o.get("max", 1)
                if rec["initial"] > rec["max"]:
                    self.err(f"{at}.initial", f"{rec['initial']} is above the semaphore's max of {rec['max']}")
            elif t == "event":
                rec["initial"] = o.get("initial", 0)
            elif t == "msgq":
                if ("item" in o) == ("item_size" in o):
                    self.err(at, "a message queue has an item (a C type) or an item_size (bytes), one of them")
                if "capacity" not in o:
                    self.err(at, "a message queue needs its capacity")
                rec["item"] = o.get("item")
                rec["item_size"] = o.get("item_size")
                rec["capacity"] = o.get("capacity", 1)
                most = cfg.get("CONFIG_EMB_MSGQ_MAX_ITEM")
                if isinstance(most, int) and o.get("item_size", 0) > most:
                    self.err(f"{at}.item_size", f"{o['item_size']} bytes is above CONFIG_EMB_MSGQ_MAX_ITEM ({most})")
            elif t == "timer":
                if "callback" not in o:
                    self.err(at, "a timer needs its callback")
                rec["callback"], rec["arg"] = o.get("callback", "NULL"), o.get("arg", "NULL")
            elif t == "workq":
                for k in ("priority", "stack"):
                    if k not in o:
                        self.err(at, f"a work queue needs its {k}")
                if "priority" in o:
                    self._priority(cfg, f"{at}.priority", o["priority"])
                if "stack" in o:
                    self._stack(cfg, f"{at}.stack", o["stack"])
                rec["priority"], rec["stack"] = o.get("priority", 0), o.get("stack", 0)
            out.append(rec)
        return out

    def _names(self, threads, objects, spi, i2c):
        """Every name becomes a C symbol: one of each in the file."""
        seen = {}
        for section, items in (("threads", threads), ("objects", objects), ("spi_targets", spi), ("i2c_targets", i2c)):
            for item in items:
                if item["name"] in seen:
                    self.err(f"{section}.{item['name']}", f"{item['name']!r} is already the name of {seen[item['name']]}")
                else:
                    seen[item["name"]] = f"{section}.{item['name']}"

    # -------------------------------------------------------------- devices, targets, watchdog

    def _settings(self, r):
        """Each setting the system makes, and the value the board had."""
        out = {}
        for dev, opts in sorted(self.model.data.get("devices", {}).items()):
            out[dev] = {opt: {"value": v, "board": self.board_devices.get(dev, {}).get(opt)}
                        for opt, v in sorted(opts.items())}
        return out

    def _baud(self, r, devices):
        """A UART's rate as the system sets it: the peripheral's clock divides to it within 2 %."""
        for dev, opts in sorted(self.model.data.get("devices", {}).items()):
            d = devices.get(dev)
            if d is None or d["class"] != "uart" or "baud" not in opts:
                continue
            clock_hz = next((f["value"] for f in d["fields"] if f["field"] == "clock_hz"), None)
            baud = opts["baud"]
            if not isinstance(baud, int) or not isinstance(clock_hz, int):
                continue
            div = round(clock_hz / baud)
            got = clock_hz / div if div else 0
            if div < 1 or abs(got - baud) > baud * BAUD_TOLERANCE:
                self.err(f"devices.{dev}.baud", f"{baud} baud is not within 2 % of a rate {dev}'s "
                                                f"{clock_hz} Hz clock divides to (nearest {got:.0f})")

    def _bus(self, devices, at, name, cls):
        d = devices.get(name)
        if d is None:
            self.err(at, f"the board has no device {name!r}")
        elif d["class"] != cls:
            self.err(at, f"{name} is a {d['class']} device, not an {cls.upper()} controller")
        else:
            return d
        return None

    def _spi_targets(self, r, devices):
        out = []
        board_devs = {d["name"]: d for d in r["board"].get("devices", [])}
        taken = {}
        for t in self.model.data.get("spi_targets", []):
            at = f"spi_targets.{t['name']}"
            d = self._bus(devices, f"{at}.bus", t["bus"], "spi")
            rec = {"name": t["name"], "bus": t["bus"], "hz": t["hz"], "mode": t.get("mode", 0), "bits": t.get("bits", 8),
                   "cs": t.get("cs"), "doc": t.get("doc", "")}
            if d is not None:
                clock_hz = next((f["value"] for f in d["fields"] if f["field"] == "clock_hz"), None)
                lo, hi = d["limits"].get("divider", (None, None))
                if not isinstance(t["hz"], int):
                    self.err(f"{at}.hz", f"{t['hz']!r} is not a frequency")
                elif lo is not None and isinstance(clock_hz, int) and t["hz"] < clock_hz // hi:
                    self.err(f"{at}.hz", f"{t['hz']} Hz is below the slowest clock {t['bus']} makes, "
                                         f"{clock_hz // hi} Hz ({clock_hz} Hz / {hi})")
                if lo is None:
                    self.err(f"{at}.bus", f"the {d['compatible']} binding states no divider limits for its targets")
                if rec["cs"] is not None:
                    cs = model.cs_list(board_devs.get(t["bus"], {}))
                    if rec["cs"] >= len(cs):
                        self.err(f"{at}.cs", f"{t['bus']} has {len(cs)} chip select(s) in the board description, "
                                             f"not a chip select {rec['cs']}")
                    key = (t["bus"], rec["cs"])
                    if key in taken:
                        self.err(f"{at}.cs", f"{t['bus']}'s chip select {rec['cs']} is already {taken[key]}'s")
                    taken[key] = t["name"]
            out.append(rec)
        return out

    def _i2c_targets(self, devices):
        out = []
        taken = {}
        for t in self.model.data.get("i2c_targets", []):
            at = f"i2c_targets.{t['name']}"
            d = self._bus(devices, f"{at}.bus", t["bus"], "i2c")
            speed = t.get("speed", "standard")
            if d is not None:
                speeds = d["limits"].get("speeds")
                if speeds is None:
                    self.err(f"{at}.bus", f"the {d['compatible']} binding states no speeds for its targets")
                elif speed not in speeds:
                    self.err(f"{at}.speed", f"{t['bus']} ({d['compatible']}) runs at {', '.join(speeds)}, not {speed}")
                key = (t["bus"], t["address"])
                if key in taken:
                    self.err(f"{at}.address", f"address 0x{t['address']:02X} on {t['bus']} is already {taken[key]}'s")
                taken[key] = t["name"]
            out.append({"name": t["name"], "bus": t["bus"], "address": t["address"], "speed": speed,
                        "hz": SPEEDS[speed], "doc": t.get("doc", "")})
        return out

    def _watchdog(self, devices):
        w = self.model.data.get("watchdog")
        if w is None:
            return None
        ms = duration_ms(w["timeout"])
        d = devices.get(w["device"])
        if d is None:
            self.err("watchdog.device", f"the board has no device {w['device']!r}")
        elif d["class"] != "watchdog":
            self.err("watchdog.device", f"{w['device']} is a {d['class']} device, not a watchdog")
        else:
            rng = d["limits"].get("timeout_ms")
            if rng is None:
                self.err("watchdog.device", f"the {d['compatible']} binding states no timeout range")
            elif not rng[0] <= ms <= rng[1]:
                self.err("watchdog.timeout", f"{ms} ms is outside {w['device']}'s {rng[0]} to {rng[1]} ms "
                                             f"({d['compatible']})")
        return {"device": w["device"], "timeout_ms": ms, "doc": w.get("doc", "")}


# what system.json and system.md show of the resolved configuration
_CONFIG_SHOWN = ("CONFIG_EMB_PRIORITY_COUNT", "CONFIG_EMB_ARCH_STACK_MIN", "CONFIG_EMB_PROFILE")


def resolve(system_path, root, board_ref=None):
    """The system resolved on one of its boards (the first by default)."""
    refs = boards(system_path, root)
    ref = board_ref if board_ref is not None else refs[0]
    if ref not in refs:
        raise HwError([f"{load.rel(system_path, root)}:1: boards: {ref!r} is not one of the system's boards"])
    return System(root, ref, len(refs) > 1).resolve(system_path)
