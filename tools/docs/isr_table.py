#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The table of functions an interrupt handler may call, from the headers' @ctx tags.

    isr_table.py [--check]

Every public function in include/emb/ carries "@ctx" with the contexts it may be called from:
thread, isr, prekernel. This writes the functions whose @ctx names isr, with their header and
their @blocks, between the markers of docs/user/interrupts.md. With --check it writes nothing
and exits 1 when the page is stale (CI lint).
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PAGE = ROOT / "docs" / "user" / "interrupts.md"
BEGIN = "<!-- isr-safe:begin (tools/docs/isr_table.py) -->"
END = "<!-- isr-safe:end -->"


def tags(block):
    clean = re.sub(r"^\s*\* ?", "", block, flags=re.M)
    out, active = {}, None
    for line in clean.splitlines():
        m = re.match(r"^[ \t]*@(ctx|blocks)(?:[ \t]+([^\n]*))?$", line)
        if m:
            active = m[1]
            out[active] = (m[2] or "").strip()
        elif not line.strip() or line.lstrip().startswith("@"):
            active = None
        elif active is not None:
            out[active] += " " + line.strip()
    return out


def isr_safe():
    rows = []
    for header in sorted((ROOT / "include" / "emb").glob("*.h")):
        if header.name in ("arch.h", "compiler.h"):
            continue
        for block in re.findall(r"/\*\*(.*?)\*/", header.read_text(encoding="utf-8"), re.S):
            clean = re.sub(r"^\s*\* ?", "", block, flags=re.M)
            name = re.search(r"\b(emb_[A-Za-z0-9_]+)\(\)", clean)
            t = tags(block)
            if name and "isr" in t.get("ctx", "").split():
                rows.append((name[1], header.name, t.get("blocks", "")))
    return sorted(set(rows))


def render(rows):
    out = [BEGIN, "", f"{len(rows)} functions, from the headers' `@ctx` tags.", "",
           "| Function | Header | Blocks |", "|---|---|---|"]
    out += [f"| `{f}()` | `<emb/{h}>` | {b or 'no'} |" for f, h, b in rows]
    return "\n".join(out + ["", END])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv)
    text = PAGE.read_text(encoding="utf-8")
    if BEGIN not in text or END not in text:
        print(f"isr_table: {PAGE} has no markers", file=sys.stderr)
        return 1
    head, rest = text.split(BEGIN, 1)
    tail = rest.split(END, 1)[1]
    new = head + render(isr_safe()) + tail
    if args.check:
        if new != text:
            print("isr_table: docs/user/interrupts.md is stale: run tools/docs/isr_table.py", file=sys.stderr)
            return 1
        print("isr_table: docs/user/interrupts.md is current")
        return 0
    PAGE.write_text(new, encoding="utf-8")
    print(f"isr_table: wrote {len(isr_safe())} functions to {PAGE.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
