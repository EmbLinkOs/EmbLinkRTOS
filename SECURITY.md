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

| Version | Supported | Until |
|---|---|---|
| 0.2.x (preview) | Security fixes, best effort, by one maintainer | the next minor release (0.3.0), when 0.2.x ends |
| development (`main`) | Fixes land here first | — |

0.2.0 is a preview: its API and formats may change before 1.0 ([release notes](docs/engineering/release-0.2.0.md)). A fix to a supported line is a patch release (0.2.1, …) with its own signed tag. There is no long-term support; each release line states its window here. The support policy is described in `docs/architecture/05-engineering-system.md` §7 and the release checklist in `docs/engineering/releases.md`.

## For manufacturers: the EU Cyber Resilience Act

The Cyber Resilience Act's obligations fall mainly on manufacturers that place products with digital elements on the EU market. Since 11 September 2026 they must report actively exploited vulnerabilities and severe incidents; the remaining obligations apply from 11 December 2027. For a product built on EmbLinkRTOS, the project provides:

- **Advisories** through GitHub Security Advisories for this repository, with a CVE identifier for each confirmed vulnerability, the affected versions or commits, and the fix.
- **A vulnerability known to be actively exploited** is handled ahead of others, and its advisory is published as soon as a fix or a mitigation exists, so manufacturers can meet their own reporting deadlines.
- **An SBOM for an image**: `tools/sbom/sbom.py` writes an SPDX 2.3 SBOM with the build manifest for any build ([SBOM](docs/engineering/sbom.md)). Each release will carry its own.
- **Reproducible builds**, so a manufacturer can check that an image comes from the source it claims ([reproducible builds](docs/engineering/reproducible-builds.md)).

If you build a product on EmbLinkRTOS and find a vulnerability that affects it, report it as above, and say whether it is exploited, so it can be handled first.

## Scope

The kernel, architecture ports, SoC and board support, drivers, tooling, generated code, and the build and release pipeline. Vulnerabilities in third-party middleware should be reported to that project; please also tell us so we can track the dependency in our software bill of materials.
