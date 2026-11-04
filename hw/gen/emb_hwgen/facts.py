# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""HW-001's analysis (A69): hardware facts come only from the descriptions.

- A board described in YAML holds board.yaml and generated/, and nothing written by hand.
- C under soc/ and drivers/ carries no address-like constant (a hexadecimal literal of seven
  or more digits): register bases, memory regions and vector tables come from hw_config.h.
  Register offsets and bit masks stay in the drivers as driver knowledge.
"""
import re
from pathlib import Path

ADDRESS = re.compile(r"\b0[xX][0-9A-Fa-f]{7,}[uUlL]*\b")
BOARD_FILES = {"board.yaml", "generated"}


def check(root):
    root = Path(root)
    problems = []
    for gen in sorted(root.glob("boards/*/*/generated")):
        board = gen.parent
        for p in sorted(board.iterdir()):
            if p.name not in BOARD_FILES and not p.name.startswith("."):
                problems.append(f"{p.relative_to(root).as_posix()}: a board described in YAML has no hand-written "
                                "files; describe it in board.yaml or the SoC description")
    for d in ("soc", "drivers"):
        if not (root / d).is_dir():
            continue  # a board root (A84) may hold boards only
        for p in sorted((root / d).rglob("*")):
            if p.suffix not in (".c", ".h") or not p.is_file():
                continue
            for n, line in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
                m = ADDRESS.search(line)
                if m:
                    problems.append(f"{p.relative_to(root).as_posix()}:{n}: address-like constant {m.group(0)}; "
                                    "take it from hw_config.h (HW-001)")
    return problems
