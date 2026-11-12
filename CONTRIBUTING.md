# Contributing to EmbLinkRTOS

Thank you for your interest. This page explains what kind of contributions are useful right now, the legal terms, and the process.

## Where to start

The kernel is implemented and runs on hardware; the first release is being prepared ([project status](docs/STATUS.md), [roadmap](docs/architecture/07-roadmap.md)). Build it and run its tests first: the [quick start](README.md#quick-start) takes a few minutes on Linux or macOS.

Good ways to contribute now, from the smallest:

- **Run the suites on a board you own** and report the result. A run on a new board, archived with `tools/hil/run_hil.py --archive`, is evidence the project cannot produce without that board ([hardware runs](docs/engineering/hil.md)).
- **Add a board.** A board is a description, not code: an SoC the project already supports needs a short YAML file ([hardware descriptions](docs/engineering/hwgen.md), [any-board plan](docs/engineering/any-board-plan.md)).
- **Documentation:** the [user guide](docs/user/index.md), examples, and corrections wherever the docs and the code disagree.
- **Tests and benchmarks:** a conformance case for a requirement that has none yet ([case evidence](docs/engineering/case-evidence.md)).
- **Review the specifications** against the code, and file an issue for each mismatch.
- **Drivers and ports**, after an issue agrees the approach: the next milestone's drivers are planned for the STM32F4 and RP2350.
- **Design changes**, written as an Architecture Decision Record (the format is in `docs/architecture/06-decision-records.md`), or answers to `docs/architecture/08-open-questions.md`.

Issues labelled [`good first issue`](https://github.com/EmbLinkOs/EmbLinkRTOS/labels/good%20first%20issue) are scoped for a first contribution. Ask on an issue before starting a large change. Everyone whose pull request has merged is listed in [CONTRIBUTORS.md](CONTRIBUTORS.md); add your row there in your first pull request.

Every source file follows [the coding standard](docs/CODING-STANDARD.md) and the specification it implements. A change to kernel semantics updates the specification, the reference model (`tools/model/`) and the conformance suite in the same pull request.

## Licence and the Developer Certificate of Origin

EmbLinkRTOS is licensed under the Apache License, Version 2.0 (see `LICENSE`). By contributing you agree that your contribution is licensed under the same licence: inbound licence equals outbound licence. There is no contributor licence agreement.

Every commit must carry a `Signed-off-by` line certifying the [Developer Certificate of Origin](DCO):

```
Signed-off-by: Your Name <your.email@example.com>
```

Add it with `git commit -s`. The Repository policy check requires a matching author trailer on every non-merge commit in the change. GitHub-generated merge commits are excluded; authored conflict-resolution changes belong in a separate signed-off commit. DCO certification is distinct from cryptographic signing.

Every C, assembly and linker-script source starts with this header (CODING-STANDARD CS-11.1):

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 <your name or organization> */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    my_driver.c
 * @brief   What the file is, in one sentence, with the specification sections it follows.
 *
 * @details
 * The rest of what it implements and why, as long as it needs.
 *
 * @see     my_driver.h
 * @author  Your Name
 * @date    2026-11-13 (created)
 */
```

`@author` is whoever created the file and `@date` the day it was created; neither changes later. `@see` names the file's companion header or source, where it has one. You can write the first three comments and a plain description comment, and let `python3 tools/dev/file_headers.py --write my_driver.c` turn it into the rest, with your name and today's date. CI runs `tools/dev/file_headers.py --check`.

Hardware descriptions, scripts, and other files start with the SPDX and copyright lines in their comment syntax.

## Process

1. Open an issue first for anything beyond a small fix, so the approach can be agreed before work starts. Architecture changes need an ADR.
2. Branch from `main`, keep each pull request to one logical change, and write commit messages that explain why, not only what.
3. Code changes must pass existing CI and add evidence appropriate to the changed contract. The complete target quality gates are described in `docs/architecture/05-engineering-system.md` §5: build matrix, conformance suite, static analysis, coverage, footprint thresholds, traceability, and documentation for any touched public API. Some gates remain unimplemented (STATUS.md); their absence is recorded as an evidence gap, not represented as a passing check.
4. A kernel semantics change must update the executable reference model and the conformance suite in the same pull request.
5. Reviews are by the owners listed in `.github/CODEOWNERS`.

Use the bug/feature issue forms and PR checklist. The required GitHub check names are **CI merge gate**, **Documentation build**, and **Repository policy**; the first aggregates the full existing compiler/port matrix plus lint/model/configuration jobs. See [Repository workflow](docs/engineering/repository-workflow.md) for DCO checks, limitations and activation of the `main` ruleset. A policy file alone does not enable branch protection.

For editor support, run `cmake --preset native-gcc` once: every configure writes `compile_commands.json`, and the repository's `.clangd` points clangd at that build so it sees the include paths, the generated headers and the `.clang-tidy` checks. Format with clang-format 18, the version the lint job runs; later versions lay out some existing files differently.

A user-visible change adds a file to [`changes/`](changes/README.md), not an edit of `CHANGELOG.md`, so pull requests do not conflict on the changelog. Maintainers merge through a merge queue, so a pull request never needs `main` merged in just to be up to date. Use the [release checklist](docs/engineering/releases.md) for versioning, artifacts, evidence, signing and support; no supported release is implied by the current development version.

## Hardware data and licences

SoC and board descriptions may be bootstrapped from CMSIS-SVD files and from DeviceTree sources. Import only from permissively licensed sources (for example Apache-2.0, BSD, or MIT, including dual-licensed files where one option is permissive) and record the source and its licence in the description file's header.

## Conduct

This project follows the [Code of Conduct](CODE_OF_CONDUCT.md).

## Security issues

Do not open public issues for vulnerabilities. See `SECURITY.md`.
