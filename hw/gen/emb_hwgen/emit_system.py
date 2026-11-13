# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

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
    if s["tasks"]:
        out += '\n#include "tasks.h"\n'
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
    doc.update({k: s[k] for k in ("name", "boards", "threads", "tasks", "objects", "devices", "spi_targets",
                                  "i2c_targets", "watchdog", "config", "includes", "conf", "scaffold")})
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
    if s["tasks"]:
        out += "\n## Tasks\n\n| Task | Model | Priority | Stack | States (initial first) | Events |\n|---|---|---|---|---|---|\n"
        for t in sorted(s["tasks"], key=lambda t: (-t["priority"], t["name"])):
            model = t["model"] + (f", {t['period_ms']} ms" if t["model"] == "periodic" else f", queue {t['queue']}")
            states = [t["initial"]] + [x for x in t["states"] if x != t["initial"]]
            out += (f"| `{t['name']}` | {model} | {t['priority']} | {t['stack']} | {', '.join(states)} | "
                    f"{', '.join(t['events']) or '—'} |\n")
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


# --------------------------------------------------------------------------- tasks (SPEC-020)

def _up(name):
    return name.upper()


def _event_type(t):
    return t["event"] or "emb_task_event_t"


def _handler_decls(t):
    """The handlers a task's application file defines, as declarations."""
    n, out = t["name"], []
    for st in t["states"]:
        if t["model"] == "periodic":
            out.append(f"emb_state_t {n}_on_{st}({n}_t *self);")
        else:
            out.append(f"emb_state_t {n}_on_{st}({n}_t *self, const {_event_type(t)} *ev);")
    out += [f"void {n}_enter_{st}({n}_t *self);" for st in t["entry"]]
    out += [f"void {n}_exit_{st}({n}_t *self);" for st in t["exit"]]
    return out


def tasks_h(r, c_comment):
    s = r["system"]
    out = _intro(r, c_comment, f"The system {s['name']}'s tasks: their states, events, types, handlers and handles "
                               "(SPEC-020).")
    out += "#ifndef EMB_TASKS_H\n#define EMB_TASKS_H\n\n#include <emb/emb.h>\n\n#include <stddef.h>\n"
    own = [t for t in s["tasks"] if s["scaffold"] and (t["data"] or t["event"])]
    if own or s["includes"]:
        out += "\n" + "".join(f'#include "{h}"\n' for h in s["includes"])
        out += "".join(f'#include "tasks/{t["name"]}.h"\n' for t in own)
    out += "\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
    for t in s["tasks"]:
        n, N = t["name"], _up(t["name"])
        out += f"\n/* {n}: {t['model']}" + (f", every {t['period_ms']} ms" if t["model"] == "periodic" else "") + " */\n"
        for i, st in enumerate(t["states"]):  # typed: a handler's ternary of two states stays an emb_state_t
            out += f"#define {N}_{_up(st)} ((emb_state_t){i}u)\n"
        out += f"#define {N}_STATE_COUNT ((emb_state_t){len(t['states'])}u)\n"
        for i, ev in enumerate(t["events"]):
            out += f"#define {N}_EV_{_up(ev)} (EMB_EV_USER + {i}u)\n"
        out += f"typedef struct {n} {{\n    emb_task_t task; /* first: the runtime's */\n"
        if t["data"]:
            out += f"    {t['data']} data;\n"
        out += f"}} {n}_t;\n"
        out += f"extern {n}_t {n};\n#define {N}_TASK (&{n}.task)\n"
        if t["event"]:
            out += (f"_Static_assert(offsetof({t['event']}, id) == 0u, \"{t['event']}: its first member is its id\");\n")
        out += "".join(d + "\n" for d in _handler_decls(t))
    out += "\n#ifdef __cplusplus\n}\n#endif\n\n#endif /* EMB_TASKS_H */\n"
    return out


def tasks_c(r, c_comment):
    s = r["system"]
    out = _intro(r, c_comment, f"The system {s['name']}'s tasks: each one's queue, dispatch, description and thread "
                               "(SPEC-020); the runtime is emb_task_run().")
    out += '#include "tasks.h"\n'
    for t in s["tasks"]:
        n, N, ev_t = t["name"], _up(t["name"]), _event_type(t)
        event = t["model"] == "event"
        out += f"\n/* ---- {n} ---- */\n"
        if event:
            out += f"static {ev_t} {n}_event;\n"
            out += f"EMB_MSGQ_DEFINE({n}_task_queue, sizeof({ev_t}), {t['queue']}u, 0u);\n"
            if t["timeouts"]:
                vals = ", ".join(f"{t['timeouts'].get(st, 0)}u" for st in t["states"])
                out += f"static const uint32_t {n}_timeouts[{len(t['states'])}] = {{{vals}}};\n"
        out += f"\nstatic emb_state_t {n}_dispatch(emb_state_t state, uint8_t why)\n{{\n"
        out += "    emb_state_t next;\n"
        if event and t["timeouts"]:
            out += f"    if (why == EMB_TASK_WHY_TIMEOUT) {{\n        {n}_event.id = EMB_EV_TIMEOUT;\n"
            if not t["event"]:
                out += f"        {n}_event.value = 0u;\n"
            out += "    }\n"
        else:
            out += "    (void)why;\n"
        out += "    switch (state) {\n"
        for st in t["states"]:
            call = f"{n}_on_{st}(&{n})" if not event else f"{n}_on_{st}(&{n}, &{n}_event)"
            out += f"    case {N}_{_up(st)}:\n        next = {call};\n        break;\n"
        out += "    default:\n        next = state;\n        break;\n    }\n    return next;\n}\n"
        for kind, names in (("enter", t["entry"]), ("exit", t["exit"])):
            if not names:
                continue
            out += f"\nstatic void {n}_{kind}(emb_state_t state)\n{{\n    switch (state) {{\n"
            for st in names:
                out += f"    case {N}_{_up(st)}:\n        {n}_{kind}_{st}(&{n});\n        break;\n"
            out += "    default:\n        break;\n    }\n}\n"
        enter = f"{n}_enter" if t["entry"] else "NULL"
        leave = f"{n}_exit" if t["exit"] else "NULL"
        evbuf = f"&{n}_event" if event else "NULL"
        queue = f"&{n}_task_queue" if event else "NULL"
        timeouts = f"{n}_timeouts" if (event and t["timeouts"]) else "NULL"
        size = f"(uint16_t)sizeof({ev_t})" if event else "0u"
        model = "EMB_TASK_EVENT" if event else "EMB_TASK_PERIODIC"
        out += (f"\nstatic const emb_task_desc_t {n}_desc = {{\"{n}\", {n}_dispatch, {enter}, {leave}, {evbuf}, {queue},\n"
                f"                                       {timeouts}, {t['period_ms']}u, {size}, {N}_{_up(t['initial'])},\n"
                f"                                       {N}_STATE_COUNT, {model}, 0u}};\n")
        # designated: the counters, and the task's data, start at zero
        out += f"{n}_t {n} = {{.task = {{.desc = &{n}_desc, .state = {N}_{_up(t['initial'])}}}}};\n"
        out += f"\nstatic void {n}_main(void *arg)\n{{\n    (void)arg;\n    emb_task_run(&{n}.task);\n}}\n"
        out += f"\nEMB_THREAD_DEFINE({n}_thread, {n}_main, NULL, {t['priority']}u, {t['stack']}u, 0u);\n"
    return out


# --------------------------------------------------------------------------- the application's own files (A126)

def _app_note(system_path, kind):
    """The note an application's own files carry: stable, so editing system.yaml does not touch it."""
    text = f"Generated by emb-hwgen from {system_path}; yours to edit: regeneration merges its changes (A126)."
    if kind == "hash":
        return "# SPDX-License-Identifier: Apache-2.0\n# " + text + "\n"
    return "/* SPDX-License-Identifier: Apache-2.0 */\n/*\n * " + text + "\n */\n"


def main_c(r):
    s = r["system"]
    out = _app_note("system.yaml", "c")
    out += f"/* {s['name']}: the board, the kernel, then the threads and tasks of system.yaml. */\n"
    out += "#include <emb/emb.h>\n\n#include <emb_board.h>\n\n#include \"system.h\"\n\n"
    out += "int main(void)\n{\n    emb_board_init();\n    emb_kernel_init();\n"
    out += "    emb_kernel_start(); /* the threads and tasks run from here: it does not return */\n}\n"
    return out


def task_c(r, t):
    n = t["name"]
    out = _app_note("system.yaml", "c")
    out += f"/* The {n} task's handlers (SPEC-020): each state's returns the state to be in next. */\n"
    out += '#include "tasks.h"\n'
    for st in t["states"]:
        if t["model"] == "periodic":
            out += f"\nemb_state_t {n}_on_{st}({n}_t *self)\n{{\n    (void)self;\n    return {_up(n)}_{_up(st)};\n}}\n"
        else:
            out += (f"\nemb_state_t {n}_on_{st}({n}_t *self, const {_event_type(t)} *ev)\n{{\n    (void)self;\n"
                    f"    (void)ev;\n    return {_up(n)}_{_up(st)};\n}}\n")
    for kind, names in (("enter", t["entry"]), ("exit", t["exit"])):
        for st in names:
            out += f"\nvoid {n}_{kind}_{st}({n}_t *self)\n{{\n    (void)self;\n}}\n"
    return out


def task_h(r, t):
    n = t["name"]
    out = _app_note("system.yaml", "c")
    out += f"/* The {n} task's own types: its data" + (" and its event" if t["event"] else "") + ". */\n"
    guard = f"TASK_{_up(n)}_H"
    out += f"#ifndef {guard}\n#define {guard}\n\n#include <stdint.h>\n"
    if t["data"]:
        out += f"\ntypedef struct {n}_data {{\n    uint32_t count; /* the task's own state: change it */\n}} {t['data']};\n"
    if t["event"]:
        out += f"\ntypedef struct {n}_event {{\n    uint32_t id; /* first: the runtime reads it */\n    uint32_t value;\n}} {t['event']};\n"
    return out + f"\n#endif /* {guard} */\n"


def app_cmakelists(r, rtos_cmake):
    s = r["system"]
    out = _app_note("system.yaml", "hash")
    out += f"# {s['name']}'s build: the RTOS's package, its system description and its sources.\n"
    out += "cmake_minimum_required(VERSION 3.20)\n\n"
    out += f"set(EmbLinkRTOS_DIR \"{rtos_cmake}\" CACHE PATH \"The RTOS's cmake/ directory\")\n"
    out += "set(EMB_SYSTEM \"${CMAKE_CURRENT_LIST_DIR}/system.yaml\" CACHE FILEPATH \"The application's system description\")\n"
    if s["conf"]:
        confs = ";".join(f"${{CMAKE_CURRENT_LIST_DIR}}/{c}" for c in s["conf"])
        out += f"set(EMB_CONF \"{confs}\" CACHE STRING \"Configuration fragments\")\n"
    out += ("if(EXISTS \"${CMAKE_CURRENT_LIST_DIR}/stack-roots.json\")\n"
            "  set(EMB_STACK_ROOTS \"${CMAKE_CURRENT_LIST_DIR}/stack-roots.json\" CACHE FILEPATH \"The stack check's roots\")\n"
            "endif()\n")
    out += "find_package(EmbLinkRTOS REQUIRED)\n\n"
    out += f"project({s['name']} LANGUAGES C)\n\nemb_kernel()\n"
    sources = ["src/main.c"] + [f"src/tasks/{t['name']}.c" for t in s["tasks"]]
    out += f"emb_add_executable({s['name']} {' '.join(sources)})\n"
    return out


def app_files(r, rtos_cmake):
    """{path relative to the system description: text} for the application's own files."""
    s = r["system"]
    files = {"CMakeLists.txt": app_cmakelists(r, rtos_cmake), "src/main.c": main_c(r)}
    for t in s["tasks"]:
        files[f"src/tasks/{t['name']}.c"] = task_c(r, t)
        if t["data"] or t["event"]:
            files[f"src/tasks/{t['name']}.h"] = task_h(r, t)
    return files
