# Changelog

User-visible changes are recorded here. There are no released versions or release tags in the reviewed repository; `0.2.0` in the headers identifies development, not a supported release. Move relevant Unreleased entries into a dated version section only when the [release checklist](docs/engineering/releases.md) is complete. Record support scope and known limits with each release.

## Unreleased

### Changed

- The idle context is no longer reachable through a thread handle, and `emb_thread_self()` returns `EMB_HANDLE_NULL` in it (KRN-THR-026, ADR-050). On the tiny profile without an idle thread only the prefix of its control block is allocated: AVR `footprint_t1` kernel static RAM drops from 64 to 43 bytes.

- The historical M2-01 `cancel_waiter_disinherits` seed-5 report is closed as not reproducible (input unavailable, cause unknown) after bounded triage: not fixed, not classified, still listed among the known limits, with named reopen triggers. `timing-and-evidence.md` defines that disposition.

- `emb_sem_init()` and `emb_mutex_init()` refuse storage that still holds an active object of their type (fault in checked builds, `EMB_EBUSY` in release) instead of overwriting it, when `CONFIG_EMB_OBJ_LIST` is on (default except on the tiny profile). Storage is reused only through destroy. Semaphore and mutex storage grows by one list node where the option is on. KRN-OBJ-007 now specifies `EMB_EBUSY` instead of `EMB_EEXIST` (ADR-045).

### Added

- `CONFIG_EMB_OBJ_LIST`: per-type lists of active semaphores and mutexes (KRN-OBJ-011).
- A `.clangd` and exported `compile_commands.json` for editor support; `.clang-tidy` disables, with reasons, the checks that contradict the coding standard.

- Stable numeric IDs in public API misuse fault records, a generated header and downloadable decoder map, with checked-in assignments and compatibility checks. Shared helpers retain the public caller ID and location strings remain optional.

- The Arduino Uno conformance suite on EmbSIM's simulated Uno (`avr-uno-embsim` preset,
  `EMB_TEST_EMULATOR=embsim`, `tools/embsim/run_embsim.py`) and an `avr-embsim` CI job that builds
  EmbSIM from source (SIM-002).
- The NUCLEO-F446RE board (`boards/st/nucleo_f446re`): the STM32F446RE at 180 MHz from the PLL
  fed by the ST-LINK's 8 MHz MCO (HSE bypass, voltage scale 1 with over-drive, five flash wait
  states), the console on USART2 (the ST-LINK's virtual COM port, PA2/PA3), LD2 on PA5, and a
  run that ends masked in WFI (`emb_cm_halt`). Its conformance suite runs on EmbSIM's
  NUCLEO-F446RE (`cm4-nucleo-embsim` preset) in a `cm4-embsim` CI job.
- The ESP32-C3-DevKitM-1 board (`boards/espressif/esp32c3_devkitm1`) and the RISC-V port on the
  ESP32-C3 (`CONFIG_EMB_RV_ESP32C3`, variant `rv32imc`): SYSTIMER's comparator 0 as the tick,
  FROM_CPU_INTR0 as interrupt 0 through the interrupt matrix, a vectored mtvec, the boot ROM's
  watchdogs stopped; each image is also written as the flash image the ROM boots
  (`tools/esp/esp_image.py`). The conformance suite runs on EmbSIM's DevKitM-1
  (`rv32-c3-embsim` preset) in an `rv32-embsim` CI job. The CLINT code moved to
  `arch/riscv/rv_clint.c`; QEMU virt is unchanged.

- Experimental Linux x86-64 presets for an EmbCC-built kernel with a GCC POSIX host, a build-time ABI layout guard and scoped executed conformance/model evidence.

- Searchable Sphinx documentation with native user tutorials, extracted C API, a design-requirement catalog and downloadable metadata.
- Strict documentation builds with complete extracted API annotations and integrity checks, plus optional GitHub Pages deployment.
- Bug/feature issue forms, a pull-request checklist and an ordered M2 implementation checklist.
- Stable CI merge checks, authored-commit DCO checking and a reviewable `main` branch ruleset policy.
- M2-01 native deep-campaign evidence, exact replay inputs and the original timeout-arm diagnosis, with separate corrected-source outcomes.
- Exact native timer-boundary regression checks and a periodic-clock native preset in CI.
- Automatic differential failure bundles, exact offline/live replay, bounded confirmed schedule reduction, a native evidence workflow check and CI retention of default failure bundles.
- Bounded historical seed-5 triage evidence and M2-02 contract proposals for review before kernel implementation.
- Pinned EmbCC ten-item capability review, scoped local compiler/tool/EmbSim evidence and current RTOS integration findings.

### Fixed

- Thread diagnostic IDs remain stable across priority changes on tiny and unique among live objects on every profile. IDs are released only by successful destroy; exhaustion returns `EMB_ENOMEM` without partial construction.

- Generic IRQ priorities now increase with urgency on Cortex-M, with board-derived bounds and validation before register writes; fixed-level native/AVR/RV32 reject nonzero levels. SysTick defaults to highest kernel-aware urgency. Raw kernel-independent vectors remain unsupported.

- EmbCC now emits storage-layout probe markers without an injected header; generation and kernel-archive compilation pass on eight configurations. Full Linux-native compilation advances to the separate missing-POSIX-header boundary.
- The reference model now delivers already-due tickless timers between sections without requiring another tick; the archived seed-3 trace and reduced cases agree. The periodic differential harness now stops idle ticking when finite waits end. Kernel/architecture implementations are unchanged; the historical seed-5 finding remains open.

### Changed

- Thread storage is reused only through `emb_thread_destroy()` followed by `emb_thread_init()` (SPEC-008 §5, §12; KRN-THR-015). `emb_thread_init()` on storage that still holds an undestroyed thread, including a joined or detached terminated one, is now misuse: a fault in checked builds, `EMB_EBUSY` in release. Re-initializing without destroy previously leaked the diagnostic identity and, with the all-threads list, linked the object twice.

- IRQ callers must use advertised limits; previously ignored/clamped priority requests now fail. Cortex-M configuration derives the public domain from declared priority width and the validated masking threshold; non-nesting configurations require a single kernel-aware level.

- Reconciled design contracts, current implementation limits, timing/evidence obligations and the staged first-release scope.
- Clarified API parameter/storage lifetimes, wait outcomes, configuration and current interrupt-priority/thread-index limitations.
- Refreshed obsolete EmbCC limitations in compiler/AVR/debug/linker/SVD/C++ documentation while retaining incomplete RTOS compiler qualification and existing kernel policies.
- CI test commands reject zero registered tests instead of accepting an empty run.
- Official GitHub Actions are pinned to reviewed Node 24 release commits, replacing deprecated Node 20 action versions.

### Known limits

- The blocking kernel remains incomplete: event flags, condition variables, work queues, software timers and message queues are planned.
- Deep differential failures and known port/introspection inconsistencies remain open. No physical-board, isolation, universal requirement coverage or supported-release claim is made.
- Repository policy files do not activate GitHub settings themselves. Consult the [repository workflow](docs/engineering/repository-workflow.md) and verify the live ruleset and check results.

See [Project status](docs/STATUS.md) for the complete implementation/evidence inventory. This changelog begins with the documentation/workflow preparation; it does not invent a historical release for the existing M1 source.
