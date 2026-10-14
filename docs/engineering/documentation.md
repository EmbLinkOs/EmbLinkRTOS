# Building and maintaining documentation

**Status:** Documentation tooling configured in October 2026. The site covers current user guidance, design documents, extracted C declarations and the normative requirement catalog. It does not claim every promised user manual or executed-evidence matrix is complete.

## Toolchain and layout

Sphinx builds the site; MyST reads the existing Markdown; Furo provides navigation/search and light/dark presentation. Doxygen 1.15.0 extracts the current C headers into XML, and Breathe renders that XML. Sphinx Needs renders structured requirement cards and their declared requirement links. HTML, catalogs, staged Markdown, normalized header copies and XML are generated under ignored `build/docs/`.

Edit authored Markdown in `docs/`, never `build/docs/source` or generated HTML. User tutorials and operational guidance belong in `docs/user`; design rationale in architecture/ADRs; normative statements in requirements; subsystem detail in specs; port implementation in ports; engineering process here; research with cited sources in research. Keep group `index.md` toctrees current when adding a page: strict Sphinx builds catch orphan pages and broken internal references.

The existing header comments use `@argument` prose alongside project contract tags. The build normalizes argument references/parameter labels in generated copies for Doxygen, preserving the original public headers. Doxygen's documentation view expands opaque handle wrappers and compiler annotation macros; it does not define a new ABI or require a target build. Parameter/contract omissions and unresolved requirement IDs are recorded in the API annotation audit and fail the strict build. Complete metadata is documentation coverage, not verified behavior.

## Install once

Use Python 3.12 or newer with the pinned tool versions. The automatic Doxygen installer supports Linux x86-64, including the cloud machine and CI. On another platform install trusted Doxygen 1.15.0 and supply its path with `--doxygen`.

```sh
python3 -m venv .venv-docs
.venv-docs/bin/python -m pip install -r docs/requirements.txt
python3 tools/docs/bootstrap.py
```

The Doxygen installer checks the official release archive against its pinned SHA-512 before extraction; the checksum was published in RIOT's official Doxygen build configuration for the same release. It uses TLS verification, a safe archive filter and an ignored local installation. Rerunning installation reuses the installed version; a mismatch or incomplete install stops for diagnosis. Network access is needed for installation, not the normal local build.

`docs/requirements.in` lists direct dependencies; `requirements.txt` pins the tested resolved Python closure. Refresh it in a clean temporary virtual environment, review the dependency changes, then rerun the strict builds. Do not generate a lock from an environment containing unrelated application packages.

## Build and checks

From the repository root:

```sh
.venv-docs/bin/python tools/docs/build.py
```

The build validates unique requirement IDs, required fields and design status; regenerates deterministic JSON/CSV catalogs; extracts actual API functions; and runs Sphinx with warnings as errors. It must extract a nonzero number of requirements/functions and complete with a successful exit status. Open `build/docs/html/index.html` from the local filesystem or use the CI artifact. Public hosting is a separate operation.

Additional builders:

```sh
.venv-docs/bin/python tools/docs/build.py --builder needs
.venv-docs/bin/python tools/docs/build.py --builder linkcheck
```

`needs` produces the requirement graph export. `linkcheck` requires network access; failures may indicate upstream availability or stale links and must be diagnosed rather than suppressed. Anchor checks on external sites are excluded because upstream dynamic rendering makes them unreliable; internal Sphinx references still undergo build validation. A normal HTML build has no external-link requests.

Outputs include:

| Artifact | Meaning |
|---|---|
| `html/` | Searchable static site, API reference and requirement cards |
| `doxygen/xml/` | Extracted declaration/comment model |
| `catalog/requirements.json`, `.csv` | Normative source records and known requirement relations |
| `catalog/api-annotations.json` | Extracted declarations, available contract tags and missing documentation fields |
| `html/needs.json` or `needs/needs.json` | Sphinx Needs catalog export |

Design status and evidence remain independent. Extraction never turns Accepted into Verified, infers implementation from a header link, or establishes test coverage. The existing requirements can name planned paths. API annotations must resolve into the normative catalog, but a related requirement may be broader than the implemented portion; the audit cannot detect all semantic errors. Human review and the future executed-case matrix remain necessary. User tutorials and explicit implementation limits are maintained in `docs/user/`.

## CI and publishing

`.github/workflows/docs.yml` builds the full site on main pushes and pull requests, then uploads HTML and catalogs as an artifact. Doxygen or Sphinx warnings fail the job. CI also checks that generation leaves tracked files unchanged.

Publishing is opt-in. Merge the reviewed tooling branch into the default branch first so GitHub can discover the manual workflow. A repository administrator selects **GitHub Pages → Source: GitHub Actions** in repository settings. Run the Documentation workflow manually on the desired reviewed branch with **Publish site** enabled. The deploy job has Pages/id-token permissions and the `github-pages` environment; repository rules may require approval. Ordinary pushes and pull requests do not deploy. No Pages site is assumed to exist until a deployment succeeds and returns its URL.

This pipeline publishes a single development documentation snapshot. A tagged/versioned documentation switcher and PDF manual are separate later additions, once release branches and supported content exist. Preserve the matching source revision, tools and configuration with published artifacts; the source commit timestamp makes build dates reproducible, but documentation generation alone is not firmware reproducibility evidence.
