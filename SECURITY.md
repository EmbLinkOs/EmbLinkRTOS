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

EmbLinkRTOS has delivered its M1 and M2 milestones and is preparing its first release, but has no released versions or declared security-supported release lines yet. The support policy is described in `docs/architecture/05-engineering-system.md` §7 and the release checklist in `docs/engineering/releases.md`. Each actual release line will state its concrete support window and end-of-life date here; no standing LTS cadence or unstaffed maintenance guarantee is implied.

## For manufacturers: the EU Cyber Resilience Act

The Cyber Resilience Act's obligations fall mainly on manufacturers that place products with digital elements on the EU market. Since 11 September 2026 they must report actively exploited vulnerabilities and severe incidents; the remaining obligations apply from 11 December 2027. For a product built on EmbLinkRTOS, the project provides:

- **Advisories** through GitHub Security Advisories for this repository, with a CVE identifier for each confirmed vulnerability, the affected versions or commits, and the fix.
- **A vulnerability known to be actively exploited** is handled ahead of others, and its advisory is published as soon as a fix or a mitigation exists, so manufacturers can meet their own reporting deadlines.
- **An SBOM for an image**: `tools/sbom/sbom.py` writes an SPDX 2.3 SBOM with the build manifest for any build ([SBOM](docs/engineering/sbom.md)). Each release will carry its own.
- **Reproducible builds**, so a manufacturer can check that an image comes from the source it claims ([reproducible builds](docs/engineering/reproducible-builds.md)).

If you build a product on EmbLinkRTOS and find a vulnerability that affects it, report it as above, and say whether it is exploited, so it can be handled first.

## Scope

The kernel, architecture ports, SoC and board support, drivers, tooling, generated code, and the build and release pipeline. Vulnerabilities in third-party middleware should be reported to that project; please also tell us so we can track the dependency in our software bill of materials.
