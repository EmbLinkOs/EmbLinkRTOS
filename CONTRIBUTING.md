# Contributing to EmbLinkRTOS

Thank you for your interest. This page explains what kind of contributions are useful right now, the legal terms, and the process.

## Current phase: Milestone 1, first execution

The architecture is accepted (`docs/architecture/`, `docs/specs/`, `docs/requirements/`) and the code of Milestone 1 of the [roadmap](docs/architecture/07-roadmap.md) is being written: the configuration and build system, the public headers, the kernel core, the native and AVR ports, and the conformance suite. Every source file follows [the coding standard](docs/CODING-STANDARD.md) and the specification it implements; a change to kernel semantics updates the specification, the reference model (`tools/model/`), and the conformance suite in the same pull request.

Valuable contributions now:

- review comments and issues on the specifications and on the code against them;
- proposals for new or changed decisions, written as an Architecture Decision Record (see the format in `docs/architecture/06-decision-records.md`);
- answers and evidence for the items in `docs/architecture/08-open-questions.md`;
- experience reports from other RTOSes that bear on a specific decision;
- once the ports exist: architecture ports, SoC and board descriptions, drivers, tooling, tests, benchmarks, and documentation.

The core kernel (scheduler, wait protocol, synchronization, object model) is written and maintained by the project owner so that its semantics stay understood end to end; proposals there are welcome as ADRs and reference-model changes first.

## Licence and the Developer Certificate of Origin

EmbLinkRTOS is licensed under the Apache License, Version 2.0 (see `LICENSE`). By contributing you agree that your contribution is licensed under the same licence: inbound licence equals outbound licence. There is no contributor licence agreement.

Every commit must carry a `Signed-off-by` line certifying the [Developer Certificate of Origin](DCO):

```
Signed-off-by: Your Name <your.email@example.com>
```

Add it with `git commit -s`. Use your real name and a working email address. Pull requests with unsigned commits are not merged.

Every source file starts with an SPDX header:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 <your name or organization> */
```

Hardware descriptions, scripts, and other files use the same two lines in their comment syntax.

## Process

1. Open an issue first for anything beyond a small fix, so the approach can be agreed before work starts. Architecture changes need an ADR.
2. Branch from `main`, keep each pull request to one logical change, and write commit messages that explain why, not only what.
3. Code changes must pass the quality gates in `docs/architecture/05-engineering-system.md` §5: build matrix, conformance suite, static analysis, coverage, footprint thresholds, traceability, and documentation for any touched public API.
4. A kernel semantics change must update the executable reference model and the conformance suite in the same pull request.
5. Reviews are by the owners listed in `.github/CODEOWNERS`.

## Hardware data and licences

SoC and board descriptions may be bootstrapped from CMSIS-SVD files and from DeviceTree sources. Import only from permissively licensed sources (for example Apache-2.0, BSD, or MIT, including dual-licensed files where one option is permissive) and record the source and its licence in the description file's header.

## Conduct

This project follows the [Code of Conduct](CODE_OF_CONDUCT.md).

## Security issues

Do not open public issues for vulnerabilities. See `SECURITY.md`.
