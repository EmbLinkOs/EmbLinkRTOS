# 08 - Open Questions for Review

This is the historical owner-decision ledger. Answered entries remain for provenance; unanswered recommendations are working defaults, not evidence of implementation or a request to reapprove accepted work. ADR-039 records the 2026-10-14 documentation revision; STATUS.md and the roadmap describe current scope. The remaining operational input is the physical HIL inventory. Q16 is now decided by the documentation tooling setup.

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
*R-001 §4.1:* seven of nine kernels default to priority-then-FIFO; ThreadX's FIFO default is a documented source of inversion. Confirmed.

**Q5. Mutex owner termination.** Fault (recommended default) or release with `EMB_EOWNERDEAD`?
*Recommendation:* fault in checked builds, configurable `EOWNERDEAD` for release builds that need it.
*R-001 §5:* only ThreadX, NuttX, and uC/OS-III handle owner death at all, and none informs the next owner. Confirmed; algorithm in ADR-028.
*Answered 2026-10-07 (SPEC-005 §3.6, KRN-SYNC-025):* `CONFIG_EMB_MUTEX_OWNER_DEATH` = `FAULT` (default) or `RELEASE` with `EMB_EOWNERDEAD` to the next owner; the inconsistent mark is cleared by the recovering owner's unlock.

**Q6. Thread cancellation in 1.0.** Cooperative cancellation (recommended) or none until later?
*Recommendation:* include it; it is small once the wait protocol has wake results.
*Answered 2026-10-07 (SPEC-008 §7, KRN-THR-021):* cooperative cancellation with a per-thread disable count is in 1.0; there is no asynchronous terminate.

**Q7. Notification width.** 32 bits everywhere, or 8 bits on tiny?
*Recommendation:* configurable, default 32, tiny profile may select 8 or 16.
*R-003 ADR-027:* binding objects to notification bits needs spare bits; `base` keeps 32 with at least 16 for the application, `tiny` may select 8 with at least 4 (KRN-NOTIF-006). Confirmed.

**Q8. Time slicing per priority level (KRN-SCH-037).** Worth the configuration surface?
*Recommendation (revised by R-003 §3.1):* no per-level table; one quantum and one threshold priority at or below which slicing applies (Zephyr `TIMESLICE_PRIORITY`). KRN-SCH-037 revised accordingly. Answered.

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
*Answered 2026-10-07 (SPEC-004 §16.6):* Python 3 with exhaustive section-granularity exploration in `tools/model/` and `hypothesis` for randomized runs; the bridge is a trace replay.

**Q16. Documentation toolchain.** Sphinx with a requirements extension (mature traceability tooling) or MkDocs plus project scripts (simpler)?
*Decided 2026-10-14:* Sphinx with MyST Markdown, Doxygen/Breathe for current C API, Sphinx Needs for the design catalog, and Furo for presentation. `tools/docs/` builds the site and independent metadata exports; the executed requirement/test evidence matrix remains an M2 deliverable (ADR-040).

**Q17. CI hardware.** Which boards will be physically available for HIL in M1 to M3? This determines the HIL runner's first targets.

## E. EmbLink ecosystem

**Q18. EmbCC status today.** Which C standard features and attributes does it support for Cortex-M and RISC-V? Does it emit DWARF suitable for the debug descriptor consumer? When is AVR support planned? The answers set the compiler portability layer's shape and the M1 compiler for AVR.
*Answered 2026-10-07; refreshed 2026-10-14 against pinned EmbCC source; see document 09 and the [capability review](../engineering/embcc-capability-review.md).* AVR DWARF/remote layout, program-memory support and scripts, Armv8-M stack limits/soft-float CMSE/M23, small atomics, SVD structured output and Cortex-M/RV32 C++ are now implemented upstream. Preserve the stated feature restrictions and incomplete current RTOS integration/CI rather than treating the former compiler backlog as an RTOS qualification result.

**Q19. EmbDebug and EmbFlash status.** Do they exist in usable form for M3 HIL, or does the HIL runner start on OpenOCD, pyOCD, or probe-rs?
*Refreshed 2026-10-14.* EmbDBG now includes AVR's remote register layout, and EmbFlash exists as a GDB-server flash client in EmbCC. Their upstream availability does not establish RTOS board/probe/HIL integration. Existing OpenOCD/pyOCD, `picotool` and `avrdude` workflows remain references; the proposed RTOS `emb flash` facade and physical support need separate evidence. See the [capability review](../engineering/embcc-capability-review.md).

**Q20. EmbBuild.** Does it exist? If not, CMake is the only build until it does, which the architecture already assumes.
*Answered 2026-10-07.* EmbBuild exists as a typed-manifest format (`.ebm`) and walker on EmbLinkOS, with a host reference walker in the EmbCC repository. CMake stays the reference build and gains a manifest emitter (BLD-006).

---

Specification work items 1 to 14 and 6b already exist. Next work follows the revised M2/M3a gates: finish the included kernel, implement evidence generation, and validate the primary physical board. Update a decision only when it changes; do not restart the completed specification phase.
