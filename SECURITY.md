# Security Policy

## Reporting a vulnerability

Please do not report security vulnerabilities through public issues, discussions, or pull requests.

Use GitHub's private vulnerability reporting for this repository (Security tab, "Report a vulnerability"). If that is unavailable to you, contact the maintainer listed in `.github/CODEOWNERS` directly and mark the message as security related.

Include what you can of: affected component and version or commit, a description of the issue and its impact, steps or code to reproduce, and any suggested fix.

## What to expect

- Acknowledgement of your report within 7 days.
- An assessment and a plan, or a request for more information, within 14 days of acknowledgement.
- Coordinated disclosure: we aim to publish a fix and an advisory within 90 days of the report, and will agree the timeline with you. Credit is given unless you prefer otherwise.

## Supported versions

EmbLinkRTOS has an implemented M1 kernel subset and is completing M2, but has no released versions or declared security-supported release lines. The support policy is described in `docs/architecture/05-engineering-system.md` §7 and the release checklist in `docs/engineering/releases.md`. Each actual release line will state its concrete support window and end-of-life date here; no standing LTS cadence or unstaffed maintenance guarantee is implied.

## Scope

The kernel, architecture ports, SoC and board support, drivers, tooling, generated code, and the build and release pipeline. Vulnerabilities in third-party middleware should be reported to that project; please also tell us so we can track the dependency in our software bill of materials.
