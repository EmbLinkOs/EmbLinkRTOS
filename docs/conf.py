# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Sphinx configuration; invoke through tools/docs/build.py."""
import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("EMB_DOCS_BUILD", ROOT / "build/docs"))
project = "EmbLinkRTOS"
author = "EmbLinkRTOS contributors"
copyright = "2026, EmbLinkRTOS contributors"
version_header = (ROOT / "include/emb/version.h").read_text()
parts = [re.search(rf"^#define EMB_VERSION_{name}\s+(\d+)\b", version_header, re.M)[1]
         for name in ("MAJOR", "MINOR", "PATCH")]
version = ".".join(parts[:2])
release = ".".join(parts) + "-dev"
extensions = ["myst_parser", "breathe", "sphinx_needs"]
source_suffix = {".md": "markdown", ".rst": "restructuredtext"}
root_doc = "index"
myst_enable_extensions = ["colon_fence", "deflist"]
myst_heading_anchors = 6
exclude_patterns = [".emblink-generated"]
html_theme = "furo"
html_title = "EmbLinkRTOS development documentation"
html_static_path = ["_static"]
html_css_files = ["emblink.css"]
html_last_updated_fmt = None
html_show_sourcelink = True
breathe_projects = {"EmbLinkRTOS": str(BUILD / "doxygen/xml")}
breathe_default_project = "EmbLinkRTOS"
breathe_domain_by_extension = {"h": "c"}
needs_types = [{"directive": "req", "title": "Requirement", "prefix": "", "color": "#E3EEF8", "style": "node"}]
needs_id_regex = r"^[A-Z][A-Z0-9-]*-\d+$"
needs_fields = {
    "status": {"schema": {"type": "string", "enum": ["Proposed", "Accepted", "Implemented", "Verified", "Withdrawn"]}},
    "evidence": {"schema": {"type": "string", "enum": ["not evaluated"]}},
    "design_source": {"schema": {"type": "string"}},
}
needs_build_json = True
needs_reproducible_json = True
linkcheck_timeout = 15
linkcheck_retries = 1
linkcheck_anchors = False
