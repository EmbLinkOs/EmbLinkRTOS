# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""Description loading (SPEC-014 §2): YAML with a node-to-line map, unit normalization and
`extends` flattening.

A node path names list items by their `name` field when they have one
(`peripherals.usart2.base`) and by index otherwise (`clocks.prescalers.apb[2]`), so a
path stays the same when a part file overrides a node of its die file.
"""
import hashlib
import os
import re
from pathlib import Path

from . import HwError

_SIZE = re.compile(r"^(\d+)([KMG])$")
_FREQ = re.compile(r"^(\d+)(?:\.(\d+))?(Hz|kHz|MHz|GHz)$")
_SIZE_MUL = {"K": 1 << 10, "M": 1 << 20, "G": 1 << 30}
_FREQ_MUL = {"Hz": 1, "kHz": 10**3, "MHz": 10**6, "GHz": 10**9}

# Top-level keys that describe a file, not the hardware: never inherited through extends.
OWN_KEYS = ("name", "extends", "abstract", "doc", "part", "package", "curated", "imported_from")


def rel(path, root):
    """The path as the generator prints and stamps it: relative to the repository root."""
    try:
        return Path(path).resolve().relative_to(Path(root).resolve()).as_posix()
    except ValueError:
        return Path(path).as_posix()


def inside(path, root):
    try:
        Path(path).resolve().relative_to(Path(root).resolve())
        return True
    except ValueError:
        return False


def stamp(path, root, board_dir):
    """The path as an output records it (HW-003): relative to the repository root, or, for a
    board kept outside it (EMB_BOARD_ROOT, A76), '@board/' and the path from the board's
    directory, so the outputs do not name the machine they were generated on."""
    if inside(path, root):
        return rel(path, root)
    return "@board/" + Path(os.path.relpath(Path(path).resolve(), Path(board_dir).resolve())).as_posix()


def normalize(value):
    """512K -> 524288, 16MHz -> 16000000, 32kHz -> 32000; anything else unchanged."""
    if isinstance(value, str):
        m = _SIZE.match(value)
        if m:
            return int(m.group(1)) * _SIZE_MUL[m.group(2)]
        m = _FREQ.match(value)
        if m:
            whole, frac, unit = m.group(1), m.group(2) or "", m.group(3)
            mul = _FREQ_MUL[unit]
            hz = int(whole) * mul + (int(frac) * mul // 10 ** len(frac) if frac else 0)
            return hz
    return value


def _child(path, key):
    return f"{path}.{key}" if path else str(key)


class Doc:
    """One loaded file: its normalized data and the line of every node path."""

    def __init__(self, path, root, data, lines):
        self.path = Path(path)
        self.name = rel(path, root)
        self.data = data
        self.lines = lines  # node path -> (file name, line)
        self.sha256 = hashlib.sha256(self.path.read_bytes()).hexdigest()


def load(path, root):
    import yaml  # PyYAML; the only third-party dependency of the build

    path = Path(path)
    name = rel(path, root)
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as e:
        raise HwError([f"{name}:1: (file): cannot read: {e.strerror}"])
    loader = yaml.SafeLoader(text)
    errors = []
    lines = {}
    try:
        node = loader.get_single_node()
    except yaml.YAMLError as e:
        mark = getattr(e, "problem_mark", None)
        line = mark.line + 1 if mark is not None else 1
        raise HwError([f"{name}:{line}: (file): not valid YAML: {getattr(e, 'problem', e)}"])

    def build(n, p):
        lines[p] = (name, n.start_mark.line + 1)
        if isinstance(n, yaml.MappingNode):
            out = {}
            for k, v in n.value:
                key = loader.construct_object(k, deep=True)
                if not isinstance(key, (str, int)):
                    errors.append(f"{name}:{k.start_mark.line + 1}: {p or '(root)'}: key {key!r} is not a name")
                    continue
                key = str(key)
                if key in out:
                    errors.append(f"{name}:{k.start_mark.line + 1}: {_child(p, key)}: duplicate key")
                    continue
                out[key] = build(v, _child(p, key))
            return out
        if isinstance(n, yaml.SequenceNode):
            items = []
            for i, v in enumerate(n.value):
                label = None
                if isinstance(v, yaml.MappingNode):
                    for k, kv in v.value:
                        if k.value == "name" and isinstance(kv, yaml.ScalarNode):
                            label = kv.value
                items.append((label, v))
                lines[f"{p}[{i}]"] = (name, v.start_mark.line + 1)  # by index too: a duplicate name's own line
            return [build(v, _child(p, label) if label else f"{p}[{i}]") for i, (label, v) in enumerate(items)]
        return normalize(loader.construct_object(n, deep=True))

    data = build(node, "") if node is not None else {}
    loader.dispose()
    if errors:
        raise HwError(errors)
    if not isinstance(data, dict):
        raise HwError([f"{name}:1: (root): a description is a mapping"])
    return Doc(path, root, data, lines)


def _merge(base, over):
    """Deep merge: mappings by key, lists of named mappings by name, anything else replaced."""
    if isinstance(base, dict) and isinstance(over, dict):
        out = dict(base)
        for k, v in over.items():
            out[k] = _merge(base[k], v) if k in base else v
        return out
    if isinstance(base, list) and isinstance(over, list) and _named(base) and _named(over):
        out = list(base)
        index = {item["name"]: i for i, item in enumerate(out)}
        for item in over:
            if item["name"] in index:
                out[index[item["name"]]] = _merge(out[index[item["name"]]], item)
            else:
                out.append(item)
        return out
    return over


def _named(items):
    return bool(items) and all(isinstance(i, dict) and "name" in i for i in items)


class Model:
    """A flattened description: the leaf file merged over everything it extends."""

    def __init__(self, data, lines, docs):
        self.data = data
        self.lines = lines
        self.docs = docs  # leaf last

    @property
    def leaf(self):
        return self.docs[-1]

    def at(self, path):
        """'file:line: path' for a node, or its closest ancestor that has a line."""
        p = path
        while True:
            if p in self.lines:
                f, line = self.lines[p]
                return f"{f}:{line}: {path or '(root)'}"
            if not p:
                return f"{self.leaf.name}:1: {path or '(root)'}"
            p = p.rsplit(".", 1)[0] if "." in p else ""


def flatten(path, root, seen=None):
    """Load a file and everything its `extends` chain names, base first."""
    path = Path(path).resolve()
    seen = list(seen or [])
    if path in seen:
        chain = " -> ".join(rel(p, root) for p in seen + [path])
        raise HwError([f"{rel(path, root)}:1: extends: cycle: {chain}"])
    doc = load(path, root)
    ext = doc.data.get("extends")
    if ext is None:
        return Model(doc.data, dict(doc.lines), [doc])
    if not isinstance(ext, str):
        raise HwError([f"{doc.name}:{doc.lines.get('extends', (0, 1))[1]}: extends: not a file name"])
    target = path.parent / ext
    if not target.is_file():
        raise HwError([f"{doc.name}:{doc.lines['extends'][1]}: extends: no such file {rel(target, root)}"])
    base = flatten(target, root, seen + [path])
    inherited = {k: v for k, v in base.data.items() if k not in OWN_KEYS}
    data = _merge(inherited, doc.data)
    lines = {k: v for k, v in base.lines.items() if k.split(".", 1)[0].split("[", 1)[0] not in OWN_KEYS}
    lines.update(doc.lines)
    return Model(data, lines, base.docs + [doc])
