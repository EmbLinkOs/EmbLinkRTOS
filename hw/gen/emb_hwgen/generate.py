# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Resolve a board and render every output: the generator's one entry point."""
from pathlib import Path

from . import emit, load, model


def generate(board_dir, root):
    """({file name: text}, [warning]) for a board directory; raises HwError on a description error."""
    resolved = model.resolve(board_dir, root)
    # the regeneration hint: the board's path in the tree, or a placeholder for a board outside it
    inside = load.inside(board_dir, root)
    resolved["board_dir"] = load.rel(Path(board_dir), root) if inside else f"<path to>/{Path(board_dir).resolve().name}"
    warnings = resolved["warnings"]
    if not inside:
        # the outputs hold no path of the machine they were generated on (HW-013): a board
        # outside the tree is '@board/' in them, as in its input stamps
        prefixes = sorted({Path(board_dir).resolve().as_posix() + "/", Path(board_dir).as_posix().rstrip("/") + "/"},
                          key=len, reverse=True)

        def portable(w):
            for prefix in prefixes:
                w = w.replace(prefix, "@board/")
            return w
        resolved["warnings"] = [portable(w) for w in warnings]
    return emit.render(resolved), warnings
