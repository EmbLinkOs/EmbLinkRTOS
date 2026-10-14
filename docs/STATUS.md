# Project status and evidence

**Reviewed:** 2026-10-14 against baseline `f0f3afb473a61ebf02a47a9106cf9690f98ece2e` and the documentation reconciliation in ADR-039. This inventory is maintained manually until evidence tooling exists. It is not a generated support matrix.

This page is the authoritative inventory of implementation and available evidence. Specifications define target contracts; their acceptance dates do not mean that all described APIs exist. The [roadmap](architecture/07-roadmap.md) determines release eligibility. [Timing and evidence](engineering/timing-and-evidence.md) defines how a claim becomes substantiated. Port pages explain implementation details and historical measurements.

## Implemented subset

| Area | Current implementation | Evidence and limits |
|---|---|---|
| Execution and scheduling | UP fixed priority, base bitmap/FIFO scheduler, tiny thread table, scheduler lock, thread lifecycle | Common conformance cases; tiny has unique base priorities and restricted scheduling semantics |
| Waiting and time | Shared wait protocol, sleep, deadlines, cancellation, suspend overlay, ordered timeouts | Native virtual time exercises periodic and tickless modes; embedded ports currently reject tickless |
| Synchronization | Semaphores, mutexes with inheritance, ceiling, recursion and owner-death policy | Conformance and native differential bridge; inheritance and deadlock detection have a configured depth limit |
| Notifications | Per-thread bits and one semaphore-to-thread binding | Binding takes a one-bit mask; no unbind implementation; pointer lifetime remains caller responsibility |
| Objects | Caller-owned generated storage and POINTER handles | No INDEXED or TABLE handles, capability rights, partition restart or system freeze implementation |
| Native port | POSIX host-thread gate, deterministic IRQ injection, virtual time | Base/tiny development workflow; no current wall-clock, asynchronous signal stress, simulated isolated profile or Win32 backend |
| Embedded ports | ATmega328P; Cortex-M3/M4F; RV32IMAC machine mode | QEMU jobs defined in CI; no real-board timing or peripheral/HIL validation recorded here |
| Configuration/build | Kconfig subset, port manifests, CMake/Ninja, layout probe, footprint tool | GCC/Clang native CI jobs; cross-GCC QEMU jobs; EmbCC toolchain files exist but RTOS execution is not qualified |
| Observability | M1 trace sink and fault reporting | CTF rings/metadata, deferred log decoder, retained crash records, debug descriptor and worst-case monitors remain planned |
| Documentation | Sphinx/MyST manuals, Doxygen/Breathe C API, Sphinx Needs design catalog, JSON/CSV downloads and annotation audit | Strict local HTML build; source-preservation and catalog-integrity checks; CI/opt-in Pages workflow configured, remote deployment not verified |
| Semantic model | UP scheduler/wait/mutex/notification/lifecycle scenarios, exhaustive section-level exploration and randomized walks | Scenario-scoped exploration, not a proof of all C execution, arbitrary programs, SMP or hardware protection |

## Specified but unimplemented

Event flags, condition variables, barriers, software timers, work queues, message queues, pipes and buffer pools; indexed handles and capability tables; static partitions, syscall marshallers and supervisor restart; execution budgets and partition shares; full SoC/board/system schema validation and generation; reusable driver classes; power management; boot/update services; full observability formats; executed-case requirement traceability/coverage reports and support matrix; fuzz/HIL campaigns; SMP/AMP ports and transports.

An API or path named in a design or a requirement's verification field may be a planned deliverable. Check this inventory and the filesystem before treating it as available.

## Validation recorded in this workspace

The onboarding runs on the baseline used GCC 14.2.0, Python 3.12.14, CMake 3.31.10 and Ninja 1.13.0 on Linux. `native-gcc`, `native-release`, and `native-tiny` each passed ten CTest targets, including nine conformance executables and the differential bridge. Those are runner targets, not ten individual assertions. The base checked run reported 82 internal conformance cases with zero skips, and 210/210 bridge runs agreed with the model with one scenario excluded for that configuration. Counts for other configurations must be read from their own logs.

The reference model passed 44 pytest tests with Hypothesis installed. The banned-construct check reported zero findings. Configuration generation for native/base passed. A repeat setup run reused build outputs and passed. These are current-workspace observations, not archived release artifacts or evidence of a new machine, compiler matrix, physical board, or published environment.

## CI definitions versus outcomes

`.github/workflows/ci.yml` defines native GCC/Clang, release/tiny, ASan/UBSan and TSan jobs; Cortex-M, RV32 and AVR cross-GCC/QEMU jobs; formatting, banned-construct and configuration checks; model tests; and AVR footprint gates. A job definition is not a successful run. Link immutable run results and artifacts before advertising a target/version as supported or validated.

Generated requirement coverage, coverage percentages, clang-tidy/cppcheck gates, include-boundary checks, double-build reproducibility, fuzzing and HIL are not currently CI gates. The roadmap must deliver them for the released scope rather than assume the engineering-system target description is already enforced.

The separate documentation toolchain is installed in `.venv-docs` with a checksummed Doxygen 1.15.0 binary under `build/tools/`. Its local HTML build extracts 361 design requirements and 82 current API functions. Seventy functions have all eight contract tags; the audit records remaining tag/parameter omissions and unmigrated requirement references. This is extraction/documentation evidence, not API semantic compliance or test coverage. The documentation integrity suite checks malformed/duplicate/empty catalogs and keeps design acceptance separate from execution evidence.

## Known limits and unresolved evidence

- Hardware timing and total memory use remain to be demonstrated on the primary Cortex-M board. Map-derived kernel subtotals exclude port, board, runtime and stack costs.
- The Cortex-M base TCB figures in the port page exceed R-003 T4's target; that is a disclosed target miss, not a failed native setup.
- Deep differential campaigns have two unresolved replay cases described in `tools/model/README.md`. They are unclassified failures until the diagnosis and reproductions establish whether the bridge, model, or kernel is responsible. Passing the six-schedule CI campaign does not close them.
- AVR launch/restore uses `out SREG; pop r0; ret`; pending IRQ coherence during that tail remains a physical validation obligation (A27), not a verified atomic out/ret pair.
- Embedded fault-expectation cases that require host `fork` are skipped. A passing emulator job does not establish those misuse paths on hardware.
- Planned isolation must meet SPEC-010's DMA, pointer lifetime, generation and restart obligations before fault containment is claimed. CPU protection alone is insufficient.

## Updating this page

Record source revision, configuration, tool versions, runner results, individual cases, skips and artifacts together. Move a feature from specified to implemented after inspecting the code; call a requirement verified only after its named evidence passes for the applicable profile. Do not infer universal coverage from one representative configuration. Preserve historical port measurements with their original toolchain and feature set.
