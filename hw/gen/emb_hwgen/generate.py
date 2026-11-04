# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Resolve a board and render every output: the generator's one entry point."""
from pathlib import Path

from . import emit, load, model


def generate(board_dir, root):
    """({file name: text}, [warning]) for a board directory; raises HwError on a description error."""
    resolved = model.resolve(board_dir, root)
    resolved["board_dir"] = load.rel(Path(board_dir), root)
    return emit.render(resolved), resolved["warnings"]
