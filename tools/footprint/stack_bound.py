#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""A static worst-case stack bound for each thread and the interrupt stack (KRN-MEM-009).

    stack_bound.py --elf build/controller/controller.elf --build build/controller \\
                   --roots examples/controller/stack-roots.json [--objdump arm-none-eabi-objdump] \\
                   [--json out.json]

The call graph comes from the linked image's disassembly: every bl, every branch to another
function's start (a tail call), and every blx through a register, which the roots file
resolves. Each function's frame is GCC's -fstack-usage figure (the .su files the build writes
next to its objects), or, for code without one (assembly, naked functions, the C library),
what its prologue pushes and subtracts. A cycle in the graph, a dynamic frame or an indirect
call the roots file does not resolve makes the bound unknown, and the tool says where.

On Cortex-M, a preempted thread also holds the exception's hardware frame (32 bytes, 104
with an active floating-point context) and what the switch saves (r4-r11 and EXC_RETURN, 36
bytes, and s16-s31 for a floating-point context, 64), plus 4 for the frame's alignment. A
thread has a floating-point context when anything it can reach executes a VFP instruction.
The interrupt stack holds the handlers: one per priority level at once, each nested level
adding its own hardware frame. The roots file lists the levels, lowest first; main()'s
frames go before the first switch (A90), so the boot path and the handlers do not overlap.

Exit status 0 when every bound is known and within its stack, 1 otherwise, 2 on a usage error.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

FUNC = re.compile(r"^([0-9a-f]+) <([^>]+)>:$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4}\s?){1,2}\s+(\S+)\s*(.*)$")
TARGET = re.compile(r"\b([0-9a-f]+) <([^>+]+)(\+0x[0-9a-f]+)?>")
SU = re.compile(r"^(.*):(\d+):(\d+):(\S+)\s+(\d+)\s+(\S+)$")
FP_OPS = re.compile(r"^v[a-z]")
HW_FRAME, HW_FRAME_FP = 32, 104
SW_SAVE, SW_SAVE_FP = 36, 64
ALIGN = 4


def read_su(build):
    """{function: (bytes, kind)} from every .su under build; the largest where names repeat."""
    out = {}
    for p in Path(build).rglob("*.su"):
        for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
            m = SU.match(line.strip())
            if not m:
                continue
            name, size, kind = m.group(4), int(m.group(5)), m.group(6)
            if name not in out or size > out[name][0]:
                out[name] = (size, kind)
    return out


def regs_in(text):
    """The number of registers in a {...} list, ranges included."""
    m = re.search(r"\{([^}]*)\}", text)
    if not m:
        return 0
    n = 0
    for part in m.group(1).split(","):
        part = part.strip()
        if "-" in part:
            a, b = (int(re.sub(r"\D", "", x)) for x in part.split("-"))
            n += b - a + 1
        elif part:
            n += 1
    return n


def disassemble(elf, objdump):
    """{function: {"start", "insns": [(addr, op, args)]}} from the image's code."""
    text = subprocess.run([objdump, "-d", "--no-show-raw-insn", str(elf)], capture_output=True, text=True,
                          check=True).stdout
    funcs, cur = {}, None
    for line in text.splitlines():
        m = FUNC.match(line)
        if m:
            cur = {"start": int(m.group(1), 16), "insns": []}
            funcs[m.group(2)] = cur
            continue
        if cur is None:
            continue
        m = re.match(r"^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$", line)
        if m and not m.group(2).startswith("."):
            cur["insns"].append((int(m.group(1), 16), m.group(2), m.group(3)))
    return funcs


def prologue_frame(insns):
    """What a function's first instructions push and subtract from sp, up to its first branch."""
    total = 0
    for _, op, args in insns[:16]:
        base = op.split(".")[0]
        if base == "push" or (base == "stmdb" and args.startswith("sp!")):
            total += 4 * regs_in(args)
        elif base == "vpush":
            regs = regs_in(args)
            total += regs * (8 if "d" in args.split("{")[1][:2] else 4)
        elif base in ("sub", "subw") and re.match(r"sp,\s*(sp,\s*)?#", args):
            total += int(re.search(r"#(0x[0-9a-f]+|\d+)", args).group(1), 0)
        elif base.startswith("b") or base in ("pop", "ldmia"):
            break
    return total


def table_targets(elf, objdump, nm, table, funcs):
    """The functions a linker table of function pointers holds (__start_<table> to __stop_<table>)."""
    syms = {}
    for line in subprocess.run([nm, str(elf)], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    start, stop = syms.get(f"__start_{table}"), syms.get(f"__stop_{table}")
    if start is None or stop is None:
        raise ValueError(f"no __start_{table}/__stop_{table} in the image")
    dump = subprocess.run([objdump, "-s", f"--start-address={start:#x}", f"--stop-address={stop:#x}", str(elf)],
                          capture_output=True, text=True, check=True).stdout
    data = bytearray()
    for line in dump.splitlines():
        m = re.match(r"^\s*([0-9a-f]{4,}) ((?:[0-9a-f]{2,8} ?){1,4})", line)
        if m and int(m.group(1), 16) >= start:
            data += bytes.fromhex(m.group(2).replace(" ", ""))
    starts = {f["start"]: name for name, f in funcs.items()}
    out = []
    for i in range(0, len(data) - 3, 4):
        addr = int.from_bytes(data[i:i + 4], "little") & ~1
        if addr in starts:
            out.append(starts[addr])
    return out


def call_graph(funcs):
    """{function: {"calls": set, "indirect": [address], "fp": bool}} from bl, tail branches and
    blx; "indirect" holds the address of each indirect call or jump."""
    starts = {f["start"]: name for name, f in funcs.items()}
    graph = {}
    for name, f in funcs.items():
        calls, indirect, fp = set(), [], False
        for addr, op, args in f["insns"]:
            base = op.split(".")[0]
            if FP_OPS.match(base) and base not in ("vmrs", "vmsr"):
                fp = True
            if base in ("bl", "blx", "b") or (base.startswith("b") and len(base) <= 3):
                m = TARGET.search(args)
                if m:
                    addr = int(m.group(1), 16)
                    target = starts.get(addr)
                    if target and target != name and (base in ("bl", "blx") or not m.group(3)):
                        calls.add(target)
                elif base == "blx" or (base == "bx" and not args.startswith("lr")):
                    indirect.append(addr)
            elif base == "bx" and not args.startswith("lr"):
                indirect.append(addr)
        graph[name] = {"calls": calls, "indirect": sorted(set(indirect)), "fp": fp}
    return graph


CLONE = re.compile(r"\.(?:constprop|isra|part|cold|lto_priv)\.\d+")


def base_name(name):
    """A function's name without GCC's clone suffixes (raise_info.constprop.0 is raise_info)."""
    return CLONE.sub("", name)


def inline_chains(elf, addr2line, addresses):
    """{address: [function, innermost first]}: the functions inlined at each address, from the
    debug information (addr2line -i). Under LTO, or wherever a function is inlined, an indirect
    call sits in a function other than the one that makes it in the source."""
    if not addresses:
        return {}
    out = subprocess.run([addr2line, "-f", "-i", "-a", "-e", str(elf), *(f"{a:#x}" for a in addresses)],
                         capture_output=True, text=True, check=True).stdout.splitlines()
    # each address's line, then one (function, file:line) pair per inlined level, innermost first
    chains, cur, pair = {}, None, 0
    for line in out:
        if re.match(r"^0x[0-9a-f]+$", line):
            cur, pair = int(line, 16), 0
            chains[cur] = []
            continue
        if cur is not None and pair % 2 == 0 and line.strip() != "??":
            chains[cur].append(line.strip())
        pair += 1
    return chains


def resolve_indirect(graph, resolve, chains_of):
    """Each indirect call's targets, by the function that makes it in the source: the function
    itself, or the first of the functions inlined at the call that the roots file names. Sets
    node["targets"] and node["unresolved"]; chains_of(addresses) gives the inline chains."""
    pending = {}
    for name, node in graph.items():
        node["targets"], node["unresolved"] = set(), []
        if not node["indirect"]:
            continue
        key = name if name in resolve else base_name(name)
        if key in resolve:
            node["targets"] |= set(resolve[key])
        else:
            pending[name] = node
    chains = chains_of([a for node in pending.values() for a in node["indirect"]]) if pending else {}
    for name, node in pending.items():
        for addr in node["indirect"]:
            hit = next((base_name(f) for f in chains.get(addr, []) if base_name(f) in resolve), None)
            if hit is None:
                node["unresolved"].append(addr)
            else:
                node["targets"] |= set(resolve[hit])


class Bound:
    def __init__(self, graph, frames, resolve):
        self.graph, self.frames, self.resolve = graph, frames, resolve
        self.memo, self.problems = {}, []

    def frame(self, name):
        return self.frames.get(name, 0)

    def depth(self, name, path=()):
        """(bytes, worst path, fp reached) of the deepest chain from name."""
        if name in path:
            self.problems.append(f"recursion: {' -> '.join(path + (name,))}")
            return 0, [name], False
        if name in self.memo:
            return self.memo[name]
        node = self.graph.get(name)
        if node is None:
            self.problems.append(f"{name}: not in the image")
            return 0, [name], False
        callees = set(node["calls"])
        if "targets" in node:  # resolved by resolve_indirect, call by call
            callees |= node["targets"]
            for addr in node["unresolved"]:
                self.problems.append(f"{name}: an indirect call the roots file does not resolve (at {addr:#x})")
        elif node["indirect"]:  # by the function's name only
            key = name if name in self.resolve else base_name(name)
            if key in self.resolve:
                callees |= set(self.resolve[key])
            else:
                self.problems.append(f"{name}: an indirect call the roots file does not resolve")
        best, best_path, fp = 0, [], node["fp"]
        for c in sorted(callees):
            d, p, f = self.depth(c, path + (name,))
            fp = fp or f
            if d > best:
                best, best_path = d, p
        result = (self.frame(name) + best, [name] + best_path, fp)
        self.memo[name] = result
        return result


def symbol_sizes(elf, nm):
    """{function: size} of the image's sized code symbols (nm -S)."""
    out = {}
    for line in subprocess.run([nm, "-S", str(elf)], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[2] in "tTwW":
            out[parts[3]] = int(parts[1], 16)
    return out


def trim(funcs, sizes):
    """Each function's instructions within its symbol's size. Bytes after a function and before
    the next symbol, such as string constants Clang places after the code, are not code, though
    objdump disassembles them as the function's: as data they can look like an indirect branch."""
    for name, f in funcs.items():
        size = sizes.get(name)
        if size:
            f["insns"] = [i for i in f["insns"] if i[0] < f["start"] + size]
    return funcs


def analyze(elf, build, roots, objdump, nm, addr2line=None):
    funcs = trim(disassemble(elf, objdump), symbol_sizes(elf, nm))
    su = read_su(build)
    graph = call_graph(funcs)
    frames, sources, problems = {}, {}, []
    for name, f in funcs.items():
        pro = prologue_frame(f["insns"])
        if name in su:
            size, kind = su[name]
            if "dynamic" in kind and "bounded" not in kind:
                problems.append(f"{name}: a dynamic frame ({kind})")
            frames[name], sources[name] = max(size, pro), "su" if size >= pro else "prologue"
        else:
            frames[name], sources[name] = pro, "prologue"
    resolve = {}
    for caller, targets in roots.get("indirect", {}).items():
        if isinstance(targets, dict):  # {"table": name}: read the image's linker table
            resolve[caller] = table_targets(elf, objdump, nm, targets["table"], funcs)
        else:
            resolve[caller] = targets
    tool = addr2line or re.sub(r"objdump$", "addr2line", objdump)
    resolve_indirect(graph, resolve, lambda addrs: inline_chains(elf, tool, addrs))
    b = Bound(graph, frames, resolve)
    report = {"threads": {}, "isr": {}, "problems": problems, "resolved": resolve}
    for thread, spec in roots["threads"].items():
        d, path, fp = b.depth(spec["entry"])
        switch = (HW_FRAME_FP + SW_SAVE + SW_SAVE_FP) if fp else (HW_FRAME + SW_SAVE)
        total = d + switch + ALIGN
        report["threads"][thread] = {
            "entry": spec["entry"], "calls": d, "switch": switch + ALIGN, "bound": total,
            "fp": fp, "size": spec["size"], "path": [{"function": n, "frame": frames.get(n, 0),
                                                       "from": sources.get(n, "?")} for n in path]}
    levels, total, prev_fp = [], 0, False
    for i, level in enumerate(roots["isr"]["levels"]):
        best = None
        for r in level["roots"]:
            d = b.depth(r)
            if best is None or d[0] > best[0]:
                best = d
        frame = 0 if i == 0 else (HW_FRAME_FP if prev_fp else HW_FRAME) + ALIGN
        prev_fp = prev_fp or best[2]
        total += best[0] + frame
        levels.append({"name": level["name"], "roots": level["roots"], "calls": best[0], "frame": frame,
                       "fp": best[2], "path": [{"function": n, "frame": frames.get(n, 0),
                                                "from": sources.get(n, "?")} for n in best[1]]})
    boot, boot_path, _ = b.depth(roots["isr"].get("boot", "main"))
    report["isr"] = {"levels": levels, "nesting": total, "boot": boot,
                     "boot_path": [{"function": n, "frame": frames.get(n, 0)} for n in boot_path],
                     "bound": max(total, boot), "size": roots["isr"]["size"]}
    report["problems"] += sorted(set(b.problems))
    return report


def text(rep):
    lines = ["| Stack | Static bound | Size | Of which: calls | Preemption | FP context |", "|---|---:|---:|---:|---:|---|"]
    for name, t in rep["threads"].items():
        lines.append(f"| {name} | {t['bound']} | {t['size']} | {t['calls']} | {t['switch']} | {'yes' if t['fp'] else 'no'} |")
    i = rep["isr"]
    lines.append(f"| interrupt stack | {i['bound']} | {i['size']} | {i['nesting']} nested, {i['boot']} at boot | | |")
    lines.append("")
    for name, t in rep["threads"].items():
        lines.append(f"{name}: " + " -> ".join(f"{p['function']} ({p['frame']})" for p in t["path"]))
    for lv in i["levels"]:
        lines.append(f"isr level {lv['name']} (+{lv['frame']} frame): " +
                     " -> ".join(f"{p['function']} ({p['frame']})" for p in lv["path"]))
    lines.append("boot: " + " -> ".join(f"{p['function']} ({p['frame']})" for p in i["boot_path"]))
    for p in rep["problems"]:
        lines.append(f"PROBLEM: {p}")
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--elf", required=True)
    ap.add_argument("--build", required=True, help="the build directory, for the .su files")
    ap.add_argument("--roots", required=True, help="the application's roots file (JSON)")
    ap.add_argument("--objdump", default="arm-none-eabi-objdump")
    ap.add_argument("--nm", default="arm-none-eabi-nm")
    ap.add_argument("--addr2line", help="default: the objdump's addr2line")
    ap.add_argument("--json", help="write the report here")
    args = ap.parse_args(argv)
    try:
        roots = json.loads(Path(args.roots).read_text(encoding="utf-8"))
        rep = analyze(args.elf, args.build, roots, args.objdump, args.nm, args.addr2line)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as e:
        print(f"stack_bound: {e}", file=sys.stderr)
        return 2
    print(text(rep))
    if args.json:
        Path(args.json).write_text(json.dumps(rep, indent=2) + "\n", encoding="utf-8")
    over = [n for n, t in rep["threads"].items() if t["bound"] > t["size"]]
    if rep["isr"]["bound"] > rep["isr"]["size"]:
        over.append("interrupt stack")
    for n in over:
        print(f"OVER: {n}", file=sys.stderr)
    return 1 if rep["problems"] or over else 0


if __name__ == "__main__":
    sys.exit(main())
