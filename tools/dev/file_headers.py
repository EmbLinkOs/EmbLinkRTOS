#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The file header of every C, assembly and linker-script source (CODING-STANDARD CS-11.1).

A source starts with its SPDX and copyright lines, then a comment saying it is part of
EmbLinkRTOS and under which licence, then a Doxygen block naming the file:

    /**
     * @file    sched.c
     * @brief   What the file is, in its first sentence.
     *
     * @details
     * The rest of its description, as written.
     *
     * @see     its companion header or source, where it has one
     * @author  who created it, from git (.mailmap gives one name per person)
     * @date    2026-10-07 (created)
     */

`--write` converts files with the older header (SPDX, copyright, one description comment) and
leaves converted ones alone; the author and date come from the file's first commit, followed
across renames, or for a file not yet committed from git's user.name and today. `--check`
fails on any file without the header, or whose @file is not its own name; CI runs it.

Not touched: third-party code, archived evidence (its sources are records of a run), and files
under another licence. Generated files are the generator's to write: `--check` only checks that
they carry the same notice and an @file block, without an author or a date.
"""
import argparse
import datetime
import pathlib
import re
import subprocess
import sys
import textwrap

ROOT = pathlib.Path(__file__).resolve().parents[2]
SPDX = "/* SPDX-License-Identifier: Apache-2.0 */"
NOTICE = [
    "/*",
    " * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.",
    " * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.",
    " */",
]
SUFFIXES = (".c", ".h", ".S", ".ld")
WIDTH = 100
TAG = " * @{:<7} "  # " * @brief   ": the values line up after the longest tag
HANG = " " * len(TAG.format(""))
NOT_ENDS = ("e.g.", "i.e.", "etc.", "vs.", "cf.", "approx.", "No.")
BRIEF_MAX = 100  # a first sentence longer than this is cut at its colon, if it has one


def sources():
    out = subprocess.run(["git", "ls-files", "-co", "--exclude-standard", "--", *(f"*{s}" for s in SUFFIXES)],
                         cwd=ROOT, capture_output=True, text=True, check=True).stdout.split()
    for rel in sorted(set(out)):
        if rel.startswith("third_party/") or "/evidence/" in rel:
            continue
        path = ROOT / rel
        if not path.is_file():
            continue
        lines = path.read_text(encoding="utf-8").split("\n")
        if len(lines) < 2 or lines[0] != SPDX:
            continue  # another licence: not ours to rewrite
        if "/generated/" in rel:
            yield rel, path, lines, True  # the generator writes it (hw/gen/emb_hwgen/emit.py)
        elif lines[1].startswith("/* Copyright "):
            yield rel, path, lines, False


def converted(lines):
    """Whether the lines already carry the header: the notice, then the @file block."""
    at = copyright_end(lines)
    return lines[at:at + len(NOTICE)] == NOTICE and lines[at + len(NOTICE) + 1:at + len(NOTICE) + 3][:1] == ["/**"]


def copyright_end(lines):
    at = 1
    while at < len(lines) and lines[at].startswith("/* Copyright "):
        at += 1
    return at


def description(lines, at):
    """The first comment after the copyright lines, as text, and the line after it."""
    while at < len(lines) and not lines[at].strip():
        at += 1
    if at >= len(lines) or not lines[at].startswith("/*") or lines[at].startswith("/**"):
        return None, at
    first = lines[at]
    if first.rstrip().endswith("*/") and first.strip() != "/*":  # one line: /* text */
        return [first.strip()[2:-2].strip()], at + 1
    body = [] if first.strip() == "/*" else [first.strip()[2:].strip()]
    at += 1
    while at < len(lines):
        line = lines[at]
        end = line.rstrip().endswith("*/")
        text = line.rstrip()[:-2] if end else line
        text = re.sub(r"^\s*\* ?", "", text) if text.strip().startswith("*") else text.strip()
        if end:
            if text.strip():
                body.append(text.rstrip())
            return body, at + 1
        body.append(text.rstrip())
        at += 1
    raise ValueError("an unterminated comment")


def split_brief(body):
    """The first sentence, and the rest of the description, its paragraphs kept."""
    paragraphs, cur = [], []
    for line in body:
        if line.strip():
            cur.append(line)
        elif cur:
            paragraphs.append(cur)
            cur = []
    if cur:
        paragraphs.append(cur)
    if not paragraphs:
        return "", []
    first = paragraphs[0]
    listy = any(re.match(r"\s*(?:[-*+]|\d+[.)])\s", l) for l in first[1:]) or any(l.startswith("  ") for l in first)
    text = " ".join(l.strip() for l in first)
    end = None
    for m in re.finditer(r"\.(?=\s|$)", text):
        word = text[:m.end()].split()[-1]
        if word.endswith(NOT_ENDS) or re.fullmatch(r"[A-Z]\.", word):
            continue
        end = m.end()
        break
    if listy:  # a paragraph with a list in it: the brief is its first line's sentence, if it ends there
        head = first[0].strip()
        if end is not None and end <= len(head):
            brief, rest = text[:end], [head[end:].strip()] + first[1:] if head[end:].strip() else first[1:]
        else:
            brief, rest = head, first[1:]
        return brief, ([rest] if rest else []) + paragraphs[1:]
    sentence = text if end is None else text[:end]
    rest = "" if end is None else text[end:].strip()
    colon = top_level_colon(sentence) if len(sentence) > BRIEF_MAX else None
    if colon is not None:  # "The thing (refs): what it does at length": the brief is the thing
        tail = sentence[colon + 1:].strip()
        first = tail.split()[0]
        if re.fullmatch(r"[a-z][a-z'-]*,?", first):  # a word, not an identifier: it starts a sentence now
            tail = tail[0].upper() + tail[1:]
        sentence, rest = sentence[:colon] + ".", (tail + " " + rest).strip()
    if not rest:
        return sentence, paragraphs[1:]
    return sentence, [textwrap.wrap(rest, WIDTH - 3, break_on_hyphens=False)] + paragraphs[1:]


def top_level_colon(text):
    """The first ': ' outside parentheses and backquotes, or None."""
    depth, quoted = 0, False
    for i, ch in enumerate(text):
        if ch == "`":
            quoted = not quoted
        elif not quoted and ch in "([":
            depth += 1
        elif not quoted and ch in ")]":
            depth -= 1
        elif ch == ":" and depth == 0 and not quoted and text[i + 1:i + 2] == " ":
            return i
    return None


def companion(rel, names):
    path = pathlib.PurePosixPath(rel)
    want = {".c": ".h", ".h": ".c", ".S": ".h"}.get(path.suffix)
    if not want:
        return None
    found = [n for n in names if pathlib.PurePosixPath(n).name == path.stem + want]
    if len(found) != 1:
        return None
    other = pathlib.PurePosixPath(found[0])
    return other.name if other.parent == path.parent else str(other)


def origin(rel):
    """Who created the file, and when, from its first commit; git's user and today if none."""
    log = subprocess.run(["git", "log", "--use-mailmap", "--follow", "--diff-filter=A", "--format=%aN\t%as", "--", rel],
                         cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip().splitlines()
    if log:
        name, date = log[-1].split("\t")
        return name, date
    name = subprocess.run(["git", "check-mailmap", "<" + git_config("user.email") + ">"], cwd=ROOT,
                          capture_output=True, text=True).stdout.strip()
    name = re.sub(r"\s*<.*>$", "", name) or git_config("user.name")
    return name, datetime.date.today().isoformat()


def git_config(key):
    return subprocess.run(["git", "config", key], cwd=ROOT, capture_output=True, text=True).stdout.strip()


def tagged(tag, value):
    first = TAG.format(tag)
    lines = textwrap.wrap(value, WIDTH, initial_indent=first, subsequent_indent=" *" + HANG[2:],
                          break_on_hyphens=False, break_long_words=False)
    return lines or [first.rstrip()]


def render(name, brief, details, see, author, date):
    out = ["/**"] + tagged("file", name) + tagged("brief", brief)
    if details:
        out += [" *", " * @details"]
        for i, paragraph in enumerate(details):
            if i:
                out.append(" *")
            out += [(" * " + l).rstrip() for l in paragraph]
    out.append(" *")
    if see:
        out += tagged("see", see)
    out += tagged("author", author) + tagged("date", f"{date} (created)") + [" */"]
    return out


def convert(rel, lines, names):
    at = copyright_end(lines)
    body, after = description(lines, at)
    if body is None:
        raise ValueError("no description comment after the copyright lines: write one first")
    brief, details = split_brief(body)
    if not brief:
        raise ValueError("an empty description")
    author, date = origin(rel)
    head = lines[:at] + NOTICE + [""] + render(pathlib.PurePosixPath(rel).name, brief, details,
                                                companion(rel, names), author, date)
    return head + lines[after:]


def generated_problems(rel, lines):
    """A generated file: the notice and its @file and @brief, with no author or date."""
    if lines[1:1 + len(NOTICE)] != NOTICE or lines[1 + len(NOTICE):3 + len(NOTICE)] != ["", "/**"]:
        return ["the generator did not write the notice and @file block (run hw/gen/emb-hwgen.py generate)"]
    text = "\n".join(lines[3 + len(NOTICE):40])
    name = re.search(r"^ \* @file\s+(\S+)$", text, re.M)
    if not name or name[1] != pathlib.PurePosixPath(rel).name:
        return [f"@file names {name[1] if name else 'nothing'}, not {pathlib.PurePosixPath(rel).name}"]
    return [] if re.search(r"^ \* @brief\s+\S", text, re.M) else ["no @brief"]


def problems(rel, lines):
    if not converted(lines):
        return ["no EmbLinkRTOS notice and @file block after the copyright lines (tools/dev/file_headers.py --write)"]
    at = copyright_end(lines) + len(NOTICE) + 1
    block = []
    for line in lines[at:]:
        block.append(line)
        if line.strip() == "*/":
            break
    text = "\n".join(block)
    found = []
    name = re.search(r"^ \* @file\s+(\S+)$", text, re.M)
    if not name or name[1] != pathlib.PurePosixPath(rel).name:
        found.append(f"@file names {name[1] if name else 'nothing'}, not {pathlib.PurePosixPath(rel).name}")
    for tag in ("brief", "author"):
        if not re.search(rf"^ \* @{tag}\s+\S", text, re.M):
            found.append(f"no @{tag}")
    if not re.search(r"^ \* @date\s+\d{4}-\d{2}-\d{2} \(created\)$", text, re.M):
        found.append("no @date YYYY-MM-DD (created)")
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true", help="convert files with the older header")
    mode.add_argument("--check", action="store_true", help="fail on a file without the header")
    ap.add_argument("files", nargs="*", help="only these (repository paths); default all")
    args = ap.parse_args()
    every = list(sources())
    names = [rel for rel, _, _, generated in every if not generated]
    chosen = [s for s in every if not args.files or s[0] in set(args.files)]
    failed = changed = 0
    for rel, path, lines, generated in chosen:
        if generated and not args.check:
            continue  # the generator's to write
        if args.check:
            for p in (generated_problems if generated else problems)(rel, lines):
                print(f"{rel}: {p}")
                failed += 1
            continue
        if converted(lines):
            continue
        try:
            new = convert(rel, lines, names)
        except ValueError as e:
            print(f"{rel}: {e}")
            failed += 1
            continue
        path.write_text("\n".join(new), encoding="utf-8")
        changed += 1
    if args.check:
        print(f"file_headers: {len(chosen)} files, {failed} problem(s)")
    else:
        print(f"file_headers: {changed} converted, {failed} left as they were")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
