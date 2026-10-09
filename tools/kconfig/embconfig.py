#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""embconfig: the EmbLinkRTOS configuration tool (05 §2.1, ADR-005, BLD-007).

Reads a Kconfig tree (the subset documented in tools/kconfig/README.md), an
architecture manifest (SPEC-011 §2), and an ordered list of configuration
fragments, resolves every symbol, validates dependencies and ranges, and emits:

  emb/config.h   every symbol as a macro (bools as 0 or 1, CODING-STANDARD CS-10.5)
  config.cmake   set(CONFIG_EMB_X ...) for the build system
  config.json    for the generator and the tools
  .config        the resolved configuration in fragment syntax

A configuration that sets a symbol whose dependencies are unmet, or that is
refused by a `require` statement, fails with a message naming the symbols and,
for manifest-derived symbols, the manifest line (SPEC-011 §2).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from dataclasses import dataclass, field
from typing import Optional

CONFIG_VERSION = 1

# --------------------------------------------------------------------------- errors


class ConfigError(Exception):
    pass


# --------------------------------------------------------------------------- expressions

_TOKEN_RE = re.compile(
    r"\s*(?:(\|\||&&|!=|<=|>=|[=<>!()])|(\"(?:[^\"\\]|\\.)*\")|([A-Za-z0-9_\-]+))"
)


class Expr:
    """Expression tree: tuples ('sym', name) ('const', value) ('not', e)
    ('and', a, b) ('or', a, b) (op, a, b) for comparisons."""


def tokenize(text: str) -> list[tuple[str, str]]:
    pos = 0
    out: list[tuple[str, str]] = []
    text = text.strip()
    while pos < len(text):
        m = _TOKEN_RE.match(text, pos)
        if not m or m.end() == pos:
            raise ConfigError(f"bad expression near: {text[pos:]!r}")
        pos = m.end()
        if m.group(1):
            out.append(("op", m.group(1)))
        elif m.group(2):
            out.append(("str", m.group(2)[1:-1]))
        else:
            out.append(("word", m.group(3)))
    return out


def parse_expr(text: str):
    toks = tokenize(text)
    pos = 0

    def peek():
        return toks[pos] if pos < len(toks) else None

    def take():
        nonlocal pos
        t = toks[pos]
        pos += 1
        return t

    def p_or():
        a = p_and()
        while peek() == ("op", "||"):
            take()
            a = ("or", a, p_and())
        return a

    def p_and():
        a = p_not()
        while peek() == ("op", "&&"):
            take()
            a = ("and", a, p_not())
        return a

    def p_not():
        if peek() == ("op", "!"):
            take()
            return ("not", p_not())
        return p_cmp()

    def p_cmp():
        a = p_atom()
        t = peek()
        if t and t[0] == "op" and t[1] in ("=", "!=", "<", ">", "<=", ">="):
            take()
            return (t[1], a, p_atom())
        return a

    def p_atom():
        t = take()
        if t == ("op", "("):
            e = p_or()
            if take() != ("op", ")"):
                raise ConfigError(f"missing ')' in expression: {text!r}")
            return e
        if t[0] == "str":
            return ("const", t[1])
        if t[0] == "word":
            w = t[1]
            if w in ("y", "n"):
                return ("const", w)
            if re.fullmatch(r"-?(0x[0-9a-fA-F]+|\d+)", w):
                return ("const", w)
            return ("sym", w)
        raise ConfigError(f"unexpected token {t[1]!r} in expression: {text!r}")

    e = p_or()
    if pos != len(toks):
        raise ConfigError(f"trailing tokens in expression: {text!r}")
    return e


def expr_symbols(e, out: set[str]):
    if e[0] == "sym":
        out.add(e[1])
    elif e[0] in ("not",):
        expr_symbols(e[1], out)
    elif e[0] != "const":
        expr_symbols(e[1], out)
        expr_symbols(e[2], out)


def expr_text(e) -> str:
    k = e[0]
    if k == "sym":
        return e[1]
    if k == "const":
        return e[1] if e[1] in ("y", "n") or re.fullmatch(r"-?(0x[0-9a-fA-F]+|\d+)", e[1]) else f'"{e[1]}"'
    if k == "not":
        return "!" + expr_text(e[1])
    if k in ("and", "or"):
        op = "&&" if k == "and" else "||"
        return f"({expr_text(e[1])} {op} {expr_text(e[2])})"
    return f"{expr_text(e[1])} {k} {expr_text(e[2])}"


# --------------------------------------------------------------------------- model


@dataclass
class Default:
    value: str
    cond: Optional[tuple]


@dataclass
class Range:
    lo: str
    hi: str
    cond: Optional[tuple]


@dataclass
class Symbol:
    name: str
    kind: str = ""  # bool | int | hex | string
    prompt: Optional[str] = None
    defaults: list[Default] = field(default_factory=list)
    ranges: list[Range] = field(default_factory=list)
    depends: list[tuple] = field(default_factory=list)
    selects: list[tuple[str, Optional[tuple]]] = field(default_factory=list)
    help: str = ""
    choice: Optional["Choice"] = None
    origin: str = ""  # "file:line" for Kconfig symbols, manifest line for derived ones
    user: Optional[str] = None
    user_origin: str = ""
    selected_by: list[str] = field(default_factory=list)
    value: Optional[str] = None
    _busy: bool = False


@dataclass
class Choice:
    name: str
    prompt: str = ""
    members: list[Symbol] = field(default_factory=list)
    defaults: list[Default] = field(default_factory=list)
    depends: list[tuple] = field(default_factory=list)
    origin: str = ""


@dataclass
class Require:
    cond: tuple
    message: str
    origin: str


class Tree:
    def __init__(self):
        self.symbols: dict[str, Symbol] = {}
        self.order: list[str] = []
        self.choices: list[Choice] = []
        self.requires: list[Require] = []
        self.mainmenu = "EmbLinkRTOS configuration"

    def sym(self, name: str, origin: str = "") -> Symbol:
        s = self.symbols.get(name)
        if s is None:
            s = Symbol(name, origin=origin)
            self.symbols[name] = s
            self.order.append(name)
        return s


# --------------------------------------------------------------------------- parser


class Parser:
    def __init__(self, tree: Tree, variables: dict[str, str], root: str):
        self.tree = tree
        self.variables = variables
        self.root = root

    def parse_file(self, path: str, cond_stack: list[tuple]):
        with open(path, encoding="utf-8") as f:
            lines = f.read().split("\n")
        self._parse_lines(lines, path, cond_stack)

    def _subst(self, text: str) -> str:
        def rep(m):
            name = m.group(1)
            if name not in self.variables:
                raise ConfigError(f"undefined variable $({name})")
            return self.variables[name]

        return re.sub(r"\$\(([A-Za-z0-9_]+)\)", rep, text)

    def _parse_lines(self, lines: list[str], path: str, cond_stack: list[tuple]):
        i = 0
        n = len(lines)
        current: Optional[Symbol] = None
        choice: Optional[Choice] = None
        menu_depth = 0
        if_stack: list[tuple] = list(cond_stack)
        base_if_depth = len(if_stack)

        def here():
            return f"{os.path.relpath(path, self.root)}:{i + 1}"

        def conds() -> list[tuple]:
            return list(if_stack)

        while i < n:
            raw = lines[i]
            line = raw.split("#", 1)[0].rstrip() if not raw.lstrip().startswith("#") else ""
            # a '#' inside a quoted string is not a comment
            if '"' in raw and "#" in raw:
                line = raw.rstrip()
                if line.lstrip().startswith("#"):
                    line = ""
                else:
                    # strip trailing comment outside quotes
                    out = []
                    inq = False
                    for ch in line:
                        if ch == '"':
                            inq = not inq
                        if ch == "#" and not inq:
                            break
                        out.append(ch)
                    line = "".join(out).rstrip()
            stripped = line.strip()
            if not stripped:
                i += 1
                continue
            word, _, rest = stripped.partition(" ")
            rest = rest.strip()
            if word == "mainmenu":
                self.tree.mainmenu = self._unquote(rest)
            elif word == "menu":
                menu_depth += 1
            elif word == "endmenu":
                menu_depth -= 1
                current = None
            elif word == "source":
                rel = self._unquote(self._subst(rest))
                self.parse_file(os.path.join(os.path.dirname(path), rel) if not os.path.isabs(rel) else rel,
                                conds())
                current = None
            elif word == "if":
                if_stack.append(parse_expr(rest))
                current = None
            elif word == "endif":
                if len(if_stack) <= base_if_depth:
                    raise ConfigError(f"{here()}: endif without if")
                if_stack.pop()
                current = None
            elif word == "config":
                name = rest.strip()
                if not re.fullmatch(r"[A-Z0-9_]+", name):
                    raise ConfigError(f"{here()}: bad symbol name {name!r}")
                current = self.tree.sym(name, here())
                if not current.origin:
                    current.origin = here()
                current.depends.extend(conds())
                if choice is not None:
                    current.choice = choice
                    choice.members.append(current)
                    current.depends.extend(choice.depends)
            elif word == "choice":
                choice = Choice(name=rest.strip() or f"choice@{here()}", origin=here())
                choice.depends.extend(conds())
                self.tree.choices.append(choice)
                current = None
            elif word == "endchoice":
                choice = None
                current = None
            elif word == "require":
                m = re.fullmatch(r"(.*?)\s+(\"(?:[^\"\\]|\\.)*\")", rest)
                if not m:
                    raise ConfigError(f"{here()}: require needs <expr> \"message\"")
                cond = parse_expr(m.group(1))
                for c in reversed(conds()):  # a require inside `if` applies only there
                    cond = ("or", ("not", c), cond)
                self.tree.requires.append(Require(cond, self._unquote(m.group(2)), here()))
            elif word in ("bool", "int", "hex", "string"):
                target = current
                if target is None:
                    raise ConfigError(f"{here()}: {word} outside config")
                target.kind = word
                if rest:
                    target.prompt = self._unquote(rest)
            elif word == "prompt":
                m = re.fullmatch(r"(\"(?:[^\"\\]|\\.)*\")(?:\s+if\s+(.*))?", rest)
                if not m:
                    raise ConfigError(f"{here()}: bad prompt")
                if current is not None:
                    current.prompt = self._unquote(m.group(1))
                elif choice is not None:
                    choice.prompt = self._unquote(m.group(1))
                else:
                    raise ConfigError(f"{here()}: prompt outside config or choice")
            elif word == "default":
                m = re.fullmatch(r"(\"(?:[^\"\\]|\\.)*\"|[A-Za-z0-9_\-]+)(?:\s+if\s+(.*))?", rest)
                if not m:
                    raise ConfigError(f"{here()}: bad default")
                val = m.group(1)
                if val.startswith('"'):
                    val = self._unquote(val)
                cond = parse_expr(m.group(2)) if m.group(2) else None
                if current is not None:
                    current.defaults.append(Default(val, cond))
                elif choice is not None:
                    choice.defaults.append(Default(val, cond))
                else:
                    raise ConfigError(f"{here()}: default outside config or choice")
            elif word == "depends":
                m = re.fullmatch(r"on\s+(.*)", rest)
                if not m:
                    raise ConfigError(f"{here()}: bad depends")
                e = parse_expr(m.group(1))
                if current is not None:
                    current.depends.append(e)
                elif choice is not None:
                    choice.depends.append(e)
                else:
                    raise ConfigError(f"{here()}: depends outside config or choice")
            elif word == "range":
                m = re.fullmatch(r"([A-Za-z0-9_\-]+)\s+([A-Za-z0-9_\-]+)(?:\s+if\s+(.*))?", rest)
                if not m or current is None:
                    raise ConfigError(f"{here()}: bad range")
                current.ranges.append(Range(m.group(1), m.group(2), parse_expr(m.group(3)) if m.group(3) else None))
            elif word == "select":
                m = re.fullmatch(r"([A-Z0-9_]+)(?:\s+if\s+(.*))?", rest)
                if not m or current is None:
                    raise ConfigError(f"{here()}: bad select")
                current.selects.append((m.group(1), parse_expr(m.group(2)) if m.group(2) else None))
            elif word == "help":
                # help text: the following lines indented more than the 'help' keyword
                indent = len(raw) - len(raw.lstrip())
                i += 1
                text = []
                while i < n:
                    l2 = lines[i]
                    if l2.strip() == "":
                        text.append("")
                        i += 1
                        continue
                    if len(l2) - len(l2.lstrip()) <= indent:
                        break
                    text.append(l2.strip())
                    i += 1
                if current is not None:
                    current.help = "\n".join(text).strip()
                continue
            else:
                raise ConfigError(f"{here()}: unknown keyword {word!r}")
            i += 1
        if len(if_stack) != base_if_depth:
            raise ConfigError(f"{path}: unterminated if")
        if menu_depth != 0:
            raise ConfigError(f"{path}: unterminated menu")

    @staticmethod
    def _unquote(s: str) -> str:
        s = s.strip()
        if len(s) >= 2 and s[0] == '"' and s[-1] == '"':
            return s[1:-1].replace('\\"', '"').replace("\\\\", "\\")
        return s


# --------------------------------------------------------------------------- resolver


class Resolver:
    def __init__(self, tree: Tree):
        self.tree = tree
        self.errors: list[str] = []

    # -- expression evaluation ------------------------------------------------
    def ev(self, e) -> str:
        """Evaluate to 'y'/'n' for logical results, or to the raw value for atoms."""
        k = e[0]
        if k == "const":
            return e[1]
        if k == "sym":
            s = self.tree.symbols.get(e[1])
            if s is None:
                # an undefined symbol reads as n / empty, as in Kconfig
                return "n"
            return self.value(s)
        if k == "not":
            return "n" if self.truth(e[1]) else "y"
        if k == "and":
            return "y" if (self.truth(e[1]) and self.truth(e[2])) else "n"
        if k == "or":
            return "y" if (self.truth(e[1]) or self.truth(e[2])) else "n"
        a, b = self.ev(e[1]), self.ev(e[2])
        if k == "=":
            return "y" if self._eq(a, b) else "n"
        if k == "!=":
            return "n" if self._eq(a, b) else "y"
        ia, ib = self._int(a), self._int(b)
        if ia is None or ib is None:
            raise ConfigError(f"non-numeric comparison: {expr_text(e)}")
        return "y" if {"<": ia < ib, ">": ia > ib, "<=": ia <= ib, ">=": ia >= ib}[k] else "n"

    @staticmethod
    def _int(v: str) -> Optional[int]:
        try:
            return int(v, 0)
        except (ValueError, TypeError):
            return None

    def _eq(self, a: str, b: str) -> bool:
        ia, ib = self._int(a), self._int(b)
        if ia is not None and ib is not None:
            return ia == ib
        return a == b

    def truth(self, e) -> bool:
        v = self.ev(e)
        if v == "y":
            return True
        if v == "n" or v == "":
            return False
        iv = self._int(v)
        return bool(iv) if iv is not None else True

    # -- symbol values ---------------------------------------------------------
    def deps_ok(self, s: Symbol) -> bool:
        return all(self.truth(d) for d in s.depends)

    def type_default(self, s: Symbol) -> str:
        return {"bool": "n", "int": "0", "hex": "0x0", "string": ""}[s.kind]

    def value(self, s: Symbol) -> str:
        if s.value is not None:
            return s.value
        if s._busy:
            raise ConfigError(f"dependency cycle through {s.name} ({s.origin})")
        s._busy = True
        try:
            v = self._compute(s)
        finally:
            s._busy = False
        s.value = v
        return v

    def _compute(self, s: Symbol) -> str:
        if not s.kind:
            raise ConfigError(f"symbol {s.name} is referenced but never given a type")
        if not self.deps_ok(s):
            if s.user is not None and s.user != self.type_default(s):
                self.errors.append(
                    f"{s.user_origin}: CONFIG_{s.name}={s.user} but its dependency "
                    f"{' && '.join(expr_text(d) for d in s.depends)} is not satisfied"
                    + self._explain_deps(s))
            if s.selected_by:
                self.errors.append(
                    f"CONFIG_{s.name} is selected by {', '.join('CONFIG_' + x for x in s.selected_by)} "
                    f"but its dependency {' && '.join(expr_text(d) for d in s.depends)} is not satisfied"
                    + self._explain_deps(s))
            return self.type_default(s)
        if s.choice is not None:
            return self._choice_value(s)
        if s.kind == "bool" and s.selected_by:
            if s.user == "n":
                self.errors.append(
                    f"{s.user_origin}: CONFIG_{s.name}=n but it is selected by "
                    f"{', '.join('CONFIG_' + x for x in s.selected_by)}")
            return "y"
        if s.user is not None:
            v = self._coerce(s, s.user, s.user_origin)
        else:
            v = None
            for d in s.defaults:
                if d.cond is None or self.truth(d.cond):
                    v = self._coerce(s, self._deref(d.value, s), s.origin)
                    break
            if v is None:
                v = self.type_default(s)
        self._check_range(s, v, s.user_origin if s.user is not None else s.origin)
        return v

    def _deref(self, value: str, s: Symbol) -> str:
        """A default may name another symbol of the same kind."""
        if value in self.tree.symbols and value != s.name and not (s.kind == "bool" and value in ("y", "n")):
            return self.value(self.tree.symbols[value])
        return value

    def _coerce(self, s: Symbol, v: str, origin: str) -> str:
        if s.kind == "bool":
            if v in ("y", "n"):
                return v
            if v in ("1", "true"):
                return "y"
            if v in ("0", "false"):
                return "n"
            self.errors.append(f"{origin}: CONFIG_{s.name} is bool, got {v!r}")
            return "n"
        if s.kind in ("int", "hex"):
            iv = self._int(v)
            if iv is None:
                self.errors.append(f"{origin}: CONFIG_{s.name} is {s.kind}, got {v!r}")
                return "0"
            return hex(iv) if s.kind == "hex" else str(iv)
        return v

    def _check_range(self, s: Symbol, v: str, origin: str):
        if s.kind not in ("int", "hex"):
            return
        iv = self._int(v)
        for r in s.ranges:
            if r.cond is None or self.truth(r.cond):
                lo = self._int(self._deref(r.lo, s))
                hi = self._int(self._deref(r.hi, s))
                if lo is None or hi is None:
                    raise ConfigError(f"{s.origin}: non-numeric range for {s.name}")
                if not (lo <= iv <= hi):
                    self.errors.append(f"{origin}: CONFIG_{s.name}={v} is outside its range {lo}..{hi}")
                break

    def _choice_value(self, s: Symbol) -> str:
        ch = s.choice
        assert ch is not None
        chosen = None
        users = [m for m in ch.members if m.user == "y" and self.deps_ok(m)]
        if len(users) > 1:
            self.errors.append(
                f"choice {ch.prompt or ch.name}: more than one member set: "
                + ", ".join("CONFIG_" + m.name for m in users))
        if users:
            chosen = users[0]
        else:
            for d in ch.defaults:
                if d.cond is None or self.truth(d.cond):
                    cand = self.tree.symbols.get(d.value)
                    if cand is not None and cand.choice is ch and self.deps_ok(cand):
                        chosen = cand
                        break
            if chosen is None:
                for m in ch.members:
                    if self.deps_ok(m):
                        chosen = m
                        break
        return "y" if chosen is s else "n"

    def _explain_deps(self, s: Symbol) -> str:
        names: set[str] = set()
        for d in s.depends:
            expr_symbols(d, names)
        parts = []
        for nme in sorted(names):
            d = self.tree.symbols.get(nme)
            if d is None:
                parts.append(f"CONFIG_{nme} is undefined")
                continue
            parts.append(f"CONFIG_{nme}={self.value(d)}" + (f" ({d.origin})" if d.origin else ""))
        return (" [" + "; ".join(parts) + "]") if parts else ""

    # -- the whole run ----------------------------------------------------------
    def resolve(self):
        # selects first: a select is a forced y when the selecting symbol is y
        for name in self.tree.order:
            s = self.tree.symbols[name]
            if s.kind == "bool" and s.selects:
                if self.value(s) == "y":
                    for target, cond in s.selects:
                        if cond is None or self.truth(cond):
                            t = self.tree.symbols.get(target)
                            if t is None:
                                self.errors.append(f"{s.origin}: select of undefined symbol {target}")
                                continue
                            t.selected_by.append(s.name)
                            t.value = None  # recompute with the select applied
        for name in self.tree.order:
            self.value(self.tree.symbols[name])
        for r in self.tree.requires:
            if not self.truth(r.cond):
                self.errors.append(f"{r.origin}: {r.message} (condition: {expr_text(r.cond)})")
        if self.errors:
            raise ConfigError("\n".join(self.errors))


# --------------------------------------------------------------------------- manifest

MANIFEST_FEATURE_BOOLS = [
    "irq_nesting", "zero_latency_irqs", "cas", "interrupt_stack", "hw_stack_limit",
    "fpu_lazy", "cycle_counter", "tickless", "smp", "fault_entry",
]


def load_manifest(path: str, variant: Optional[str], host_word_bits: int) -> dict[str, tuple[str, str, str]]:
    """Return {symbol: (kind, value, origin)} derived from arch/<arch>/arch.yaml."""
    import yaml  # PyYAML; the only third-party dependency of the build

    with open(path, encoding="utf-8") as f:
        text = f.read()
    doc = yaml.safe_load(text)
    lines = text.split("\n")

    def origin(key: str) -> str:
        for idx, l in enumerate(lines):
            if re.match(rf"\s*{re.escape(key)}\s*:", l):
                return f"{path}:{idx + 1}"
        return path

    arch = doc["arch"]
    variants = doc.get("variants", [])
    if variant is None:
        variant = variants[0] if variants else ""
    elif variants and variant not in variants:
        raise ConfigError(f"{origin('variants')}: variant {variant!r} is not one of {variants}")
    out: dict[str, tuple[str, str, str]] = {}
    out["EMB_ARCH"] = ("string", arch, origin("arch"))
    out["EMB_ARCH_" + arch.upper()] = ("bool", "y", origin("arch"))
    out["EMB_ARCH_VARIANT"] = ("string", variant, origin("variants"))
    wb = doc.get("word_bits", 32)
    if wb == "host":
        wb = host_word_bits
    out["EMB_ARCH_WORD_BITS"] = ("int", str(wb), origin("word_bits"))
    stack = doc.get("stack", {})
    out["EMB_ARCH_STACK_ALIGN"] = ("int", str(stack.get("align", 8)), origin("stack"))
    out["EMB_ARCH_STACK_MIN"] = ("int", str(stack.get("min_thread", 0)), origin("stack"))
    out["EMB_ARCH_CONTEXT_FRAME"] = ("int", str(stack.get("context_frame", 0)), origin("stack"))
    feats = doc.get("features", {})
    for name in MANIFEST_FEATURE_BOOLS:
        v = feats.get(name, False)
        if isinstance(v, list):
            on = variant in v
        else:
            on = bool(v)
        out["EMB_ARCH_HAS_" + name.upper()] = ("bool", "y" if on else "n", origin(name))
    out["EMB_ARCH_MPU"] = ("string", str(feats.get("mpu", "none")), origin("mpu"))
    out["EMB_ARCH_SYSCALL_TRAP"] = ("string", str(feats.get("syscall_trap", "none")), origin("syscall_trap"))
    tcb = doc.get("tcb_extension_bytes", 0)
    if tcb != "host":
        out["EMB_ARCH_TCB_EXTENSION"] = ("int", str(tcb), origin("tcb_extension_bytes"))
    return out


# --------------------------------------------------------------------------- fragments

_FRAG_RE = re.compile(r"^\s*CONFIG_([A-Z0-9_]+)\s*=\s*(.*?)\s*$")
_FRAG_NOT_SET_RE = re.compile(r"^\s*#\s*CONFIG_([A-Z0-9_]+) is not set\s*$")


def apply_fragment(tree: Tree, path: str):
    with open(path, encoding="utf-8") as f:
        for idx, line in enumerate(f, 1):
            m = _FRAG_NOT_SET_RE.match(line)
            if m:
                _set_user(tree, m.group(1), "n", f"{path}:{idx}")
                continue
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            m = _FRAG_RE.match(line)
            if not m:
                raise ConfigError(f"{path}:{idx}: cannot parse {line.strip()!r}")
            _set_user(tree, m.group(1), m.group(2), f"{path}:{idx}")


def _set_user(tree: Tree, name: str, raw: str, origin: str):
    s = tree.symbols.get(name)
    if s is None:
        raise ConfigError(f"{origin}: unknown symbol CONFIG_{name}")
    if s.origin.endswith("(manifest)"):
        raise ConfigError(f"{origin}: CONFIG_{name} is derived from the architecture manifest and cannot be set")
    v = raw.strip()
    if len(v) >= 2 and v[0] == '"' and v[-1] == '"':
        v = v[1:-1]
    s.user = v
    s.user_origin = origin
    s.value = None


# --------------------------------------------------------------------------- output


def c_literal(s: Symbol, v: str) -> str:
    if s.kind == "bool":
        return "1" if v == "y" else "0"
    if s.kind == "int":
        return v
    if s.kind == "hex":
        return v if v.startswith("0x") else hex(int(v, 0))
    return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'


def emit(tree: Tree, res: Resolver, args):
    rows = []
    for name in tree.order:
        s = tree.symbols[name]
        rows.append((s, res.value(s)))
    dotconfig_lines = []
    for s, v in rows:
        if s.kind == "bool":
            dotconfig_lines.append(f"CONFIG_{s.name}=y" if v == "y" else f"# CONFIG_{s.name} is not set")
        elif s.kind == "string":
            dotconfig_lines.append(f'CONFIG_{s.name}="{v}"')
        else:
            dotconfig_lines.append(f"CONFIG_{s.name}={v}")
    dotconfig = "\n".join(dotconfig_lines) + "\n"
    digest = hashlib.sha256(dotconfig.encode()).hexdigest()
    hash32 = "0x" + digest[:8]

    header = ["/* Generated by tools/kconfig/embconfig.py; do not edit. */",
              "/* SPDX-License-Identifier: Apache-2.0 */",
              "#ifndef EMB_CONFIG_H", "#define EMB_CONFIG_H", "",
              f"#define EMB_CONFIG_VERSION {CONFIG_VERSION}",
              f"#define EMB_CONFIG_HASH {hash32}u",
              f'#define EMB_CONFIG_HASH_STRING "{digest[:16]}"', ""]
    for s, v in rows:
        header.append(f"#define CONFIG_{s.name} {c_literal(s, v)}")
    header += ["", "#endif /* EMB_CONFIG_H */", ""]

    cmake = ["# Generated by tools/kconfig/embconfig.py; do not edit.",
             f'set(EMB_CONFIG_VERSION {CONFIG_VERSION})', f'set(EMB_CONFIG_HASH "{digest[:16]}")']
    for s, v in rows:
        val = ("1" if v == "y" else "0") if s.kind == "bool" else v
        cmake.append(f'set(CONFIG_{s.name} "{val}")')
    cmake.append("")

    jdoc = {"version": CONFIG_VERSION, "hash": digest[:16], "symbols": {}}
    for s, v in rows:
        jdoc["symbols"]["CONFIG_" + s.name] = {
            "type": s.kind,
            "value": (v == "y") if s.kind == "bool" else (int(v, 0) if s.kind in ("int", "hex") else v),
            "origin": s.user_origin or s.origin,
        }

    def write(path: Optional[str], text: str):
        if not path:
            return
        d = os.path.dirname(path)
        if d:
            os.makedirs(d, exist_ok=True)
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                if f.read() == text:
                    return  # keep the timestamp: nothing rebuilds
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)

    write(args.out_header, "\n".join(header))
    write(args.out_cmake, "\n".join(cmake))
    write(args.out_json, json.dumps(jdoc, indent=2) + "\n")
    write(args.out_dotconfig, dotconfig)


# --------------------------------------------------------------------------- main


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--kconfig", required=True, help="root Kconfig file")
    ap.add_argument("--manifest", help="arch/<arch>/arch.yaml")
    ap.add_argument("--variant", help="architecture variant from the manifest")
    ap.add_argument("--host-word-bits", type=int, default=64)
    ap.add_argument("--conf", action="append", default=[], help="configuration fragment, applied in order")
    ap.add_argument("--set", action="append", default=[], help="SYMBOL=value override, applied last")
    ap.add_argument("--var", action="append", default=[], help="NAME=value for $(NAME) in source paths")
    ap.add_argument("--out-header")
    ap.add_argument("--out-cmake")
    ap.add_argument("--out-json")
    ap.add_argument("--out-dotconfig")
    ap.add_argument("--print", action="store_true", help="print the resolved configuration")
    args = ap.parse_args(argv)

    variables = {}
    for v in args.var:
        k, _, val = v.partition("=")
        variables[k] = val
    tree = Tree()
    try:
        manifest = load_manifest(args.manifest, args.variant, args.host_word_bits) if args.manifest else {}
        for name, (kind, value, origin) in manifest.items():
            s = tree.sym(name, origin + " (manifest)")
            s.kind = kind
            s.user = value
            s.user_origin = origin
        root_kconfig = os.path.abspath(args.kconfig)
        Parser(tree, variables, os.path.dirname(root_kconfig)).parse_file(root_kconfig, [])
        for name, (kind, value, origin) in manifest.items():
            s = tree.symbols[name]
            if s.kind != kind:
                raise ConfigError(f"{s.origin}: manifest symbol {name} declared as {s.kind}, manifest gives {kind}")
            s.origin = origin + " (manifest)"
        for path in args.conf:
            apply_fragment(tree, path)
        for item in args.set:
            k, _, val = item.partition("=")
            _set_user(tree, k.removeprefix("CONFIG_"), val, "command line")
        res = Resolver(tree)
        res.resolve()
        emit(tree, res, args)
        if args.print:
            for name in tree.order:
                s = tree.symbols[name]
                print(f"CONFIG_{name}={res.value(s)}")
    except ConfigError as e:
        print(f"embconfig: error:\n{e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
