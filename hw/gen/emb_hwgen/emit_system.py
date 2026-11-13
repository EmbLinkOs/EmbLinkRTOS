# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""A system description's outputs (SPEC-014 §5.1, A125): system.h, system.c, system.json and
system.md. Data and macro calls only: the code they set in motion is the kernel's (the static
definition macros, SPEC-001 §6.4) and the drivers'."""
import json
import textwrap

from . import SCHEMA_VERSION, VERSION

HANDLE = {"thread": "emb_thread_t", "mutex": "emb_mutex_t", "sem": "emb_sem_t", "cond": "emb_cond_t",
          "event": "emb_event_t", "msgq": "emb_msgq_t", "timer": "emb_timer_t", "workq": "emb_workq_t"}
PROTOCOL = {"inherit": "EMB_MUTEX_INHERIT", "ceiling": "EMB_MUTEX_CEILING", "none": "EMB_MUTEX_NONE"}
SPEED = {"standard": "EMB_DRV_I2C_STANDARD", "fast": "EMB_DRV_I2C_FAST", "fast_plus": "EMB_DRV_I2C_FAST_PLUS"}


def _sym(name):
    return name.upper().replace("-", "_")


def _intro(r, cmd_comment, what):
    lines = textwrap.wrap(what, 96)
    text = "/* " + "\n * ".join(lines) + " */\n"
    return "/* SPDX-License-Identifier: Apache-2.0 */\n" + cmd_comment + text


def system_h(r, c_comment):
    s = r["system"]
    out = _intro(r, c_comment, f"The system {s['name']}'s threads, objects, targets and watchdog for the application's "
                               "sources (SPEC-014 §5, A125).")
    out += "#ifndef EMB_SYSTEM_H\n#define EMB_SYSTEM_H\n\n"
    # the API, and the storage types the static definition macros take (<emb/storage.h>)
    out += "#include <emb/emb.h>\n"
    if s["includes"]:
        out += "\n" + "".join(f'#include "{h}"\n' for h in s["includes"])
    out += "\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
    if s["threads"]:
        out += "\n/* threads: their entries, which the application defines, and their handles */\n"
        for entry in sorted({t["entry"] for t in s["threads"]}):
            out += f"void {entry}(void *arg);\n"
        for t in s["threads"]:
            out += f"extern emb_thread_t {t['name']};\n"
    timers = [o for o in s["objects"] if o["type"] == "timer"]
    if s["objects"]:
        out += "\n/* objects */\n"
        for cb in sorted({o["callback"] for o in timers}):
            out += f"void {cb}(emb_timer_t timer, void *arg);\n"
        for o in s["objects"]:
            out += f"extern {HANDLE[o['type']]} {o['name']};\n"
    if s["spi_targets"] or s["i2c_targets"]:
        out += "\n/* bus targets, for the class's calls */\n"
        for t in s["spi_targets"]:
            out += f"extern const emb_drv_spi_target_t {t['name']};\n"
        for t in s["i2c_targets"]:
            out += f"extern const emb_drv_i2c_target_t {t['name']};\n"
    if s["watchdog"]:
        w = s["watchdog"]
        out += (f"\n/* the watchdog: emb_drv_wdt_start(EMB_DEVICE({w['device']}), EMB_SYSTEM_WATCHDOG_TIMEOUT_MS) */\n"
                f"#define EMB_SYSTEM_WATCHDOG_TIMEOUT_MS {w['timeout_ms']}u\n")
    out += "\n#ifdef __cplusplus\n}\n#endif\n\n#endif /* EMB_SYSTEM_H */\n"
    return out


def system_c(r, c_comment):
    s = r["system"]
    out = _intro(r, c_comment, f"The system {s['name']}'s static definitions: the kernel creates each from its "
                               "initialization table (SPEC-001 §6.4, SPEC-014 §5, A125).")
    out += '#include "system.h"\n'
    if s["spi_targets"]:
        out += "\n#include <soc_devices.h>\n"
    for t in s["threads"]:
        flags = " | ".join(t["flags"]) if t["flags"] else "0u"
        out += (f"\nEMB_THREAD_DEFINE({t['name']}, {t['entry']}, {t['arg']}, {t['priority']}u, {t['stack']}u, {flags});")
    for o in s["objects"]:
        n, k = o["name"], o["type"]
        if k == "mutex":
            out += f"\nEMB_MUTEX_DEFINE({n}, {PROTOCOL[o['protocol']]}, {o.get('ceiling', 0)}u, 0u);"
        elif k == "sem":
            out += f"\nEMB_SEM_DEFINE({n}, {o['initial']}u, {o['max']}u);"
        elif k == "cond":
            out += f"\nEMB_COND_DEFINE({n}, 0u);"
        elif k == "event":
            out += f"\nEMB_EVENT_DEFINE({n}, {o['initial']}u, 0u);"
        elif k == "msgq":
            size = f"sizeof({o['item']})" if o["item"] else f"{o['item_size']}u"
            out += f"\nEMB_MSGQ_DEFINE({n}, {size}, {o['capacity']}u, 0u);"
        elif k == "timer":
            out += f"\nEMB_TIMER_DEFINE({n}, {o['callback']}, {o['arg']});"
        elif k == "workq":
            out += f"\nEMB_WORKQ_DEFINE({n}, {o['priority']}u, {o['stack']}u);"
    if s["threads"] or s["objects"]:
        out += "\n"
    for t in s["spi_targets"]:
        cs = f"EMB_SOC_{_sym(t['bus'])}_CS{t['cs']}" if t["cs"] is not None else "{NULL, 0u, 0u, {0u, 0u}}"
        out += (f"\n/* {t['bus']}: at most {t['hz']} Hz, mode {t['mode']} */\n"
                f"const emb_drv_spi_target_t {t['name']} = {{{t['hz']}u, EMB_DRV_SPI_MODE_{t['mode']}, {t['bits']}u, 0u, 0u, {cs}}};\n")
    for t in s["i2c_targets"]:
        out += (f"\n/* {t['bus']}: address 0x{t['address']:02X}, {t['speed'].replace('_', ' ')} mode */\n"
                f"const emb_drv_i2c_target_t {t['name']} = {{{SPEED[t['speed']]}, 0x{t['address']:02X}u, {{0u, 0u}}}};\n")
    return out


def system_json(r):
    s = r["system"]
    doc = {"generator": {"name": "emb-hwgen", "version": VERSION}, "schema_version": SCHEMA_VERSION,
           "inputs": r["inputs"], "board": r["board"]["name"], "profile": r["profile"]}
    doc.update({k: s[k] for k in ("name", "boards", "threads", "objects", "devices", "spi_targets", "i2c_targets",
                                  "watchdog", "config", "includes", "conf")})
    return json.dumps(doc, indent=2, sort_keys=True) + "\n"


def system_md(r, stamp):
    s = r["system"]
    out = f"# {s['name']}: the system on {r['board']['name']}\n\n"
    out += (f"Generated from `{s['path']}` (SPEC-014 §5, A125). Profile {r['profile']}, "
            f"{s['config'].get('CONFIG_EMB_PRIORITY_COUNT', '?')} priorities, a minimum stack of "
            f"{s['config'].get('CONFIG_EMB_ARCH_STACK_MIN', '?')} bytes.\n")
    if s["threads"]:
        out += "\n## Threads\n\n| Thread | Entry | Priority | Stack | Starts |\n|---|---|---|---|---|\n"
        for t in sorted(s["threads"], key=lambda t: (-t["priority"], t["name"])):
            out += f"| `{t['name']}` | `{t['entry']}` | {t['priority']} | {t['stack']} | {'yes' if t['autostart'] else 'no'} |\n"
    if s["objects"]:
        out += "\n## Objects\n\n| Object | Type | Settings |\n|---|---|---|\n"
        for o in s["objects"]:
            if o["type"] == "mutex":
                what = o["protocol"] + (f", ceiling {o['ceiling']} from {', '.join(o['users'])}" if o["protocol"] == "ceiling" else "")
            elif o["type"] == "msgq":
                what = f"{o['capacity']} of {('`' + o['item'] + '`') if o['item'] else str(o['item_size']) + ' bytes'}"
            elif o["type"] == "sem":
                what = f"{o['initial']} of {o['max']}"
            elif o["type"] == "event":
                what = f"initial 0x{o['initial']:X}"
            elif o["type"] == "timer":
                what = f"`{o['callback']}`"
            elif o["type"] == "workq":
                what = f"priority {o['priority']}, stack {o['stack']}"
            else:
                what = ""
            out += f"| `{o['name']}` | {o['type']} | {what} |\n"
    if s["devices"]:
        out += "\n## Device settings\n\n| Device | Option | Value | The board's |\n|---|---|---|---|\n"
        for dev, opts in s["devices"].items():
            for opt, v in opts.items():
                board = "—" if v["board"] is None else v["board"]
                out += f"| {dev} | {opt} | {v['value']} | {board} |\n"
    if s["spi_targets"] or s["i2c_targets"]:
        out += "\n## Bus targets\n\n| Target | Bus | Settings |\n|---|---|---|\n"
        for t in s["spi_targets"]:
            cs = f", chip select {t['cs']}" if t["cs"] is not None else ""
            out += f"| `{t['name']}` | {t['bus']} | at most {t['hz']} Hz, mode {t['mode']}{cs} |\n"
        for t in s["i2c_targets"]:
            out += f"| `{t['name']}` | {t['bus']} | address 0x{t['address']:02X}, {t['speed'].replace('_', ' ')} mode |\n"
    if s["watchdog"]:
        out += f"\n## Watchdog\n\n{s['watchdog']['device']}, {s['watchdog']['timeout_ms']} ms.\n"
    return out + stamp
