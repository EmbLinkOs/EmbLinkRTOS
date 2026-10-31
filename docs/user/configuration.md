# Configuration and testing

The configuration pipeline resolves Kconfig defaults seeded by a port manifest, board defaults, a profile, optional fragments and explicit overrides. Later fragments win. Manifest-derived architecture capabilities are read-only. CMake produces generated configuration and storage headers; do not edit those outputs. Invalid combinations should fail configuration rather than silently change semantics.

## Select a reference configuration

| Native preset | Purpose |
|---|---|
| `native-gcc` | GCC, checked base configuration |
| `native-release` | GCC, release checks/behavior |
| `native-tiny` | Tiny thread-table configuration and narrower time/notification choices |
| `native-periodic` | GCC, checked base with a periodic clock for timer/wait validation |
| `native-clang` | Clang compiler coverage; requires Clang |
| `native-asan` / `native-tsan` | Sanitizer configurations; require supported compiler runtimes |
| `native-coverage` | GCC `--coverage` build; after CTest, `tools/coverage/report.py` prints kernel statement and branch coverage |
| `native-timeslice` | GCC, checked base with round-robin time slicing (quantum 2 ticks, levels up to 20) and a periodic clock, so the differential bridge checks rotation |
| `native-timeslice-tickless` | The same slicing with the tickless clock; conformance only |

Use the same preset name for configure, build and CTest. A tiny result does not verify base-only features; a release result does not exercise checked-build faults. Embedded presets additionally require their cross-compiler, libc and emulator; do not interpret a build with no registered tests as conformance.

Onboarding validated the first three GCC presets in this cloud machine. Clang/sanitizer and embedded toolchains are not installed here. CI definitions describe a broader matrix, whose successful outcomes must be recorded separately.

The later [timer-delivery validation](../engineering/m2-01-timer-correction.md) also exercised `native-periodic` and added exact timer-boundary regressions to every applicable native CTest build. Its recorded outcomes distinguish periodic and tickless runs.

## Kernel object options

| Option | Default | Notes |
|---|---|---|
| `CONFIG_EMB_EVENT`, `CONFIG_EMB_EVENT_BITS` | y; 32 bits (8 on tiny) | event flags |
| `CONFIG_EMB_COND` | y (n on tiny) | condition variables; needs `CONFIG_EMB_MUTEX` |
| `CONFIG_EMB_MSGQ`, `CONFIG_EMB_MSGQ_MAX_ITEM` | y; 64 bytes (16 on tiny) | message queues; the item bound limits the copy made with interrupts masked |
| `CONFIG_EMB_IPC_SMALL` | n (y on tiny) | 8-bit queue counts, at most 255 items |
| `CONFIG_EMB_WORKQ` | y | work queues and, with timers, delayed work; not available with the tiny profile's table scheduler |
| `CONFIG_EMB_SYSTEM_WORKQ`, `_PRIO`, `_STACK` | y; 0 (second-highest priority); 2048 bytes | the system work queue, linked only when `emb_workq_system()` is referenced |
| `CONFIG_EMB_TIMER` | y | software timers; not available with the table scheduler |
| `CONFIG_EMB_OBJ_LIST` | y (n on tiny) | exact refusal of init on live storage |

## Scheduler options

| Option | Default | Notes |
|---|---|---|
| `CONFIG_EMB_TIME_SLICING` | n | round robin among equal effective priorities; not available with the table scheduler |
| `CONFIG_EMB_TIMESLICE_TICKS` | 10 | the quantum, started at each dispatch |
| `CONFIG_EMB_TIMESLICE_PRIORITY` | 255 | the highest sliced level; higher levels run until they block or yield |

The tiny `footprint_t1` reference configuration turns event flags and message queues off.

## Add diagnostic options without changing the reference preset

Activate native tools, then configure a separate ignored build directory. `EMB_SET` uses Kconfig names without the generated `CONFIG_` prefix; quote the semicolon-separated list so the shell passes one argument:

```sh
cmake --preset native-gcc -B build/native-inspect \
  '-DEMB_SET=EMB_STACK_STATS=y;EMB_TLS_SLOTS=2'
cmake --build build/native-inspect --parallel 4
ctest --test-dir build/native-inspect --output-on-failure --no-tests=error
```

`EMB_STACK_STATS` enables fill-pattern estimates, and `EMB_TLS_SLOTS=2` exposes two application pointer slots. Native/base already enables the trace sink, names and thread registry by default; inspect resolved values rather than assuming another profile/port does. Changing feature options can change control-block sizes, so compile the application against the headers and libraries from the same build directory.

For a reusable extra fragment, save a file such as `build/local-debug.conf` containing:

```text
CONFIG_EMB_STACK_STATS=y
CONFIG_EMB_TLS_SLOTS=2
```

Select it with `-DEMB_CONF=build/local-debug.conf` in a separate configure invocation, clearing any previous explicit overrides with `-DEMB_SET=` if you want the fragment to determine the values. `EMB_CONF` accepts a quoted semicolon-separated list and applies it after board/profile fragments; `EMB_SET` applies last. These CMake cache arguments persist in that build directory until changed or cleared.

## Inspect generated results

| Output in the selected build directory | Use |
|---|---|
| `.config` | Resolved Kconfig values and configuration hash input |
| `generated/config.json` | Symbol values and their origins |
| `generated/include/emb/config.h` | C feature macros, version and configuration hash |
| `generated/include/emb/storage.h` | Measured configured object sizes, alignments and definition macros |
| `generated/config.cmake` | CMake feature values |

The layout probe generates storage after configuration during the build. A missing `emb/storage.h` usually means the generation/build step was not completed or the application's include path points at the wrong build directory. Reusing headers from a different profile/compiler is not a supported ABI shortcut.

## Limits that configuration cannot remove

Tiny uses unique base-priority slots and 32-bit time with a signed half-range deadline window. Disabled feature declarations may still appear in the generated documentation because the reference extractor enables feature gates to show the full current header surface; an application must use the configured availability contract. Selecting a symbol for a planned service does not supply its implementation.

Embedded ports currently reject tickless mode. Native virtual time is not a real-time host clock. `CONFIG_EMB_IRQ_LOCK_STATS` and other architecture target options are not evidence that the corresponding measurement/export service exists. Cross-compiler, emulator and physical-board preparation remain separate from native setup. See [Choosing current APIs](feature-status.md) and [Project status](../STATUS.md).

For configuration overrides and generated files, read `tools/kconfig/README.md` in the repository. For implementation and platform limits, read [Project status](../STATUS.md). For declaring a timing or footprint result, use [Timing and evidence](../engineering/timing-and-evidence.md).
