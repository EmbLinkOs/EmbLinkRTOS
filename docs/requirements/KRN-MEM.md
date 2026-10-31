# KRN-MEM - Memory

Group `KRN-MEM`. Design: `docs/architecture/03-kernel-architecture.md` §9, `docs/specs/SPEC-009-objects-capabilities-and-storage.md`, `docs/specs/SPEC-011-architecture-port-contract.md` §10. Related groups: KRN-OBJ (caller-owned storage), API (generated storage sizes), HW (regions).

KRN-MEM-007 to 011 originate in 03 §9 and are restated verbatim. KRN-MEM-001 to 006 originate in v0.1 §11, whose text is not in the repository; the two that other documents cite are restated from the text that fixes their meaning, and 003 to 006 stay reserved. New requirements start at 012.

---

## Requirements

### KRN-MEM-001  No allocator required
**Statement.** The kernel shall not require a dynamic memory allocator: every kernel object shall live in storage the application provides.
**Rationale.** ADR-006 rejects a required allocator as violating this requirement; KRN-THR restates it for threads.
**Status.** Accepted 2026-10-31 (restated); implemented.
**Verification.** Analysis: no allocation call in `kernel/`; Test: every conformance suite builds its objects in static storage.
**Trace.** ADR-006; SPEC-009; v0.1 §11

### KRN-MEM-002  Static allocation of opaque objects
**Statement.** Every kernel object shall be statically allocatable by the application through a storage type whose size and alignment are known at compile time, without exposing the object's layout.
**Rationale.** API-019: opaque objects and static allocation are both required; generated sizes reconcile them (ADR-006).
**Status.** Accepted 2026-10-31 (restated); implemented.
**Verification.** Analysis: the layout probe and the generated `<emb/storage.h>`; Test: the storage-generator tests.
**Trace.** ADR-006; SPEC-001 §6.2; v0.1 §11

### KRN-MEM-007  Generated regions
**Statement.** Memory regions and their attributes shall be generated from the hardware description, not hand-written per board.
**Rationale.** 03 §9.1.
**Status.** Proposed (03 §9.1); not implemented: board linker scripts are hand-written.
**Verification.** Analysis: planned generator output check.
**Trace.** 03 §9.1; HW

### KRN-MEM-008  DMA placement from region attributes
**Statement.** DMA buffer placement and cache maintenance requirements shall be derivable from region attributes.
**Rationale.** 03 §9.1.
**Status.** Proposed (03 §9.1); not implemented.
**Verification.** Analysis: planned generator output check.
**Trace.** 03 §9.1; HW

### KRN-MEM-009  Stack sizing methodology
**Statement.** Stack sizing methodology shall be documented per architecture: static analysis (EmbCC call graph where available), fill-pattern high-water measurement, and interrupt stack accounting.
**Rationale.** 03 §9.3; CS-4.3.
**Status.** Accepted (03 §9.3); partly implemented: `-fstack-usage` reports and `CONFIG_EMB_STACK_STATS` fill-pattern estimates exist; the per-port worst-case depth tables are incomplete.
**Verification.** Test: `thread_stack_info_reports_size`; Inspection: the port pages.
**Trace.** 03 §9.3; CODING-STANDARD CS-4.3; SPEC-011 §10

### KRN-MEM-010  Stack overflow detection
**Statement.** Stack overflow detection shall offer: fill-pattern check at switch (all profiles), hardware stack limit registers (Armv8-M, RISC-V with PMP), MPU guard regions (isolated profiles).
**Rationale.** 03 §9.3.
**Status.** Accepted (03 §9.3); the fill-pattern guard check (`CONFIG_EMB_STACK_CHECK`) is implemented; hardware limits and MPU guards are not.
**Verification.** Test: none yet (planned: a guard-overwrite case that expects the stack fault).
**Trace.** 03 §9.3

### KRN-MEM-011  Dedicated interrupt stacks
**Statement.** Dedicated interrupt stacks shall be used wherever the architecture permits, so thread stacks are sized for thread work only.
**Rationale.** 03 §9.3.
**Status.** Accepted (03 §9.3); implemented on Cortex-M (MSP) and RISC-V (`CONFIG_EMB_ISR_STACK_SIZE`); AVR has no separate interrupt stack and reserves per-thread space instead.
**Verification.** Inspection: the port pages; Test: the interrupt suites on each port.
**Trace.** 03 §9.3; SPEC-011
