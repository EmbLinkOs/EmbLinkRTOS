#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The double build of BLD-004: the same commit built in two places must give the same images.

    double_build.py [--preset NAME]... [--app DIR:BOARD[:TOOLCHAIN_FILE]]... [--work DIR] [--keep]

The tree this runs from is the first place. A second checkout of the same commit, made with
`git clone --shared` under --work (default: a temporary directory), is the second: a different
absolute path, as a different machine or CI workspace would have. Each target is configured and
built from scratch in both, with the same toolchain, and every image it produces (.elf, .hex,
.bin, .uf2, and a native program) is compared byte for byte. For an image that differs, the ELF
sections that differ are named.

- --preset NAME builds a CMake preset of the RTOS (`cmake --preset NAME`, then its build).
- --app DIR:BOARD builds an application directory of the tree for a board through
  find_package(EmbLinkRTOS), optionally with a toolchain file relative to the tree.

The tree must be clean, since the second checkout holds only the committed sources: the build
identity names the commit and a dirty flag (A72), and a dirty tree's images would differ.
Map files, stack-usage files, logs and CMake's own probes (CMakeFiles/) are not compared; they
are build by-products, not images.

Exit status 0 when every image is identical and there was at least one, 1 when one differs or
a target produced none, 2 on a usage, git or build error.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IMAGES = (".elf", ".hex", ".bin", ".uf2")


class BuildError(Exception):
    pass


def run(cmd, cwd, log):
    with open(log, "a", encoding="utf-8") as f:
        f.write("$ " + " ".join(str(c) for c in cmd) + "\n")
        f.flush()
        p = subprocess.run([str(c) for c in cmd], cwd=cwd, stdout=f, stderr=subprocess.STDOUT, check=False)
    if p.returncode != 0:
        raise BuildError(f"{' '.join(str(c) for c in cmd)} failed in {cwd} (exit {p.returncode}); see {log}")


def git(*args, cwd=ROOT):
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True, check=True).stdout.strip()


def targets(args):
    """(name, how to configure it relative to a tree, the build directory relative to it)."""
    out = []
    for p in args.preset or []:
        out.append((p, ("preset", p), Path("build") / p))
    for spec in args.app or []:
        parts = spec.split(":")
        if len(parts) not in (2, 3):
            raise SystemExit(f"double_build: --app {spec}: expected DIR:BOARD[:TOOLCHAIN_FILE]")
        d, board = parts[0], parts[1]
        name = f"{Path(d).name}-{board}" + (f"-{Path(parts[2]).stem}" if len(parts) == 3 else "")
        out.append((name, ("app", d, board, parts[2] if len(parts) == 3 else None), Path("build") / "repro" / name))
    if not out:
        raise SystemExit("double_build: give at least one --preset or --app")
    return out


def build(tree, how, bdir, log, python):
    """Configure and build one target from scratch in one tree."""
    full = tree / bdir
    shutil.rmtree(full, ignore_errors=True)
    extra = [f"-DPython3_EXECUTABLE={python}"] if python else []
    if how[0] == "preset":
        run(["cmake", "--preset", how[1], *extra], tree, log)
        run(["cmake", "--build", "--preset", how[1]], tree, log)
    else:
        _, d, board, toolchain = how
        cfg = ["cmake", "-S", tree / d, "-B", full, "-G", "Ninja", f"-DEMB_BOARD={board}", *extra]
        if toolchain:
            cfg.append(f"-DCMAKE_TOOLCHAIN_FILE={tree / toolchain}")
        run(cfg, tree, log)
        run(["cmake", "--build", full], tree, log)
    return full


EXECUTABLE_MAGIC = (b"\x7fELF", b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe")  # ELF, Mach-O 64 and 32


def is_image(p):
    """An image the build made: by suffix, or an executable without one (a native program)."""
    if not p.is_file() or "CMakeFiles" in p.parts:  # CMake's own compiler probes
        return False
    if p.suffix in IMAGES:
        return True
    if p.suffix or not os.access(p, os.X_OK):
        return False
    with open(p, "rb") as f:
        return f.read(4) in EXECUTABLE_MAGIC


def images(bdir):
    return sorted(p.relative_to(bdir) for p in bdir.rglob("*") if is_image(p))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def differing_sections(a, b):
    """The ELF sections whose contents differ, by the section table of each file (no tools needed)."""
    try:
        sa, sb = elf_sections(a), elf_sections(b)
    except ValueError:
        return []
    names = sorted(set(sa) | set(sb))
    return [n for n in names if sa.get(n) != sb.get(n)]


def elf_sections(path):
    """{name: sha256 of contents} for an ELF32 or ELF64 file of either byte order."""
    import struct
    data = path.read_bytes()
    if data[:4] != b"\x7fELF":
        raise ValueError("not ELF")
    is64, end = data[4] == 2, "<" if data[5] == 1 else ">"
    if is64:
        shoff, = struct.unpack_from(end + "Q", data, 0x28)
        shentsize, shnum, shstrndx = struct.unpack_from(end + "HHH", data, 0x3A)
    else:
        shoff, = struct.unpack_from(end + "I", data, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from(end + "HHH", data, 0x2E)
    heads = []
    for i in range(shnum):
        o = shoff + i * shentsize
        if is64:
            name, typ, _, _, off, size = struct.unpack_from(end + "IIQQQQ", data, o)
        else:
            name, typ, _, _, off, size = struct.unpack_from(end + "IIIIII", data, o)
        heads.append((name, typ, off, size))
    _, _, stroff, strsize = heads[shstrndx]
    strtab = data[stroff:stroff + strsize]
    out = {}
    for name, typ, off, size in heads:
        n = strtab[name:strtab.index(b"\0", name)].decode(errors="replace")
        body = b"" if typ == 8 else data[off:off + size]  # SHT_NOBITS holds no bytes
        out[n or "<null>"] = hashlib.sha256(body).hexdigest()
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--preset", action="append", help="a CMake preset of the RTOS")
    ap.add_argument("--app", action="append", help="DIR:BOARD[:TOOLCHAIN_FILE], DIR relative to the tree")
    ap.add_argument("--work", help="where the second checkout goes (default: a temporary directory)")
    ap.add_argument("--keep", action="store_true", help="keep the second checkout")
    ap.add_argument("--python", help="Python3_EXECUTABLE for the configure (the generators need PyYAML)")
    args = ap.parse_args(argv)
    todo = targets(args)

    if git("status", "--porcelain", "--untracked-files=no"):
        print("double_build: the tree has uncommitted changes; commit them, since the second checkout "
              "holds only the commit", file=sys.stderr)
        return 2
    head = git("rev-parse", "HEAD")
    work = Path(args.work).resolve() if args.work else Path(tempfile.mkdtemp(prefix="emb-repro-"))
    second = work / "second"
    shutil.rmtree(second, ignore_errors=True)
    work.mkdir(parents=True, exist_ok=True)
    log = work / "double_build.log"
    log.write_text("", encoding="utf-8")
    try:
        subprocess.run(["git", "clone", "--quiet", "--shared", "--no-checkout", str(ROOT), str(second)], check=True)
        git("checkout", "--quiet", head, cwd=second)
        print(f"double_build: commit {head[:12]}\n  first:  {ROOT}\n  second: {second}")

        failures = 0
        compared = 0
        for name, how, bdir in todo:
            try:
                a = build(ROOT, how, bdir, log, args.python)
                b = build(second, how, bdir, log, args.python)
            except BuildError as e:
                print(f"double_build: {name}: {e}", file=sys.stderr)
                return 2
            ia, ib = images(a), images(b)
            if not ia:
                print(f"{name}: no images")
                failures += 1
                continue
            if ia != ib:
                print(f"{name}: different image sets: only first {sorted(set(ia) - set(ib))}, "
                      f"only second {sorted(set(ib) - set(ia))}")
                failures += 1
            same = 0
            for rel in sorted(set(ia) & set(ib)):
                compared += 1
                if digest(a / rel) == digest(b / rel):
                    same += 1
                    continue
                failures += 1
                secs = differing_sections(a / rel, b / rel) if rel.suffix == ".elf" else []
                print(f"{name}: {rel} differs" + (f" in {', '.join(secs)}" if secs else ""))
            print(f"{name}: {same} of {len(set(ia) & set(ib))} images identical")
        print(f"double_build: {compared} images compared, {failures} problem(s)")
        return 1 if failures or not compared else 0
    finally:
        if not args.keep:
            shutil.rmtree(second, ignore_errors=True)
            if not args.work:
                shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
