# Project status and evidence

**Reviewed:** 2026-10-14 against implementation baseline `f0f3afb473a61ebf02a47a9106cf9690f98ece2e`, the documentation foundation merged in `eea5b5d`, and the subsequent user-guide/API-comment review. This inventory is maintained manually until evidence tooling exists. It is not a generated support matrix.

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
| Documentation | Sphinx/MyST manuals and native tutorials, Doxygen/Breathe C API, Sphinx Needs design catalog, JSON/CSV downloads and annotation audit | Strict local HTML build, complete extracted function annotations, source-preservation and catalog-integrity checks; merged `efff40c` docs build and Pages deployment independently observed successful |
| Repository process | Issue forms, PR checklist, changelog, release/M2 work checklists, stable CI merge checks, authored-commit DCO checker and a main ruleset policy | Workflow/form/ruleset syntax and DCO/gate failure behavior checked locally; the checked-in ruleset does not activate GitHub settings; live enforcement not verified here |
| Semantic model | UP scheduler/wait/mutex/notification/lifecycle scenarios, exhaustive section-level exploration and randomized walks | Scenario-scoped exploration, not a proof of all C execution, arbitrary programs, SMP or hardware protection |

## Specified but unimplemented

Event flags, condition variables, barriers, software timers, work queues, message queues, pipes and buffer pools; indexed handles and capability tables; static partitions, syscall marshallers and supervisor restart; execution budgets and partition shares; full SoC/board/system schema validation and generation; reusable driver classes; power management; boot/update services; full observability formats; executed-case requirement traceability/coverage reports and support matrix; fuzz/HIL campaigns; SMP/AMP ports and transports.

An API or path named in a design or a requirement's verification field may be a planned deliverable. Check this inventory and the filesystem before treating it as available.

## Validation recorded in this workspace

The onboarding runs on the baseline used GCC 14.2.0, Python 3.12.14, CMake 3.31.10 and Ninja 1.13.0 on Linux. `native-gcc`, `native-release`, and `native-tiny` each passed ten CTest targets, including nine conformance executables and the differential bridge. Those are runner targets, not ten individual assertions. The base checked run reported 82 internal conformance cases with zero skips, and 210/210 bridge runs agreed with the model with one scenario excluded for that configuration. Counts for other configurations must be read from their own logs.

The reference model passed 44 pytest tests with Hypothesis installed. The banned-construct check reported zero findings. Configuration generation for native/base passed. A repeat setup run reused build outputs and passed. These are current-workspace observations, not archived release artifacts or evidence of a new machine, compiler matrix, physical board, or published environment.

The [M2-01 timer correction](engineering/m2-01-timer-correction.md), source `ca0ed3f`, subsequently passed 53 model tests, eleven CTest targets on each of native checked/release/tiny/periodic, sixteen exact live regression inputs and 11,200 unfiltered deep-campaign runs with zero divergences. Its source/build/configuration identities and outputs are archived separately from the original failing captures. The corrected model accepts all seven original archived traces; production kernel and architecture sources are unchanged.

## CI definitions versus outcomes

`.github/workflows/ci.yml` defines native GCC/Clang, release/tiny/periodic, ASan/UBSan and TSan jobs; Cortex-M, RV32 and AVR cross-GCC/QEMU jobs; formatting, banned-construct and configuration checks; model tests; and AVR footprint gates. A job definition is not a successful run. Link immutable run results and artifacts before advertising a target/version as supported or validated.

On 2026-10-14, GitHub reported the complete pre-workflow-preparation matrix successful for merged source `efff40cc2dbf803a56bab5c3fc844b78a4837f9f`: [CI run](https://github.com/EmbLinkOs/EmbLinkRTOS/actions/runs/38505570243), [docs build](https://github.com/EmbLinkOs/EmbLinkRTOS/actions/runs/38505570241) and [Pages deployment](https://github.com/EmbLinkOs/EmbLinkRTOS/actions/runs/38505739354). The configured site URL is `https://emblinkos.github.io/EmbLinkRTOS/`. These are observed workflow/job outcomes, not individual requirement coverage or physical hardware qualification; the new stable checks need their own PR results.

The repository preparation adds a stable `CI merge gate` depending on all of those matrix/lint jobs, rejects empty CTest runs, and selects Bash with pipeline failure propagation for footprint commands. `Documentation build` names the strict docs check and `Repository policy` checks matching author DCO trailers on non-merge commits. The [repository workflow](engineering/repository-workflow.md) describes the exact scope, limitations and additive main ruleset policy; GitHub settings must be activated and verified separately. Local validation used actionlint 1.7.7, parsed the issue forms/policy, exercised success/failure/skip/cancellation/empty dependency outcomes, and checked DCO rejection/acceptance with an actual temporary Git range. These checks do not establish remote CI or live branch enforcement.

Generated requirement coverage, coverage percentages, clang-tidy/cppcheck gates, include-boundary checks, double-build reproducibility, fuzzing and HIL are not currently CI gates. The roadmap must deliver them for the released scope rather than assume the engineering-system target description is already enforced.

The separate documentation toolchain is installed in `.venv-docs` with a checksummed Doxygen 1.15.0 binary under `build/tools/`. Its local HTML build extracts 361 design requirements and 82 current API functions. All extracted functions have eight contract tags and parameter descriptions; their declared requirement IDs resolve in the normative catalog. The strict build rejects omissions and unresolved IDs. This is extraction/documentation evidence, not API semantic compliance or test coverage. A related requirement can describe more than the implemented subset. The documentation integrity suite checks malformed/duplicate/empty catalogs, annotation omissions and wrapped contract fields, and keeps design acceptance separate from execution evidence.

The user-guide pass also configured native/base with stack statistics and two TLS slots in a separate `native-inspect` build; all ten CTest targets passed. Both complete tutorial programs were compiled with strict C11 warnings and run with the documented expected output and zero exit status against checked base, release, tiny and this diagnostic configuration. The diagnostic callback fragment compiled as C11, and all three fragments compiled as C++17 against native/base headers. These are local tutorial/configuration checks; they add no physical-board or universal API-coverage claim.

## Known limits and unresolved evidence

- Hardware timing and total memory use remain to be demonstrated on the primary Cortex-M board. Map-derived kernel subtotals exclude port, board, runtime and stack costs.
- The Cortex-M base TCB figures in the port page exceed R-003 T4's target; that is a disclosed target miss, not a failed native setup.
- The [M2-01 timer correction](engineering/m2-01-timer-correction.md) addresses the reproduced seed-3 model timing gap with archived/live regressions and passing deep campaigns. The historical seed-5 cancellation finding remains open without its original schedule/build, as does general automatic failure-bundle capture/minimization. Passing current campaigns does not establish the historical cause or close all of M2-01.
- AVR launch/restore uses `out SREG; pop r0; ret`; pending IRQ coherence during that tail remains a physical validation obligation (A27), not a verified atomic out/ret pair.
- Embedded fault-expectation cases that require host `fork` are skipped. A passing emulator job does not establish those misuse paths on hardware.
- `emb_irq_set_level()` does not yet implement KRN-IRQ-028's generic priority convention consistently: Cortex-M maps zero to the most urgent kernel-aware level and larger values toward lower urgency; native, AVR and current RV32 ignore the requested level. Header comments and the debugging guide disclose this implementation gap; no priority-programming code was changed during documentation work.
- Tiny uses its current base-priority table slot as `emb_thread_index()`, so reprioritization changes that diagnostic index. KRN-THR-025's stable-index target is not met across that operation. Generic fault records also leave `api_id` at zero; retained crash records and full API attribution remain incomplete.
- Planned isolation must meet SPEC-010's DMA, pointer lifetime, generation and restart obligations before fault containment is claimed. CPU protection alone is insufficient.

## Updating this page

Record source revision, configuration, tool versions, runner results, individual cases, skips and artifacts together. Move a feature from specified to implemented after inspecting the code; call a requirement verified only after its named evidence passes for the applicable profile. Do not infer universal coverage from one representative configuration. Preserve historical port measurements with their original toolchain and feature set.
