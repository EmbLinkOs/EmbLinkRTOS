# Functional safety readiness (IEC 61508)

**Status, 2026-11-08: EmbLinkRTOS is not certified, and makes no safety claim.** This page compares what the project has with what a functional safety assessment of the kernel would ask for, and orders the work. It does not show compliance, and it is not a plan any assessor has agreed to. 01-vision says no certification is claimed until a product scope, a process and an assessment exist, and that remains true.

## Why IEC 61508

IEC 61508 is the base standard for the functional safety of programmable electronics. The automotive (ISO 26262), medical (IEC 62304), rail (EN 50128) and appliance (IEC 60730) certificates of commercial kernels are issued on the same evidence: ThreadX, embOS-Safe and SafeRTOS all began with IEC 61508 ([R-002 §3](../research/R-002-market-certification-and-positioning.md)). Software developed without a safety life cycle from its start can be assessed as a pre-existing element ("route 3S"), which is the route Zephyr is taking for a defined subset at SIL 3. The reference here is **SIL 3, the kernel as a compliant item for use in products not yet known**.

**The clause references below come from public summaries of IEC 61508-3:2010, not from the standard.** The project does not have the standard's text, which IEC sells. Before any of this is relied on, each row must be checked against the standard.

## Where the project stands

**Met** means the evidence exists in the form the topic asks for. **Partly** means some of it exists. **Missing** means none does.

| Topic (IEC 61508) | What it asks for, in short | What exists | Status |
|---|---|---|---|
| Management of functional safety (part 1, clause 6) | A safety plan: the life cycle, roles, competence, and who verifies and assesses what | The engineering process is documented ([05](../architecture/05-engineering-system.md), [repository workflow](repository-workflow.md)), but not as a safety plan, and with no safety roles | Missing |
| A defined scope | The element under assessment, its boundary and its configurations | None. Today's kernel and public headers are about 12,000 non-blank lines, in many configurations | Missing |
| Software safety requirements (part 3, 7.2) | Requirements derived from the safety functions, testable, traced | About 395 requirements, each with an ID, statement, rationale and verification method ([requirements](../requirements/README.md)). They describe kernel behaviour, not safety functions; no hazard analysis derives them | Partly |
| Validation planning (7.3) | How the requirements will be shown met | Each milestone's exit review ([M2](m2-exit-review.md)), the [release checklist](releases.md) | Partly |
| Software architecture (7.4.3) | A design that meets the requirements, with its fault avoidance and detection | Fifteen accepted specifications and their amendments, the ADRs, checked builds with a transition checker (ADR-035), stack guards (A90), the crash record | Partly: freedom from interference between applications needs the MPU isolation of M3b |
| Support tools and languages (7.4.4) | A language subset; each tool classified by its effect on the output (T1, T2, T3), and the compiler shown fit | A coding standard with recorded deviations ([CODING-STANDARD](../CODING-STANDARD.md)); pinned toolchains; [reproducible builds](reproducible-builds.md); several independent compilers (ADR-053) | Partly: no tool is classified; no compiler is qualified |
| Detailed design and coding (7.4.5, 7.4.6) | The coding standard applied and checked, static analysis | clang-tidy and cppcheck as merge gates, with each deviation at its site ([static analysis](static-analysis.md)) | Partly: no MISRA rule checker runs yet |
| Module and integration testing (7.4.7, 7.4.8) | Requirement-based tests, and structural coverage justified to 100% | Conformance suites on native, QEMU (Armv6-M, Armv7-M, Armv8-M, RV32) and EmbSIM (AVR); differential testing against the executable reference model ([differential evidence](differential-evidence.md)); the requirement matrix as a CI gate ([case evidence](case-evidence.md)); HIL runs on four boards ([support matrix](support-matrix.md)) | Partly: kernel coverage is 92.5% of lines and 70.7% of branches, with no justification of the rest and no MC/DC |
| Timing and resources | Worst cases bounded and shown | The [resource-bound inventory](resource-bounds.md), [timing and evidence](timing-and-evidence.md)'s claim categories, measured timing on the NUCLEO ([controller timing](controller-timing-2026-11-07.md)) | Partly: maximum observed, not established bounds |
| Verification (7.9), with independence | Each phase's output checked, by someone other than its author as the SIL requires | CI gates on every change; ADRs and reviews | Partly: one maintainer, so no independence |
| Modification (7.8) | The impact of each change analyzed before it is made | ADRs, the CHANGELOG, DCO sign-off, pull requests | Partly: no recorded impact analysis |
| Configuration management | Every item and its version identified, builds repeatable | Pinned toolchains and actions, double builds, an SPDX SBOM with the build manifest ([SBOM](sbom.md)) | Met for builds; release baselines are not yet cut |
| Safety manual of a compliant item (part 3) | What a product must do for the element's claims to hold: assumptions of use, configuration, the faults it detects | User documentation, without safety assumptions | Missing |
| Functional safety assessment (clause 8) | An independent assessor's judgement | None | Missing |

## What one maintainer cannot provide

IEC 61508 asks that verification at SIL 3 be done by someone other than the author, and the assessment by an independent person or organization. Both need people the project does not have. Everything else in the table can be produced by the project. The assessment itself is a paid engagement with a certification body.

## The order of the work

The first steps cost little and are useful whether or not the kernel is ever assessed. Later steps need a decision, a sponsor or both.

1. **Run a MISRA rule checker:** cppcheck's MISRA addon, with a baseline gate. In progress after [static analysis](static-analysis.md)'s cppcheck gate.
2. **Define the scope.** Name a kernel core (scheduler, threads, time, wait queues, mutexes, semaphores, the critical-section and interrupt contract) and one or two configurations. Every later item is measured on the scope, not the tree.
3. **Coverage of the scope.** Raise it toward 100% of statements and branches, and justify each line left (defensive code, configuration-dependent code). Measure MC/DC: GCC 14 has `-fcondition-coverage`.
4. **Classify the tools.** List each tool that produces or checks the image, with its class (T1, T2, T3) and why. Record what the compiler's correctness rests on: several compilers, the double build, the test suites on each.
5. **A failure analysis of the scope.** For each service, how it can fail, and what the kernel detects (checked builds, guards, the crash record) and what it leaves to the product.
6. **A first safety manual,** from items 2 to 5: assumptions of use, supported configurations, detected faults, the product's own obligations.
7. **Established timing bounds** on the scope, from the resource-bound inventory, per [timing and evidence](timing-and-evidence.md).
8. **Decisions for the owner:** whether to seek an assessment, and with whom; how to provide independent verification; whether isolation (M3b) is in the scope.

## Security

Security standards (IEC 62443-4-1 and 4-2, ISO/SAE 21434) are outside this page. The EU Cyber Resilience Act is a regulation, not a standard. Its obligations fall mainly on the manufacturers of products that use the kernel; [SECURITY.md](https://github.com/EmbLinkOs/EmbLinkRTOS/blob/main/SECURITY.md) says what the project provides them.
