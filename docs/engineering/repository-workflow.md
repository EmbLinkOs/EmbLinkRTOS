# Repository workflow and required checks

Work branches start from reviewed `main`. Use the bug/feature forms in `.github/ISSUE_TEMPLATE/` and the pull-request template for scope, evidence, limitations and DCO certification. Architecture changes need an ADR; kernel semantics changes update the model, specification and conformance evidence together. Read [Contributing](https://github.com/EmbLinkOs/EmbLinkRTOS/blob/main/CONTRIBUTING.md), [Project status](../STATUS.md) and the [M2 work order](m2-checklist.md).

## Three stable merge checks

| Required check name | Workflow/job | What it actually checks |
|---|---|---|
| `CI merge gate` | `ci.yml` / `merge_gate` | All native, Cortex-M, RISC-V, AVR matrix jobs and lint must succeed |
| `Documentation build` | `docs.yml` / `build` | Pinned docs toolchain, integrity tests, complete API annotations, strict site build and source preservation |
| `Repository policy` | `repository.yml` / `policy` | DCO checker tests and matching author sign-off trailers on the change's non-merge commits |

The CI merge gate depends on the entire existing matrix: six native presets, four Cortex-M presets, three RV32 presets, three AVR presets, and lint/model/configuration checks. It runs even after a failed dependency and accepts only `success`; skipped, cancelled or failed dependencies are failures. Every CTest invocation uses `--no-tests=error`. A missing emulator/test registration therefore cannot become a passing empty run. CI explicitly selects Bash, including pipeline failure propagation, so a footprint tool failure cannot be hidden by a successful `tee`. The individual matrix checks remain visible for diagnosis; require the aggregate stable name rather than trying to maintain every expanded matrix name in GitHub settings.

The documentation publishing `deploy` job is manual and **not** a merge requirement. Normal PRs must build the docs but do not publish a website. Workflows run on every PR and main push without path filters, so a required check is not silently absent for documentation-only changes. The repository does not use a merge queue; enabling one later requires `merge_group` triggers and equivalent checks first.

Official checkout, Python setup and artifact/Pages actions are pinned to full reviewed release commit SHAs, with version comments. They use the supported Node 24 runtime on GitHub-hosted Ubuntu 24.04; update pins by checking the official release/runtime compatibility and rerunning the affected workflows. Pinning these actions does not pin every compiler/package or establish release reproducibility.

These checks do not yet enforce complete statement/branch coverage, semantic requirement traceability, clang-tidy/cppcheck, reproducibility, SBOM generation, fuzzing or HIL. Those remain delivery/release gates with explicit evidence obligations. The presence of a workflow or ruleset file is not proof that a remote run passed or that GitHub settings are active.

## DCO certification

Use `git commit -s` with your own Git author identity. `tools/repository/check_dco.py` parses the actual footer trailer block and requires a `Signed-off-by` matching the commit author. A reviewer's sign-off or a quoted example in the message body does not certify the author. DCO is a legal certification, not a cryptographic commit/tag signature.

On a PR, the check covers base SHA through head SHA. On a main push, it covers the previous SHA through the new SHA. A first push without a previous revision checks the latest non-merge commit. Merge commits are excluded to accommodate GitHub-generated merges; put any authored conflict-resolution changes into a separate signed-off commit. Squash/rebase merges must preserve a matching author trailer in the resulting authored commits; prefer a merge commit when preserving existing per-commit certification. A passing PR DCO check does not manufacture certification for a later unsigned squash commit.

Local check, from the repository root:

```sh
python3 -m unittest discover -s tools/repository -p 'test_*.py' -v
python3 tools/repository/check_dco.py --base "$(git rev-parse main)" --head HEAD
```

Keep the failing commit intact while diagnosing it; if it belongs to your unmerged branch, add the correct certification according to the DCO and update that branch. Do not add another person's certification on their behalf.

## Main branch protection

The reviewed JSON policy is [`.github/rulesets/main.json`](https://github.com/EmbLinkOs/EmbLinkRTOS/blob/main/.github/rulesets/main.json). It defines an **active branch ruleset for `refs/heads/main`**:

- Require a pull request, passing checks and a branch up to date with `main`.
- Require resolution of review conversations.
- Block branch deletion and non-fast-forward pushes.
- Require the three named checks from GitHub Actions.
- Include administrators by providing no bypass actors.

The initial approval count is **zero** because CODEOWNERS currently has one maintainer, who cannot approve their own PR. Human review still follows CONTRIBUTING; this setting avoids an impossible self-approval requirement. When another active maintainer joins, update CODEOWNERS and set at least one approval plus required code-owner review. Cryptographically signed commits are not required by this initial ruleset; signed release tags are a separate release obligation. Merge, squash and rebase methods are allowed, subject to preserving DCO certification.

A repository administrator activates this policy after the new workflows have run on a PR:

1. Inspect **Settings → Rules → Rulesets** and any classic branch protection or organization rules. Preserve existing stronger protections.
2. Import the JSON using **New ruleset → Import a ruleset**, or create a branch ruleset named **EmbLinkRTOS main** with the settings above. Verify the target is `main` and enforcement is Active.
3. Confirm required checks are exactly `CI merge gate`, `Documentation build` and `Repository policy`, sourced from GitHub Actions. The JSON uses GitHub.com's GitHub Actions integration ID `15368`. If creating the rule manually, choose GitHub Actions as the source after the first run.
4. Confirm no bypass actors, force pushes or deletions are enabled. Check how any existing organization/classic rules combine with it; this policy must not remove them.
5. Open a PR and verify the merge box requires these checks and resolved conversations. A deliberately failing check must block merge. Do not disable a requirement to work around an unclassified failure.

With authenticated GitHub CLI/API administration access, inspect existing rulesets with `gh api repos/EmbLinkOs/EmbLinkRTOS/rulesets`. If no repository ruleset with this name exists, the JSON can be submitted as a new additive policy:

```sh
gh api --method POST repos/EmbLinkOs/EmbLinkRTOS/rulesets \
  --input .github/rulesets/main.json
```

Read the returned ruleset by ID and the effective branch rules to verify activation. Do not repeatedly POST duplicate policies or overwrite an existing ruleset without inspecting it. A checked-in JSON file does not activate anything. API administration requires access to `api.github.com` and adequate repository permissions; Git clone/push access alone does not establish that permission.

Private repositories can require a GitHub plan that supports rulesets/protected branches. If GitHub rejects activation for permissions or plan eligibility, record it as blocked rather than claiming protection. The same three-check policy can be configured through eligible classic branch protection; avoid requiring both duplicated policies accidentally.

During preparation, the API exposed an existing `Ruleset 1` with an empty branch include list and no effective ruleset rules returned for `main`. It was left unchanged. Creating the new policy was rejected with `Resource not accessible by integration` (HTTP 403), despite the current user's repository administrator role: the integration itself lacks the required API administration permission. The JSON policy is therefore prepared, not activated by this change. An administrator can import it through the GitHub UI above; do not assume a passing CI run or successful Git push changed that state.

## Before merging

Confirm the three checks ran on the current PR revision, read case-level skips/known limits, resolve feedback and update the Unreleased changelog for user-visible changes. Passing the minimum checks does not override a relevant unimplemented release gate or known semantic failure. Keep the source SHA and workflow artifacts with any validation claim. Publishing the updated docs is a separate reviewed manual operation after merge.
