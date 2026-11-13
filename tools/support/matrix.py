#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The board support matrix (ADR-051, A81): each board's tier, from evidence, never declared.

    matrix.py                  # write docs/engineering/support-matrix.md
    matrix.py --check          # CI: the page is current and the inventory is consistent

  tier 1  hardware   an archived run on the physical board, every test passed
                     (docs/engineering/hardware-runs.json, each entry naming its report and,
                     since A89, the directory of its console logs and records)
  tier 2  emulated   the suite runs in CI on the board's emulator (or, for native, the host)
  tier 3  described  CI builds the board from its description; nothing runs it
  -       untested   the board exists, but CI neither runs nor builds it

CI's runs and builds are read from .github/workflows/ci.yml (`ctest --preset` and
`cmake --build --preset`, with each job's preset matrix); a preset's board from
CMakePresets.json; its execution scope from tools/evidence/configurations.json. A preset CI
runs that the inventory does not list is an error, and so is a hardware run whose board,
preset or report does not exist. Exit status 0, 1 when --check finds the page stale, or 2 on
an inconsistency.
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PAGE = Path("docs/engineering/support-matrix.md")
TIERS = {1: "1 hardware", 2: "2 emulated", 3: "3 described", None: "untested"}
# each CMake toolchain file's compiler family (ADR-053): what the "Toolchains in CI" column says
TOOLCHAINS = {"arm-none-eabi-gcc": "GCC", "avr-gcc": "GCC", "riscv-gcc": "GCC", "native-gcc": "GCC",
              "native-coverage": "GCC", "native-clang": "Clang", "native-sanitizers": "GCC or Clang",
              "native-tsan": "GCC or Clang", "embcc-native": "EmbCC", "embcc-native-mixed": "EmbCC",
              "embcc-avr": "EmbCC", "embcc-xtensa": "EmbCC", "xtensa-esp32-gcc": "GCC", "xtensa-esp32-clang": "Clang",
              "arm-none-eabi-clang": "Clang", "riscv-clang": "Clang", "avr-clang": "Clang"}


class MatrixError(Exception):
    pass


def boards(root):
    """{name: {dir, soc, core, kind}} for every board directory."""
    out = {}
    for d in sorted(root.glob("boards/*/*")):
        hw = d / "generated" / "docs" / "hw.json"
        if (d / "board.yaml").is_file() and hw.is_file():
            j = json.loads(hw.read_text(encoding="utf-8"))
            soc = j.get("soc", {})
            out[d.name] = {"dir": d.relative_to(root).as_posix(), "kind": "description",
                           "soc": soc.get("part") or soc.get("name", "?"),
                           "core": soc.get("cpu", {}).get("core", "?")}
        elif (d / "board.cmake").is_file():
            text = (d / "board.cmake").read_text(encoding="utf-8")
            mcu = re.search(r"set\(EMB_BOARD_MCU\s+(\S+)\)", text)
            variant = re.search(r"set\(EMB_BOARD_VARIANT\s+(\S+)\)", text)
            arch = re.search(r"set\(EMB_BOARD_ARCH\s+(\S+)\)", text)
            soc = mcu.group(1) if mcu else "host" if d.name == "native" else "(no description yet)"
            out[d.name] = {"dir": d.relative_to(root).as_posix(), "kind": "hand-written", "soc": soc,
                           "core": (variant or arch).group(1) if (variant or arch) else "?"}
    return out


def _inherited(by, name, get, seen=()):
    """A preset's own value, or the first one up its `inherits` chain."""
    p = by[name]
    v = get(p)
    if v is not None:
        return v
    parents = p.get("inherits", [])
    for parent in ([parents] if isinstance(parents, str) else parents):
        if parent in by and parent not in seen:
            v = _inherited(by, parent, get, seen + (name,))
            if v is not None:
                return v
    return None


def _presets(root):
    d = json.loads((root / "CMakePresets.json").read_text(encoding="utf-8"))
    return {p["name"]: p for p in d["configurePresets"]}


def preset_boards(root):
    """{configure preset: board}, following `inherits`; the default board is native."""
    by = _presets(root)

    def board(p):
        v = p.get("cacheVariables", {}).get("EMB_BOARD")
        return (v["value"] if isinstance(v, dict) else v) if v is not None else None
    return {n: _inherited(by, n, board) or "native" for n in by if not by[n].get("hidden")}


def preset_toolchains(root):
    """{configure preset: compiler family}, from its toolchain file (ADR-053)."""
    by = _presets(root)
    out = {}
    for n in by:
        f = _inherited(by, n, lambda p: p.get("toolchainFile"))
        stem = Path(f).stem if f else None
        out[n] = TOOLCHAINS.get(stem, stem or "host default")
    return out


def ci_presets(root):
    """(presets CI runs with ctest, presets CI builds), from the workflow."""
    import yaml
    wf = yaml.safe_load((root / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8"))
    runs, builds = set(), set()
    for job in (wf.get("jobs") or {}).values():
        matrix = ((job.get("strategy") or {}).get("matrix") or {})
        mx = [p for p in matrix.get("preset", []) if isinstance(p, str)]
        mx += [i["preset"] for i in matrix.get("include", []) if isinstance(i, dict) and "preset" in i]
        for step in job.get("steps") or []:
            text = step.get("run") or ""
            for verb, into in (("ctest", runs), ("cmake --build", builds)):
                if re.search(verb + r" --preset \$\{\{ matrix\.preset \}\}", text):
                    into.update(mx)
                into.update(re.findall(verb + r" --preset ([A-Za-z0-9_.-]+)", text))
    return runs, builds | runs


def derive(root):
    bs = boards(root)
    pb = preset_boards(root)
    ptc = preset_toolchains(root)
    configs = json.loads((root / "tools" / "evidence" / "configurations.json").read_text(encoding="utf-8"))["configurations"]
    runs, built = ci_presets(root)
    problems = []
    for p in sorted(runs):
        if p not in pb:
            problems.append(f"ci.yml runs preset {p!r}, which CMakePresets.json does not define")
        elif p not in configs:
            problems.append(f"ci.yml runs preset {p!r}, which tools/evidence/configurations.json does not list")
    hw = json.loads((root / "docs" / "engineering" / "hardware-runs.json").read_text(encoding="utf-8"))["runs"]
    for i, r in enumerate(hw):
        at = f"hardware-runs.json run {i} ({r.get('board')}, {r.get('build')})"
        report = root / "docs" / "engineering" / r.get("report", "")
        if r.get("board") not in bs:
            problems.append(f"{at}: no board {r.get('board')!r}")
        if pb.get(r.get("preset")) != r.get("board"):
            problems.append(f"{at}: preset {r.get('preset')!r} is not a preset of that board")
        if not report.is_file():
            problems.append(f"{at}: no report docs/engineering/{r.get('report')}")
        elif r.get("preset", "") not in report.read_text(encoding="utf-8"):
            problems.append(f"{at}: its report does not name the preset")
        if "archive" in r:  # the run's console logs and records (tools/hil/archive.py, A89)
            index = root / "docs" / "engineering" / r["archive"] / "index.json"
            if not index.is_file():
                problems.append(f"{at}: no archive docs/engineering/{r['archive']}/index.json")
            else:
                ix = json.loads(index.read_text(encoding="utf-8"))
                if (ix.get("board"), ix.get("passed"), ix.get("total")) != (r.get("board"), r.get("passed"), r.get("total")):
                    problems.append(f"{at}: the archive's index says {ix.get('board')} "
                                    f"{ix.get('passed')}/{ix.get('total')}")
    if problems:
        raise MatrixError(problems)
    rows = []
    for name, b in sorted(bs.items()):
        mine = sorted(p for p, bd in pb.items() if bd == name)
        passed = [r for r in hw if r["board"] == name and r["passed"] == r["total"] and r["total"] > 0]
        emulated = [p for p in mine if p in runs and configs.get(p, {}).get("scope") in ("emulated", "host")]
        builds = [p for p in mine if p in built]
        if passed:
            tier = 1
            groups = {}
            for r in passed:  # one clause per preset, date and report
                groups.setdefault((r["preset"], r["date"], r["report"]), []).append(r)
            ev = "; ".join(f"`{p}` " + ", ".join(f"{r['build'].split(' (')[0]} {r['passed']}/{r['total']}" for r in rs)
                           + f" ({d}, [report]({rep}))" for (p, d, rep), rs in groups.items())
        elif emulated:
            tier = 2
            host = all(configs[p]["scope"] == "host" for p in emulated)
            ev = ("CI runs, on the host, " if host else "CI runs ") + ", ".join(f"`{p}`" for p in emulated)
        elif builds:
            tier = 3
            ev = "CI builds " + ", ".join(f"`{p}`" for p in builds)
        else:
            tier, ev = None, "neither run nor built in CI"
        also = [p for p in emulated if tier == 1]
        if also:
            ev += "; CI also runs " + ", ".join(f"`{p}`" for p in also)
        tcs = sorted({ptc[p] for p in mine if p in built})
        rows.append({"board": name, "tier": tier, "evidence": ev, "toolchains": ", ".join(tcs) or "-", **b})
    return rows


def render(rows):
    out = ["# Board support matrix", "",
           "Generated by `tools/support/matrix.py` ([ADR-051](../architecture/06-decision-records.md), "
           "[A81](../specs/AMENDMENTS-M3.md)); do not edit. Each board's tier comes from evidence. A board "
           "description never declares its own tier:", "",
           "- **1 hardware**: the suite passed on the physical board, in a run archived in "
           "`hardware-runs.json` with its report.",
           "- **2 emulated**: CI runs the suite on the board's emulator, or on the host for native. A "
           "merge needs it to pass.",
           "- **3 described**: CI builds the board from its description; nothing runs it.",
           "- **untested**: the board exists, but CI neither runs nor builds it.", "",
           "Emulator results are never timing evidence (SIM-003).", "",
           "The toolchains column names each compiler CI builds the board with, from its presets' "
           "toolchain files ([ADR-053](../architecture/06-decision-records.md)). A toolchain CI does "
           "not run is not claimed for the board.", "",
           "| Board | SoC | Core | Tier | Toolchains in CI | Evidence |", "|---|---|---|---|---|---|"]
    for r in rows:
        out.append(f"| `{r['board']}` | {r['soc']} | {r['core']} | {TIERS[r['tier']]} | {r['toolchains']} | {r['evidence']} |")
    counts = {t: sum(1 for r in rows if r["tier"] == t) for t in TIERS}
    out += ["", "Totals: " + ", ".join(f"{TIERS[t]}: {counts[t]}" for t in TIERS) + "."]
    return "\n".join(out) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="fail if the committed page differs")
    ap.add_argument("--root", type=Path, default=ROOT)
    a = ap.parse_args(argv)
    try:
        text = render(derive(a.root))
    except MatrixError as e:
        for p in e.args[0]:
            print(f"matrix.py: {p}", file=sys.stderr)
        return 2
    page = a.root / PAGE
    if a.check:
        if not page.is_file() or page.read_text(encoding="utf-8") != text:
            print(f"matrix.py: {PAGE} is stale; run python3 -I tools/support/matrix.py", file=sys.stderr)
            return 1
        print(f"matrix.py: {PAGE} is current")
        return 0
    page.write_text(text, encoding="utf-8")
    print(f"matrix.py: wrote {PAGE}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
