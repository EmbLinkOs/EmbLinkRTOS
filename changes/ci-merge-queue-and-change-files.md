### Changed (merging without updating every pull request, 2026-11-08)

- A user-visible change records its changelog entry as one file in `changes/`, not as an edit of `CHANGELOG.md`; `tools/release/changes.py` checks the files in CI and folds them into the changelog at release. Two pull requests no longer conflict on the changelog's first lines.
- The workflows run on `merge_group`, and `.github/rulesets/main.json` replaces "require branches to be up to date" with a merge queue, which tests each pull request on the latest `main` before merging it ([repository workflow](docs/engineering/repository-workflow.md)). The repository's live ruleset changes when an administrator applies it.
