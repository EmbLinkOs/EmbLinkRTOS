<!-- SPDX-License-Identifier: Apache-2.0 -->
# Unreleased changes, one file each

A pull request with a user-visible change adds a file here instead of editing `CHANGELOG.md`. Name it after the branch, for example `fix-memory-report-lld.md`. Two pull requests then never edit the same lines, so neither has to be updated when the other merges.

The file holds one or more entries in the changelog's form:

```markdown
### Fixed (the memory report for Clang, 2026-11-08)

- `tools/footprint/memory_report.py` reads LLD's map as well as GNU ld's, so it accounts for Clang images.
```

The kinds are Added, Changed, Fixed, Removed, Deprecated, Security and Known limits. CI checks the form (`python3 tools/release/changes.py check`). At release, `python3 tools/release/changes.py collect` folds the files into `CHANGELOG.md`'s Unreleased section, newest first, and deletes them ([release checklist](../docs/engineering/releases.md)).
