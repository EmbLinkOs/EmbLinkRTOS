# KRN-CAP - Handles and Capabilities

Group `KRN-CAP`. Design: `docs/specs/SPEC-009-objects-capabilities-and-storage.md` §3, §4. Related groups: KRN-OBJ (generations, lifecycle), KRN-PART (syscall boundary, tables per partition), KRN-NOTIF (`NOTIFY`, `BIND` rights), HW (generated initial tables).

KRN-CAP-001 to 005 originate in `docs/architecture/03-kernel-architecture.md` §7.2. All are restated here as the authoritative copy. New requirements start at 006.

---

### KRN-CAP-001  One handle type, profile-selected representation
**Statement.** Every public handle shall be a capability whose representation is selected by profile without changing API signatures.
**Rationale.** ADR-014: one API from tiny to isolated.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `EMB_DECLARE_HANDLE` is the same in every profile; the conformance suite compiles unchanged for POINTER, INDEXED, and TABLE.
**Trace.** SPEC-009 §3

### KRN-CAP-002  Validation at the boundary
**Statement.** In isolated profiles, every kernel entry from an unprivileged partition shall validate the capability's type, rights, and generation before use.
**Rationale.** An unprivileged partition can never cause a kernel fault by misusing a handle (SPEC-001 §5.3).
**Status.** Accepted 2026-10-07
**Verification.** Isolated-profile tests `part/bad_handle_*` return statuses, never fault; fuzzing of the syscall boundary.
**Trace.** SPEC-009 §3.3; 03 §8.1

### KRN-CAP-003  Rights only reduce
**Statement.** Rights shall be reducible on derivation and never increasable.
**Rationale.** The capability discipline.
**Status.** Accepted 2026-10-07
**Verification.** Test: `cap/derive_cannot_add_rights`, `cap/grant_subset_only`.
**Trace.** SPEC-009 §4.1

### KRN-CAP-004  Static capability tables
**Statement.** Capability tables shall be statically sized per partition at build time.
**Rationale.** Memory of the isolation mechanism is a visible number (ADR-033).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: generator output `partitions.c`; Test: `cap/table_exhausted_enomem`.
**Trace.** SPEC-009 §3.3

### KRN-CAP-005  Zero cost on tiny
**Statement.** In the tiny profile the capability layer shall compile to pointer passing with no runtime cost.
**Rationale.** Footprint target T1.
**Status.** Accepted 2026-10-07
**Verification.** Footprint and benchmark comparison of the tiny profile with the capability functions present and absent.
**Trace.** SPEC-009 §3.1, §4.3

### KRN-CAP-006  Handle models
**Statement.** `CONFIG_EMB_HANDLE_MODEL` shall select `POINTER` (object pointer; type and lifecycle checked in checked builds), `INDEXED` (global registry slot plus 16-bit slot generation), or `TABLE` (per-partition capability index plus 16-bit slot generation); every lookup shall be O(1).
**Rationale.** A real generation in O(1) where wanted, nothing where not; no tagged pointers.
**Status.** Accepted 2026-10-07
**Verification.** Test: `obj/handle_models` runs the handle misuse tests under each model; Benchmark: lookup cost.
**Trace.** SPEC-009 §3

### KRN-CAP-007  Capability entry and validation
**Statement.** A capability entry shall hold the object pointer, the object generation at grant time, the slot generation, the type tag, and the rights; validation shall check index range, slot generation, type, rights, object lifecycle, and object generation, returning `EMB_EINVAL`, `EMB_EPERM`, `EMB_ESTATE`, or `EMB_ESTALE` accordingly.
**Rationale.** Fixes the exact check order and codes so that the generated stubs and the conformance suite agree.
**Status.** Accepted 2026-10-07
**Verification.** Test: `cap/validation_matrix`.
**Trace.** SPEC-009 §3.3, §8

### KRN-CAP-008  Rights per type
**Statement.** Each object type shall define its rights within eight bits with `DESTROY` and `GRANT` as the two universal top bits, as listed in SPEC-009 §4.2; every operation's `@req` annotation shall name the right it needs and the generated stubs shall check exactly that right.
**Rationale.** One source of truth for rights, machine-checked.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `tools/apidoc` cross-checks annotations against the stub generator's table.
**Trace.** SPEC-009 §4.2; SPEC-001 §9

### KRN-CAP-009  Grant, derive, delete, revoke, renew
**Statement.** `grant` shall copy a capability with a subset of rights into another partition's table and require `GRANT` on it and `GRANT_TO` on the target partition; `derive` shall create a reduced copy in the caller's table; `delete` shall free the caller's slot; `revoke` (requires `DESTROY`) shall bump the object's generation and so invalidate every capability to it; `renew` shall give the owner a fresh capability afterwards.
**Rationale.** Complete, small capability algebra with coarse revocation.
**Status.** Accepted 2026-10-07
**Verification.** Test: `cap/grant_*`, `cap/derive_*`, `cap/revoke_all_stale`, `cap/renew`.
**Trace.** SPEC-009 §4.1

### KRN-CAP-010  Build-time grants
**Statement.** The generator shall emit each partition's initial capability table from the system description, including device and port capabilities granted to driver partitions.
**Rationale.** Static systems need no runtime grant protocol.
**Status.** Accepted 2026-10-07
**Verification.** Generator test `hw/gen_cap_tables`; isolated-profile boot test.
**Trace.** SPEC-009 §4.1; 04 §1.3; SPEC-014

### KRN-CAP-011  Pointer models keep the API
**Statement.** Under `POINTER` and `INDEXED`, the capability functions shall exist: trivial ones return `EMB_OK`, cross-partition ones `EMB_ENOTSUP`, so that isolated-profile code compiles and runs unchanged.
**Rationale.** One source tree per application across profiles (C3).
**Status.** Accepted 2026-10-07
**Verification.** Test: `cap/api_present_in_pointer_models`.
**Trace.** SPEC-009 §4.3

### KRN-CAP-012  Statistics
**Statement.** Registry and table occupancy high-water and stale-handle rejections per partition shall be optional statistics.
**Rationale.** Table sizing and a security signal.
**Status.** Accepted 2026-10-07
**Verification.** Statistics API test.
**Trace.** SPEC-009 §9
