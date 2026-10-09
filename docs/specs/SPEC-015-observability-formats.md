# SPEC-015 - Observability Formats: Log Records, Trace, Crash Record, Debug Descriptor, Monitors

**Status:** Accepted 2026-10-07 by the project owner ("do all"), with the defaults of §12. Specification work item 14 of the roadmap (07 §3).
**Requirements:** `docs/requirements/OBS.md` (OBS-001 to 012 restated; new from 013).
**Builds on:** 04 §7 (logging, tracing, flight recorder, descriptor, statistics), 03 §10.2 (crash record); ADR-009, ADR-011, ADR-021, ADR-034; SPEC-002 §10 (latency metrics), SPEC-010 §13 (partition events), SPEC-011 §14 (port contribution), SPEC-013 §8 (the bridge consumes the trace), SPEC-014 §6 (generated trace metadata); 09 §7 (`NOLOAD` sections and symbol-address identifiers), §9 (`embdbg`, `embread`); the trace events named in every earlier specification.
**Research:** R-001 §8: ThreadX's inlined trace with an object registry, Zephyr's dictionary logging with strings linked into a `DEVNULL` region, NuttX's critmonitor with caller addresses, ChibiOS's and uC/OS-III's debugger exports, Hubris's debugger-centric statics. This specification combines them into one compile-out set with open formats (CTF) and a versioned descriptor, which no kernel in the comparison offers together.

---

## 1. Scope and terms

| Term | Meaning |
|---|---|
| **Log record** | One deferred-format log call: an identifier plus raw arguments, decoded on the host from the ELF |
| **Trace event** | One fixed-layout kernel event in a CTF stream |
| **Crash record** | The fixed-layout record written to retained memory on a fatal fault |
| **Debug descriptor** | The versioned, read-only structure every image exports for RTOS-aware tools |
| **Monitor** | A worst-case recorder with the program counter of the opener (ADR-034) |
| **Flight recorder** | The tail of the trace ring kept in retained memory and copied into the crash record |
| **Transport** | How bytes leave the target: UART, SWO/ITM, memory channel read by a probe, USB, retained ring, network |

## 2. Logging

### 2.1 Call sites

```c
EMB_LOG_ERR(module, "sensor %u failed: %d", id, status);
EMB_LOG_WRN(module, ...);  EMB_LOG_INF(module, ...);  EMB_LOG_DBG(module, ...);
EMB_LOG_HEXDUMP_DBG(module, buf, len, "rx frame");
EMB_LOG_MODULE_DECLARE(name, CONFIG_EMB_LOG_LEVEL_<NAME>);   /* once per module; compile-time level */
```

Format strings use a printf subset (`%d %i %u %x %X %c %s %p %lld %llu %f` and width and precision flags); `%s` arguments are copied by length (bounded by `CONFIG_EMB_LOG_STR_MAX`, default 32) because a pointer is meaningless on the host. A call below the module's compile-time level compiles to nothing; a call below the runtime level (`emb_log_set_level(module, level)`) returns after one comparison (OBS-002).

### 2.2 Interning (ADR-009)

Each call site defines a `static const` metadata object (format string, level, module id, file, line, argument type signature) in the section `.emb_log` placed in a `NOLOAD` output section in a dummy memory region (`LOG_STRINGS`) by the generated linker script; the object's **address is the identifier** (09 §7, as `defmt` does), so nothing is in flash and no identifier table is needed. On a target without a linker script (AVR under EmbCC) the plain-text backend is used and metadata objects are not emitted (SPEC-012 §10).

### 2.3 Record format

```
record := id timestamp args
id        : u16 when LOG_STRINGS fits in 64 KB, else u32 (chosen at link time, stated in the ELF note)
timestamp : u32 trace clock ticks (SPEC-015 §3.4), or absent when CONFIG_EMB_LOG_TIMESTAMP=n
args      : raw little-endian values in the order and widths given by the metadata's type signature;
            %s as u8 length + bytes; hexdump as u16 length + bytes
```

Records are produced into a per-CPU lock-free ring (ISR-safe, OBS-003) with a reservation of the exact record size; when the ring is full the record is dropped and a per-ring drop counter increments; the next successful record is preceded by a `dropped(u16 count)` record (id 1). Id 0 is reserved for a stream-synchronization marker emitted every `CONFIG_EMB_LOG_SYNC_INTERVAL` records so that a decoder joining mid-stream resynchronizes.

### 2.4 Processing and transport

A log worker (a system work queue item by default, or a dedicated thread at a configured priority) drains the rings into frames (§7) and hands them to the configured transport; in `IMMEDIATE` mode (debugging early boot) the call site writes the frame itself. The plain-text backend formats on target with a minimal `vsnprintf` and the same call sites, for the tiny profile and for a host without the ELF.

### 2.5 Host decoding

`tools/logdec` reads the ELF (the `LOG_STRINGS` region's symbols and their metadata) and a byte stream, and prints text with timestamps, module, level, file and line; `embdbg` embeds the same decoder; the decoder refuses a stream whose ELF build id does not match the image's and says so.

## 3. Tracing (ADR-021)

### 3.1 Event set

Every specification named its events; the catalogue lives in `hw/trace_events.yaml` and the generator emits the enum, the inline emitters, and the CTF metadata (SPEC-014 §6). Classes and their events:

| Class | Events |
|---|---|
| `sched` | `switch(from, to, reason)`, `ready(thread, cause)`, `yield`, `sched_lock`, `sched_unlock(duration)`, `idle_enter`, `idle_exit` |
| `thread` | `init`, `start`, `exit(code)`, `join`, `suspend`, `resume`, `cancel(delivered)`, `priority(thread, base, eff, cause)`, `destroy` |
| `wait` | `wait_begin(thread, object, reason, deadline)`, `wait_end(thread, result, blocked_cycles)`, `wake(source, waker, thread, result)`, `requeue` |
| `sync` | `mutex_lock(contended)`, `mutex_unlock(handed_to)`, `prio_inherit(thread, from, to, cause)`, `pi_depth_exceeded`, `deadlock_detected`, `sem_give(handed)`, `event_set(bits, woken)`, `cond_signal(woken)` |
| `ipc` | `msgq_send`, `msgq_receive`, `msgq_full_block`, `pipe_write`, `pipe_read`, `pipe_close`, `pool_alloc`, `pool_free`, `pool_transfer` |
| `notify` | `notify_set(target, bits, woke)`, `notify_wait(mask, mode, result)`, `bind_signal(object, thread, bit)` |
| `work` | `work_submit(queue, item, result)`, `work_start(latency)`, `work_end(duration)`, `work_cancel(state)`, `dwork_schedule(deadline)` |
| `time` | `tick`, `timeout_arm`, `timeout_fire`, `timer_expire`, `timer_overrun(count)`, `tickless_sleep(until)`, `tickless_wake(elapsed)` |
| `irq` | `irq_enter(n)`, `irq_exit(n, switched)`, `irq_lock_max(cycles, pc)` (monitor threshold) |
| `tp` | `budget_consumed`, `budget_overrun(policy)`, `replenish`, `share_filter(partition, eligible)`, `deadline_miss` |
| `obj` | `obj_init(type, handle, name)`, `obj_destroy(type, handle, flushed)`, `cap_grant`, `cap_derive`, `cap_delete`, `cap_revoke`, `system_freeze` |
| `part` | `partition_fault(p, thread, class, code, pc)`, `partition_restart(p, count, duration)`, `partition_suspend`, `partition_resume`, `cap_denied(p, nr, reason)`, `irq_delivered(p, irq)`, `syscall(p, nr, status)` (optional) |
| `power` | `state_enter(state)`, `state_exit(state, residency)`, `device_suspend`, `device_resume` |
| `user` | `user_event(id, a, b)`: an application hook with two words |

Event payloads are fixed-size little-endian fields; handles appear as their object index and generation (POINTER model: the pointer), threads as their index, so the decoder maps them to names through the object registry events (`obj_init`) and the descriptor.

### 3.2 Emission and storage

`EMB_TRACE_<event>(...)` expands to nothing when the class is compiled out (`CONFIG_EMB_TRACE_CLASSES`) and otherwise to a runtime class-mask test and a reservation in the per-CPU trace ring; the emitter runs inside the kernel's critical section where the event is generated and costs a timestamp read plus the field stores. Rings are CTF streams: each ring is divided into packets (default 1 KB) with a CTF packet header (stream id, timestamp begin and end, `events_discarded` count, content size); overflow drops the oldest packet and increments `events_discarded` (OBS-005 bounded store).

### 3.3 Modes

- **Snapshot**: the ring is retained in RAM, dumped on demand (`emb trace dump` through the descriptor's ring location) or on fault (the flight recorder, §4).
- **Streaming**: the trace worker ships completed packets over the transport; back-pressure drops packets, never blocks the kernel.

### 3.4 Clock

Timestamps are `emb_arch_trace_timestamp()` (SPEC-011 §14) as a 32-bit value in the event header with the full 64-bit value in each packet header; the CTF metadata declares the clock class with the frequency from the hardware description (SPEC-014) so tools convert to time. On the tiny profile the clock is the kernel tick.

### 3.5 Tools

Babeltrace 2 and Trace Compass read the streams and the generated metadata directly; `tools/tracedec` converts to Perfetto's JSON for its viewer and to the bridge's newline-delimited JSON (SPEC-013 §8); `embdbg` reads the same streams. Nothing proprietary is needed to be useful (ADR-021).

## 4. Crash record (03 §10.2)

```
struct emb_crash_record_v1 {
  u32 magic ("EMBC"), u16 version (1), u16 length
  u8  build_id[20], u8 fw_version[8]
  u64 uptime_ticks, u32 reset_reason
  u8  fault_class, u8 fault_code, u8 cpu_id, u8 partition_id
  u16 thread_index, u16 thread_generation
  u32 fault_address, u32 pc, u32 lr_or_ra, u32 sp, u32 stack_base, u32 stack_size
  u8  status_reg_count, u8 gpr_count, u16 flight_event_count
  u32 status_regs[status_reg_count]        /* architecture status registers, layout from the descriptor */
  u32 gprs[gpr_count]
  u8  flight_recorder[...]                 /* the last trace packets, raw CTF */
  u32 crc32
}
```

- Written by `embk_fault_dispatch` before any recovery action (FLT-001) into `.retained` (a `NOLOAD` section in a region with the `retained` attribute) or, failing that, `.noinit` SRAM; the small variant for AVR (SPEC-012 §8) is `version 2` with 16-bit fields and no registers.
- On the next boot the kernel validates magic and CRC and exposes `emb_crash_record_get()` and `emb_crash_record_clear()`; an application uploads it for telemetry; `tools/crashdec` symbolizes `pc` and the flight recorder with the ELF.
- Contained partition faults (SPEC-010 §13) write a 32-byte mini-record (`"EMBP"`, partition, thread, class, code, pc, uptime) into a retained ring of `CONFIG_EMB_FAULT_RECORD_RING` entries when enabled.
- The crash record format is versioned and append-only; decoders accept every version they know.

## 5. Debug descriptor (ADR-011)

A `const struct emb_debug_descriptor` at the exported symbol `emb_debug_descriptor` (kept with `used`), found by tools through the symbol or by scanning for the magic `"EMBD"`:

| Section | Contents |
|---|---|
| header | magic, version, length, kernel version, configuration hash, profile, pointer size, endianness, port name and variant |
| types | object type tags with names and header offsets (`type_tag`, `lifecycle`, `generation`, `name`, `registry`) |
| thread | TCB field offsets: `arch.sp`, state, wait state, wait reason, wait queue, wake result, base and effective priority, name, index, generation, partition, owned list head, timeout node, notification bits, statistics block |
| scheduler | ready structure kind and location, per-CPU state location and field offsets (`current_thread`, `irq_nesting_depth`, `sched_lock_depth`, `reschedule_pending`), priority count, all-threads list head |
| objects | registry heads per type (`CONFIG_EMB_OBJ_LIST`), handle model, registry or capability table layouts |
| partitions | partition table location and layout, kstore layouts, capability table layout |
| rings | trace ring locations, packet size, class mask location; log ring locations; crash record location and version; fault mini-record ring |
| monitors | monitor block location and layout (§6) |
| arch | context frame layout (register offsets from the saved stack pointer), fault register layout, interrupt stack bounds (SPEC-011 §14) |

Tools (`embdbg`, an OpenOCD RTOS plugin, pyOCD, probe-rs) implement one generic plugin that reads the descriptor and then enumerates threads, their states and stacks, owned locks, partitions, and dumps rings, independent of the kernel version (OBS-008). The descriptor is also what the HIL runner uses to pull traces and crash records.

## 6. Monitors and statistics (ADR-034, 04 §7.5)

```c
emb_status_t emb_stats_cpu_get(uint8_t cpu, emb_stats_cpu_t *out);          /* utilization, idle residency, irq counts, max critical section */
emb_status_t emb_stats_thread_get(emb_thread_t t, emb_stats_thread_t *out);  /* cpu time, switches, blocked time, stack high-water, deadline misses, budget overruns, max boost */
emb_status_t emb_stats_object_get(emb_handle_t h, emb_stats_object_t *out);  /* contention count, max queue length */
emb_status_t emb_monitor_get(uint8_t category, emb_monitor_t *out);          /* worst cycles, opener pc, count over threshold; categories: IRQ_LOCK, SCHED_LOCK, ISR(vector), THREAD_RUN, WORK_HANDLER */
emb_status_t emb_monitor_set_threshold(uint8_t category, uint32_t cycles, uint8_t action);   /* TRACE | NOTIFY | FAULT */
void         emb_stats_reset(void);
```

Monitors record the interval length and the program counter of the code that opened it (the return address of `emb_irq_lock`, `emb_sched_lock`, the ISR vector, the thread entry, the work handler) in one compare and two stores at the closing point; the threshold action runs at the closing point too (a trace event, `K_BUDGET`-style notification to the supervisor, or a kernel fault). All statistics compile out with their options (OBS-005 discipline) and are exported through the descriptor.

## 7. Transports and framing

- Byte-stream transports (UART, USB CDC, network) carry frames: `COBS( channel u8, payload, crc16 )` with a zero delimiter; channels 0 log, 1 trace, 2 crash, 3 shell, 4 to 15 application. Decoders resynchronize on the delimiter.
- SWO/ITM carries log on stimulus port 0 and trace on port 1 without framing (the hardware frames).
- The **memory channel** (`CONFIG_EMB_MEMCHAN`) is a control block at the exported symbol `emb_memchan` describing up-and-down ring buffers in RAM that a debug probe reads and writes without stopping the target; `CONFIG_EMB_MEMCHAN_RTT_COMPAT` lays the control block out as SEGGER's RTT does so that existing probe tooling reads it unchanged, while the native layout stays the documented one.
- The retained ring transport keeps log frames in retained memory for upload after a reset.

## 8. Latency metrics (SPEC-002 §10)

The metrics SPEC-002 defines (interrupt latency, critical-section maximum, ISR-to-thread, switch cost) are produced by the monitors of §6 and by the harness's own instrumentation (R-003 §6); their results carry the metadata record of 05 §4.4 and are published per release.

## 9. Profiles

| Profile | Log | Trace | Crash record | Descriptor | Monitors |
|---|---|---|---|---|---|
| tiny | plain text over UART; deferred only with a linker script | off (optional `sched` class, tick timestamps) | small v2 in `.noinit` | present, reduced | off |
| base | deferred, memory channel or UART | snapshot ring, `sched`, `sync`, `irq` by default | v1 in retained or `.noinit` | full | optional |
| isolated, multicore | deferred; per-partition module ids | streaming or snapshot, all classes | v1 plus partition mini-records | full | on in checked builds |
| native | deferred to a host pipe, decoded live | streams to the bridge | written to a host file | full | on |

## 10. Host tools

`tools/logdec`, `tools/tracedec`, `tools/crashdec`, and `tools/descriptor` (a Python library reading the descriptor through a probe or a memory dump) ship with the kernel and are what `embdbg`, the HIL runner, and the benchmark harness use. They refuse mismatched build ids and accept every format version they know.

## 11. Worked example: a field crash

A product in the field hard-faults in a driver. The fault entry writes the v1 record with the last 1 KB of trace into the retained region and reboots. On boot the application reads the record, uploads it. `crashdec` symbolizes `pc` to the faulting line, decodes the status registers for the architecture, and renders the flight recorder: the last switches, the ISR that ran, the mutex the thread held, the notification that woke it. Nobody reproduced anything (01 §6).

## 12. Decisions taken at acceptance (2026-10-07)

1. Log identifiers are symbol addresses in a `NOLOAD` dummy region; 16-bit ids when the region fits, else 32; record format `id timestamp args` with typed arguments from the metadata signature and bounded string copies.
2. Logs and traces live in per-CPU lock-free rings; drops are counted and announced in-band; nothing blocks the kernel.
3. Trace is CTF with generated metadata from `trace_events.yaml`; the catalogue of §3.1 is the 1.0 event set; packets default to 1 KB with `events_discarded`.
4. Crash record v1 layout as in §4, append-only versioning, small v2 for AVR, partition mini-records in a retained ring.
5. The debug descriptor's sections are those of §5 and it is found by symbol or magic; one generic tool plugin serves every kernel version.
6. Monitors record worst case with the opener's program counter and act at the closing point; thresholds choose trace, notify, or fault.
7. Byte-stream framing is COBS with a channel byte and CRC-16; the memory channel offers an RTT-compatible layout as an option.
8. The host tools ship in the repository and are the only decoders the project needs.
