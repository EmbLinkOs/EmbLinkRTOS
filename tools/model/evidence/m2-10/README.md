# M2-10 campaign evidence

- `campaign-<runner>.json`: `tools/model/campaign.py` summaries (seeds 1 to 10, `--runs 40`, plus the timer regressions) with the commit, the tree's cleanliness and each runner's SHA-256.
- `coverage.json`: `tools/coverage/report.py` over the `native-coverage` build after its CTest run.
- `matrix-summary.json`: the per-configuration summary of `tools/evidence/matrix.py` over all 21 configurations' case evidence, with the implemented requirements that have no executed case.

The report is `docs/engineering/m2-10-campaign.md`.
