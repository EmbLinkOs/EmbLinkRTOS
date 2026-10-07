# 08 - Open Questions for Review

These need your decision. Each has a recommendation so the default path is clear if you agree. Answering them converts the PROPOSED items in 02 to 06 into LOCKED.

**Decided on 2026-10-06:** Q2 (licence), Q9 (hardware description), Q10 (first Cortex-M target). See ADR-004, ADR-024, ADR-025. The remaining questions keep their recommendations as the working default until answered.

---

## A. Identity and governance

**Q1. Public API prefix.** Keep `emb_` (recommended, ADR-002) or adopt a distinctive short prefix such as `elk_`?
*Recommendation:* keep `emb_`; reserve `embk_` for kernel internals. Never use `embos`.

**Q2. Licence.** Apache-2.0 (recommended), MIT, or something else? Is a commercial licensing option anticipated?
*Decided:* Apache-2.0 with the Developer Certificate of Origin; outside contributions accepted; no contributor licence agreement, so dual licensing is not planned (ADR-024).

**Q3. Is EmbLinkRTOS open source from the first commit, or developed privately until 1.0?**
*Recommendation:* public from the start; architecture discussion in the open attracts reviewers and board contributors.
*Note:* the decision to accept outside contributions implies public development; confirm if you intend otherwise.

## B. Kernel semantics

**Q4. Default wait ordering.** Priority-then-FIFO (recommended) or pure FIFO by default?
*Recommendation:* priority-then-FIFO; pure FIFO as a per-object option.

**Q5. Mutex owner termination.** Fault (recommended default) or release with `EMB_EOWNERDEAD`?
*Recommendation:* fault in checked builds, configurable `EOWNERDEAD` for release builds that need it.

**Q6. Thread cancellation in 1.0.** Cooperative cancellation (recommended) or none until later?
*Recommendation:* include it; it is small once the wait protocol has wake results.

**Q7. Notification width.** 32 bits everywhere, or 8 bits on tiny?
*Recommendation:* configurable, default 32, tiny profile may select 8 or 16.

**Q8. Time slicing per priority level (KRN-SCH-037).** Worth the configuration surface?
*Recommendation:* yes; it is a small table and solves the common "round-robin for background only" need.

## C. Platform

**Q9. Hardware description: own YAML schema with importers (recommended, ADR-004) or DeviceTree source directly?**
This is the highest-impact platform decision. Own schema costs generator work up front; DeviceTree costs authoring pain forever and a dependency on its toolchain.
*Decided:* own YAML schema with CMSIS-SVD and DeviceTree importers, borrowing `compatible` strings, bus hierarchy, named references, status flags, layered inheritance, and fixed partitions from DeviceTree; a DeviceTree exporter if interop ever requires it (ADR-004).

**Q10. First Cortex-M SoC family for M3.** STM32F4 (ubiquitous, Discovery boards cheap), STM32L4/U5 (low power plus Armv8-M for isolation), nRF52/nRF53 (BLE later, nRF53 is dual-core Armv8-M), RP2350 (Armv8-M plus RISC-V on one chip, cheap, ideal for AMP and for proving two ports on one board)?
*Decided:* STM32F4 (STM32F407 Discovery or NUCLEO-F446RE) for the port, with the RP2350 (Pico 2) acquired at the same time for Armv8-M isolation in M3, RISC-V in M5, and SMP and AMP in M6; an STM32U5 or H5 class part as the second family in M4 (ADR-025).

**Q11. Image format.** MCUboot-compatible (recommended, ADR-008) or project-specific?

**Q12. Trace format.** CTF (recommended, ADR-021), or a simpler project format with an EmbDebug-only viewer?

**Q13. Logging.** Deferred-format as the default in base and above (recommended, ADR-009), or opt-in?

## D. Engineering process

**Q14. Configuration.** Kconfig semantics (recommended, ADR-005) or a project-specific typed configuration language designed for EmbStudio?
*Note:* a typed schema can be layered on Kconfig output later; starting with a custom language delays M1.

**Q15. Reference model language.** Python (fast to write, readable to non-C reviewers) or C on the host (closer to kernel, reusable in differential tests without a bridge)?
*Recommendation:* Python for the model and TLA+ or exhaustive Python exploration for the wake race; the differential test bridge is a small JSON protocol.

**Q16. Documentation toolchain.** Sphinx with a requirements extension (mature traceability tooling) or MkDocs plus project scripts (simpler)?
*Recommendation:* Sphinx; traceability is the point.

**Q17. CI hardware.** Which boards will be physically available for HIL in M1 to M3? This determines the HIL runner's first targets.

## E. EmbLink ecosystem

**Q18. EmbCC status today.** Which C standard features and attributes does it support for Cortex-M and RISC-V? Does it emit DWARF suitable for the debug descriptor consumer? When is AVR support planned? The answers set the compiler portability layer's shape and the M1 compiler for AVR.
*Answered 2026-10-07 from the EmbCC repository; see document 09.* EmbCC already targets AVR (ATmega328P), Cortex-M0+ through M33, and RISC-V 32 and 64, compiles C17 plus GNU extensions, emits DWARF 4 (empty on AVR today), and builds FreeRTOS's ports unmodified. The design rules that follow from its limits are in 09 §§3 to 7.

**Q19. EmbDebug and EmbFlash status.** Do they exist in usable form for M3 HIL, or does the HIL runner start on OpenOCD, pyOCD, or probe-rs?
*Partly answered 2026-10-07.* EmbDebug is `embdbg` in the EmbCC repository: a DWARF reader, symbolizer, crash-report analyzer, and GDB remote protocol client for QEMU and OpenOCD, covering ARM M-profile and RISC-V but not AVR. The HIL runner therefore starts on OpenOCD or pyOCD stubs with `embdbg` as the client. **EmbFlash was not found in the EmbCC repository**; does it exist elsewhere, or does flashing start on OpenOCD, pyOCD, and `avrdude`?

**Q20. EmbBuild.** Does it exist? If not, CMake is the only build until it does, which the architecture already assumes.
*Answered 2026-10-07.* EmbBuild exists as a typed-manifest format (`.ebm`) and walker on EmbLinkOS, with a host reference walker in the EmbCC repository. CMake stays the reference build and gains a manifest emitter (BLD-006).

---

When you answer, the next step is to update the status markers in 02 to 06, open `docs/requirements/` with one file per identifier group, and begin specification work item 1 (API conventions) from the roadmap.
