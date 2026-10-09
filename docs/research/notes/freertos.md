# Research note: FreeRTOS kernel

**Kind:** Read-only source analysis, 2026-10-07, on a shallow clone of FreeRTOS/FreeRTOS-Kernel at commit `8be86d4a24fd` (kernel self-identifies as V11.1.0+, include/task.h:57; single tree with SMP). Paths relative to the repository root. Line numbers drift; file paths and symbol names are the durable reference. Synthesized in `R-001`.

---

## Q1 Scheduler
- Ready structure: `pxReadyTasksLists[configMAX_PRIORITIES]`, one doubly linked list per priority, 0 is lowest (tasks.c:478, 390). Generic selection: `uxTopReadyPriority` is a hint; selection walks down to the first non-empty list (tasks.c:197-212). Port-optimised: `uxTopReadyPriority` becomes a bitmap and `portGET_HIGHEST_PRIORITY` is `31 - clz(bitmap)` (portable/GCC/ARM_CM4F/portmacro.h:166-171; tasks.c:238-258); capped at 32 priorities (portmacro.h:161-162).
- Equal priority: round-robin via the list cursor `pxIndex` in `listGET_OWNER_OF_NEXT_ENTRY` (include/list.h:176, tasks.c:210); new ready tasks are appended at the end (tasks.c:286-293); a tick forces a switch only if `configUSE_TIME_SLICING` and more than one task is at the running priority (tasks.c:4955-4968).
- `taskYIELD()` is `portYIELD()`: pend PendSV plus dsb and isb (portmacro.h:88-96); selection happens in `vTaskSwitchContext` called from the PendSV handler (port.c:504-556).
- Scheduler lock: `uxSchedulerSuspended` is incremented without a critical section, justified by a long comment (tasks.c:3890-3934). While suspended, `vTaskSwitchContext` only sets `xYieldPendings[0]` (tasks.c:5219-5223), ticks accumulate in `xPendedTicks`, and tasks woken by ISRs are parked on `xPendingReadyList` via their event list item (tasks.c:5540-5545). `xTaskResumeAll` drains that list, replays pended ticks, and yields if needed (tasks.c:4064-4190).
- SMP: `pxCurrentTCBs[]`, `xYieldPendings[]`, `xIdleTaskHandles[]` (tasks.c:470, 510, 514); per-TCB `xTaskRunState`, `uxCoreAffinityMask` (tasks.c:386-396). `prvSelectHighestPriorityTask` scans lists for a task not running elsewhere whose affinity mask includes this core (tasks.c:994-1130, 1078); `prvYieldCore` sends an IPI via `portYIELD_CORE` (tasks.c:352-368); `configRUN_MULTIPLE_PRIORITIES==0` prevents lower-priority tasks running concurrently (tasks.c:1031-1040). Two spinlocks (task lock, ISR lock) are taken in `vTaskSwitchContext` (tasks.c:5312-5313); the critical nesting count moves into the TCB (tasks.c:340-342).
- Idle: one `prvIdleTask` plus N-1 `prvPassiveIdleTask` (tasks.c:3624-3637); idle frees self-deleted tasks (`prvCheckTasksWaitingTermination`, tasks.c:5934), runs hooks, enters tickless sleep (tasks.c:5985-6030).
- Unusual: per-task preemption disable (tasks.c:3141-3192), `xTaskAbortDelay` (4737), `xTaskCatchUpTicks` (4707), `uxTopUsedPriority` for OpenOCD (517-519).

## Q2 Context switch and interrupts
- `portYIELD_FROM_ISR(x)` is `portEND_SWITCHING_ISR(x)`: pend PendSV only if `x != pdFALSE` (portmacro.h:101-114). PendSV runs at the lowest priority (port.c:433-434), saves r4-r11 and r14 and lazily s16-s31 (EXC_RETURN bit 4), raises BASEPRI, calls `vTaskSwitchContext`, restores (port.c:504-556). SysTick pends PendSV itself when `xTaskIncrementTick` returns true (port.c:560-583).
- Critical sections: task-level `vPortEnterCritical` raises BASEPRI to `configMAX_SYSCALL_INTERRUPT_PRIORITY` and bumps a static `uxCriticalNesting` (port.c:161, 475-500), asserting not-in-ISR at nesting 1 (port.c:487). ISR-level `taskENTER_CRITICAL_FROM_ISR` is `ulPortRaiseBASEPRI()`, which returns the saved BASEPRI to restore, with no counter (portmacro.h:120-121, 229-256; task.h:241-264).
- Priority split: interrupts logically above `configMAX_SYSCALL_INTERRUPT_PRIORITY` are never masked and may not call any API (queue.c:1181-1195). Nesting is native NVIC; the kernel only saves and restores BASEPRI.
- FromISR duplication: every blocking API has a non-blocking twin with `pxHigherPriorityTaskWoken` (queue.c:1167-1170). Safety net: `xTaskRemoveFromEventList` also sets `xYieldPendings[0]` "in case the user is not using xHigherPriorityTaskWoken" (tasks.c:5552-5560), so a missed yield is picked up at the next tick.
- Misuse detection: `portASSERT_IF_INTERRUPT_PRIORITY_INVALID` (20 call sites in core files) reads IPSR, looks up the NVIC IPR byte, asserts `>= ucMaxSysCallPriority` and asserts the priority grouping (port.c:849-905); limits are probed at start by writing 0xFF to an IPR (port.c:347-429). `configASSERT` defaults to nothing (FreeRTOS.h:97-101). Blocking with the scheduler suspended is only asserted (queue.c:1680-1684).

## Q3 Time
- `xTickCount` is incremented in `xTaskIncrementTick` (tasks.c:4831-4860); `TickType_t` is 16, 32, or 64 bits via `configTICK_TYPE_WIDTH_IN_BITS` (FreeRTOS.h:52-75; portmacro.h:63-74; `portTICK_TYPE_IS_ATOMIC` only for 32-bit).
- Delayed tasks sit in `pxDelayedTaskList` sorted by absolute wake tick, or in `pxOverflowDelayedTaskList` if `xTimeToWake < now` (tasks.c:8730-8760). When the tick wraps to 0, `taskSWITCH_DELAYED_LISTS` swaps the pointers, bumps `xNumOfOverflows`, and asserts the old list is empty (tasks.c:267-280, 4850-4854). `xNextTaskUnblockTime` caches the head wake time so the tick is O(1) when nothing expires (tasks.c:4863-4870, 6621-6640). Timeouts store (overflow count, entry tick) (tasks.c:5665-5690).
- Tickless: idle samples the expected idle time, suspends the scheduler, calls `portSUPPRESS_TICKS_AND_SLEEP` (tasks.c:5985-6030). CM4F reprograms SysTick, checks `eTaskConfirmSleepModeStatus`, WFI, then `vTaskStepTick` (port.c:589-805; 24-bit limit `xMaximumPossibleSuppressedTicks`, port.c:818). `vTaskStepTick` asserts it never jumps past the unblock time and converts an exact hit into a pended tick (tasks.c:4665-4700).
- Software timers: a daemon task plus `xTimerQueue` of commands (timers.c:143-149, 237-328); API calls only enqueue (timers.c:463-482), so start and stop are asynchronous. Daemon loop: next expiry, block via `vQueueWaitForMessageRestricted`, process commands (timers.c:750-840). Callbacks run in daemon context (timers.c:742, 1021). Two active lists switched on wrap, detected by `prvSampleTimeNow` comparing against a static (timers.c:869-890, 1091-1113). Auto-reload is drift-free: next expiry is previous expiry plus period, and `prvReloadTimer` fires the callback once per missed period to catch up (timers.c:697-713). `xTimerPendFunctionCall` reuses the queue for deferred ISR work (timers.c:945-958).

## Q4 Blocking and waiting
- `Queue_t` has `xTasksWaitingToSend` and `xTasksWaitingToReceive` (queue.c:114-115). A blocked task's `xEventListItem` is inserted sorted by `configMAX_PRIORITIES - priority` (tasks.c:1929; `vTaskPlaceOnEventList` tasks.c:5402-5428), FIFO among equals (list.c:160-165), while `xStateListItem` goes to a delayed list.
- Send protocol: critical-section fast path; otherwise `vTaskSuspendAll` plus `prvLockQueue`, `xTaskCheckForTimeOut`, re-check full, place on the event list, `prvUnlockQueue`, `xTaskResumeAll`, yield if not already yielded (queue.c:1113-1140). Queue lock equals `cTxLock` and `cRxLock` counters: ISRs posting to a locked queue only increment (queue.c:51-53, 277-299); `prvUnlockQueue` replays the wake-ups with `vTaskMissedYield` (queue.c:2499-2600).
- Timeout and wake race: both paths loop back to the top; `xTaskCheckForTimeOut` subtracts elapsed ticks and detects wrap via the overflow count (tasks.c:5693-5757); a tick timeout also removes the task from its event list (tasks.c:4885-4893).
- `vQueueDelete` only asserts the waiter lists are empty (queue.c:2263-2264); no wake-up.
- Queue sets: the set is a queue of member handles; posting to a member copies its handle into the container (`prvNotifyQueueSetContainer`, queue.c:3344-3398); `xQueueSelectFromSet` is `xQueueReceive` on the container (queue.c:3307-3320).
- Notifications: per-task `ulNotifiedValue[]` and `ucNotifyState[]` (tasks.c:445-448), states 0, 1, 2 (tasks.c:113-115). Take marks WAITING in a critical section and goes straight to the delayed list with no event list (tasks.c:7797-7890); notify updates the value and moves the task to ready directly (tasks.c:8013-8125). Cheapest because there is no object, no sorted insert, and a single receiver.

## Q5 Synchronization and priority inheritance
- A mutex is a queue with item size 0 and length 1; `pcHead == NULL` tags it; `xMutexHolder` and `uxRecursiveCallCount` live in a union (queue.c:56-82, 617-640).
- Inheritance happens in `xQueueSemaphoreTake` just before blocking (queue.c:1767-1775). `xTaskPriorityInherit` raises only `pxMutexHolderTCB` (tasks.c:6745-6836, 6805): not transitive; if the holder is itself blocked on another mutex, that holder is untouched.
- Disinherit on give only when `uxMutexesHeld == 0` (tasks.c:6848-6930, 6870). On timeout, `vTaskPriorityDisinheritAfterTimeout` lowers to max(base, highest remaining waiter) but only if exactly one mutex is held, labelled a "simplification" (tasks.c:6942-7055, 6977).
- Recursive mutex: owner check by handle plus count (queue.c:818-860, 759-800).
- Owner death: `vTaskDelete` never touches `uxMutexesHeld` or any mutex (only references are tasks.c:6862-7784); a deleted holder leaves the mutex locked forever. ISR gives have a NULL holder, so no inheritance (tasks.c:6752).
- Event groups: wait bits and control flags are packed into the list item value (event_groups.c:231, 394; `eventEVENT_BITS_CONTROL_BYTES` event_groups.h:46-50), so only 24 usable bits with 32-bit ticks. Set-bits walks all waiters under `vTaskSuspendAll` (event_groups.c:567-642); from an ISR it is deferred to the timer daemon (event_groups.c:826).
- No condition variable, no reader-writer lock; mutexes are unusable from ISRs.

## Q6 IPC and memory
- Queues copy by value with memcpy (queue.c:2425-2439, 2494); zero-size items for semaphores (queue.c:82).
- Stream and message buffers: a single writer and reader are assumed and asserted (`xTaskWaitingToSend == NULL`, stream_buffer.c:894, 1119). The data copy runs with no lock; volatile `xHead` and `xTail` are published after the copy (stream_buffer.c:234-235, 270-277); the critical section only registers the waiting task (884-906, 1102-1133); wake via notification; trigger level (237, 931); length prefix type (222); batching flag (227).
- heap_1 no free (heap_1.c:30-32); heap_2 size-ordered free list, no coalescing (heap_2.c:135-145); heap_3 wraps malloc and free under `vTaskSuspendAll` (heap_3.c:63-89); heap_4 address-ordered first-fit with two-way coalescing (heap_4.c:248-252, 504-540), allocated flag in the size MSB (80-84), optional XOR canary on links (106-134); heap_5 is heap_4 across regions (heap_5.c:173). All lock with `vTaskSuspendAll` (heap_4.c:221), so none is ISR-callable.
- Static allocation: `StaticTask_t`, `StaticQueue_t`, and the rest are dummy structs mirroring every member under the same `#if`s (FreeRTOS.h:3176-3234, 3250-3276); creation asserts `sizeof(StaticTask_t) == sizeof(TCB_t)` via a volatile (tasks.c:1284-1285, queue.c:403-406). Documented as a MISRA 11.3 deviation (MISRA.md:65-82).

## Q7 Isolation
- Privilege is chosen by `portPRIVILEGE_BIT` in the priority (timers.c:264), stored in `xMPUSettings.ulTaskFlags` (portmacrocommon.h:364-376). `xMPU_SETTINGS` must be the second TCB member for the assembly (tasks.c:380-382); on MPU ports the register context is saved in TCB `ulContext[]`, not the task stack (portmacrocommon.h:375; stack_macros.h:56-60).
- Regions (CM33): 0-3 privileged flash, unprivileged flash, syscall flash, privileged RAM; 4 task stack; 5 to 7 per-task `xRegions` (portmacrocommon.h:173-181; port.c:928-1010, 1916-2010; `vTaskAllocateMPURegions` tasks.c:6155). Linker symbols `__privileged_functions_start__` and similar are required.
- Syscalls (v2): wrappers SVC from a dedicated syscall flash region; `vSystemCallEnter` checks the caller PC is in that region, that no syscall is in progress, and that the number is implemented, copies the exception frame onto a per-task privileged syscall stack, rewrites PC to `uxSystemCallImplementations[n]` and LR to `vRequestSystemCallExit`, switches PSP and PSPLIM (port.c:1263-1404; stack size portmacrocommon.h:228-245); exit restores (port.c:1419-1510). v1 raise-privilege is still selectable (port.c:1235-1246).
- Handles: `xKernelObjectPool[configPROTECTED_KERNEL_OBJECT_POOL_SIZE]` of {internal handle, type, data} (mpu_wrappers_v2.c:75-90, 253); user code receives an index. Each wrapper validates the index, checks the per-task ACL bitmap `ulAccessControlList` (portmacrocommon.h:381; port.c:2279-2350), looks up the typed handle, validates user buffers against the task's MPU regions with `xPortIsAuthorizedToAccessBuffer` (port.c:2053-2100), then calls the kernel (`MPU_xQueueGenericSendImpl`, mpu_wrappers_v2.c:2175-2230). `vGrantAccessToKernelObject` at 346-378; timer callbacks trampolined by `MPU_TimerCallback` (418-445).
- Not protected: privileged tasks have full access; single address space; task create, delete, and priority-set are privileged-only after start (mpu_wrappers_v2.c:685, 1547-1700); stack-overflow checking is disabled for MPU ports (stack_macros.h:67), relying on the stack region and PSPLIM (port.c:1612, 1699); no kernel fault handler; `MPU_GetIndexForHandle` is an O(N) scan (297-315).

## Q8 Observability
- 504 `trace*` defaults in FreeRTOS.h, 408 of them `traceENTER_` and `traceRETURN_` pairs on every API (FreeRTOS.h:1061 onward), plus semantic hooks: switched in and out (FreeRTOS.h:631; tasks.c:5228, 5265), moved-to-ready (286), ISR enter and exit (port.c:564-575), low-power begin and end (tasks.c:6017-6019). `uxTCBNumber` and `uxTaskNumber` for trace tools (tasks.c:413-414).
- Run-time stats: `ulRunTimeCounter` accumulated at switch-out with a port counter, explicitly no overflow protection (tasks.c:5231-5252; FreeRTOS.h:2703-2707). `uxTaskGetSystemState` walks all state lists under `vTaskSuspendAll` (tasks.c:4589-4660).
- Stacks filled with 0xa5 (tasks.c:121, 1835); the high-water mark scans for the fill (6470-6480); overflow method 1 (pointer) and 2 (guard bytes) at every switch (stack_macros.h:67-137) call `vApplicationStackOverflowHook`.
- No crash handling beyond `configASSERT` and `prvTaskExitError` for returning task functions (port.c:234-258); optional list integrity bytes (list.h:108-137). Debugger aids: `pxCurrentTCB` kept via `portDONT_DISCARD`, legacy `tskTCB` and `QueueDefinition` names (tasks.c:375, queue.c:103), `uxTopUsedPriority` (tasks.c:517-519), the queue registry (queue.c:148-173).

## Q9 Configuration, portability, quality
- `FreeRTOSConfig.h` is included by FreeRTOS.h, which supplies `#ifndef` defaults and `#error`s for mandatory items and SMP requirements (FreeRTOS.h:58-211, 420-580); template in examples/template_configuration; CMake wants a `freertos_config` INTERFACE target plus FREERTOS_PORT and FREERTOS_HEAP (CMakeLists.txt:4-43).
- Port contract: portmacro.h types, `portSTACK_GROWTH`, `portBYTE_ALIGNMENT`, `portYIELD`, critical and mask macros, optional CLZ macros and `portSUPPRESS_TICKS_AND_SLEEP` (portmacro.h:63-190); port.c `pxPortInitialiseStack`, `xPortStartScheduler`, `vPortEndScheduler`, MPU hooks (portable.h:126-280); the heap is also port layer (portable.h:178-210).
- Compiler macros: `portFORCE_INLINE`, `portNOP`, `portMEMORY_BARRIER`, `portDONT_DISCARD`, `PRIVILEGED_FUNCTION` and `PRIVILEGED_DATA`, `configLIST_VOLATILE`, `portTASK_FUNCTION` (portmacro.h:132-258; FreeRTOS.h:2767).
- MISRA C:2012 compliance claimed, checked with Coverity 2023.6.1; deviations in MISRA.md, inline `coverity[...]` and "MISRA Ref" comments (35 in tasks.c), project-wide deviations in examples/coverity/coverity_misra.config.
- No tests in this repository: CI checks out FreeRTOS/FreeRTOS and runs its Test/CMock suite with sanitizers and lcov (.github/workflows/unit-tests.yml); also coverity_scan.yml, kernel-checks.yml, uncrustify.cfg, cSpell. `mtCOVERAGE_TEST_MARKER()` on every else-branch, `LCOV_EXCL_BR_LINE` (queue.c:3353), a `STATIC` macro to expose statics to tests (tasks.c:151-153). No CBMC directory here.

## Ideas to borrow
1. Bitmap plus CLZ priority selection behind a port macro (portmacro.h:166-171).
2. The `xNextTaskUnblockTime` cache making the tick O(1) (tasks.c:4863).
3. Pending-ready list plus pended ticks so ISRs progress under a scheduler lock (tasks.c:5540, 4140).
4. Queue lock counters decoupling ISR posts from waiter-list edits (queue.c:277-299, 2499).
5. Direct-to-task notification as the base primitive; stream buffers built on it (tasks.c:7797; stream_buffer.c:146).
6. Timeout record as (overflow count, entry tick), wrap-safe (tasks.c:5665).
7. Yield-pending safety net for ISR callers that ignore the woken flag (tasks.c:5558).
8. Dummy static types with a sizeof assert (FreeRTOS.h:3176; tasks.c:1284).
9. Boot-time NVIC probing and per-call ISR priority assertion (port.c:347-429, 849-905).
10. Absolute-time auto-reload timers with catch-up (timers.c:697-713).
11. The MPU v2 design: index handles, ACL bitmap, buffer permission checks, per-task syscall stack (mpu_wrappers_v2.c:2175; port.c:1263).
12. Heap link XOR canary (heap_4.c:106-134).

## Weaknesses
1. Priority inheritance is non-transitive, disinherits only on the last mutex, and timeout disinherit works only with one mutex held (tasks.c:6805, 6870, 6977).
2. No owner-death handling: `vTaskDelete` ignores held mutexes.
3. API duplication: every primitive has a FromISR twin with an out-parameter; the fallback yield is tick-delayed (tasks.c:5558); 408 ENTER and RETURN macros.
4. Tick-wrap list swap duplicated in tasks and timers (tasks.c:267-280; timers.c:143-146, 876), with an assert as the only guard (tasks.c:272).
5. Five separate wait mechanisms (sorted event lists, notifications, unordered event lists with packed bits, stream-buffer notifications, the timer command queue); no condition variable; the mutex is not ISR-usable; queue sets are queues of handles.
6. Isolation retrofitted: single address space, privileged tasks unconstrained, fixed TCB layout for assembly, O(N) handle scan per syscall, stack checks off on MPU ports (stack_macros.h:56-67), a 5,323-line hand-written wrapper file plus 2,055-line assembly stubs.
7. `vTaskSuspendAll` increments without a critical section, correctness argued by comment (tasks.c:3897-3920); blocking under suspension only asserted (queue.c:1680).
8. The run-time stats counter has no overflow protection (tasks.c:5235-5240).
9. Event groups limited to 24 bits (event_groups.h:46-50).
10. Sorted-list insertion is O(n) for delayed and event lists (list.c:160-165); the CLZ path is capped at 32 priorities.
11. The timer API is asynchronous (start returns before the timer is armed); callbacks share one daemon stack; ISR set-bits latency goes through the daemon (event_groups.c:826).
12. `vQueueDelete` with waiters is only an assert (queue.c:2263-2264).
13. Heaps use `vTaskSuspendAll`, so no allocation from ISRs or critical sections (heap_4.c:221).
14. Self-deleted task memory is reclaimed only when idle runs (tasks.c:5934).
