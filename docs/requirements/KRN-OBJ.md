# KRN-OBJ - Kernel Objects, Lifecycle, and Storage

Group `KRN-OBJ`. Design: `docs/specs/SPEC-009-objects-capabilities-and-storage.md`. Related groups: KRN-CAP (handles and rights), KRN-WAIT (flush on destroy), KRN-PART (restart), BLD (storage generator), OBS (debug descriptor).

KRN-OBJ-001 to 005 originate in `docs/architecture/03-kernel-architecture.md` §7.4 (004 and 005 added by R-003 and ADR-032). All are restated here as the authoritative copy. New requirements start at 006.

---

### KRN-OBJ-001  Destroy with waiters
**Statement.** Destroying an object with waiters is a kernel fault in checked builds unless the object was created with `ABORT_WAITERS`, in which case every waiter wakes with `DESTROYED`.
**Rationale.** ADR-020: lifecycle bugs are caught; deliberate teardown is explicit.
**Status.** Accepted 2026-10-07
**Verification.** Test: `wait/destroy_abort_waiters`, `misuse/destroy_with_waiters`; model scenario `destroy_with_waiters`.
**Trace.** SPEC-009 §6; SPEC-004 §6.7; ADR-020

### KRN-OBJ-002  Generation bump on destroy
**Statement.** After destruction, the object's generation shall be bumped so stale capabilities fail validation in profiles that check them.
**Rationale.** Use-after-destroy becomes `EMB_ESTALE` instead of corruption.
**Status.** Accepted 2026-10-07
**Verification.** Test: `obj/stale_after_destroy` (INDEXED and TABLE models).
**Trace.** SPEC-009 §3.2, §3.3, §6

### KRN-OBJ-003  Caller owns storage
**Statement.** Object storage ownership belongs to whoever provided it; the kernel never frees caller storage.
**Rationale.** No allocator in the kernel path.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: no free call in `kernel/`; Test: storage reusable after destroy.
**Trace.** SPEC-009 §5, §6

### KRN-OBJ-004  System freeze
**Statement.** `emb_system_freeze()` shall, once called, make every create and destroy operation fail with `EMB_EPERM` (fault in checked builds) for the rest of the run; the isolated reference configuration shall call it at the end of initialization.
**Rationale.** uC/OS-III's safety-critical start; a frozen object set is what a certification argument wants.
**Status.** Accepted 2026-10-07
**Verification.** Test: `obj/freeze_blocks_init_destroy`; isolated configuration review.
**Trace.** SPEC-009 §6

### KRN-OBJ-005  Thread and partition generations
**Statement.** Thread and partition handles shall carry a generation in every profile that checks generations; a wait on a handle whose generation changed because of a restart shall complete with `EMB_ESTALE`.
**Rationale.** ADR-032: restarts are visible to peers.
**Status.** Accepted 2026-10-07
**Verification.** Isolated-profile test `part/join_restarted_thread_estale`; model scenario (SPEC-010).
**Trace.** SPEC-009 §6; ADR-032

### KRN-OBJ-006  Common header
**Statement.** Every object shall begin with the header of SPEC-009 §2 (`type_tag`, `lifecycle`, `flags`, and the configured optional fields); fields not configured shall occupy no storage; the tiny release header shall be one byte.
**Rationale.** One place for validation, registry, binding, and naming; zero cost where not used.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `_Static_assert` on header size per configuration; footprint test.
**Trace.** SPEC-009 §2

### KRN-OBJ-007  Lifecycle states
**Statement.** Objects shall move `UNINIT` to `ACTIVE` at init, `ACTIVE` to `DESTROYING` to `UNINIT` at destroy; init on active storage shall be `EMB_EEXIST` (fault in checked builds); operations on a `DESTROYING` object shall return `EMB_ESTATE`.
**Rationale.** Double init and destroy races have defined outcomes.
**Status.** Accepted 2026-10-07
**Verification.** Test: `misuse/double_init`, SMP test `obj/op_during_destroy_estate`.
**Trace.** SPEC-009 §6

### KRN-OBJ-008  Generated storage types
**Statement.** `<emb/storage.h>` shall be generated from a layout probe compiled for the target with the final configuration and read with the toolchain's object reader; kernel sources shall statically assert the real sizes and alignments against it; the header shall carry the configuration hash and a mismatch shall fail the build.
**Rationale.** ADR-006 without mirrored structs or exposed layouts.
**Status.** Accepted 2026-10-07
**Verification.** Build test: a deliberately stale header fails to build; Analysis: generator review.
**Trace.** SPEC-009 §5.1; ADR-006; BLD-007

### KRN-OBJ-009  Init table
**Statement.** `_DEFINE` macros shall register objects in the bracketed section `.emb_init_table`; kernel start shall construct them in link order before `main()`; `EMB_INIT_AFTER` shall order dependent objects; the mechanism shall work on every target without a linker script.
**Rationale.** Static objects without hand-written init code, on AVR too (09 §7).
**Status.** Accepted 2026-10-07
**Verification.** Test: `obj/define_constructed_before_main` on every port including AVR.
**Trace.** SPEC-009 §5.2; SPEC-001 §6.4

### KRN-OBJ-010  Type-specific destroy conditions
**Statement.** Destroy shall be refused with `EMB_EBUSY` (fault in checked builds) for an owned mutex, a pool with outstanding blocks, a timer with a pending or running callback, a work queue with items, and a thread that is not inactive or terminated-and-reusable.
**Rationale.** The kernel never leaves a dangling owner.
**Status.** Accepted 2026-10-07
**Verification.** Test: `misuse/destroy_*` per type.
**Trace.** SPEC-009 §6, §8

### KRN-OBJ-011  Names and registry
**Statement.** With `CONFIG_EMB_OBJ_NAMES` objects shall keep their attribute name by pointer; with `CONFIG_EMB_OBJ_LIST` every active object shall be on a per-type list iterable under the scheduler lock; the debug descriptor shall export header offsets, type tags, list heads, and the registry or capability table layout.
**Rationale.** Debuggers and statistics enumerate objects without symbols (ADR-011).
**Status.** Accepted 2026-10-07
**Verification.** Test: debug descriptor consumer lists every object of the conformance run.
**Trace.** SPEC-009 §7; OBS-008

### KRN-OBJ-012  No flash-resident objects in 1.0
**Statement.** Kernel objects shall live in RAM and be constructed at boot; no const-initializer form of an object shall be part of the public API in 1.0.
**Rationale.** A const form would expose the layout the storage type hides.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: header review.
**Trace.** SPEC-009 §5.3, §12.5

### KRN-OBJ-013  Trace
**Statement.** Object init and destroy (with the number of waiters flushed), capability changes, and system freeze shall be trace events.
**Rationale.** Lifecycle debugging.
**Status.** Accepted 2026-10-07
**Verification.** Test: trace decoder.
**Trace.** SPEC-009 §9; OBS-005
