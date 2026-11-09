# BOOT - Boot, images and update

Group `BOOT`. Design: `docs/architecture/04-platform-architecture.md` §5; `docs/specs/SPEC-019-boot-images-and-update.md` (planned, M4.0). Decisions: ADR-008 (MCUboot's image format), ADR-058 (upstream MCUboot, proposed). Related groups: DRV (the flash class), SEC (signing, anti-rollback, debug policy), HW (flash partitions), OBS (the boot's output).

BOOT-001 to 005 originate in `docs/architecture/04-platform-architecture.md` §5 and are restated here as the authoritative copy. BOOT-006 to 008 were added with M4's plan (2026-11-08). New requirements start at 009.

---

## Requirements

### BOOT-001  An MCUboot-compatible image
**Statement.** Images shall carry a header with version, size, flags, and a signature TLV in an MCUboot-compatible layout.
**Rationale.** Existing signing tools and bootloaders work with the images (ADR-008).
**Status.** Accepted 2026-10-07
**Verification.** Test: `imgtool verify` and the project's own reader (`tools/image/`, planned) accept a signed image of each reference board.
**Trace.** 04 §5.2; ADR-008; SPEC-019

### BOOT-002  A/B update, confirm or revert, power-loss safe
**Statement.** A/B slot update with confirm-or-revert, anti-rollback counters, and power-loss-safe swap shall be supported by the storage layout and the update service.
**Rationale.** A field update that can brick a device, or be rolled back to a vulnerable image, is worse than none.
**Status.** Accepted 2026-10-07
**Verification.** Test: the power-loss sweep in a native flash simulator (BOOT-007); Demonstration: update, revert and power-loss runs on both reference boards.
**Trace.** 04 §5; ADR-058; SPEC-019

### BOOT-003  Boot measurements for attestation
**Statement.** Boot measurements (hashes of each stage) shall be available to the runtime for attestation when a root of trust exists.
**Rationale.** Attestation proves what booted; it needs each stage's measurement handed on.
**Status.** Accepted 2026-10-07
**Verification.** Test: the runtime reads the bootloader's and the image's hashes, and they match the images (on a board with a root of trust; none of today's reference boards provisions one).
**Trace.** 04 §5.1, §6.2

### BOOT-004  Multi-image boot in the manifest
**Statement.** Multi-image boot (several cores, several partitions loaded separately) shall be expressible in the manifest.
**Rationale.** AMP systems and isolated partitions update their images separately.
**Status.** Accepted 2026-10-07
**Verification.** Inspection: the manifest schema; Test: a two-image layout validates and generates (planned with multicore).
**Trace.** 04 §5.1, §8.2

### BOOT-005  A measured boot time
**Statement.** Boot time budget shall be measurable and published per reference board.
**Rationale.** Boot time is part of a product's availability; signature checking changes it.
**Status.** Accepted 2026-10-07
**Verification.** Demonstration: reset release to the application's first marker, with and without signature checking, measured externally on each reference board.
**Trace.** 04 §5.1; SPEC-019

### BOOT-006  Confirm or revert from the application
**Statement.** The application shall be able to confirm the running image, request a test or permanent upgrade, and read each slot's state through the update API; an image not confirmed after a test boot shall be reverted at the next reset.
**Rationale.** Only the application knows that it works; the bootloader must act on its word, and on its silence.
**Status.** Accepted 2026-11-09 at the M4.0 review
**Verification.** Test: an image that confirms stays; one that does not is reverted; both on the native simulator and the reference boards.
**Trace.** ADR-058; SPEC-019; BOOT-002

### BOOT-007  Power-loss injection evidence
**Statement.** Every flash operation of an upgrade and of a revert shall be shown interruptible: after power is lost at any of them, the next boot shall run either the old image or the new one, valid, and never neither.
**Rationale.** Power-loss safety is MCUboot's design; it must be shown for this port, layout and flash driver.
**Status.** Accepted 2026-11-09 at the M4.0 review
**Verification.** Test: CI sweeps the interruption point over every operation in a native flash simulator, torn writes included; Demonstration: deterministic resets after operation N and random external power cuts on both boards, at least 500 injections each, archived.
**Trace.** ADR-058; SPEC-019; BOOT-002

### BOOT-008  The application links at its slot
**Statement.** When a board declares a bootloader, the application shall be linked at its slot's start plus the image header, and its reset path shall set the vector table to its own; without a bootloader the layout shall be unchanged.
**Rationale.** MCUboot jumps to the image in its slot; the image must run from there, with its own vectors.
**Status.** Accepted 2026-11-09 at the M4.0 review
**Verification.** Test: the generator's layouts with and without a bootloader; Demonstration: the application boots through MCUboot on both boards.
**Trace.** ADR-058; SPEC-014; SPEC-019
