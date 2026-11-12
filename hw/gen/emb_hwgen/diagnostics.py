# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The generator's findings as data, for tools that read them (`--format json`, HW-002).

Every error and warning the generator raises is a line 'file:line: node.path: message' (HW-002);
the HW-001 check's problems are 'file[:line]: message'. Here each becomes a record with those
parts apart, and a command's outcome one JSON document (hw/schemas/diagnostics.schema.json): what
ran, its status and exit code, its findings, and the files it wrote. A line that does not have the
shape keeps its whole text as the message, so nothing is lost.
"""
import json
import re

from . import VERSION

FORMAT = 1  # the document's own version: a field added keeps it, one changed or removed bumps it

# a Windows drive is part of the file name; a node path has no spaces, and its segments may be
# compatibles (bindings.st,stm32f4-iwdg.class)
_LINE = re.compile(r"^(?P<file>(?:[A-Za-z]:)?[^:\n]+?)(?::(?P<line>\d+))?: "
                   r"(?:(?P<path>\(root\)|\(file\)|[A-Za-z_][\w,+-]*(?:\.[\w,+-]+|\[\d+\])*): )?"
                   r"(?P<message>.+)$", re.S)
_FACT = re.compile(r"^(?P<file>(?:[A-Za-z]:)?[^:\n]+?)(?::(?P<line>\d+))?: (?P<message>.+)$", re.S)


def finding(text, severity, rule, with_path=True):
    """One finding from its line of text; with_path false for the HW-001 check's lines, which name
    no node."""
    m = (_LINE if with_path else _FACT).match(text)
    if not m:
        return _record(severity, rule, None, None, None, text, text)
    return _record(severity, rule, m["file"], m["line"], m.groupdict().get("path"), m["message"], text)


def _record(severity, rule, file, line, path, message, text):
    return {"severity": severity, "rule": rule, "file": file, "line": int(line) if line else None,
            "path": path, "message": message, "text": text}


def stale(file, message, diff=None):
    """A generated file that differs from what the generator writes now (HW-003)."""
    rec = _record("error", "HW-003", file, None, None, message, f"{file}: {message}")
    rec["diff"] = diff
    return rec


def document(command, status, exit_code, findings=(), files=(), boards=None, error=None):
    doc = {"tool": "emb-hwgen", "version": VERSION, "format": FORMAT, "command": command,
           "status": status, "exit_code": exit_code, "diagnostics": list(findings), "files": list(files)}
    if boards is not None:
        doc["boards"] = boards
    if error is not None:
        doc["error"] = error
    return doc


def dumps(doc):
    return json.dumps(doc, indent=2, sort_keys=True) + "\n"
