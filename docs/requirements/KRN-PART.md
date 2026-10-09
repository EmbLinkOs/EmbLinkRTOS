# KRN-PART - Partitions and the System Call Boundary

Group `KRN-PART`. Design: `docs/specs/SPEC-010-partitions-and-syscall-boundary.md`. Related groups: KRN-CAP (tables, rights), KRN-OBJ (generations, freeze), KRN-THR (threads of a partition), KRN-NOTIF (`K_FAULT`, IRQ delivery), KRN-TP (shares), KRN-MEM (regions), FLT (fault records), HW (generator outputs), SEC.

KRN-PART-001 to 007 originate in `docs/architecture/03-kernel-architecture.md` §8 (007 added by ADR-033). All are restated here as the authoritative copy. New requirements start at 008.

---

### KRN-PART-001  Every thread has one partition
**Statement.** Every thread shall belong to exactly one partition.
**Rationale.** The partition is the unit of accounting, rights, and containment.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: generator rejects a thread without a partition; the kernel partition is the default in unprotected builds.
**Trace.** SPEC-010 §2; ADR-015

### KRN-PART-002  Compiles to nothing without protection hardware
**Statement.** Builds without protection hardware shall have exactly one partition and no partition code.
**Rationale.** Footprint of tiny and base.
**Status.** Accepted 2026-10-07
**Verification.** Footprint test: the partition symbols are absent from tiny and base images.
**Trace.** SPEC-010 §14

### KRN-PART-003  Memory confinement
**Statement.** On protection hardware, memory access of an unprivileged partition shall be limited to its regions; violations shall be partition faults.
**Rationale.** Freedom from interference.
**Status.** Accepted 2026-10-07
**Verification.** Isolated-profile tests `part/access_outside_region_faults` on Armv7-M and Armv8-M; fuzzing of pointer arguments.
**Trace.** SPEC-010 §4, §6

### KRN-PART-004  Restart without reboot
**Statement.** Partition restart shall not require system reboot and shall restore the partition's static data to its initial image.
**Rationale.** Recovery of a contained fault while the rest of the system keeps its guarantees.
**Status.** Accepted 2026-10-07
**Verification.** Test: `part/restart_restores_data`, `part/restart_keeps_other_partition_latency` (T12 measurement).
**Trace.** SPEC-010 §7

### KRN-PART-005  Policy in the supervisor
**Statement.** Fault policy shall be per partition and applied by a supervisor partition, not hard-coded in the kernel.
**Rationale.** Hubris and Tock: policy outside the kernel keeps the kernel small and the policy testable.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: the kernel's fault path only records and notifies; Test: replacing the reference supervisor changes the behavior.
**Trace.** SPEC-010 §6

### KRN-PART-006  Static partitions in 1.0
**Statement.** Partition definitions shall be static, build-time data in 1.0; dynamic partition creation is FUTURE.
**Rationale.** Static tables are auditable and need no kernel allocator.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: no runtime partition creation API exists.
**Trace.** SPEC-010 §1, §2

### KRN-PART-007  Partition-local kernel storage
**Statement.** In isolated profiles, the kernel state that describes a partition (its threads, objects, wait nodes, capability table) shall live in a kernel-owned sub-region of that partition's memory, sized by the generator and re-initialized on restart; a partition shall not be able to consume kernel memory outside it.
**Rationale.** ADR-033.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: generator report lists kstore size per partition; Test: `part/init_outside_kstore_refused`.
**Trace.** SPEC-010 §3

### KRN-PART-008  Partition description
**Statement.** A partition shall be described by privilege, supervisor, fault policy, restart limit, regions, threads, objects, initial capabilities, capability table size, and optional share; the generator shall emit the partition table, capability tables, descriptors with kstore addresses, device grants, linker sections, and protection tables from it.
**Rationale.** One source of truth for isolation (SPEC-014).
**Status.** Accepted 2026-10-07
**Verification.** Generator tests `hw/gen_partitions_*`; schema validation.
**Trace.** SPEC-010 §2

### KRN-PART-009  Layout and hardware constraints
**Statement.** Each partition's RAM shall be one block with the kstore at its end, privileged-only; code shall be in per-partition flash sections; the generator shall enforce the region count, alignment, size, and kstore-exclusion constraints of the protection hardware and shall fail the build naming the violated constraint.
**Rationale.** Armv7-M's power-of-two regions and the region budget are the hard part; it belongs to the generator, not to runtime.
**Status.** Accepted 2026-10-07
**Verification.** Generator tests for Armv7-M and Armv8-M constraint violations; boot test of the isolated reference configuration.
**Trace.** SPEC-010 §3, §4.1, §4.3

### KRN-PART-010  Cross-partition switch cost only
**Statement.** Only a context switch between threads of different partitions shall reprogram the protection hardware and the privilege bit; switches within a partition shall cost the same as in the base profile.
**Rationale.** Isolation must not tax every switch.
**Status.** Accepted 2026-10-07
**Verification.** Benchmark: harness switch cost intra- and cross-partition (T5, T12).
**Trace.** SPEC-010 §4.4

### KRN-PART-011  Per-thread kernel stack, syscalls in privileged thread mode
**Statement.** Every unprivileged thread shall have a kernel stack in its partition's kstore; a system call shall switch to it and execute the marshaller and implementation in privileged thread mode with interrupts enabled, so that it can block through the wait protocol; the per-CPU exception stack shall be used only for trap entry and exit.
**Rationale.** Blocking is impossible in exception context; the kstore accounts the memory to the partition.
**Status.** Accepted 2026-10-07
**Verification.** Test: `part/syscall_blocks_and_resumes`; stack high-water of kernel stacks reported.
**Trace.** SPEC-010 §5.2

### KRN-PART-012  Generated stubs, marshallers, and table
**Statement.** Trap stubs, marshallers, and the syscall table shall be generated from the API annotations with explicit argument-kind classification; the generator shall refuse an unclassifiable prototype; syscall numbers shall be stable within a release line.
**Rationale.** No hand-written wrapper layer (R-001 §7, FreeRTOS); the review gate for new API is the generator.
**Status.** Accepted 2026-10-07
**Verification.** Generator test with a deliberately unclassifiable prototype; ABI document check in CI.
**Trace.** SPEC-010 §5.3

### KRN-PART-013  Argument validation
**Statement.** Marshallers shall validate handles through capability lookup with the annotated right, pointer arguments against the caller's regions with the needed access and bounded length, attribute structs by copy before validation, names by copy into kstore buffers, scalars by range, and callback pointers against executable regions; every failure shall be a status to the caller.
**Rationale.** No time-of-check to time-of-use window; no unvalidated user pointer (R-001 §7, NuttX).
**Status.** Accepted 2026-10-07
**Verification.** Test: `part/validate_*` matrix; fuzzing of the boundary with random arguments never faults the kernel.
**Trace.** SPEC-010 §5.4; KRN-CAP-002

### KRN-PART-014  Privileged code never traps
**Statement.** Privileged code shall call kernel implementations directly; an `SVC` or `ecall` from privileged code shall be a kernel fault in checked builds and `EMB_ENOTSUP` in release builds.
**Rationale.** One calling convention per privilege level; no nested syscall state.
**Status.** Accepted 2026-10-07
**Verification.** Test: `misuse/svc_from_privileged`.
**Trace.** SPEC-010 §5.1, §12

### KRN-PART-015  Fault capture and delivery
**Statement.** A fault in an unprivileged thread shall suspend that thread with its state preserved and the `FAULTED` flag, append a descriptor to the supervisor's fault ring, and set `EMB_NOTIFY_K_FAULT` on the supervisor's thread; a `halt` policy shall suspend the whole partition first; without a supervisor the fault shall be a kernel fault; faults in privileged threads, interrupt context, the kernel partition, or the supervisor shall be kernel faults.
**Rationale.** Containment with the evidence intact for the supervisor and the debugger.
**Status.** Accepted 2026-10-07
**Verification.** Test: `part/fault_delivered_to_supervisor`, `part/fault_without_supervisor_is_kernel_fault`, `part/fault_halt_policy`.
**Trace.** SPEC-010 §6

### KRN-PART-016  Restart procedure
**Statement.** Restart shall, in order: stop every thread of the partition without delivering results, flush other partitions' waiters on its objects and threads with `STALE`, release mutexes it owns outside itself with owner-death semantics; bump its generations; reset its devices; re-copy `.data`, zero `.bss`, re-initialize the kstore, objects, and capability table from the generated tables; start its autostart threads and count the restart.
**Rationale.** A defined, auditable sequence that leaves no dangling reference.
**Status.** Accepted 2026-10-07
**Verification.** Test: `part/restart_*` for each step's observable effect; model scenario `partition_restart` (M3).
**Trace.** SPEC-010 §7

### KRN-PART-017  Restart limits and delay
**Statement.** The reference supervisor shall enforce a per-partition restart limit over a window, escalating to the system action when exceeded, and shall wait a configurable minimum delay (default 5 ms) before restarting so that the faulted state can be recorded.
**Rationale.** Hubris's lesson: a crash loop must be visible, and the evidence must survive it.
**Status.** Accepted 2026-10-07
**Verification.** Test: `supervisor/restart_limit_escalates`, `supervisor/min_delay`.
**Trace.** SPEC-010 §6.3

### KRN-PART-018  Supervisor interface
**Statement.** The kernel shall offer the supervisor: dequeue of fault descriptors, partition info, suspend, resume, restart, and resumption of a faulted thread for debugging, each gated by the corresponding partition rights; the reference supervisor shall ship as a replaceable subsystem.
**Rationale.** Policy out of the kernel, mechanism in it.
**Status.** Accepted 2026-10-07
**Verification.** Test: `supervisor/*`; `misuse/partition_restart_without_right`.
**Trace.** SPEC-010 §6.3

### KRN-PART-019  Device capabilities and interrupt delivery
**Statement.** A device capability shall map the device's MMIO range into the partition and deliver its interrupts as a notification bit on a designated thread after the kernel masks the line; the thread shall unmask with `emb_irq_ack`, which requires the `irq` right; DMA shall require the DMA capability and buffers in a `DMA` region the partition may access.
**Rationale.** Drivers in partitions without kernel code per driver (Hubris, R-001 §7).
**Status.** Accepted 2026-10-07
**Verification.** Test: `part/device_irq_as_notification`, `misuse/irq_ack_without_right`; latency measured (T6 class).
**Trace.** SPEC-010 §8

### KRN-PART-020  Shared regions
**Statement.** Shared memory between partitions shall be declared at top level with each participant's permissions and mapped as a region for each; the kernel shall not mediate accesses to it.
**Rationale.** Zero-copy sharing with explicit permissions.
**Status.** Accepted 2026-10-07
**Verification.** Test: `part/shared_region_perms`.
**Trace.** SPEC-010 §9

### KRN-PART-021  Partition accounting
**Statement.** When partition shares are configured, the kernel shall charge thread execution to the thread's partition at every switch and deliver share and budget events per ADR-029.
**Rationale.** Temporal protection needs per-partition time.
**Status.** Accepted 2026-10-07
**Verification.** Test: `tp/partition_share_enforced_under_load`.
**Trace.** SPEC-010 §10; KRN-TP-007 to 009

### KRN-PART-022  Observability
**Statement.** Partition faults, restarts, suspends, resumes, denied capability uses, and delivered interrupts shall be trace events; syscall tracing shall be an option; per-partition statistics shall include syscalls, denials, faults, restarts, and CPU time; the debug descriptor shall export the partition table, kstore layouts, and capability tables; contained faults shall optionally produce retained crash-record entries.
**Rationale.** Containment that cannot be observed cannot be trusted.
**Status.** Accepted 2026-10-07
**Verification.** Test: trace decoder; descriptor consumer; retained record read-back after a contained fault.
**Trace.** SPEC-010 §13; OBS-005, OBS-008

### KRN-PART-023  M3 scope
**Statement.** M3 shall implement static partitions, kstores, generated layout and protection tables, cross-partition switching, the syscall boundary, fault delivery, the reference supervisor, restart, device capabilities with interrupt delivery, and shared regions on Armv7-M and Armv8-M; PMP, ports and leases, budget donation, secure partitions, and the trap-free kernel page are deferred.
**Rationale.** The roadmap's M3 exit criteria (07 §2).
**Status.** Accepted 2026-10-07
**Verification.** M3 exit review against this list.
**Trace.** SPEC-010 §15; 07 §2 M3
