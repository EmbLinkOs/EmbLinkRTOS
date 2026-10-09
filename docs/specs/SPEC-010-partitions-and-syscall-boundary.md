# SPEC-010 - Partitions and the System Call Boundary

**Status:** Accepted 2026-10-07 by the project owner ("do all"), with the defaults of §16. Specification work item 9 of the roadmap (07 §3), scoped to what M3 implements (§15).
**Requirements:** `docs/requirements/KRN-PART.md` (KRN-PART-001 to 007 restated; new from 008).
**Builds on:** 03 §8 (partition model), §8.1 (syscall boundary), §9.1 (regions), §10 (faults); SPEC-009 (capability tables, generations, freeze); SPEC-008 (threads, exit, restart interplay); SPEC-006 (`K_FAULT` bit, IRQ delivery as notification); SPEC-005 §3.6 (owner death); SPEC-002 §9 (fault entry), §7 (stacks); SPEC-001 §5.3 (unprivileged callers always get a status); 04 §1.3 (`partitions.c`, `regions.ld`), §2, §3 (devices and drivers), §6 (security); ADR-015, ADR-029, ADR-032, ADR-033, ADR-035; 09 §7 (linker outputs per target).
**Research:** R-001 §7: Hubris (eight MPU regions per task rewritten on every switch, generation-tagged ids, supervisor out of the kernel with restart rate limiting, IRQs delivered as notifications), Tock (grants carved from process memory, fault policy objects), Zephyr (privileged stack per user thread, generated marshallers, MPU reprogrammed in PendSV), FreeRTOS MPU v2 (per-task syscall stack, index handles, ACLs), NuttX (generated proxies and stubs, unvalidated pointers: the thing to avoid). This specification takes the designed-in mechanisms and leaves out what retrofits cost.

---

## 1. Scope and terms

| Term | Meaning |
|---|---|
| **Partition** | The unit of isolation and fault containment (ADR-015): threads, regions, capability table, kernel-owned storage, optional budget or share, fault policy, supervisor, devices |
| **Kernel partition** | Always present, privileged; the only partition in `tiny` and `base`, where the partition type compiles to nothing (KRN-PART-002) |
| **Privileged / unprivileged** | Whether the partition's threads run in the processor's privileged mode. Unprivileged threads reach the kernel only through the syscall boundary |
| **Region** | A range of memory with permissions, from the hardware description (03 §9.1); a partition references the regions it may access |
| **kstore** | The kernel-owned sub-region inside a partition's memory that holds the kernel's state about that partition (ADR-033) |
| **Supervisor** | A privileged partition that receives fault notifications and applies policy (03 §8) |
| **Restart** | Terminating a partition's threads, re-initializing its memory and objects from the image, and starting its entry threads again, without rebooting (KRN-PART-004) |
| **Syscall boundary** | The trap (`SVC`, `ecall`) through which an unprivileged thread calls the kernel, where every argument is validated |
| **Device capability** | The right to a device: its MMIO region becomes accessible to the partition and its interrupts are delivered to it as notifications |

Everything here is static, build-time data in 1.0 (KRN-PART-006); the generator (SPEC-014) emits it from the system description.

## 2. Partition definition

```yaml
partitions:
  - name: sensors
    privilege: unprivileged
    supervisor: supervisor                 # partition that receives this one's faults
    fault_policy: restart                  # halt | restart | notify | escalate: the supervisor's default action
    restart_limit: { count: 3, window_ms: 10000 }
    regions:
      - { ref: sram_app, size: 16K, perms: rw }      # data, bss, stacks, kstore are carved from here
      - { ref: flash, section: text, perms: rx }     # code
      - { ref: shared_frames, perms: rw }            # a shared region declared at top level
    threads:
      - { name: acq, entry: acq_main, priority: 5, stack: 1K, autostart: true, critical: true }
      - { name: filt, entry: filt_main, priority: 4, stack: 2K }
    objects:
      - { name: frames, type: msgq, item_size: 32, capacity: 8 }
      - { name: ready, type: sem, max: 1 }
    capabilities:                           # initial grants into this partition's table
      - { object: display.cmds, rights: [send] }
      - { device: adc1, rights: [access, irq] }
      - { partition: supervisor, rights: [observe] }
    cap_table_size: 16
    share: { percent: 30, critical_capacity_us: 500 }   # ADR-029, optional
```

The generator turns this into `partitions.c` (partition table, per-partition capability tables, thread and object descriptors with their kstore addresses, device grants), `regions.ld` (per-partition sections and the kstore placement), and the MPU or PMP configuration constants (§4). It rejects a definition the hardware cannot express (§4.3).

## 3. Memory layout and the kstore

Each partition's RAM is one block carved from the referenced region:

```
+------------------+  partition RAM block (aligned as the protection hardware needs)
| .data + .bss     |  unprivileged RW, XN
| thread stacks    |  unprivileged RW, XN (one per thread, guard per §4.2)
| shared buffers   |  if the partition owns a shared region, it is separate (declared at top level)
+------------------+
| kstore           |  privileged only: TCBs, kernel stacks of the partition's threads (§5.2), object storage,
|                  |  capability table, name buffers, fault descriptor ring (supervisor), accounting
+------------------+
```

- The **kstore** is sized by the generator from the declared threads and objects plus the `cap_table_size` (KRN-PART-007); its size appears in the generated report so that kernel memory per partition is a visible number. A partition cannot allocate kernel memory outside it: `init` of an object by an unprivileged partition must name storage inside its kstore, which the generated `_DEFINE` tables do automatically.
- Code lives in a per-partition flash section (`.partition.<name>.text` and `.rodata`), so that a code region can be granted execute permission without exposing other partitions' code. Shared library code (the C runtime, `libemb` stubs) is linked into every partition's section that uses it, or into a read-only common region every partition may execute; the generator chooses the common region when more than one partition uses a symbol and the hardware has a spare region (§4.3).
- `.data` initial values live in flash next to the code and are copied at start and at restart (KRN-PART-004).
- Kernel-partition memory is ordinary kernel RAM; the kernel partition has no kstore of its own.

## 4. Protection hardware

### 4.1 Mapping

| Hardware | Regions | Model | Notes |
|---|---|---|---|
| Armv7-M MPU (STM32F4) | 8 | power-of-two size and alignment, 8 subregion disable bits | the generator rounds each partition block to a power of two and uses subregion disables for the kstore (it must fall on subregion boundaries: block size at least 256 bytes, kstore a multiple of block/8) |
| Armv8-M MPU (RP2350, Cortex-M33) | 8 or 16 | base and limit, 32-byte granularity, MAIR attributes | direct; the kstore is simply excluded from the unprivileged region |
| RISC-V PMP | 8 or 16 entries | NAPOT or TOR | TOR pairs for data, NAPOT for code; M5 |
| native | n/a | software checks in the gate (SPEC-013) | validation logic tested, protection simulated |

The kernel runs with the **privileged default map** (ARM `PRIVDEFENA`, RISC-V M-mode without PMP enforcement for M-mode) so that kernel code and the kstores need no MPU region of their own; unprivileged access to anything outside the active partition's regions faults.

### 4.2 Per-partition region set

| Purpose | Count | Permissions |
|---|---|---|
| code section (or the common read-only region) | 1 (2 with a common region) | unprivileged RX |
| data, bss, stacks block | 1 | unprivileged RW, XN |
| each shared region the partition references | 1 each | as declared |
| each device granted | 1 each (adjacent devices may share one region when the generator can prove it covers nothing else) | unprivileged RW, XN, device memory attributes |
| active thread stack guard (`CONFIG_EMB_STACK_GUARD_MPU`) | 1 | no access, placed at the active thread's stack limit at every switch (ChibiOS guard pages, R-001 §A6); unnecessary on Armv8-M, which uses `PSPLIM` |

### 4.3 Generator checks

The region budget per partition must fit the hardware with the kernel's reservations; alignment and size constraints must hold; the kstore must be excludable; no two partitions' blocks may overlap; a shared region must be declared at top level with every participant's permissions. A violation fails the build with the partition and the constraint named (BLD-007 pattern). Region count and layout are also emitted into the build report.

### 4.4 Switch cost

At a context switch the kernel compares the partition of the outgoing and incoming threads; only a **cross-partition** switch reprograms the partition region set (N register pairs, N as above), sets the privilege bit (`CONTROL.nPRIV`, `mstatus.MPP`) and, where configured, the stack guard. Switches inside a partition cost the same as in the base profile. The reprogramming cost is published by the harness (R-003 T12).

## 5. The system call boundary

### 5.1 Selection at build time

The public API header is the same for every caller (SPEC-001). A translation unit compiled for an unprivileged partition (the generator sets `EMB_PARTITION_UNPRIVILEGED` for that partition's sources) resolves each public function to its **trap stub** instead of the kernel implementation; privileged code calls the implementation directly. No source changes between placements (03 §8.1; the request/completion driver model is what lets a driver live in either, 04 §3.1).

### 5.2 Mechanism (Cortex-M)

1. The stub loads the syscall number into `r12`, the first four arguments in `r0`..`r3`, further arguments on the caller's stack as the AAPCS already does, and executes `svc #0`.
2. The `SVC` handler runs on the per-CPU exception stack (`MSP`). It records the caller's `PSP`, checks that the caller was unprivileged (an `SVC` from privileged code is a kernel fault in checked builds: privileged code never traps), checks `r12 < EMB_SYSCALL_COUNT`, switches `PSP` to the thread's **kernel stack** in the kstore (`CONFIG_EMB_KSTACK_SIZE`, default 512 bytes), builds an exception frame that returns into `embk_syscall_dispatch(nr, args, user_frame)` in **privileged thread mode**, and returns from the exception.
3. `embk_syscall_dispatch` runs with interrupts enabled at the thread's priority: it calls the generated marshaller for `nr`, which validates every argument (§5.4) and calls the implementation. Blocking happens here, in thread mode, through the ordinary wait protocol; the kernel stack holds the thread's kernel context while blocked.
4. On return the marshaller writes out-parameters through validated pointers, places the status in the user frame's `r0`, and executes `svc #1` (`SYSCALL_RETURN`): the handler restores `PSP` to the user stack, clears privilege, and returns into the instruction after the original `svc`.

RISC-V: `ecall` from U-mode traps to M-mode; the trap entry does the same stack switch and returns with `mret` into the dispatcher in M-mode on the thread's kernel stack; the return path is a second `ecall`-free `mret` with `MPP=U`. The per-CPU trap stack is used only inside the trap entry and exit. There is no S-mode on the targets of 1.0.

Why a per-thread kernel stack rather than running syscalls on the exception stack: a syscall may block (SPEC-001 §5: thread-only functions block), and an exception context cannot. Zephyr's privileged stacks and FreeRTOS's per-task syscall stacks solve the same problem (R-001 §7); the kstore is where ours live, so their memory is accounted to the partition.

### 5.3 Syscall table generation

`tools/gen_syscalls` reads the API annotations of SPEC-001 §9 (`@ctx`, `@req`, argument kinds from the prototype: handle, pointer-in, pointer-out, length, scalar, attribute struct) and emits:

- the stubs (`embsys_*`), one per public function that is callable from thread context;
- the marshallers (`embk_sys_*`), one per function, with the validation of §5.4 derived from the argument kinds and the right named by `@req`;
- `embk_syscall_table[EMB_SYSCALL_COUNT]` and the numbering header; numbers are stable within a release line and listed in the ABI document (SPEC-001 §11).

Pre-kernel-only functions and ISR-only entry points get no stub. The generator refuses a prototype whose argument kinds it cannot classify, which is the review gate for new API (NuttX's CSV and Zephyr's markers, with the classification made explicit, R-001 §7).

### 5.4 Argument validation

| Argument kind | Check |
|---|---|
| handle | `embk_cap_lookup(caller partition, handle, type, right from @req)` (SPEC-009 §3.3); failures are statuses, never faults (KRN-CAP-002) |
| pointer in, with length | the range `[ptr, ptr+len)` lies within the caller's readable regions; `len` bounded by the function's documented maximum |
| pointer out, with length | same, writable; the kernel writes through it only after the operation, never reads it |
| attribute struct | copied into a kernel-stack buffer of the known size after a readable check; the implementation sees the copy, so a concurrent change by another thread of the partition cannot race the validation (no time-of-check to time-of-use) |
| name pointer in an attribute | copied into the object's name buffer in the kstore (`CONFIG_EMB_OBJ_NAME_LEN`, default 16, truncated) rather than kept by pointer |
| scalar, enumeration | range-checked as the public function documents |
| timeout | any value is valid |
| callback or entry pointer (thread entry, timer callback) | must lie within the partition's executable regions; the kernel calls it only in that partition's threads |

A failed check returns `EMB_EPERM` (rights, access) or `EMB_EINVAL` (range) to the caller. No check faults the kernel: an unprivileged partition cannot crash the system through the API (SPEC-001 §5.3 isolated profiles).

### 5.5 Cost and fast paths

A syscall costs two exception entries and exits plus the stack switch and the marshaller; the harness publishes it (R-003 §6). `emb_time_now()` and `emb_thread_self()` are the only calls an unprivileged partition makes often enough to matter; a read-only kernel page mapped into every partition (tick count, current thread index) that lets their stubs avoid the trap is FUTURE (§16.9).

## 6. Faults

### 6.1 Capture

A synchronous fault in an unprivileged thread (`emb_arch_fault_entry`, SPEC-002 §9) captures an `emb_fault_info_t` (fault class and code, faulting address, `pc`, `sp`, the thread and partition, architecture status registers) and calls `embk_fault_dispatch`, which classifies it as a **partition fault** (03 §10.1). A fault in a privileged thread, in interrupt context, in the supervisor, or in the kernel partition is a **kernel fault** and follows 03 §10: crash record, then the configured action.

### 6.2 Delivery

The faulting thread is suspended with the `FAULTED` flag (its stack and registers are preserved for the supervisor and the debugger, as Hubris does); the fault descriptor is appended to the supervisor's fault ring (in the supervisor's kstore, `CONFIG_EMB_FAULT_RING`, default 4; overflow counts and keeps the oldest); `EMB_NOTIFY_K_FAULT` is set on the supervisor's designated thread (SPEC-006 §2.1). The kernel takes no further action (KRN-PART-005). Without a declared supervisor, a partition fault is a kernel fault with the crash record and the configured system action (halt by default).

Other threads of the faulting partition keep running until the supervisor decides; a `fault_policy` of `halt` in the description makes the kernel stop the whole partition (suspend all its threads) before notifying, for partitions whose continued execution after a fault is unacceptable.

### 6.3 Supervisor interface

```c
emb_status_t emb_partition_fault_next(emb_fault_desc_t *out);      /* dequeue one descriptor; EMB_ENOENT when empty */
emb_status_t emb_partition_info(emb_partition_t p, emb_partition_info_t *out);  /* state, restart count, uptime since start */
emb_status_t emb_partition_suspend(emb_partition_t p);               /* stop all its threads (overlay) */
emb_status_t emb_partition_resume(emb_partition_t p);
emb_status_t emb_partition_restart(emb_partition_t p);               /* §7; requires RESTART */
emb_status_t emb_partition_thread_resume_faulted(emb_thread_t t);   /* debugging aid: continue a faulted thread (OBSERVE + RESTART rights) */
```

The reference supervisor (`subsys/supervisor/`, enabled by `CONFIG_EMB_SUPERVISOR_DEFAULT`) applies the described `fault_policy` per partition, enforces `restart_limit` (exceeding it escalates to the kernel's system action), waits `CONFIG_EMB_RESTART_MIN_DELAY` (default 5 ms, Hubris's value) so that a debugger or the crash recorder can read the faulted state first, records every fault in the trace, and notifies interested partitions (those holding `OBSERVE` on the faulted partition) with an application bit of their choosing. It is a partition like any other; a product may replace it.

## 7. Restart

`emb_partition_restart(p)` runs in the caller's thread context, in this order, at a safe point (on uniprocessor none of `p`'s threads is running; on SMP each running thread of `p` is stopped by IPI at its next kernel entry or preemption point before step 1 proceeds):

1. **Stop.** Every thread of `p`: removed from the ready structure or from its wait queue (dequeued with no result delivered: the thread never returns to user mode), its timeout disarmed, its `FAULTED` flag cleared. Threads of other partitions waiting *on* `p`'s objects or threads (join, port requests, bound notifications) are flushed with `STALE` (ADR-032, KRN-OBJ-005). Mutexes outside `p` owned by `p`'s threads are released with owner-death semantics to their waiters regardless of `CONFIG_EMB_MUTEX_OWNER_DEATH`, because the owner is certainly gone (SPEC-005 §3.6).
2. **Generations.** The partition's generation and the generations of all its threads and objects are bumped; every capability to them held elsewhere is now stale until re-granted by the build-time table (step 5) or by a new grant.
3. **Devices.** Each device granted to `p` is reset through its lifecycle (04 §2.2: `READY` to `UNINIT` to `READY`), its interrupts masked and pending notifications discarded.
4. **Memory.** `.data` is re-copied from the image, `.bss` zeroed, stacks re-filled, the kstore re-initialized: TCBs rebuilt from the generated descriptors, objects re-`init`ed from the generated table, the capability table rebuilt from the generated initial table. The system freeze flag does not block this path (KRN-OBJ-004).
5. **Start.** The partition's `autostart` threads are started; `restart_count` increments; the `partition_restart` trace event is emitted; partitions holding `OBSERVE` are notified.

Restart never requires a system reboot and touches no memory outside `p`'s regions and the kernel's own tables (KRN-PART-004). Its duration is dominated by the `.data` copy and is published per reference board (R-003 T12); other partitions keep their latency guarantees during it because the restart runs at the restarting thread's priority and is preemptible between steps.

## 8. Devices in partitions

A device capability (`access`, `irq` rights) gives a partition:

- an MPU or PMP region over the device's MMIO range (§4.2), so the driver code in the partition touches registers directly;
- interrupt delivery as a notification: the kernel's handler for a granted interrupt masks the interrupt line and sets the bound application bit on the partition's designated thread; the thread services the device and calls `emb_irq_ack(irq)` (requires `irq`) to unmask (Hubris's scheme: the thread names the interrupt by its own bit, no ownership lookup on the hot path, R-001 §7);
- DMA: only when the device's DMA capability is also granted; DMA buffers must lie in a region marked `DMA` that the partition may access, and the generator places them there (KRN-MEM-008).

Interrupt-to-thread latency for a partition driver is the kernel handler plus a notification wake (R-003 T6 class); the documentation states it, and drivers with hard latency needs stay in the kernel partition. The same driver source serves both placements (04 §3.1).

## 9. Shared memory and cross-partition objects

- A shared region is declared at top level with each participant's permissions; after setup the kernel is not involved in accesses. Synchronization uses kernel objects with capabilities granted to the participants (a semaphore with `GIVE` in one partition and `TAKE` in the other), or a message queue of indices into the shared region.
- Cross-partition kernel objects live in the owner partition's kstore; the kernel accesses them in privileged mode on behalf of any capability holder. An object outlives its holders' capabilities but not its owner's restart.
- Notifications across partitions require the `NOTIFY` right on the target thread (SPEC-006 §2.2).
- Ports and leases are FUTURE (SPEC-007 §5).

## 10. Budgets, shares, and accounting

The kernel charges a thread's execution to its partition when partition shares are configured (ADR-029, 03 §2.3): the per-partition accumulator is updated at every switch, and the sliding-window share logic reads it. `CRITICAL` threads are declared in the partition description. Budget overruns with the `NOTIFY` policy set `EMB_NOTIFY_K_BUDGET` on the supervisor's thread with a descriptor naming the partition; `FAULT` produces a partition fault with the consumed time in the descriptor (KRN-TP-009).

## 11. Pre-kernel and the kernel partition

Pre-kernel code (`main()` before `emb_kernel_start()`) runs privileged and may initialize objects for any partition only through the generated tables; hand-written pre-kernel code touching another partition's kstore is misuse that the generator cannot detect and the documentation forbids. The kernel partition's threads are privileged; drivers placed there call the kernel directly. The supervisor is normally a thread of the kernel partition in small systems and its own small partition when freedom from interference between the supervisor and kernel-partition drivers is wanted.

## 12. Misuse and diagnostics

| Condition | Checked build | Release build |
|---|---|---|
| unprivileged call with a bad handle, missing right, inaccessible pointer, out-of-range scalar | status | `EMB_EINVAL` / `EMB_EPERM` / `EMB_ESTALE` (never a fault, KRN-CAP-002) |
| `SVC` executed by privileged code | fault `EMB_FAULT_API_CONTEXT` | `EMB_ENOTSUP` returned by the handler |
| unknown syscall number | status | `EMB_ENOTSUP` |
| memory access outside the partition's regions | partition fault (hardware) | same |
| restart of the kernel partition or of the supervisor by itself | fault `EMB_FAULT_API_OWNER` | `EMB_EPERM` |
| restart limit exceeded | escalation per the supervisor | same |
| partition description the hardware cannot express | build error (generator) | build error |
| fault ring overflow | counted, oldest kept | same |

## 13. Observability

Trace events: `partition_fault(p, thread, class, code, pc)`, `partition_restart(p, count, duration)`, `partition_suspend`, `partition_resume`, `syscall(p, nr, status)` (optional, high rate), `cap_denied(p, nr, reason)`, `irq_delivered(p, irq)`. Statistics: per partition, syscall count, denied count, faults, restarts, CPU time; the debug descriptor exports the partition table, each kstore's layout, and the capability tables so a debugger shows "who may do what to whom". Partition faults also produce a non-fatal crash-record entry in the retained ring when `CONFIG_EMB_FAULT_RECORD_PARTITION` is set, so field diagnostics cover contained faults too (04 §7.3).

## 14. Profiles

| Profile | Partitions | Syscall boundary | Supervisor |
|---|---|---|---|
| tiny, base | one (kernel), type compiles to nothing | none; every call is direct | none; faults are kernel faults |
| isolated | N, static | generated stubs and marshallers | reference supervisor or product's own |
| multicore | N per core image; cross-core via ports (FUTURE) | same | per core |
| native | N simulated; validation logic real, protection simulated by the gate | same stubs, trap simulated | same |

## 15. M3 scope

Implemented in M3 on the STM32F407 Discovery, NUCLEO-F446RE (Armv7-M MPU) and RP2350 (Armv8-M MPU): static partitions with kstores, generated layout and MPU tables with the generator checks, cross-partition switch with region reprogramming, the syscall boundary with generated stubs and marshallers, fault capture and delivery, the reference supervisor, restart, device capabilities with interrupt delivery as notifications, shared regions. Deferred: RISC-V PMP (M5), ports and leases, budget donation, TrustZone secure partitions (04 §6.2: the secure side is a PSA-style manager, not a partition), the read-only kernel page fast path.

## 16. Decisions taken at acceptance (2026-10-07)

1. Partitions, their regions, threads, objects, capabilities, devices, and policies are static YAML in the system description; the generator emits `partitions.c`, `regions.ld`, and the protection tables, and fails the build on anything the hardware cannot express.
2. The kstore sits at the end of the partition's RAM block, privileged-only, sized by the generator; it holds TCBs, per-thread kernel stacks, object storage, the capability table, name buffers, and (for a supervisor) the fault ring.
3. Only cross-partition switches reprogram the protection hardware.
4. Syscalls execute in privileged thread mode on a per-thread kernel stack (default 512 bytes) so they can block; the exception stack is used only for entry and exit.
5. Stubs, marshallers, and the table are generated from the API annotations; argument kinds are classified explicitly and an unclassifiable prototype is a generator error.
6. Attribute structs are copied before validation; names from unprivileged partitions are copied into kstore buffers (16 bytes).
7. A faulted thread is suspended with its state preserved; the fault goes to the supervisor's ring and `K_FAULT` bit; without a supervisor a partition fault is a kernel fault. A `halt` policy stops the whole partition before notifying.
8. Restart order: stop, generations, devices, memory, start; mutexes owned outside the partition are released with owner-death semantics unconditionally; minimum restart delay 5 ms and a restart limit are the reference supervisor's job.
9. Interrupts granted to a partition are delivered as notification bits with explicit `emb_irq_ack`; the read-only kernel page for trap-free `emb_time_now()` is FUTURE.
10. The reference supervisor ships as `subsys/supervisor/` and is replaceable.
