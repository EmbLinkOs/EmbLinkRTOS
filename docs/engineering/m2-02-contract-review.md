# M2-02: Contract review before implementation

**Review source:** `37887c9e92e2242ff4d11cfa58c6972fce4b0e0e`, 2026-10-14. **Status:** proposals for the next joint coding sessions. This review changes no public contract, requirement acceptance status, kernel behavior or model transition. The [work order](m2-checklist.md) remains the implementation/evidence gate.

The recommendations below preserve the specified priority direction, stable thread identity and attributable misuse faults. They turn the disclosed gaps into bounded implementation tasks. Merge of this review records the proposals; acceptance of a particular behavior requires the corresponding specification, API, implementation and regression review together.

**Subsequent implementation:** [kernel-aware IRQ priority correction](m2-02-irq-priority.md), ADR-042, 2026-10-21. The source observations/proposals below describe the reviewed revision; the correction supplies bounded domains, range validation, Cortex-M mapping and scoped emulator evidence. Other M2-02 work remains open.

## Interrupt priority direction and supported range

KRN-IRQ-028 and SPEC-002 §8 require larger generic levels to mean greater urgency. `kernel/irq/irq.c` currently delegates to the port after a context check. Cortex-M's `emb_arch_irq_set_level()` in `arch/cortex_m/cm_port.c` computes `BASEPRI + 32 * level`, clamps at `0xC0`, and succeeds. NVIC gives smaller encodings greater urgency, so this reverses the contract and silently collapses out-of-range requests. Native, AVR and RV32 accept any level for an in-range interrupt number and ignore it. Their delivery order is fixed by the harness or controller, rather than by this API.

**Recommendation:** retain larger-is-more-urgent and make the supported domain explicit. Reject levels outside that domain before writing a controller register. Do not make a requested level appear implemented by ignoring or clamping it.

For the currently assumed three NVIC priority bits, let `B` be a validated nonzero BASEPRI encoding aligned to `0x20`, with `B <= 0xC0`. A candidate kernel-aware mapping is `M = (0xC0 - B) / 0x20`, `encoded(level) = 0xC0 - 0x20 * level` for `0 <= level <= M`. With `B = 0x20`, level 0 maps to `0xC0` and level 5 to `0x20`. `0xE0` remains reserved for PendSV. This candidate must be reconciled with the board's implemented priority bits, priority grouping, configured default IRQ priority and timer priority; the current Kconfig ranges do not enforce all those constraints. Supporting more priority bits is a separate explicit mapping, not an assumption made by this formula.

| Proposed case | Proposed outcome | Required evidence |
|---|---|---|
| Wrong call context | API-015 context fault when checked; `EMB_EPERM` in release | ISR invocation with a valid number and level; fault API identity checked |
| Unknown interrupt number | API-015 argument fault when checked; `EMB_EINVAL` in release | Each port's actual interrupt-number validity rules, with controller state unchanged |
| Level above the advertised port maximum | API-015 argument fault when checked; `EMB_EINVAL` in release | Maximum and maximum + 1, plus 255 where applicable; no register write |
| Valid programmable kernel-aware level | `EMB_OK`, with larger levels more urgent | Encoding checks and observed pending/delivery/preemption order on an applicable emulator or board |
| Fixed-level port, level 0 | `EMB_OK` as an explicit single-level operation | Port advertises maximum 0; tests never infer configurable urgency from success |
| Valid advertised level requiring an unsupported optional class | `EMB_ENOTSUP` in both build kinds | Explicit feature/capability contract before advertising the domain |

The fixed-level recommendation makes any nonzero request out of range, rather than reporting a successful priority change. It follows SPEC-002's above-maximum `EMB_EINVAL` rule. It deliberately changes today's no-op behavior and needs a compatibility note. If a later controller advertises multiple classes but cannot program one in a configuration, `EMB_ENOTSUP` applies to that supported-domain capability failure; it is not a substitute for range validation.

`EMB_IRQ_LEVEL_KERNEL_MAX` expands to `CONFIG_EMB_IRQ_KERNEL_LEVEL`, which already exists in Kconfig but is not connected to the port's actual domain at the reviewed revision; `EMB_IRQ_LEVEL_MAX` from the specification is not provided. This corrects the earlier note that the Kconfig symbol was absent. Supply usable limits and validate the configuration as part of this task. The generic kernel-independent class, hardware-description generation and board-only zero-latency setup need their own explicit supported scope. Correcting kernel-aware `set_level` alone does not establish every clause of KRN-IRQ-028 or KRN-IRQ-031.

The native timer-first ordering is part of the current model's applicability. Native level programming must not be simulated by changing that ordering in this task without a separate model review. Software encoding assertions do not establish Cortex-M preemption or physical timer jitter. Those outcomes stay unrun until an actual suitable emulator/board experiment completes.

## Stable thread indexes across priority changes

**Implementation follow-up:** [stable diagnostic identity](m2-02-thread-identity.md) implements this recommendation. The observations below describe the reviewed baseline.

KRN-THR-025 and SPEC-008 §3 call for a stable diagnostic index. `embk_thread_index()` in `kernel/include/embk/kernel.h` returns `base_prio` for the table scheduler and an `index` field otherwise. Reprioritizing a tiny thread therefore changes its trace and fault identity. There is another source-level limit: base's `next_index` in `kernel/thread/thread.c` stops incrementing at `0xFD`; later initializations receive that same value. The current allocation path neither reclaims indexes nor reports diagnostic-index exhaustion. Existing query tests check that an index is not `0xFF`, without establishing uniqueness over repeated construction. This saturation observation is code analysis, not an executed exhaustion test.

**Recommendation:** keep a separate diagnostic index in every scheduler profile, stable from successful init until successful destroy. Keep table slots as an internal scheduling detail. Indexes must be unique among live threads; they may be reused after destruction and are not generation-checked handles or durable identities across storage reuse/restart.

Preserve `0xFE` for idle and `0xFF` for invalid/non-thread context. A proposed pool is 0–253 for application and other non-idle threads. Admission failure returns `EMB_ENOMEM` in both build kinds, without publishing a partial handle or corrupting scheduler storage. An explicit fixed-size free-index bitmap is a candidate allocator; compare its storage and bounded scan/init cost with profile limits before choosing the implementation. The tiny TCB gains an independent field, but its actual size increase depends on padding and target ABI: report layout-probe results rather than assuming one byte of total cost.

Implementation evidence must cover stable identity after base-priority changes, inheritance boosts/restoration, suspend/resume and termination; uniqueness while multiple threads are live; reuse only after destruction; invalid-handle results; and exhaustion without partial construction. Review what lifetime excludes an observer racing destroy/reuse. Native differential output currently uses the diagnostic index to identify scenario threads, especially tiny's priority-based IDs; update runner translation explicitly so identity correction does not silently rename the scenario or corrupt replay. Query complexity stays O(1); allocation/free costs and configured RAM costs must be stated separately.

If tiny's resource budget cannot admit stable identity, propose a scoped KRN-THR-025/SPEC-008 amendment with the measured cost and a trace-consumer migration. Do not silently weaken the existing stable-index target to match the implementation.

## Public API identity in misuse faults

API-017 and SPEC-001 §5.3 require the public function and offending argument. `raise_info()` in `kernel/fault/fault.c` currently assigns `api_id = 0` for every fault. `EMBK_MISUSE` preserves the class, negated return status as code, argument and optional `__func__`, but common helpers can report their own name rather than the public caller. The hook's stack-local record is not the retained crash record described by the later observability milestone.

**Recommendation:** assign explicit, unique, nonzero `uint16_t` IDs to public APIs in a checked-in registry, with zero reserved for faults without public API attribution. Keep assigned values stable; do not derive them from declaration order, line numbers, addresses or optional strings. Pass the public entry ID and argument numbering explicitly through shared implementation helpers. Avoid a mutable global “current API,” which can be overwritten by nested entry or ISR activity.

For example, `emb_sem_take()` and `emb_sem_take_until()` both reach `take_common()` but must have distinct public IDs. The ID must survive context, handle and argument failure before a call reaches the helper as well as misuse detected within it. Argument indexes are one-based; zero identifies a context/sub-state failure or a failure without a single offending parameter. Preserve API-013 error precedence when more than one failure applies.

The implementation task must specify which manually raised application faults, kernel invariant/hardware faults and `EMB_CHECK` failures retain ID zero, instead of assigning a plausible but false public caller. Verify registry uniqueness, representative public wrappers sharing a helper, every implemented misuse class, thread/ISR/prekernel attribution, and builds with source-location strings disabled. Release builds still return statuses where specified; identifiers do not justify adding retained crash storage or claiming API-017's full crash-record verification complete.

## POINTER lifetime, lifecycle checks and resource costs

`embk_obj_from_raw()` checks a nonzero raw address by reading the object's type and active state. It does not validate arbitrary address accessibility or carry a generation. After destroy, valid still-owned storage can fail the active-state check; after reinitializing the same address, an old copied POINTER handle can refer to the new instance. The caller must keep the storage accessible and invalidate all copies before reuse. An arbitrary dangling/forged pointer is not a safe negative test that promises `EMB_EINVAL`.

The current thread/sem/mutex init paths reset supplied storage; they do not implement general active-storage double-init detection. Destroy paths enforce relevant waiter/ownership/thread-reuse conditions and mark storage uninitialized, without implementing the full planned DESTROYING protocol or generation protection. Record KRN-OBJ-007's double-init/state-transition gap explicitly. A future solution must account for zero-initialized versus uninitialized supplied storage and the cost of any registry; blindly reading an uninitialized type tag is not an adequate design.

**Recommendation:** keep POINTER ownership explicit and review each existing object's init/destroy preconditions, error precedence and storage-reuse boundary. Do not add TABLE/INDEXED semantics by wording alone. Binding target teardown, producer quiescence and storage reuse remain M2-03 work; present binding has no unbind API and must not be advertised as safely detached by an unrelated thread-index change.

Costs require separate bounds for waiters, armed timeouts, owned mutexes, inheritance depth, ready-table scans, batch wakes, stack fill and hooks. `embk_prio_propagate()` stops at `CONFIG_EMB_PI_MAX_DEPTH`: checked builds fault on exceeding it and release builds truncate propagation. `chain_reaches()` inspects only that many owner links. Neither path establishes unrestricted cycle detection or complete inheritance for an over-depth graph. Define admission/overflow behavior and adversarial depth tests before claiming that larger graphs are safe. Update affected complexity comments and specifications alongside a behavior change; an O(1) handle check does not make the complete operation O(1).

## Review and implementation sequence

1. Review each recommendation against SPEC-001/002/008/009 and the admitted first-release profile/board. Record any contract or architecture amendment before adopting it; this page is preparation, not an accepted ADR.
2. Implement one contract at a time with its public comments, configuration validation, model where affected, and conformance cases. Start with IRQ domain/mapping, then stable diagnostic indexes and attributable misuse faults.
3. Reconcile lifecycle preconditions and depth/resource bounds, with binding teardown reserved for M2-03. Do not add new blocking primitives until their prerequisite contracts and evidence are in place.
4. Run applicable checked/release/tiny/periodic and supported port checks. Record individual cases, exclusions, source/configuration identities and unrun hardware obligations. Only then check off the corresponding M2-02 implementation item.
