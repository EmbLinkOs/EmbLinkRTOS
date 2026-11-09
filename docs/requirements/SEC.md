# SEC - Security

Group `SEC`. Design: `docs/architecture/04-platform-architecture.md` §6. Decisions: ADR-008, ADR-058 (proposed). Related groups: BOOT (signed images, anti-rollback), DRV (the entropy class), REL (release signing and SBOM).

SEC-006 to 011 originate in `docs/architecture/04-platform-architecture.md` §6.2 and are restated here as the authoritative copy. SEC-001 to 005 originate in the v0.1 specification's §17, which is not in the tree; they are reserved and will be restated from it, never renumbered. SEC-012 to 014 were added with M4's plan (2026-11-08). New requirements start at 015.

---

## Requirements

### SEC-006  A threat model
**Statement.** The project shall maintain a written threat model that each security requirement traces to.
**Rationale.** A security requirement without the threat it counters cannot be judged complete (04 §6.1).
**Status.** Accepted 2026-10-07
**Verification.** Inspection: the threat model document, and each SEC requirement's trace to it (not written yet).
**Trace.** 04 §6.1

### SEC-007  Keys by handle
**Statement.** Cryptographic keys shall be referenced through handles with usage policy; raw key material shall not cross the public API.
**Rationale.** Key material that never leaves the key store cannot leak through an application bug (04 §6.2).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: no public API function takes or returns key bytes; Test: an operation outside a key's usage policy is refused.
**Trace.** 04 §6.2

### SEC-008  An SBOM with every release
**Statement.** Every release shall ship a software bill of materials (SPDX or CycloneDX) covering kernel, middleware, and generated code inputs.
**Rationale.** A user must be able to tell what is in an image (REL-001).
**Status.** Accepted 2026-10-07; implemented for images: `tools/sbom/sbom.py` writes an SPDX 2.3 SBOM with the build manifest, CI validates the reference controller's, and release 0.2.0 shipped it.
**Verification.** Analysis: CI's `application` job writes the controller's SBOMs and validates them with `spdx-tools` (`docs/engineering/sbom.md`).
**Trace.** 04 §6.2; REL-001; BLD-003

### SEC-009  Reproducible builds
**Statement.** Builds shall be reproducible from a pinned toolchain description; release verification shall rebuild and compare.
**Rationale.** A user can check that a released image comes from its source.
**Status.** Accepted 2026-10-07; implemented: CI's double build compares every image byte for byte, LTO included (`docs/engineering/reproducible-builds.md`).
**Verification.** Analysis: CI's `reproducibility` job (`tools/repro/double_build.py`).
**Trace.** 04 §6.2; BLD-004

### SEC-010  Disclosure and support windows before 1.0
**Statement.** A vulnerability disclosure process and support window per release line shall be published before 1.0.
**Rationale.** A user needs to know how to report a flaw and how long a release line gets fixes.
**Status.** Accepted 2026-10-07; implemented: `SECURITY.md` gives private reporting and the 0.2.x support window.
**Verification.** Inspection: `SECURITY.md`.
**Trace.** 04 §6.2; REL-001

### SEC-011  Debug access policy at provisioning
**Statement.** Debug access policy (open, authenticated, locked) shall be a provisioning-time decision supported by the boot chain.
**Rationale.** A locked product and an open development board run the same image; the policy belongs to provisioning.
**Status.** Accepted 2026-10-07
**Verification.** Demonstration: on a board whose SoC supports it, each policy set by provisioning and checked through the debug port (not on today's reference boards).
**Trace.** 04 §6.2; BOOT-001

### SEC-012  An absent entropy source is reported, not faked
**Statement.** A board without a hardware entropy source shall report the entropy class as unsupported (`EMB_ENOTSUP`); no software generator shall stand in for it under the same API.
**Rationale.** A deterministic generator presented as entropy produces predictable keys; an honest absence lets the application decide (the STM32F446 has no RNG).
**Status.** Proposed 2026-11-09
**Verification.** Test: on the NUCLEO-F446RE, capability discovery reports no entropy device and the class returns `EMB_ENOTSUP`.
**Trace.** SPEC-017 (entropy class); ADR-057

### SEC-013  Signing keys stay out of the tree
**Statement.** No private signing key for images or releases shall be in the repository; a key used by tests shall be named and marked as a test key, and images signed with it shall be refused by a production configuration.
**Rationale.** A committed key signs anything; a test key must never pass for a real one.
**Status.** Proposed 2026-11-09
**Verification.** Analysis: a check refuses private-key material outside the marked test-key file; Test: a production bootloader configuration rejects an image signed with the test key.
**Trace.** ADR-058; BOOT-001

### SEC-014  OTP is never burned automatically
**Statement.** One-time-programmable memory (fuses, OTP rows, option bytes that cannot be reverted) shall never be written by CI, by the hardware-in-the-loop runner or by any test.
**Rationale.** A wrong OTP write destroys a board; anti-rollback in OTP is documented, and provisioning it is a person's deliberate act.
**Status.** Proposed 2026-11-09
**Verification.** Analysis: no flashing command and no test image writes an OTP or irreversible option-byte address.
**Trace.** ADR-058; BOOT-002
