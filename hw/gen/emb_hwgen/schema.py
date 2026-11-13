# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""JSON Schema 2020-12 validation for the keyword subset hw/schemas/ uses (A67).

Supported: type, const, enum, pattern, minimum, maximum, minItems, maxItems, required,
properties, additionalProperties, items, oneOf, $ref to #/$defs. Any other keyword in a
schema is refused, so the schemas cannot silently rely on one this validator ignores; CI
checks the same descriptions with the reference implementation (jsonschema).
"""
import json
import re
from pathlib import Path

SCHEMA_DIR = Path(__file__).resolve().parent.parent.parent / "schemas"

_ANNOTATIONS = {"$schema", "$id", "title", "description", "$defs"}
_SUPPORTED = {
    "type", "const", "enum", "pattern", "minimum", "maximum", "minItems", "maxItems",
    "required", "properties", "additionalProperties", "items", "oneOf", "$ref",
} | _ANNOTATIONS


def load_schema(kind):
    with open(SCHEMA_DIR / f"{kind}.schema.json", encoding="utf-8") as f:
        schema = json.load(f)
    _check_keywords(schema, f"{kind}.schema.json")
    return schema


def _check_keywords(s, where):
    if isinstance(s, dict):
        for k, v in s.items():
            if k not in _SUPPORTED:
                raise ValueError(f"{where}: keyword {k!r} is outside the supported subset")
            if k in ("properties", "$defs"):
                for name, sub in v.items():
                    _check_keywords(sub, f"{where}/{k}/{name}")
            elif k in ("items", "additionalProperties"):
                _check_keywords(v, f"{where}/{k}")
            elif k == "oneOf":
                for i, sub in enumerate(v):
                    _check_keywords(sub, f"{where}/oneOf/{i}")


def _type_ok(value, t):
    if t == "object":
        return isinstance(value, dict)
    if t == "array":
        return isinstance(value, list)
    if t == "string":
        return isinstance(value, str)
    if t == "boolean":
        return isinstance(value, bool)
    if t == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if t == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if t == "null":
        return value is None
    raise ValueError(f"type {t!r}")


def _describe(value):
    if isinstance(value, bool):
        return "a boolean"
    if isinstance(value, int):
        return f"integer {value}"
    if isinstance(value, str):
        return f"string {value!r}"
    if isinstance(value, list):
        return "a list"
    if isinstance(value, dict):
        return "a mapping"
    return repr(value)


def _child(path, key):
    return f"{path}.{key}" if path else str(key)


def validate(value, schema, root=None, path=""):
    """[(node path, message)]; empty when the value is valid."""
    root = root if root is not None else schema
    if "$ref" in schema:
        ref = schema["$ref"]
        if not ref.startswith("#/$defs/"):
            raise ValueError(f"unsupported $ref {ref}")
        return validate(value, root["$defs"][ref[len("#/$defs/"):]], root, path)
    errs = []
    if "type" in schema:
        types = schema["type"] if isinstance(schema["type"], list) else [schema["type"]]
        if not any(_type_ok(value, t) for t in types):
            return [(path, f"expected {' or '.join(types)}, found {_describe(value)}")]
    if "const" in schema and not _equal(value, schema["const"]):
        errs.append((path, f"must be {schema['const']!r}, found {_describe(value)}"))
    if "enum" in schema and not any(_equal(value, e) for e in schema["enum"]):
        errs.append((path, f"must be one of {', '.join(map(str, schema['enum']))}; found {_describe(value)}"))
    if "pattern" in schema and isinstance(value, str) and not re.search(schema["pattern"], value):
        errs.append((path, f"{value!r} does not match {schema['pattern']}"))
    if _type_ok(value, "number"):
        if "minimum" in schema and value < schema["minimum"]:
            errs.append((path, f"{value} is below the minimum {schema['minimum']}"))
        if "maximum" in schema and value > schema["maximum"]:
            errs.append((path, f"{value} is above the maximum {schema['maximum']}"))
    if isinstance(value, list):
        if "minItems" in schema and len(value) < schema["minItems"]:
            errs.append((path, f"needs at least {schema['minItems']} item(s), has {len(value)}"))
        if "maxItems" in schema and len(value) > schema["maxItems"]:
            errs.append((path, f"allows at most {schema['maxItems']} item(s), has {len(value)}"))
        if "items" in schema:
            for i, item in enumerate(value):
                label = item.get("name") if isinstance(item, dict) and isinstance(item.get("name"), str) else None
                errs += validate(item, schema["items"], root, _child(path, label) if label else f"{path}[{i}]")
    if isinstance(value, dict):
        for req in schema.get("required", []):
            if req not in value:
                errs.append((path, f"missing required key {req!r}"))
        props = schema.get("properties", {})
        extra = schema.get("additionalProperties", True)
        for k, v in value.items():
            if k in props:
                errs += validate(v, props[k], root, _child(path, k))
            elif extra is False:
                errs.append((_child(path, k), "unknown key"))
            elif isinstance(extra, dict):
                errs += validate(v, extra, root, _child(path, k))
    if "oneOf" in schema:
        matches = sum(1 for sub in schema["oneOf"] if not validate(value, sub, root, path))
        if matches != 1:
            errs.append((path, f"{_describe(value)} matches {matches} of the allowed forms, not exactly one"))
    return errs


def _equal(a, b):
    # JSON equality: true is not 1
    if isinstance(a, bool) or isinstance(b, bool):
        return isinstance(a, bool) and isinstance(b, bool) and a == b
    return a == b
