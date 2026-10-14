#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Build manuals, extracted C API and a design-requirement catalog without a kernel build."""
import argparse
import csv
import json
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from bootstrap import BINARY, VERSION

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "docs"
FIELDS = ("Statement", "Rationale", "Status", "Verification", "Trace")


def validate_api_annotations(records):
    """Reject incomplete declaration documentation without inferring behavior evidence."""
    failures = []
    for record in records:
        missing = []
        for key, label in (("missing_tags", "contract tags"),
                           ("missing_parameters", "parameter descriptions"),
                           ("unresolved_requirement_ids", "requirement IDs")):
            if record[key]:
                missing.append(f"{label}: {', '.join(record[key])}")
        if missing:
            failures.append(record["function"] + " (" + "; ".join(missing) + ")")
    if failures:
        raise ValueError("Incomplete API documentation:\n" + "\n".join(failures))


def project_version():
    text = (ROOT / "include/emb/version.h").read_text()
    parts = []
    for name in ("MAJOR", "MINOR", "PATCH"):
        value = re.search(rf"^#define EMB_VERSION_{name}\s+(\d+)\b", text, re.M)
        if not value:
            raise ValueError(f"Missing EMB_VERSION_{name} in version header")
        parts.append(value[1])
    return ".".join(parts) + "-dev"


def contract_tags(block):
    """Read contract fields, including comment continuations reflowed by clang-format."""
    clean = re.sub(r"^\s*\* ?", "", block, flags=re.M)
    tags = {}
    active = None
    for line in clean.splitlines():
        tag = re.match(r"^[ \t]*@(ctx|blocks|time|owns|config|req|since|stable)(?:[ \t]+([^\n]*))?$", line)
        if tag:
            active = tag[1]
            tags[active] = (tag[2] or "").strip()
        elif not line.strip() or line.lstrip().startswith("@"):
            active = None
        elif active is not None:
            tags[active] += " " + line.strip()
    return tags


def prepare_headers(output):
    """Translate the existing @argument comment style without editing public headers."""
    folder = output / "doxygen-input"
    folder.mkdir(exist_ok=True)
    contracts = {"ctx", "blocks", "time", "owns", "config", "req", "since", "stable", "isr-note"}
    standard = {"param", "return", "returns", "brief", "note", "warning", "deprecated", "see", "ref"}
    def comment(match):
        text = match[0]
        text = re.sub(r"@(\w+):", lambda m: f"@param {m[1]}", text)
        lines = []
        for line in text.splitlines():
            tag = re.match(r"\s*\*\s*@([\w-]+)(?:\s|$)", line)
            if not tag or tag[1] not in contracts:
                line = re.sub(r"@([A-Za-z_][\w-]*)", lambda m: m[0] if m[1] in standard else rf"\p {m[1]}", line)
            lines.append(line)
        return "\n".join(lines)
    for header in sorted((ROOT / "include/emb").glob("*.h")):
        if header.name in {"arch.h", "compiler.h"}:
            continue
        text = re.sub(r"/\*\*.*?\*/", comment, header.read_text(), flags=re.S)
        (folder / header.name).write_text(text)


def requirement_catalog():
    records = []
    for path in sorted((DOCS / "requirements").glob("*.md")):
        if path.name == "README.md":
            continue
        text = path.read_text()
        for line in text.splitlines():
            if line.startswith("### ") and not re.fullmatch(r"### [A-Z][A-Z0-9-]*-\d+  .+", line):
                raise ValueError(f"{path.name}: malformed requirement heading: {line}")
        headings = list(re.finditer(r"^### ([A-Z][A-Z0-9-]*-\d+)  (.+)$", text, re.M))
        for i, match in enumerate(headings):
            block = text[match.end():headings[i + 1].start() if i + 1 < len(headings) else len(text)]
            values = {}
            for field in FIELDS:
                value = re.search(rf"\*\*{field}\.\*\*\s*(.*?)(?=\n\*\*(?:{'|'.join(FIELDS)})\.\*\*|\Z)", block, re.S)
                if not value or not value.group(1).strip():
                    raise ValueError(f"{path.name}: {match[1]} missing {field}")
                values[field.lower()] = value.group(1).strip()
            status = values["status"].split()[0]
            if status not in {"Proposed", "Accepted", "Implemented", "Verified", "Withdrawn"}:
                raise ValueError(f"{match[1]}: unknown status {status}")
            records.append({"id": match[1], "title": match[2], "group": match[1].rsplit("-", 1)[0],
                            "document": path.relative_to(DOCS).as_posix(), **values,
                            "design_status": status, "evidence": "not evaluated"})
    ids = {r["id"] for r in records}
    if len(ids) != len(records):
        raise ValueError("Duplicate requirement IDs")
    if not records:
        raise ValueError("No requirements extracted")
    for record in records:
        record["links"] = sorted((set(re.findall(r"\b[A-Z][A-Z0-9-]*-\d+\b", record["trace"])) & ids) - {record["id"]})
    return records


def generate_requirements(source, output, records):
    folder = source / "_generated/requirements"
    folder.mkdir(parents=True)
    overview = """# Requirement catalog

This catalog is generated from the normative Markdown sources. Design status is separate from execution evidence: no requirement is marked tested by extracting it or finding its ID in a header. All evidence here is **not evaluated**. The executed-case/profile matrix remains a separate M2 deliverable.

Download: {download}`JSON <../../_exports/requirements.json>` or {download}`CSV <../../_exports/requirements.csv>`.

```{needtable}
:columns: id;title;status;evidence;design_source
```

```{toctree}
:maxdepth: 1

"""
    for group in sorted({r["group"] for r in records}):
        overview += group + "\n"
        page = f"# {group} requirements\n\n"
        for record in (r for r in records if r["group"] == group):
            page += f"::::{{req}} {record['title']}\n:id: {record['id']}\n:status: {record['design_status']}\n:evidence: not evaluated\n:design_source: {record['document']}\n"
            if record["links"]:
                page += ":links: " + ";".join(record["links"]) + "\n"
            page += "\n" + record["statement"] + "\n\n"
            for field in ("rationale", "status", "verification", "trace"):
                page += f"**{field.capitalize()}:** {record[field]}\n\n"
            page += f"[Normative source](../../{record['document']}).\n\n::::\n\n"
        (folder / f"{group}.md").write_text(page)
    (folder / "index.md").write_text(overview + "```\n")
    export = output / "catalog"
    export.mkdir(exist_ok=True)
    (export / "requirements.json").write_text(json.dumps(records, indent=2, sort_keys=True) + "\n")
    with (export / "requirements.csv").open("w", newline="") as stream:
        keys = ["id", "title", "document", "design_status", "evidence", "statement", "verification", "trace"]
        writer = csv.DictWriter(stream, fieldnames=keys, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(records)


def generate_api(source, output):
    folder = source / "_generated/api"
    folder.mkdir(parents=True)
    index = """# Current C API reference

Doxygen extracts declarations and comments from the current public headers; Breathe renders them here. Planned APIs mentioned in specifications are not fabricated. Feature-gated declarations are shown for documentation; availability still depends on each API's configuration contract. Generated storage sizes are configuration-dependent and are not fixed ABI promises.

The JSON annotation audit lists contract tags, parameter descriptions and unresolved requirement IDs. The strict build rejects omissions and unresolved IDs. Header links are declaration-level design references, not proof that the complete linked requirement is implemented or tested. Human review is still needed for semantic accuracy.

Download: {download}`Annotation audit <../../_exports/api-annotations.json>`.

```{toctree}
:maxdepth: 1

audit
"""
    xml = output / "doxygen/xml"
    records = []
    metadata = {}
    for header in sorted((ROOT / "include/emb").glob("*.h")):
        for block in re.findall(r"/\*\*(.*?)\*/", header.read_text(), re.S):
            clean = re.sub(r"^\s*\* ?", "", block, flags=re.M)
            name = re.search(r"\b(emb_[A-Za-z0-9_]+)\(\)", clean)
            if name:
                metadata[name[1]] = contract_tags(block)
    for compound in ET.parse(xml / "index.xml").getroot().findall("compound"):
        if compound.attrib["kind"] != "file":
            continue
        name = compound.findtext("name")
        index += name.removesuffix(".h") + "\n"
        (folder / (name.removesuffix(".h") + ".md")).write_text(f"# {name}\n\n```{{doxygenfile}} {name}\n:project: EmbLinkRTOS\n```\n")
        details = ET.parse(xml / (compound.attrib["refid"] + ".xml"))
        for member in details.findall(".//memberdef[@kind='function']"):
            func = member.findtext("name")
            tags = metadata.get(func, {})
            records.append({"function": func, "header": name, "tags": tags,
                            "missing_tags": sorted(set(("ctx", "blocks", "time", "owns", "config", "req", "since", "stable")) - tags.keys()),
                            "missing_parameters": sorted({p.findtext("declname") for p in member.findall("param") if p.findtext("declname")} - {p.text for p in member.findall(".//parametername")}),
                            "evidence": "not evaluated"})
    known_ids = {r["id"] for r in requirement_catalog()}
    for record in records:
        record["unresolved_requirement_ids"] = sorted(set(re.findall(r"\b[A-Z][A-Z0-9-]*-\d+\b", record["tags"].get("req", ""))) - known_ids)
    audit = "# API documentation audit\n\nThis is documentation metadata, not test coverage or proof of semantic accuracy. Missing parameters refer to extracted parameter descriptions; inline references alone are not complete parameter documentation. Every declared requirement ID must resolve in the normative catalog. Related requirements may describe a broader target than the current implementation.\n\n"
    audit += f"{len(records)} extracted functions; {sum(not r['missing_tags'] for r in records)} have all eight contract tags.\n\n"
    audit += f"Functions missing parameter descriptions: {sum(bool(r['missing_parameters']) for r in records)}. Functions with unresolved requirement IDs: {sum(bool(r['unresolved_requirement_ids']) for r in records)}.\n\n"
    audit += "| Function | Header | Missing tags | Missing parameter descriptions | Unresolved requirement IDs |\n|---|---|---|---|---|\n"
    for record in records:
        if any(record[key] for key in ("missing_tags", "missing_parameters", "unresolved_requirement_ids")):
            audit += f"| `{record['function']}` | `{record['header']}` | {', '.join(record['missing_tags']) or 'none'} | {', '.join(record['missing_parameters']) or 'none'} | {', '.join(record['unresolved_requirement_ids']) or 'none'} |\n"
    (folder / "audit.md").write_text(audit)
    (folder / "index.md").write_text(index + "```\n")
    if not records:
        raise ValueError("Doxygen extracted no API functions")
    (output / "catalog/api-annotations.json").write_text(json.dumps(records, indent=2, sort_keys=True) + "\n")
    validate_api_annotations(records)
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--builder", choices=("html", "linkcheck", "needs"), default="html")
    parser.add_argument("--output", type=Path, default=ROOT / "build/docs")
    parser.add_argument("--doxygen", default=str(BINARY))
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "build") or output == ROOT / "build":
        raise SystemExit("Output must be a dedicated directory below the ignored repository build/ tree")
    actual = subprocess.check_output([args.doxygen, "--version"], text=True).strip()
    if actual.split()[0] != VERSION:
        raise SystemExit(f"Doxygen {VERSION} required; found {actual}")
    records = requirement_catalog()
    output.mkdir(parents=True, exist_ok=True)
    source = output / "source"
    marker = source / ".emblink-generated"
    if source.exists():
        if not marker.is_file():
            raise SystemExit(f"Refusing to overwrite unowned source directory: {source}")
        shutil.rmtree(source)
    source.mkdir()
    marker.write_text("Generated by tools/docs/build.py; editable sources are in docs/.\n")
    for original in DOCS.rglob("*.md"):
        target = source / original.relative_to(DOCS)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, target)
    generate_requirements(source, output, records)
    prepare_headers(output)
    env = dict(os.environ, EMB_DOCS_REPO=str(ROOT), EMB_DOCS_BUILD=str(output), EMB_DOCS_VERSION=project_version())
    env.setdefault("SOURCE_DATE_EPOCH", subprocess.check_output(["git", "log", "-1", "--format=%ct"], cwd=ROOT, text=True).strip())
    subprocess.run([args.doxygen, str(DOCS / "Doxyfile")], cwd=ROOT, env=env, check=True)
    apis = generate_api(source, output)
    exports = source / "_exports"
    exports.mkdir()
    for catalog in (output / "catalog").glob("*"):
        shutil.copyfile(catalog, exports / catalog.name)
    subprocess.run([sys.executable, "-m", "sphinx", "-b", args.builder, "-c", str(DOCS), "-d", str(output / "doctrees"),
                    "-W", "--keep-going", "-E", "-a", str(source), str(output / args.builder)], cwd=ROOT, env=env, check=True)
    print(f"Built {args.builder}: {len(records)} requirements and {len(apis)} API declarations; evidence not evaluated")


if __name__ == "__main__":
    main()
