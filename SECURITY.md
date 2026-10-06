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

EmbLinkRTOS is in its architecture phase and has no released versions. The support and long-term support policy that will apply from 1.0 is described in `docs/architecture/05-engineering-system.md` §7. From the first release, each release line will state its security support window here.

## Scope

The kernel, architecture ports, SoC and board support, drivers, tooling, generated code, and the build and release pipeline. Vulnerabilities in third-party middleware should be reported to that project; please also tell us so we can track the dependency in our software bill of materials.
