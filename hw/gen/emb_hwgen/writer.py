# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Writing generated files without losing edits (SPEC-014 §5.3, A126).

Every generated file may be edited. The generator keeps what it last wrote, the baseline, in a
committed `.base/` directory, and on each generation decides per file:

- the new text is the baseline's: the file is left as it is, edited or not;
- the file is the baseline (not edited): it is rewritten;
- both changed: a three-way merge of the baseline, the file and the new text keeps both, and
  where both changed the same lines the file gets conflict markers for its author to resolve.

The baseline then becomes the new text. The merge is the classic three-way algorithm (as in
diff3 and bzr's merge3), line by line, so it needs no tool beyond Python.
"""
from difflib import SequenceMatcher
from pathlib import Path

MARK_OURS = "<<<<<<< yours"
MARK_BASE = "||||||| generated before"
MARK_SEP = "======="
MARK_THEIRS = ">>>>>>> generated now"
_MARKS = (MARK_OURS, MARK_SEP, MARK_THEIRS)


def _sync_regions(base, a, b):
    """The regions where base, a and b all agree: (base start, end, a start, end, b start, end)."""
    am = SequenceMatcher(None, base, a, autojunk=False).get_matching_blocks()
    bm = SequenceMatcher(None, base, b, autojunk=False).get_matching_blocks()
    ia = ib = 0
    out = []
    while ia < len(am) and ib < len(bm):
        abase, amatch, alen = am[ia]
        bbase, bmatch, blen = bm[ib]
        lo, hi = max(abase, bbase), min(abase + alen, bbase + blen)
        if lo < hi:
            asub, bsub = amatch + (lo - abase), bmatch + (lo - bbase)
            out.append((lo, hi, asub, asub + hi - lo, bsub, bsub + hi - lo))
        if abase + alen < bbase + blen:
            ia += 1
        else:
            ib += 1
    out.append((len(base), len(base), len(a), len(a), len(b), len(b)))
    return out


def merge3(base, ours, theirs):
    """(merged lines, conflict count) of two edits of base: ours (the file as edited) and theirs
    (the new generated text), each a list of lines with their endings."""
    out, conflicts = [], 0
    iz = ia = ib = 0
    for zs, ze, as_, ae, bs, be in _sync_regions(base, ours, theirs):
        if as_ > ia or bs > ib or zs > iz:
            b0, a0, t0 = base[iz:zs], ours[ia:as_], theirs[ib:bs]
            if a0 == t0:
                out += a0
            elif a0 == b0:
                out += t0  # only the generator changed it
            elif t0 == b0:
                out += a0  # only the author changed it
            else:
                conflicts += 1
                end = lambda part: part if not part or part[-1].endswith("\n") else part[:-1] + [part[-1] + "\n"]  # noqa: E731
                out += [MARK_OURS + "\n"] + end(a0) + [MARK_BASE + "\n"] + end(b0) + [MARK_SEP + "\n"] + end(t0) + [MARK_THEIRS + "\n"]
        out += base[zs:ze]
        iz, ia, ib = ze, ae, be
    return out, conflicts


def has_conflict(text):
    """Whether a file still holds the markers of an unresolved merge."""
    lines = text.splitlines()
    return any(line == MARK_OURS for line in lines) and any(line.startswith(MARK_THEIRS) for line in lines)


def write(path, text, base_path):
    """Write one generated file over whatever is there, keeping its edits; record the baseline.
    Returns what happened: created, unchanged, rewritten, merged or conflict."""
    path, base_path = Path(path), Path(base_path)
    base = base_path.read_text(encoding="utf-8") if base_path.is_file() else None
    current = path.read_text(encoding="utf-8") if path.is_file() else None
    if current is None:
        outcome, result = "created", text
    elif current == text:
        outcome, result = "unchanged", None
    elif base == text:
        outcome, result = "unchanged", None  # the generator's text did not change: the edits stay
    elif base == current:
        outcome, result = "rewritten", text  # not edited
    else:
        # edited, and the generated text changed: merge. With no baseline (a file the generator
        # had not recorded), every difference is a conflict for its author to decide.
        lines, conflicts = merge3((base or "").splitlines(True), current.splitlines(True), text.splitlines(True))
        outcome, result = ("conflict" if conflicts else "merged"), "".join(lines)
    if result is not None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(result, encoding="utf-8")
    if base != text:
        base_path.parent.mkdir(parents=True, exist_ok=True)
        base_path.write_text(text, encoding="utf-8")
    return outcome
