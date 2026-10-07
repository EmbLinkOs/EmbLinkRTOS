# OBS - Observability

Group `OBS`. Design: `docs/specs/SPEC-015-observability-formats.md`; `docs/architecture/04-platform-architecture.md` §7. Related groups: FLT (crash record use), KRN-IRQ (latency metrics), PORT (descriptor contribution, trace clock), SIM (bridge), HW (trace metadata), TEST (benchmark baselines).

OBS-001 to 012 originate in `docs/architecture/04-platform-architecture.md` §7 (009 to 012 added by ADR-034 and R-003). All are restated here as the authoritative copy. New requirements start at 013.

---

### OBS-001  Deferred-format logging
**Statement.** Logging shall support deferred formatting with string interning in a non-loaded section, decodable from the ELF.
**Rationale.** ADR-009: logging cheap enough to leave on in production.
**Status.** Accepted 2026-10-07
**Verification.** Test: `obs/log_deferred_roundtrip` decodes a stream with `tools/logdec`.
**Trace.** SPEC-015 §2

### OBS-002  Compile-time and runtime filtering
**Statement.** Log severity shall be filterable at compile time per module and at runtime per module.
**Rationale.** Zero cost for excluded levels; control in the field for the rest.
**Status.** Accepted 2026-10-07
**Verification.** Footprint test (excluded calls absent); runtime filter test.
**Trace.** SPEC-015 §2.1

### OBS-003  ISR logging bounded, drops counted
**Statement.** ISR logging shall be non-blocking and bounded; drops shall be counted, not hidden.
**Rationale.** A log call must never change timing by blocking.
**Status.** Accepted 2026-10-07
**Verification.** Test: `obs/log_isr_drop_counter` under ring overflow.
**Trace.** SPEC-015 §2.3

### OBS-004  Transports
**Statement.** Log transports shall include UART, SWO/ITM, RTT-like memory channel, USB, retained RAM ring, and network, selected by configuration.
**Rationale.** Products differ in what is wired.
**Status.** Accepted 2026-10-07
**Verification.** Transport tests per reference board.
**Trace.** SPEC-015 §7

### OBS-005  Trace points compile to nothing when off
**Statement.** Trace points shall compile to nothing when disabled and to a bounded store when enabled.
**Rationale.** Zero-cost opt-out; no unbounded buffers.
**Status.** Accepted 2026-10-07
**Verification.** Footprint test with trace classes off; overflow test with `events_discarded`.
**Trace.** SPEC-015 §3.2

### OBS-006  CTF with generated metadata
**Statement.** Trace output shall be CTF with generated metadata; EmbDebug shall not be required to decode it.
**Rationale.** ADR-021.
**Status.** Accepted 2026-10-07
**Verification.** Babeltrace 2 reads a captured stream in CI.
**Trace.** SPEC-015 §3

### OBS-007  Streaming and snapshot
**Statement.** Trace shall support streaming (probe, UART, network) and snapshot (ring buffer dumped on fault or demand) modes.
**Rationale.** Live analysis and post-mortem need both.
**Status.** Accepted 2026-10-07
**Verification.** Test: streaming over the memory channel; snapshot dump through the descriptor.
**Trace.** SPEC-015 §3.3

### OBS-008  Debug descriptor
**Statement.** The image shall export a versioned debug descriptor sufficient to enumerate threads, states, priorities, stacks, owned locks, partitions, and devices without knowledge of private struct layout.
**Rationale.** ADR-011.
**Status.** Accepted 2026-10-07
**Verification.** `tools/descriptor` enumerates everything from a memory dump of the conformance run on each port.
**Trace.** SPEC-015 §5

### OBS-009  Worst-case monitors with opener address
**Statement.** Worst-case interrupt-masked and scheduler-locked intervals shall be recorded per CPU and per thread with the address of the opener, as a compile-time option.
**Rationale.** ADR-034.
**Status.** Accepted 2026-10-07
**Verification.** Test: `obs/monitor_records_pc` with a deliberately long critical section.
**Trace.** SPEC-015 §6

### OBS-010  Monitor thresholds
**Statement.** Thresholds per monitored category shall be configurable to trace, notify, or fault.
**Rationale.** Field-detectable latency violations.
**Status.** Accepted 2026-10-07
**Verification.** Test: each action triggered by threshold crossing.
**Trace.** SPEC-015 §6

### OBS-011  Monitor export
**Statement.** Monitor state shall be exported through the debug descriptor and the statistics API.
**Rationale.** Tools and applications read the same numbers.
**Status.** Accepted 2026-10-07
**Verification.** Descriptor consumer test; `emb_monitor_get` test.
**Trace.** SPEC-015 §5, §6

### OBS-012  Benchmark baseline regression
**Statement.** Benchmark results of a release shall be compared with the previous release's baseline on the same board and configuration; a regression beyond the configured threshold shall fail the release.
**Rationale.** R-003 §4: targets become baselines.
**Status.** Accepted 2026-10-07
**Verification.** Release pipeline test with an injected regression.
**Trace.** SPEC-015 §8; TEST-012

### OBS-013  Log record format
**Statement.** A log record shall be `id timestamp args` with the identifier being the address of the call site's metadata object in a `NOLOAD` dummy region (16 bits when it fits, else 32), typed arguments per the metadata signature, bounded string copies, an in-band `dropped(count)` record, and a periodic synchronization marker; the plain-text backend shall share the call sites.
**Rationale.** Smallest records, host-side formatting, mid-stream resynchronization.
**Status.** Accepted 2026-10-07
**Verification.** Decoder tests including mid-stream join and drop records; AVR plain-text test.
**Trace.** SPEC-015 §2.2 to §2.5

### OBS-014  Trace event catalogue and emission
**Statement.** The 1.0 trace event set shall be the catalogue of SPEC-015 §3.1 defined in `trace_events.yaml`; emitters, enum, and CTF metadata shall be generated from it; events shall be fixed-size with handles as index and generation and threads as index; rings shall be per-CPU CTF streams with 1 KB packets by default and `events_discarded` on overflow; classes shall be selectable at compile time and masked at run time.
**Rationale.** One catalogue keeps every specification's events decodable by one tool.
**Status.** Accepted 2026-10-07
**Verification.** Generator golden test; Babeltrace reads every event class.
**Trace.** SPEC-015 §3.1, §3.2

### OBS-015  Trace clock
**Statement.** Event timestamps shall be 32-bit trace clock ticks with the 64-bit value in packet headers and a CTF clock class declaring the frequency from the hardware description; the tiny profile may use the kernel tick.
**Rationale.** Tools convert to time without guesswork.
**Status.** Accepted 2026-10-07
**Verification.** Metadata check; Trace Compass displays absolute times.
**Trace.** SPEC-015 §3.4

### OBS-016  Crash record format
**Statement.** The crash record shall follow the v1 layout of SPEC-015 §4 (header, build id, uptime, reset reason, fault fields, status and general registers with counts, flight recorder tail, CRC-32), written before any recovery action to the retained section or `.noinit`; a v2 small variant shall serve AVR; partition mini-records shall go to a retained ring when enabled; formats shall be versioned and append-only; the kernel shall expose get and clear on the next boot.
**Rationale.** FLT-001 made concrete and decodable by `tools/crashdec`.
**Status.** Accepted 2026-10-07
**Verification.** Fault-injection test: record written, survives reset, decodes and symbolizes.
**Trace.** SPEC-015 §4

### OBS-017  Descriptor contents and discovery
**Statement.** The descriptor shall contain the sections of SPEC-015 §5 (header, types, thread, scheduler, objects, partitions, rings, monitors, arch), be exported at `emb_debug_descriptor`, carry the magic `"EMBD"`, and be versioned so that one generic plugin serves every kernel version.
**Rationale.** OBS-008 made exact.
**Status.** Accepted 2026-10-07
**Verification.** `tools/descriptor` test on every reference configuration; version compatibility test.
**Trace.** SPEC-015 §5

### OBS-018  Statistics API and monitor categories
**Statement.** The kernel shall offer `emb_stats_cpu_get`, `emb_stats_thread_get`, `emb_stats_object_get`, `emb_monitor_get`, `emb_monitor_set_threshold`, and `emb_stats_reset`, with monitor categories `IRQ_LOCK`, `SCHED_LOCK`, `ISR(vector)`, `THREAD_RUN`, `WORK_HANDLER`; all shall compile out with their options.
**Rationale.** 04 §7.5; ADR-034.
**Status.** Accepted 2026-10-07
**Verification.** API tests; footprint with statistics off.
**Trace.** SPEC-015 §6

### OBS-019  Framing and memory channel
**Statement.** Byte-stream transports shall carry COBS frames with a channel byte and CRC-16 (channels 0 log, 1 trace, 2 crash, 3 shell, 4 to 15 application); the memory channel shall export a control block at `emb_memchan` with an optional RTT-compatible layout.
**Rationale.** One framing for every stream; probe tooling reuse.
**Status.** Accepted 2026-10-07
**Verification.** Decoder resynchronization test; RTT-compatible read by an existing probe tool.
**Trace.** SPEC-015 §7

### OBS-020  Host tools and build-id checks
**Statement.** `tools/logdec`, `tools/tracedec`, `tools/crashdec`, and `tools/descriptor` shall ship with the kernel, refuse streams whose build id mismatches the ELF, and accept every format version they know.
**Rationale.** The decoders are part of the product.
**Status.** Accepted 2026-10-07
**Verification.** Tool tests including a mismatched build id.
**Trace.** SPEC-015 §10

### OBS-021  Profile defaults
**Statement.** Observability defaults per profile shall be those of SPEC-015 §9: plain-text log and small crash record on tiny, deferred log and snapshot trace on base, streaming trace and partition records on isolated and multicore, host pipe decoding on native; the descriptor shall be present in every profile.
**Rationale.** Footprint where it matters, evidence everywhere.
**Status.** Accepted 2026-10-07
**Verification.** Configuration review; footprint tests.
**Trace.** SPEC-015 §9
