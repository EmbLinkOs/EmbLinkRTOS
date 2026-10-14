# Changelog

User-visible changes are recorded here. There are no released versions or release tags in the reviewed repository; `0.2.0` in the headers identifies development, not a supported release. Move relevant Unreleased entries into a dated version section only when the [release checklist](docs/engineering/releases.md) is complete. Record support scope and known limits with each release.

## Unreleased

### Added

- Searchable Sphinx documentation with native user tutorials, extracted C API, a design-requirement catalog and downloadable metadata.
- Strict documentation builds with complete extracted API annotations and integrity checks, plus optional GitHub Pages deployment.
- Bug/feature issue forms, a pull-request checklist and an ordered M2 implementation checklist.
- Stable CI merge checks, authored-commit DCO checking and a reviewable `main` branch ruleset policy.

### Changed

- Reconciled design contracts, current implementation limits, timing/evidence obligations and the staged first-release scope.
- Clarified API parameter/storage lifetimes, wait outcomes, configuration and current interrupt-priority/thread-index limitations.
- CI test commands reject zero registered tests instead of accepting an empty run.

### Known limits

- The blocking kernel remains incomplete: event flags, condition variables, work queues, software timers and message queues are planned.
- Deep differential failures and known port/introspection inconsistencies remain open. No physical-board, isolation, universal requirement coverage or supported-release claim is made.
- Repository policy files do not activate GitHub settings themselves. Consult the [repository workflow](docs/engineering/repository-workflow.md) and verify the live ruleset and check results.

See [Project status](docs/STATUS.md) for the complete implementation/evidence inventory. This changelog begins with the documentation/workflow preparation; it does not invent a historical release for the existing M1 source.
