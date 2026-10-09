# Research note: Hubris and Tock

**Kind:** Read-only source analysis, 2026-10-07, performed on shallow clones. File and line citations refer to Hubris commit `446dfcd5019a` (oxidecomputer/hubris) and Tock commit `40b9e1378345` (tock/tock, sparse: `kernel/`, `doc/`). Line numbers drift; file paths and symbol names are the durable reference. Synthesized in `R-001`.

---

## Part A: Hubris

### A1 Task model
- Fixed task set from `app.toml`; no create or destroy at runtime ("you can't write a fork-bomb without fork", doc/tasks.adoc:68-97). The build emits `kconfig.rs` (sys/kern/src/startup.rs:133; schema in build/kconfig/src/lib.rs:9-27, 50-76) with `HUBRIS_TASK_DESCS`, `HUBRIS_TASK_TABLE_SPACE`, and perfect-hash IRQ tables (sys/kern/build.rs:200-280).
- Each task has `REGIONS_PER_TASK = 8` region descriptors by reference; unused slots point at a shared no-access region (sys/kern/src/descs.rs:7, 35-42, 78-104). Alignment: Armv6-M and Armv7-M regions must be power-of-two, naturally aligned, at least 32 bytes (descs.rs:94-100; arm_m.rs:400-454 computes RBAR/RASR); Armv8-M needs 32-byte alignment and uses RBAR/RLAR plus MAIR (arm_m.rs:550-650). The MPU is disabled, all eight regions rewritten, and re-enabled on every switch (arm_m.rs:480-521, 598-650; "single-digit cycles", 613).
- Priorities are a fixed `u8`, 0 highest; `Priority` deliberately has no `Ord` (descs.rs:9-31); strict priority, no time slicing, cooperative within a level, up to 256 levels (doc/tasks.adoc:19-42).
- Scheduler: `priority_scan` is a linear scan of the whole table from `previous+1` picking the most important runnable task, round-robin among equals (sys/kern/src/task.rs:858-909); the kernel panics if nothing is runnable (864), so an idle task is required (task/idle/src/main.rs:13-30, `wfi`).
- States: `TaskState::Healthy(SchedState) | Faulted{fault, original_state}`; `SchedState::{Stopped, Runnable, InSend(TaskId), InReply(TaskId), InRecv(Option<TaskId>)}` (sys/abi/src/lib.rs:239-304).
- `TaskId(u16)` is a 10-bit index plus a 6-bit generation; the generation bumps on every reinit; stale ids receive a "dead code" `0xFFFF_FF00 | new_gen` (abi/src/lib.rs:13-85, 216-234; checked in task.rs:838-856). `TaskId::KERNEL = !0`.

### A2 IPC
- Synchronous send, recv, reply modelled on L4; rationale: single copy, no kernel queues to size, limits fault amplification, callers are known to be parked (doc/ipc.adoc:53-98). Messages are capped at 256 bytes because copies are uninterruptible (ipc.adoc:205-210).
- `send`: if the callee is in a matching recv, `deliver` copies via `safe_copy` and switches directly to the callee (`NextTask::Specific`); otherwise the caller blocks `InSend` (sys/kern/src/syscalls.rs:162-210, 691-725). `safe_copy` validates both slices against each task's region table (umem.rs:346-388; `Task::can_access` task.rs:209-225 forbids DEVICE, usually DMA).
- Leases: the sender passes a `ULease` table (base, len, READ or WRITE) (abi/src/lib.rs:184-214); the server reads and writes via `BORROW_READ`, `BORROW_WRITE`, `BORROW_INFO`; `borrow_lease` honours a lease only while the lender is `InReply(server)`, so leases are revoked atomically when the client resumes (syscalls.rs:588-660; ipc.adoc:215-249). Up to 255 leases.
- `reply` is fire-and-forget: a wrong or stale target is silently ignored (MINIX 3 rationale, ipc.adoc:375-409; syscalls.rs:367-392); an oversized reply faults the server (`ReplyTooBig`, 426-430). `REPLY_FAULT` lets a server fault a misbehaving client with `ReplyFaultReason::{UndefinedOperation, BadMessageSize, BadMessageContents, BadLeases, ReplyBufferTooSmall, AccessViolation}` recorded as `FaultInfo::FromServer` (abi:424-452; syscalls.rs:829-874).
- Closed receive: `recv` with a specific `TaskId` accepts only that sender (bit 31 flags the argument, task.rs:502-509; syscalls.rs:262-310); used for mutual exclusion (ipc.adoc:435-449).
- Notifications: 32 bits per task, OR'd by `post` (task.rs:236-271), consumed by `recv` with a mask; delivered as a message from `TaskId::KERNEL` with the bits in `operation` (ipc.adoc:493-535; syscalls.rs:242-254). `post` forces a reschedule only when the peer is more important (syscalls.rs:796-820).
- IRQs: `app.toml` maps `interrupts = {"usart1.irq" = "usart-irq"}` (app/gimletlet/app.toml:71-74); the kernel `DefaultHandler` looks up `HUBRIS_IRQ_TASK_LOOKUP`, masks the IRQ, and posts the notification (arm_m.rs:1247-1300). Tasks re-enable via `irq_control` by notification mask rather than IRQ number, so no ownership check is needed (doc/interrupts.adoc:66-95; syscalls.rs:732-768). Example driver loop: drv/lpc55-spi-server/src/main.rs:70,129.
- Timers: one per task {deadline: Option<Timestamp>, notification set}; fires once then clears (task.rs:758-769, 812-830; doc/timers.adoc:189-214). The time base is a 64-bit tick count (`Timestamp(u64)`, time.rs:14-18) incremented by SysTick, which also runs `process_timers` and pends PendSV if anything woke (arm_m.rs:1088-1124). Multiple deadlines are multiplexed in userland (`lib/multitimer`, timers.adoc:247-255).

### A3 Supervision
- Fault record: `FaultInfo::{MemoryAccess{address,source}, StackOverflow, BusError, DivideByZero, IllegalText, IllegalInstruction, InvalidOperation(cfsr), SyscallUsage(UsageError), Panic, Injected(TaskId), FromServer(..)}`, `FaultSource::{User, Kernel}` (abi/src/lib.rs:312-422). `force_fault` saves `original_state` and posts bit 0 to task 0 (task.rs:928-955; `HUBRIS_FAULT_NOTIFICATION = 1`, startup.rs:22). Only the task state changes; stack and registers are preserved for the debugger (arm_m.rs:1399-1480 saves r4-r12, lr, FP registers).
- Supervisor is task index 0 at priority 0; if it faults the system reboots (doc/guide/supervision.adoc:183-205). Jefe loop: on the FAULT notification it iterates `kipc::find_faulted_task`, counts faults, optionally dumps the task, then reinits or holds (task/jefe/src/main.rs:450-540). Restart is rate-limited: `MIN_RUN_TIME = 50` ms and `MIN_RESTART_DELAY = 5` ms so an error report can be collected first (main.rs:57-80).
- `reinit_task` (kernel IPC, kipc.rs:118-180): bump generation, reset timer, notifications, state, re-scribble the stack with 0xbaddcafe, reset registers (task.rs:318-325; arm_m.rs:279-376), then unblock every task `InSend`, `InReply`, `InRecv(Some)` on the old id with the dead code. The kernel does not re-initialize `.data`/`.bss` or MPU config; userlib `_start` does data init (doc/tasks.adoc:143-175). All task RAM state is lost; peers see `DEAD`, typically call `next_generation()`, and retry (abi:22-26).
- Hold-for-debug: Humility writes `JEFE_EXTERNAL_REQUEST`, `TASKINDEX`, `KICK` statics (Start, Hold, Release, Fault) polled by jefe every 100 ms; the comment explains why this bypasses IPC (priority inversion with the HIF task) (task/jefe/src/external.rs:7-44, 56-97).

### A4 Kernel internals
- No allocator: the kernel Cargo has no `alloc`; tables are `static mut` `MaybeUninit` arrays sized by kconfig (sys/kern/Cargo.toml:6-20; startup.rs:50-96); the task table is guarded by a single `TASK_TABLE_IN_USE` flag that panics on re-entry (startup.rs:20-23, 100-130).
- Syscall ABI: `SVC` with the immediate ignored; arguments in r4-r10, number in r11, returns in r4-r11, because r0-r3 land on the user stack (doc/syscalls.adoc:36-72). Numbers 0-13 in `Sysnum` (abi:454-471); kernel IPC to `TaskId::KERNEL` uses `Kipcnum` 1-10 (abi:507-519). `SVCall` assembly stores r4-r12, lr, and s16-s31 into the `Task.save` area, which must be first in the struct (arm_m.rs:983-1045; task.rs:30-35).
- Interrupt design: SysTick and IRQs do a cheap entry and set PendSV when a switch is needed; `pendsv_entry` runs `select` (arm_m.rs:16-70, 1218-1245). Faults go to `configurable_fault` then `handle_fault`, which classifies CFSR bits (MSTKERR as StackOverflow, IACCVIOL, DIVBYZERO), clears pended derived exceptions, and panics on kernel-mode faults (arm_m.rs:1685-1810). Armv8-M sets MSPLIM to the kernel stack base (arm_m.rs:790-792); the STKOF bit is decoded (1559-1560). No PSPLIM use was found; task stack overflow is caught by MPU gaps.
- Footprint (doc/tr1.adoc:28-47): kernel text 19,848 bytes plus 7,280 bytes rodata (9,824 bytes text without panic messages), 32 bytes static RAM, 408 bytes per TCB plus 32 bytes region tracking.

### A5 Observability
- `ringbuf!` static ring buffers with de-duplication counters and `counted_ringbuf!` per-variant counters, read by `humility ringbuf` or GDB, not streamed (lib/ringbuf/src/lib.rs:5-100, 166-190, 271-295). `counters!` stores event counts in `AtomicU32`s (lib/counters/src/lib.rs:5-60).
- Debugger-centric: the kernel exports `CURRENT_TASK_PTR`, `CLOCK_FREQ_KHZ`, `HUBRIS_TASK_TABLE_SPACE` as `no_mangle` statics (arm_m.rs:101-108; build.rs:425-428); Humility computes task addresses from DWARF `sizeof(Task)` (profiling.rs:26-45). Jefe exposes its whole server struct as `JEFE_SERVER_IMPL` for dumps (task/jefe/src/main.rs:82-85). Task dumps via `GetTaskDumpRegion` and `ReadTaskDumpRegion` (kipc.rs:235-300); panic messages kept to 128 bytes (doc/kipc.adoc:451). Kernel profiling hooks toggle GPIOs for logic analyzers (profiling.rs:5-23). Stack scribble lets Humility measure peak stack usage (tasks.adoc:157-161). No kernel UART logging path exists.

### A6 Priority inversion
Handled by convention, not mechanism: the "uphill send rule" (only send to higher-priority servers; "The kernel will enforce this, eventually", doc/guide/servers.adoc:24-35; ipc.adoc:100; FAQ.mkdn:32). `reply` assumes it goes downhill and never reschedules (syscalls.rs:454-457). No OS-level timeouts, by principle (FAQ.mkdn:44-49). Capabilities and IPC filtering are TODOs (intro.adoc:133-138; syscalls.rs:166).

### Hubris: ideas to borrow
1. Generation-tagged task ids with kernel-delivered dead codes so restarts are visible to peers (abi/src/lib.rs:13-26; kipc.rs:140-170).
2. Leases: borrowed memory validated on every access and implicitly revoked by the blocking state machine; no kernel copy of large buffers (syscalls.rs:588-660).
3. Syscall misuse is a fault, not an error code, and servers can `REPLY_FAULT` clients (tasks.adoc:114-117; syscalls.rs:829-874).
4. IRQs named by notification bits in the task's own namespace; no ownership check needed (interrupts.adoc:75-82).
5. Build-time generated kernel tables plus perfect-hash IRQ maps; all RAM usage knowable at compile time (build.rs:200-280; tasks.adoc:85-86).
6. Stack scribble plus debugger-readable statics instead of a logging subsystem (arm_m.rs:300-350; external.rs).
7. Supervisor policy out of the kernel with restart rate limiting and a fault "mailing list" (jefe main.rs:57-80, 540-548).

### Hubris: weaknesses
1. Priority inversion and deadlock avoided only by the unenforced uphill-send convention (servers.adoc:24-35); no inheritance.
2. Single synchronous send per task; async needs the pingback pattern and polling (ipc.adoc:536-579); the supervisor cannot safely `send` to anyone (jefe main.rs:18-24).
3. One timer per task; 1 ms SysTick polling of all tasks every tick (arm_m.rs:1088-1124).
4. O(n) scans of the task table for scheduling, receive, and faults (task.rs:882-909; syscalls.rs:326-330; supervision.adoc:224-226).
5. Eight MPU regions; power-of-two alignment on Armv7-M wastes RAM; separate compilation duplicates library code in flash (tasks.adoc:44-66).
6. Rust-only toolchain and `app.toml`; no runtime loading or partial update by design (intro.adoc:30-32).
7. A supervisor crash reboots the system; IPC filtering and MAC still TODO (supervision.adoc:205; syscalls.rs:166).

---

## Part B: Tock

### B1 Architecture
- Single kernel thread; capsules are event-driven Rust components trusted for liveness and correctness but written without `unsafe` (the core kernel holds "most unsafe code", kernel/src/lib.rs:10-11, 30-45; sensitive APIs gated by capability traits, capabilities.rs:5-50). Processes are MPU-isolated and untrusted (platform/mpu.rs:82-88).
- `kernel_loop` then `kernel_loop_operation`: tickle the watchdog; if `scheduler.do_kernel_work_now` run bottom halves and deferred calls (`execute_kernel_work`), else `scheduler.next()` leads to `do_process` or `TrySleep` with interrupts disabled (kernel.rs:374-451; scheduler.rs:48-61).
- `do_process`: start the scheduler timer with the timeslice; loop: check remaining quantum (`MIN_QUANTA_THRESHOLD_US`), ask `continue_process`, then by state: Running leads to `setup_mpu`, `enable_app_mpu`, `arm`, `switch_to`, `disarm`, `disable_app_mpu`, then handle Fault, Syscall, or Interrupted; Yielded dequeues a `Task` (FunctionCall pushes an upcall frame); YieldedFor returns the specific upcall's arguments without invoking it (kernel.rs:484-750). Kernel time on behalf of the process is charged to its slice (470-474).
- Scheduler trait: `next() -> RunProcess((pid, Option<timeslice_us>)) | TrySleep`, `result(reason, time)`, overridable `execute_kernel_work`, `do_kernel_work_now`, `continue_process` (scheduler.rs:15-97). Implementations live outside the kernel crate in capsules/system/src/scheduler/{round_robin,priority,cooperative,mlfq}.rs.

### B2 System calls
- Classes: Yield=0, Subscribe=1, Command=2, ReadWriteAllow=3, ReadOnlyAllow=4, Memop=5, Exit=6, UserspaceReadableAllow=7 (syscall.rs:85-95); yield variants NoWait, Wait, WaitFor (121-128). TRD104 ABI: four argument and four return registers r0-r3 or a0-a3, class in the `svc` immediate or a4 (doc/reference/trd104-syscalls.md:93-108); return variants encoded in r0 (207-270).
- Upcalls are delivered only on yield by pushing a callback frame whose LR returns after the `svc` (trd104:308-337; kernel.rs:640-660). The queue per process is a `RingBuffer<Task>`; `QueueFull` drops upcalls and bumps a counter (upcall.rs:60-71, 166-204; process_standard.rs:552, 598-615).
- Swap semantics: Subscribe returns the previous upcall (Null Upcall 0x0 the first time) and cancels pending invocations of the old one (trd104:555-620; kernel.rs:886-1000 via `grant::subscribe`). Allow replaces and returns the previous (address, len), 0/0 the first time (trd104:690-720; kernel.rs:1082-1130 `grant::allow_rw`). The kernel must validate that the upcall pointer is in process executable memory (kernel.rs:937-948).
- Buffers: `ReadOnlyProcessBuffer` and `ReadWriteProcessBuffer` carry pointer, length, and `ProcessId`; every `enter` re-checks process liveness and yields a `ReadableProcessSlice` of `Cell`-like bytes, so no long-lived references into process memory exist (processbuffer.rs:5-25, 40-60, 239-262, 335-350). TRD104 4.4.1 warns that buffers can change between capsule invocations (trd104:757-790). `allow_high_water_mark` stops `brk` from shrinking below allowed buffers (process_standard.rs:500, 982-995).
- Platform `SyscallFilter` can reject any non-Yield, non-Exit, non-Memop syscall per process (kernel.rs:778-816; platform/platform.rs:120-135). Memop 0-11: brk and sbrk, memory and flash bounds, grant start, writeable flash regions, stack and heap hints (memop.rs:12-45).

### B3 Grants
- A `Grant<T, Upcalls, AllowROs, AllowRWs>` is created at boot with a unique id; nothing is allocated until a capsule `enter`s it for a `ProcessId`, then `T` plus `GrantKernelData` (saved upcalls, allow slots) are carved from the process's grant region, which grows down from the top of process RAM toward `kernel_memory_break` (grant.rs:5-128; process_standard.rs:446-500). The grant pointer table lives at the top of process memory (process_standard.rs:1830-1850).
- Why: no kernel heap; per-process capsule state is accounted to and freed with the process (terminate nulls all grant pointers, process_standard.rs:821-833). Extra memory via `GrantRegionAllocator` and `enter_with_allocator` (grant.rs:1331-1370).
- Restrictions: access only inside the closure; re-entering an already entered grant panics (the pointer's low bit marks "entered", process_standard.rs:1336-1349; grant.rs:1240-1310 explains the iterate-while-entered bug); `T: Default`, fixed size; `Grant::enter` fails if the process is dead or out of grant memory (`NOMEM`, trd104:717-721).

### B4 Isolation and faults
- `unsafe trait MPU` with `MpuConfig` cached per process; `allocate_region`, `allocate_app_memory_region` (choose a block whose app part can grow up while the kernel part grows down), `update_app_memory_region(app_break, kernel_memory_break)`, `configure_mpu`; `disable_app_mpu` exists because ePMP hides user memory from the kernel (platform/mpu.rs:89-303). Alignment is MPU-specific (Region docs 26-28); the Cortex-M implementation (arch/cortex-m/src/mpu.rs) is not in the clone. Layout: a flash region R-X, a RAM region RW with stack, data, and heap growing up to `app_break`, grants above (process_standard.rs:446-480; create at 1783-1960, `MpuInvalidFlashLength` 1800-1823).
- States `Running`, `Yielded`, `YieldedFor`, `Stopped`, `Faulted`, `Terminated` (process.rs:945-1000). Faults: `set_fault_state` asks the per-process `ProcessFaultPolicy` for `FaultAction::{Panic, Restart, Stop}` (process_policies.rs:18-26; process.rs:1027-1045; process_standard.rs:752-775). Restart means terminate (empty the task queue, null grants, completion code) then `reset`, which mints a new `ProcessId` so stale ids are invalidated, resets debug counters and MPU config, and increments `restart_count` (789-840, 2379-2515). The platform hook `ProcessFault::process_fault_hook` can intercept first (kernel.rs:600-610).
- Loading: processes are TBF objects discovered sequentially in flash (`parse_tbf_header_lengths`, padding entries skipped) and turned into `ProcessBinary` then `ProcessStandard::create` (process_loading.rs:122-140, 295-360, 501-530; process_binary.rs:5-50 lists KernelVersion and fixed-address checks). `SequentialProcessLoaderMachine` runs an asynchronous `ProcessCheckerMachine` over TBF footer credentials: `AppCredentialsPolicy::{require_credentials, check_credentials}` yields Accept, Pass, or Reject, plus `AppIdPolicy` (uniqueness and ShortId compression) (process_checker.rs:5-35, 56-70, 121-208; TRD trd-appid.md). `KERNEL_MAJOR_VERSION` and `KERNEL_MINOR_VERSION` 2.4 are compiled in for compatibility (lib.rs:107-116).

### B5 Time
- `Ticks` (width, wrapping add and sub, `within_range`), `Frequency` (const Hz enums), `Time{Frequency,Ticks; now()}`, `ConvertTicks`, `Counter` and `OverflowClient`, `Alarm` and `AlarmClient` (`set_alarm(reference, dt)` so already-passed times are distinguished from far-future ones, never fires early, `minimum_dt`), `Timer` and `TimerClient` (oneshot and repeating) (hil/time.rs:24-136, 142-160, 236-335, 348-400; TRD105 sections 2-5, doc/reference/trd105-time.md:49-335).
- Virtualization: `MuxAlarm` in capsules::virtual_alarm multiplexes one hardware alarm, picking the next expiry with `min_by_key` on `Ticks: Ord` (trd105:60-72, 361-371); TRD105 section 9 gives the `set_alarm` pseudocode for past and too-near deadlines (386-423). The capsule is not in the clone (capsules/core/src/virtualizers/virtual_alarm.rs).
- Process timeouts: the userspace alarm driver (capsules/core/src/alarm.rs, driver 0x0, doc/syscalls/00000_alarm.md) is a syscall capsule over a virtual alarm; expiry becomes an upcall delivered at the next yield. Timeslices use a separate `SchedulerTimer` (`start`, `arm`, `disarm`, `get_remaining_us`), with the kernel polling expiry rather than taking an interrupt itself (platform/scheduler_timer.rs:12-57).

### B6 Observability and quality
- `debug!`, `debug_verbose!`, `debug_expr!`, `debug_gpio!` write into an internal buffer drained asynchronously via a `DebugWriter` (UART by default) (debug.rs:5-80, 518, 727-800). `CONFIG.trace_syscalls` logs every syscall and upcall (config.rs:40-45; kernel.rs:641-650).
- Panic path: flush the debug buffer, banner, CPU state, disable the app MPU, then `ProcessPrinter::print_overview` for every process, then blink LEDs forever (debug.rs:161-262, 441; process_printer.rs:5-61). `ProcessStandardDebug` keeps the last syscall, syscall count, dropped-upcall count, timeslice expirations, stack minimum pointer (process_standard.rs:124-200). `KernelInfo` introspection is capability-gated (introspection.rs:5-40). The process console capsule (capsules/core/src/process_console.rs, not in the clone) uses these.
- Process: TRDs with RFC 2119 keywords and Draft or Final status (doc/reference/trd1-trds.md); TRD104 and TRD105 are normative. Pull-request process, mandatory Safety comments on every `unsafe`, per-subsystem review guides (doc/CodeReview.md:242-330, 439-500). Threat model documents are referenced from README.md:133 but are not present in this clone.
- Testing: a few `#[cfg(test)]` units in the kernel (ring_buffer, processbuffer, time, leasable_buffer); CI via Makefile `ci-job-*` (clippy, msrv, compilation, kernel and capsule and chip tests, QEMU) (Makefile:206-330, 365-524); hardware CI with Treadmill (doc/TockHardwareCI.md:1-40); board test directories such as boards/*/src/tests/multi_alarm.rs.

### Tock: ideas to borrow
1. Grants: per-process kernel state carved from the process's own RAM, freed on termination, with enter-closure discipline (grant.rs:5-128).
2. Swap semantics for subscribe and allow returning the previous value, so userspace regains ownership deterministically (trd104:555-620, 690-720).
3. `ProcessBuffer` liveness-checked slices and the explicit "buffers can change" rule (processbuffer.rs:20-25; trd104:757-790).
4. Capability traits to gate sensitive kernel APIs at compile time (capabilities.rs).
5. Pluggable scheduler, fault, syscall-filter, and storage policies via `KernelResources` (platform.rs:20-75).
6. TBF loading with credential checking and AppId policy; a kernel version compatibility header (process_checker.rs; process_binary.rs:28-40).
7. The `set_alarm(reference, dt)` API and TRD105 wraparound guidance (hil/time.rs:302-309; trd105:386-423).
8. The TRD specification practice and mandatory Safety comments (trd1-trds.md; CodeReview.md:308-330).

### Tock: weaknesses
1. Single kernel thread: a long capsule callback or deferred call delays everything; kernel work always preempts processes, so process latency depends on capsule behaviour (scheduler.rs:48-61; kernel.rs:384-395).
2. Capsules are trusted for liveness; a buggy capsule can `panic!` the whole system (grant.rs:1246-1248), and `Grant::enter` double entry is only caught at runtime.
3. Upcalls only on yield; queue overflow drops upcalls silently except for a counter (upcall.rs:60-71); re-subscribe can lose events (trd104:572-592).
4. The IPC capsule is acknowledged as unsafe and incomplete (kernel.rs:664-676, issue 1993).
5. Process loading, checking, and dynamic binary storage add considerable kernel code (process_loading.rs 1,379 lines, process_standard.rs 2,753 lines); no footprint figures in the docs.
6. The MPU abstraction pushes alignment rules into chip crates; app memory must be a contiguous grow-both-ways block (platform/mpu.rs:201-248).
7. Timeslice accounting polls `get_remaining_us` and charges kernel time to the process; no priority inheritance or deadline support in the base kernel (kernel.rs:520-530, 470-474).
8. Rust-only kernel and capsules; threat-model and design docs now live outside the repo (doc/README.md:4).
