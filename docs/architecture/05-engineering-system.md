# 05 - Engineering System

**Status:** PROPOSED unless marked. This document fixes the conventions that shape every header, build, test, and release.

---

## 1. API conventions (ADR-002)

These are decided before the first public header is written.

### 1.1 Naming and namespaces

| Prefix | Use |
|---|---|
| `emb_` | Public API and public types |
| `EMB_` | Public macros and constants |
| `emb_arch_` | Architecture port contract (visible to ports, not to applications) |
| `embk_` | Kernel-private symbols |
| `emb_drv_<class>_` | Driver class APIs |
| `CONFIG_EMB_` | Configuration symbols |

Avoid any spelling resembling `embos`; embOS is an existing commercial RTOS trademark.

Object verbs are uniform: `init` (caller storage), `create` (where an allocator is configured), `destroy`, plus the object's operations. Blocking operations take a trailing `emb_timeout_t`. Absolute variants end in `_until`.

### 1.2 Status codes

One type, `emb_status_t` (an `int`), with its values named by `enum emb_status_code`. `EMB_OK` is zero. Errors are negative with fixed, stable values from a closed kernel set of eighteen codes (`EMB_EPERM`, `EMB_EINVAL`, `EMB_EBUSY`, `EMB_ETIMEDOUT`, `EMB_ECANCELED`, `EMB_EDESTROYED`, `EMB_EOWNERDEAD`, `EMB_ENOMEM`, `EMB_ENOTSUP`, `EMB_EIO`, `EMB_EFAULT`, `EMB_ENOENT`, `EMB_EEXIST`, `EMB_ESTATE`, `EMB_EOVERFLOW`, `EMB_ESTALE`, `EMB_EINTR`) and reserved ranges for drivers and applications; SPEC-001 §4 is authoritative. Positive values are operation-specific counts. No `errno` global and no per-thread error state in the kernel API; the POSIX layer maps codes to errno.

### 1.3 ISR-safety convention (ADR-003)

One API, not `_from_isr` duplicates. Each function is documented with one of three classes:

| Class | Meaning |
|---|---|
| **ISR-safe** | Callable from any context; never blocks; O(1) or documented bound |
| **Thread-only** | Blocks or may block; calling from ISR is a kernel fault in checked builds and returns `EMB_EPERM` in release |
| **Pre-kernel** | Callable before the scheduler starts (object init) |

Any blocking function called from an ISR with `EMB_NO_WAIT` is permitted and behaves as its ISR-safe non-blocking form; with any other timeout it is misuse. Reschedule-on-ISR-exit is handled by the kernel's `reschedule_pending` flag and the architecture exit path, so no "higher priority woken" out-parameter exists.

### 1.4 Machine-readable contracts

Every public function carries structured annotations (in the header comment, parsed by tooling): context class, blocking class, timing class (`O(1)`, `O(log n)`, `O(n)` with the n named), ownership transfer, and configuration dependencies. The documentation, the static analyzer rules (EmbCC and clang-based), and the conformance suite generator consume the same annotations.

**API-001** All public functions shall return `emb_status_t` or a documented value type; no function shall report errors through a global.
**API-002** All blocking functions shall accept a timeout and shall have an absolute-deadline variant where periodic use is expected.
**API-003** Every public function shall carry machine-readable context, blocking, timing, and ownership annotations.
**API-004** Public headers shall compile as C11 and as C++17 without warnings on EmbCC, GCC, and Clang.
**API-005** The public API shall be identical for privileged and unprivileged callers.

### 1.5 Language level

C11 is the minimum (`_Static_assert`, `_Alignas`, anonymous unions, `_Atomic` where the toolchain provides it; the portability layer wraps atomics). C17 preferred. Freestanding; no dependence on hosted libc in the kernel. C++17 for optional wrappers, with exceptions and RTTI off by default and a documented policy for static constructors.

EmbCC compiles a single dialect, C17 plus GNU extensions, and ignores `-std=` (09 §3), so the C11 floor is enforced by the GCC and Clang legs of the matrix with `-std=c11 -pedantic`. Compiler thread-local storage is banned tree-wide (KRN-THR-014). C++ wrappers are built and tested with GCC and Clang on the embedded targets and with EmbCC on the native target, since EmbCC does not yet compile C++ for 32-bit and 8-bit targets.

### 1.6 Coding standard

A written standard covering: undefined-behavior policy, integer and conversion rules, `volatile` for registers only and never for synchronization, no variable-length arrays, no recursion in kernel paths, fixed-width types at ABI boundaries, bounded loops on real-time paths, and MISRA C:2023 orientation with recorded deviations. Enforced by clang-format, clang-tidy, cppcheck, and EmbCC diagnostics in CI.

## 2. Configuration and build (ADR-005)

### 2.1 Configuration

**Kconfig semantics** (via a Python Kconfig implementation): familiar to the industry, supports dependencies and defaults from the generated board files, has mature tooling. The output is a normalized `emb_config.h` plus a `config.json` consumed by the generator and tools. Invalid combinations (userspace without protection hardware, SMP on a single-core SoC) are rejected at configuration time with a message naming the conflicting symbols.

### 2.2 Build

**CMake** with presets is the reference build. Toolchain files for GCC, Clang, and EmbCC isolate compiler differences. EmbBuild, when it exists, drives the same CMake targets or consumes the same generated inputs; it never becomes the only way to build.

```
hardware description + Kconfig + sources
        |
        v
  generator (python)  -> generated/ (tables, linker fragments, config headers, hw.json)
        |
        v
  CMake + toolchain file -> ELF, BIN/HEX, MAP, symbols, SBOM, manifest, debug descriptor check
```

**BLD-001** The reference build shall be CMake with documented presets; EmbBuild shall be optional.
**BLD-002** Compiler-specific attributes and builtins shall be isolated in `include/emb/compiler/` with one header per compiler.
**BLD-003** The build shall emit a build manifest (source revision, configuration hash, toolchain identity, hardware description hash) embedded in the image and in the SBOM.
**BLD-004** Builds shall be reproducible given the pinned toolchain container; CI shall verify by double build.
**BLD-005** The `emb` command-line tool shall wrap configure, build, flash, debug, trace, and test with per-board defaults, using EmbFlash and `embdbg` when present and third-party tools otherwise.
**BLD-006** The reference build shall be able to emit an EmbBuild manifest (`.ebm`: name, kind, inputs, args, output per target, with derived header closures) so that EmbLinkRTOS builds under EmbBuild on EmbLinkOS (09 §9).
**BLD-007** The generator shall emit a configuration consistency check that fails the build when a required configuration symbol is missing, when the configuration version does not match the kernel version, or when a profile constraint is violated (ADR-035; ChibiOS `chchecks.h`).
**BLD-008** A profile marked `safety` shall reject option combinations that remove checks, run timer callbacks in interrupt context, permit object creation after `emb_system_freeze()`, or disable stack protection (ADR-035; ThreadX `TX_SAFETY_CRITICAL`).

## 3. Repository layout (PLANNED, refined)

```
EmbLinkRTOS/
  include/emb/            public API headers
    compiler/             per-compiler portability headers
  kernel/                 exec, sched, wait, time, sync, ipc, object, part, mem, fault, work, notify, trace, power
  arch/                   avr/  cortex_m/  riscv/  native/
  soc/                    <vendor>/<family>/   startup, errata, soc description yaml
  boards/                 <vendor>/<board>/    board description yaml, board init
  drivers/                <class>/             class API + implementations
  subsys/                 power/ logging/ tracing/ security/ storage/ net/ usb/ ...
  compatibility/          cmsis_rtos2/ posix/ cxx/
  hw/                     schemas, importers, generator
  tools/                  emb cli, decoders (log, trace, crash), reference model, hil runner
  tests/                  conformance/ kernel/ arch/ drivers/ stress/ fuzz/ hil/ benchmarks/
  samples/
  docs/                   architecture/ specs/ requirements/ api/ boards/ ports/
  cmake/  scripts/  ci/
  SECURITY.md  LICENSE  CONTRIBUTING.md  CODEOWNERS
```

Include-path isolation enforces the dependency rules in 02 §5; a CI script fails the build on a forbidden include.

## 4. Verification architecture

### 4.1 The conformance suite is the specification (principle 10)

`tests/conformance/` holds the normative tests for every public API and every kernel requirement. Each test names the requirement identifiers it covers. A requirement without a test is flagged; a test without a requirement is flagged. The traceability matrix is generated, not maintained by hand.

### 4.2 Executable reference model (ADR-013)

A small, readable model of the scheduler, wait protocol, priority inheritance, and timeouts (Python, or C compiled for the host) that implements the semantics in 03 directly from the requirements, without performance concerns. It serves as:

- a **test oracle**: randomized operation sequences are run on both the model and the real kernel (native port), and state and wake results are compared;
- a **design tool**: proposed semantic changes are tried in the model first;
- a **documentation artifact**: the model is readable by reviewers who do not read kernel C.

The wake-race protocol and inheritance recomputation are additionally explored exhaustively for small thread counts (bounded model checking), either in the model or in TLA+.

### 4.3 Test layers

| Layer | Where | Gate |
|---|---|---|
| Unit tests for pure algorithms | host | every commit |
| Conformance suite | native, emulated, HIL | every commit on native and emulated; nightly on HIL |
| Differential tests against the reference model | native | every commit |
| Property-based and fuzz tests (API misuse, parsers, configuration, hardware model) | native with sanitizers | nightly |
| Fault injection (allocation failure, timeout races, power loss on storage) | native, HIL | nightly |
| Stress and soak | emulated, HIL | nightly and weekly |
| Latency and footprint benchmarks with full metadata | HIL | nightly, published |
| Compiler matrix (EmbCC, GCC, Clang; versions; -O levels; LTO) | native, emulated | nightly |
| Static analysis (clang-tidy, cppcheck, EmbCC analyses) | host | every commit |
| Coverage (statement and branch; MC/DC readiness on kernel) | native | nightly |

**TEST-008** An executable reference model of scheduler and wait semantics shall exist and shall be used as the oracle for differential testing.
**TEST-009** The requirement-to-test traceability matrix shall be generated from test annotations and shall fail CI on uncovered normative requirements.
**TEST-010** Kernel misuse paths (ISR blocking, non-owner unlock, destroy with waiters, stale capability) shall have tests in checked and release configurations.
**TEST-011** Footprint (flash and RAM) per profile and reference board shall be tracked per commit with regression thresholds.
**TEST-012** A cross-RTOS benchmark harness shall run the same operations on the same board, compiler, and options for EmbLinkRTOS, FreeRTOS, Zephyr, and ThreadX, with at least 10,000 samples per operation and the metadata of §4.4, and its raw data shall be published with every release (R-003 §6).

### 4.4 Benchmarks

Metrics from v0.1 §29 stand. Each benchmark result is stored as raw samples plus a metadata record (board, SoC revision, clock, compiler and version, flags, LTO, configuration hash, enabled features, timer source, instrumentation method, interrupt load, cache and FPU state). Published numbers include distributions and worst observed values. The HIL runner produces this automatically.

The cross-RTOS harness (TEST-012, R-003 §6) is part of this: `tests/benchmarks/` holds one adapter per kernel, competitor configurations are published and reviewed for fairness, and a regression against the previous release's baseline fails the release (OBS-012). Vendor numbers are never quoted as comparisons; only harness results are.

## 5. Quality gates

A change merges only when: build matrix green; conformance suite green on native and emulated targets; static analysis clean or deviations recorded; coverage not regressed on kernel; footprint within thresholds; traceability matrix complete for touched requirements; documentation updated for touched public API; and, for kernel semantics changes, the reference model updated in the same change.

## 6. Requirements and traceability

Identifier groups (v0.1 plus v0.2 additions):

```
KRN-THR  KRN-SCH  KRN-IRQ  KRN-TIM  KRN-SYNC  KRN-IPC  KRN-MEM  KRN-SMP   (v0.1)
KRN-MM   KRN-TP   KRN-WAIT KRN-NOTIF KRN-WQ   KRN-CAP  KRN-OBJ  KRN-PART  (v0.2 kernel)
FLT  HW  DRV  PWR  BOOT  SEC  OBS  MC  SIM  API  BLD  TEST  PORT  REL  SUP (platform and process)
```

Requirements live in `docs/requirements/` as structured text (one file per group, one block per requirement with id, statement, rationale, status, verification method, and linked tests). The architecture documents cite them; they do not duplicate them. Tooling (Sphinx with a needs extension, or a project script) renders the matrix.

## 7. Release engineering and support

### 7.1 Versioning

`0.x` while architecture and API are unstable. `1.0` freezes the public C API (source compatible for all `1.x`), the hardware description schema (versioned, with migration tooling), the trace and crash record formats (versioned), and the debug descriptor (versioned). Binary ABI stability is promised only at the syscall boundary of isolated profiles and in the versioned data formats.

### 7.2 Long-term support

LTS lines every 18 to 24 months with a published maintenance window, security fixes, critical bug fixes, qualified toolchain versions, and an end-of-life date. Non-LTS releases get security fixes until the next release.

### 7.3 Release contents

Source archive, signed tags, SBOM, build manifests for reference configurations, reproducibility attestation, benchmark data, conformance results per supported target, support matrix, release notes with known issues, and migration notes.

**REL-001** Every release shall be signed, reproducible, and accompanied by an SBOM and conformance evidence.
**REL-002** Support windows and end-of-life dates shall be published per release line.

## 8. Support levels (LOCKED from v0.1, applied per axis)

Experimental, Supported, Validated, LTS-qualified, declared independently for each architecture, SoC, board, driver, toolchain version, and profile. The support matrix is generated from CI results and the HIL inventory; a claim that is not exercised in CI cannot appear as Supported or above.

## 9. Documentation

Docs-as-code. Architecture (these documents), requirements, API reference generated from annotated headers, one generated page per board and per architecture port, porting guides for architecture and SoC, user guide per profile, and the decision log. API pages state parameters, status values, context class, blocking class, timing class, ownership, lifecycle constraints, and configuration dependencies, all sourced from the header annotations.

## 10. Licence and governance (LOCKED, ADR-024)

Apache-2.0 for kernel, ports, drivers, tools, hardware descriptions, and generated code. Outside contributions are accepted under the Developer Certificate of Origin with a `Signed-off-by` line on every commit; inbound licence equals outbound licence, and there is no contributor licence agreement. Every source file carries an SPDX identifier. `NOTICE` names the copyright holder and reserves the EmbLink names. `.github/CODEOWNERS` assigns review ownership per layer, starting with the project owner for everything.

The core kernel remains written and understood by the project owner, per v0.1 §1.2; outside contributions are expected first in ports, SoC and board descriptions, drivers, tooling, tests, benchmarks, and documentation, and kernel proposals arrive as ADRs and reference-model changes. The files `LICENSE`, `NOTICE`, `DCO`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, and `SECURITY.md` at the repository root implement this section.
