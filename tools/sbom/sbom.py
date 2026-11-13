#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""An SPDX 2.3 software bill of materials for one image, from its build (REL-001, BLD-003).

    sbom.py --build BUILD_DIR --elf IMAGE.elf [--map IMAGE.elf.map] [--out IMAGE.spdx.json]

It reads what the build already records and runs nothing but `ninja -t deps` and the compiler's
`--version`:

- **What was linked**, from the link map (GNU ld's or LLD's): the project's object files and
  archive members, and the toolchain's libraries and the members taken from them.
- **What each object was compiled from**: its source, from compile_commands.json, and every
  header it included, from Ninja's dependency log. Each project file is listed with its SHA-1
  and SHA-256 and the licence its SPDX-License-Identifier line declares.
- **The build manifest (BLD-003)**: the commit and whether the tree was dirty, the
  configuration hash, the toolchain's identity, and the hardware description's hash, with the
  image's build identity (A72) and SHA-256.

The packages are the image (FIRMWARE), EmbLinkRTOS, the application when it lies outside the
RTOS's tree, each toolchain library the image links, and the compiler (a build tool). A
toolchain library's members are named in its package's comment; its licence is declared only
where it is known for certain (libgcc), and NOASSERTION otherwise. The toolchain's own headers
are not listed file by file.

The document is deterministic: its time is the commit's, its namespace derives from the
image's hash, and every list is sorted. The same build gives the same bytes.

Exit status 0 when written, 2 on a missing or unreadable input.
"""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

TOOL = "emblinkrtos-sbom-1"
NAMESPACE = "https://github.com/EmbLinkOs/EmbLinkRTOS/spdx"
MEMBER = re.compile(r"(?P<archive>[^\s()]+\.a)\((?P<member>[^)\s]+)\)")
OBJECT = re.compile(r"(?:^LOAD |\s)(?P<obj>[^\s():]+\.(?:o|obj))(?=$|\s|:)")
SPDX_LINE = re.compile(r"SPDX-License-Identifier:\s*([A-Za-z0-9.+\- ()]+?)\s*(?:\*/|$)")
# licences known for certain from the library itself, not guessed from its name
KNOWN_LICENCES = {"libgcc.a": "GPL-3.0-or-later WITH GCC-exception-3.1"}


class SbomError(Exception):
    pass


def sha(path, algo):
    return hashlib.new(algo, Path(path).read_bytes()).hexdigest()


def cache(build):
    """CMakeCache.txt as {name: value}."""
    out = {}
    path = Path(build) / "CMakeCache.txt"
    if not path.is_file():
        raise SbomError(f"{path}: not found (not a configured build)")
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = re.match(r"^([A-Za-z0-9_.-]+):[A-Z]+=(.*)$", line)
        if m:
            out[m[1]] = m[2]
    return out


def compiler_identity(build, compiler):
    """(id, version, the first line of --version) of the C compiler CMake chose. A toolchain file
    sets the compiler as a variable, not in the cache: CMake's compiler record names it."""
    ident, version = "unknown", "unknown"
    for f in sorted(Path(build).glob("CMakeFiles/*/CMakeCCompiler.cmake")):
        text = f.read_text(encoding="utf-8", errors="replace")
        m = re.search(r'set\(CMAKE_C_COMPILER_ID "([^"]*)"\)', text)
        v = re.search(r'set\(CMAKE_C_COMPILER_VERSION "([^"]*)"\)', text)
        c = re.search(r'set\(CMAKE_C_COMPILER "([^"]*)"\)', text)
        ident, version = (m[1] if m else ident) or "unknown", (v[1] if v else version) or "unknown"
        compiler = c[1] if c and c[1] else compiler
    try:
        line = subprocess.run([compiler, "--version"], capture_output=True, text=True,
                              check=False).stdout.splitlines()[0].strip()
    except (OSError, IndexError):
        line = ""
    return ident, version, line, Path(compiler).name


def build_identity(build):
    """(commit, dirty, configuration hash, build id) from generated/build_id.c and config.json."""
    gen = Path(build) / "generated"
    commit, dirty, build_id = "unknown", False, None
    bid = gen / "build_id.c"
    if bid.is_file():
        text = bid.read_text(encoding="utf-8")
        m = re.search(r"kernel/CMakeLists.txt: ([0-9a-f]+|unknown)(\+dirty)?,", text)
        if m:
            commit, dirty = m[1], bool(m[2])
        body = text[text.index("{") + 1:text.rindex("}")] if "{" in text else ""
        hexes = re.findall(r"0x([0-9a-fA-F]{2})u", body)
        build_id = "".join(hexes).lower() or None
    cfg = gen / "config.json"
    config_hash = json.loads(cfg.read_text(encoding="utf-8")).get("hash") if cfg.is_file() else None
    return commit, dirty, config_hash, build_id


def hardware_hash(base, board):
    """SHA-256 over the board description's inputs, as the generator recorded them (hw.json)."""
    for hw in sorted(Path(base).glob(f"boards/*/{board}/generated/hw.json")):
        inputs = json.loads(hw.read_text(encoding="utf-8")).get("inputs", [])
        text = "".join(f"{i['path']} {i['sha256']}\n" for i in sorted(inputs, key=lambda i: i["path"]))
        return hashlib.sha256(text.encode()).hexdigest(), [i["path"] for i in inputs]
    return None, []


def linked_inputs(map_text):
    """({archive: {member}}, {object}) the link map names, paths as the map gives them."""
    members, objects = {}, set()
    for m in MEMBER.finditer(map_text):
        members.setdefault(m["archive"], set()).add(m["member"])
    stripped = MEMBER.sub(" ", map_text)
    for m in OBJECT.finditer(stripped):
        objects.add(m["obj"])
    return members, objects


def ninja_deps(build):
    """{object relative to the build: [dependency]} from Ninja's dependency log."""
    p = subprocess.run(["ninja", "-C", str(build), "-t", "deps"], capture_output=True, text=True, check=False)
    if p.returncode != 0:
        raise SbomError(f"ninja -t deps in {build} failed: {p.stderr.strip()}")
    deps, current = {}, None
    for line in p.stdout.splitlines():
        if line and not line[0].isspace() and ": #deps" in line:
            current = line.split(": #deps")[0]
            deps[current] = []
        elif line.strip() and current is not None:
            deps[current].append(line.strip())
    return deps


def resolve(build, path):
    p = Path(path)
    return (p if p.is_absolute() else Path(build) / p).resolve()


def licence_of(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for _ in range(12):
                m = SPDX_LINE.search(f.readline())
                if m:
                    return m[1].strip().rstrip('",\\n')
    except OSError:
        pass
    return "NOASSERTION"


def spdx_id(text):
    return re.sub(r"[^A-Za-z0-9.-]", "-", text)


def generate(build, elf, map_path):
    build, elf = Path(build).resolve(), Path(elf).resolve()
    map_path = Path(map_path) if map_path else Path(str(elf) + ".map")
    if not elf.is_file() or not map_path.is_file():
        raise SbomError(f"{elf if not elf.is_file() else map_path}: not found")
    c = cache(build)
    source_dir = Path(c.get("CMAKE_HOME_DIRECTORY", ".")).resolve()
    # the RTOS: the source tree itself, or, for an application outside it, the package
    # find_package(EmbLinkRTOS) found (EmbLinkRTOS_DIR is its cmake/ directory)
    package = c.get("EmbLinkRTOS_DIR", "")
    base = Path(c.get("EMB_BASE") or (Path(package).parent if package and "NOTFOUND" not in package
                                       else source_dir)).resolve()
    if not (base / "include/emb/version.h").is_file():
        for parent in [source_dir, *source_dir.parents]:
            if (parent / "include/emb/version.h").is_file():
                base = parent
                break
    in_tree = source_dir == base or base in source_dir.parents
    board = c.get("EMB_BOARD", "unknown")
    compiler = c.get("CMAKE_C_COMPILER", "cc")

    version_h = (base / "include/emb/version.h").read_text(encoding="utf-8")
    version = ".".join(re.search(rf"#define EMB_VERSION_{k} (\d+)", version_h)[1] for k in ("MAJOR", "MINOR", "PATCH"))
    commit, dirty, config_hash, build_id = build_identity(build)
    hw_hash, hw_inputs = hardware_hash(base, board)
    cc_id, cc_version, cc_line, cc_name = compiler_identity(build, compiler)
    created = "1970-01-01T00:00:00Z"
    if commit != "unknown":
        t = subprocess.run(["git", "show", "-s", "--format=%ct", commit], cwd=base, capture_output=True,
                           text=True, check=False).stdout.strip()
        if t.isdigit():
            import datetime
            created = datetime.datetime.fromtimestamp(int(t), datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    # what was linked
    members, objects = linked_inputs(map_path.read_text(encoding="utf-8", errors="replace"))
    db = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    by_output = {}
    for e in db:
        if "output" in e:
            by_output[resolve(e["directory"], e["output"])] = resolve(e["directory"], e["file"])
    deps = {resolve(build, k): [resolve(build, d) for d in v] for k, v in ninja_deps(build).items()}

    project_objects, toolchain = set(), {}
    for o in objects:
        p = resolve(build, o)
        if build in p.parents:
            project_objects.add(p)
        else:
            toolchain.setdefault(p, set())
    for archive, ms in members.items():
        a = resolve(build, archive)
        if build in a.parents:
            target = a.name[3:-2] if a.name.startswith("lib") else a.stem
            tdir = a.parent / "CMakeFiles" / f"{target}.dir"
            for out in by_output:
                if tdir in out.parents and out.name in ms:
                    project_objects.add(out)
        else:
            toolchain.setdefault(a, set()).update(ms)

    files = set()
    for o in project_objects:
        if o in by_output:
            files.add(by_output[o])
        files.update(deps.get(o, []))

    def owner(f):
        if base in f.parents and build not in f.parents:
            return "rtos" if in_tree or source_dir not in f.parents else "app"
        if source_dir in f.parents and build not in f.parents:
            return "app"
        if build in f.parents:
            return "generated"
        return None  # the toolchain's headers

    def name_of(f, kind):
        if kind == "generated":
            return "./build/" + f.relative_to(build).as_posix()
        root = source_dir if kind == "app" else base
        return "./" + f.relative_to(root).as_posix()

    listed = sorted(((owner(f), f) for f in files if owner(f) and f.is_file()), key=lambda x: name_of(x[1], x[0]))
    image_sha = sha(elf, "sha256")
    doc_files, rels = [], []
    pkg_files = {"rtos": [], "app": []}
    for i, (kind, f) in enumerate(listed, 1):
        fid = f"SPDXRef-File-{i}"
        lic = licence_of(f)
        doc_files.append({
            "SPDXID": fid, "fileName": name_of(f, kind),
            "checksums": [{"algorithm": "SHA1", "checksumValue": sha(f, "sha1")},
                          {"algorithm": "SHA256", "checksumValue": sha(f, "sha256")}],
            "licenseConcluded": "NOASSERTION", "licenseInfoInFiles": [lic], "copyrightText": "NOASSERTION",
            **({"comment": "generated by the build"} if kind == "generated" else {}),
        })
        pkg_files["app" if kind == "app" else "rtos"].append((fid, doc_files[-1]["checksums"][0]["checksumValue"]))

    def verification(entries):
        return {"packageVerificationCodeValue": hashlib.sha1("".join(sorted(s for _, s in entries)).encode()).hexdigest()}

    manifest = {"source_revision": commit + ("+dirty" if dirty else ""), "configuration_hash": config_hash,
                "toolchain": f"{cc_id} {cc_version}" + (f" ({cc_line})" if cc_line else ""),
                "hardware_description_sha256": hw_hash, "hardware_description_inputs": hw_inputs,
                "board": board, "build_id": build_id}
    packages = [{
        "SPDXID": "SPDXRef-Image", "name": elf.name, "versionInfo": version, "primaryPackagePurpose": "FIRMWARE",
        "downloadLocation": "NOASSERTION", "filesAnalyzed": False,
        "checksums": [{"algorithm": "SHA256", "checksumValue": image_sha}],
        "licenseConcluded": "NOASSERTION", "licenseDeclared": "NOASSERTION", "copyrightText": "NOASSERTION",
        "comment": "Build manifest (BLD-003): " + json.dumps(manifest, sort_keys=True),
    }, {
        "SPDXID": "SPDXRef-EmbLinkRTOS", "name": "EmbLinkRTOS", "versionInfo": version,
        "supplier": "Organization: EmbLinkOs", "primaryPackagePurpose": "SOURCE",
        "downloadLocation": f"git+https://github.com/EmbLinkOs/EmbLinkRTOS@{commit}" if commit != "unknown" else "NOASSERTION",
        "filesAnalyzed": bool(pkg_files["rtos"]),
        **({"packageVerificationCode": verification(pkg_files["rtos"])} if pkg_files["rtos"] else {}),
        "licenseConcluded": "NOASSERTION", "licenseDeclared": "Apache-2.0",
        "licenseInfoFromFiles": sorted({d["licenseInfoInFiles"][0] for d in doc_files
                                        if (d["SPDXID"], d["checksums"][0]["checksumValue"]) in pkg_files["rtos"]}) or ["NOASSERTION"],
        "copyrightText": "Copyright 2026 Junior Deogracias",
    }]
    rels += [{"spdxElementId": "SPDXRef-DOCUMENT", "relationshipType": "DESCRIBES", "relatedSpdxElement": "SPDXRef-Image"},
             {"spdxElementId": "SPDXRef-Image", "relationshipType": "CONTAINS", "relatedSpdxElement": "SPDXRef-EmbLinkRTOS"}]
    if pkg_files["app"]:
        packages.append({
            "SPDXID": "SPDXRef-Application", "name": source_dir.name, "versionInfo": "NOASSERTION",
            "downloadLocation": "NOASSERTION", "filesAnalyzed": True,
            "packageVerificationCode": verification(pkg_files["app"]),
            "licenseConcluded": "NOASSERTION", "licenseDeclared": "NOASSERTION",
            "licenseInfoFromFiles": sorted({d["licenseInfoInFiles"][0] for d in doc_files
                                            if (d["SPDXID"], d["checksums"][0]["checksumValue"]) in pkg_files["app"]}),
            "copyrightText": "NOASSERTION"})
        rels.append({"spdxElementId": "SPDXRef-Image", "relationshipType": "CONTAINS", "relatedSpdxElement": "SPDXRef-Application"})
    for pkg, entries in (("SPDXRef-EmbLinkRTOS", pkg_files["rtos"]), ("SPDXRef-Application", pkg_files["app"])):
        rels += [{"spdxElementId": pkg, "relationshipType": "CONTAINS", "relatedSpdxElement": fid} for fid, _ in entries]

    for n, (path, ms) in enumerate(sorted(toolchain.items(), key=lambda kv: kv[0].name + str(kv[0])), 1):
        pid = f"SPDXRef-Toolchain-{n}-{spdx_id(path.name)}"
        packages.append({
            "SPDXID": pid, "name": path.name, "versionInfo": f"{cc_id} {cc_version}",
            "downloadLocation": "NOASSERTION", "filesAnalyzed": False,
            "checksums": [{"algorithm": "SHA256", "checksumValue": sha(path, "sha256")}] if path.is_file() else [],
            "licenseConcluded": "NOASSERTION", "licenseDeclared": KNOWN_LICENCES.get(path.name, "NOASSERTION"),
            "copyrightText": "NOASSERTION",
            "comment": "From the toolchain" + (f"; members linked: {', '.join(sorted(ms))}" if ms else ""),
        })
        rels.append({"spdxElementId": "SPDXRef-Image", "relationshipType": "STATIC_LINK", "relatedSpdxElement": pid})
    packages.append({
        "SPDXID": "SPDXRef-Compiler", "name": cc_name, "versionInfo": cc_version,
        "comment": f"{cc_id}; --version: {cc_line}" if cc_line else cc_id,
        "downloadLocation": "NOASSERTION", "filesAnalyzed": False, "primaryPackagePurpose": "APPLICATION",
        "licenseConcluded": "NOASSERTION", "licenseDeclared": "NOASSERTION", "copyrightText": "NOASSERTION"})
    rels.append({"spdxElementId": "SPDXRef-Compiler", "relationshipType": "BUILD_TOOL_OF", "relatedSpdxElement": "SPDXRef-Image"})

    return {
        "spdxVersion": "SPDX-2.3", "dataLicense": "CC0-1.0", "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"{elf.name} ({board})",
        "documentNamespace": f"{NAMESPACE}/{elf.stem}-{image_sha[:16]}",
        "creationInfo": {"created": created, "creators": [f"Tool: {TOOL}"]},
        "packages": packages, "files": doc_files, "relationships": rels,
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build", required=True)
    ap.add_argument("--elf", required=True)
    ap.add_argument("--map")
    ap.add_argument("--out", help="default: the image's path with .spdx.json")
    args = ap.parse_args(argv)
    try:
        doc = generate(args.build, args.elf, args.map)
    except (SbomError, OSError, ValueError, KeyError) as e:
        print(f"sbom: {e}", file=sys.stderr)
        return 2
    out = Path(args.out or (str(Path(args.elf).with_suffix("")) + ".spdx.json"))
    out.write_text(json.dumps(doc, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    image = doc["packages"][0]
    tc = [p["name"] for p in doc["packages"] if p["SPDXID"].startswith("SPDXRef-Toolchain")]
    print(f"sbom: {out}: {len(doc['files'])} files, toolchain libraries {', '.join(tc) or 'none'}; {image['comment']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
