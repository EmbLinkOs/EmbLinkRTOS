# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""Resolve a board and render every output: the generator's one entry point."""
import os
from pathlib import Path

from . import HwError, emit, emit_system, load, model, system


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


def generate_system(system_path, root):
    """({board name: {file name: text}}, [warning]) for a system description (A125): on each of
    its boards, that board's outputs with the system's device settings, and the system's own,
    written to src/config/<board>/ beside it (A130). Every board's errors are reported together."""
    out, warnings, errors = {}, [], []
    for ref in system.boards(system_path, root):
        try:
            resolved = system.resolve(system_path, root, ref)
        except HwError as e:
            errors += e.errors
            continue
        out[resolved["board"]["name"]] = emit.render(resolved)
        warnings += [w for w in resolved["warnings"] if w not in warnings]
        if APP not in out and resolved["system"]["scaffold"]:
            # the application's own files, for every board alike: from the first board's resolution
            out[APP] = {name: emit._file_header(name.rsplit("/", 1)[-1], text)
                        for name, text in emit_system.app_files(resolved, _rtos_cmake(system_path, root)).items()}
    if errors:
        raise HwError(errors)
    return out, warnings


# The key generate_system() gives the application's own files under (A126): not a board's name,
# which starts with a letter.
APP = "_app"


def _rtos_cmake(system_path, root):
    """The RTOS's cmake/ from the application: relative in the tree, else from $EMBLINKRTOS (HW-013)."""
    if load.inside(system_path, root):
        rel = os.path.relpath(Path(root).resolve() / "cmake", Path(system_path).resolve().parent)
        return "${CMAKE_CURRENT_LIST_DIR}/" + Path(rel).as_posix()
    return "$ENV{EMBLINKRTOS}/cmake"
