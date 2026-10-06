# 01 - Vision and Principles

**Status:** LOCKED where inherited from v0.1; PROPOSED where marked.

---

## 1. Mission

EmbLinkRTOS is a deterministic real-time kernel at the center of a complete embedded software platform. It scales from an 8-bit microcontroller with 2 KB of RAM to isolated, multicore 32-bit and 64-bit systems using the same kernel semantics, the same source tree, and the same tooling.

It is built so that it *could* carry industrial controllers, robotics, automotive electronic control units, medical devices, connected products, and scientific instruments. Not every capability ships at once. No early decision may make those uses impossible.

## 2. What "modern" means for EmbLinkRTOS

**PROPOSED.** These are the properties that distinguish a 2026-era RTOS from a 2006-era one. They are commitments, not slogans, and each maps to concrete architecture in documents 02 to 05.

1. **Isolation-ready by construction.** Every kernel object is reached through a capability. Every thread belongs to a partition. On a 2 KB device there is one partition and the capability is a pointer. On an MPU device the same application source becomes spatially isolated and restartable without rewriting it.

2. **Temporal protection, not just priorities.** A runaway thread is detected and contained by budgets and deadline monitoring, rather than silently starving the system. Fixed priority remains the law; budgets are the police.

3. **Observability by default.** Deferred-format logging that costs a few bytes per message, binary tracing in a standard format, a crash record that survives reset, and debug metadata that any debugger can read. A production unit must be diagnosable in the field without a rebuild.

4. **Evidence over claims.** Every real-time property is measured and published with its configuration. Every normative requirement maps to a test. The scheduler has an executable reference model, and the conformance suite is the definition of the semantics.

5. **Hardware as data.** SoCs and boards are described in a validated, machine-readable form from which device tables, memory maps, linker fragments, pin configuration, and documentation are generated. Adding a board is writing data, not code.

6. **Secure lifecycle, not security features.** Threat model, signed and measured boot, capability enforcement, memory protection, reproducible builds, software bill of materials, vulnerability disclosure, and long-term support windows. Designed for the regulatory climate products will ship into.

7. **Heterogeneous-compute ready.** Symmetric multiprocessing and asymmetric multiprocessing are peers. Cores, accelerators, secure elements, and co-processors are described in the hardware model and reached through the same device and port abstractions.

8. **Energy is a scheduled resource.** The scheduler and timer subsystem give the power manager exact knowledge of the next deadline and of latency constraints declared by threads, so idle is as deep as the application allows and never deeper.

9. **Memory-safety trajectory.** The kernel is written in a hardened C subset with static analysis as a gate. The public ABI is language-neutral so that C++, Rust, or Zig components can bind mechanically. Hardware safety features (MPU, stack-limit registers, pointer authentication, branch target identification) are used wherever present. Nothing in the design precludes capability hardware such as CHERI.

10. **Machine-readable architecture.** Requirements, API contracts (blocking behavior, ISR safety, timing class), hardware descriptions, configuration schemas, and trace formats are all data. Tools, including static analyzers, generators, IDEs, and AI-assisted review, reason over the same artifacts people read.

## 3. Core principles

Inherited from v0.1 and **LOCKED**:

1. **Production first.** Never design around hobby-only assumptions.
2. **Portability first.** Kernel policy is independent of architecture, SoC, board, compiler, and debugger.
3. **Small systems remain first-class.** Basic operation needs no MMU, MPU, FPU, allocator, filesystem, or network stack.
4. **Scale without forks.** One kernel source for AVR through SMP; features compile in or out.
5. **EmbCC is first-class, not mandatory.** GCC and Clang are architectural requirements.
6. **Real-time claims require evidence.**
7. **No hidden dynamic behavior.** Memory and algorithmic bounds are predictable per configuration.
8. **Specifications before mechanisms.**

Added in v0.2, **PROPOSED**:

9. **One concept, many scales.** A new capability is admitted only if it has a meaningful compile-to-nothing or compile-to-trivial form on the smallest supported target. Partitions, capabilities, budgets, and tracing all satisfy this.
10. **The conformance suite is the specification.** When prose and the suite disagree, the suite is fixed or the prose is fixed, in the same change. Semantics do not live in implementations.
11. **Fail loudly in checked builds, predictably in release builds.** Misuse that cannot be rejected at compile time is a kernel fault in checked builds. Release builds never silently convert misuse into different behavior (for example, a blocking call from an ISR never silently becomes a non-blocking call).
12. **Everything is static unless the application opts in.** Dynamic allocation is an application service layered on static kernel services, never the reverse.
13. **Drivers are state machines, not blocking functions.** Blocking is a wrapper around request and completion. This is what makes the same driver work under DMA, inside an isolated partition, or across a core boundary.
14. **Data before code for hardware.** No peripheral instance, pin, clock, or memory region is hand-written in C when it can be generated from the hardware description.

## 4. Non-goals

EmbLinkRTOS is not:

- a teaching scheduler or an Arduino helper library;
- a POSIX operating system (a POSIX subset may exist as a compatibility layer);
- a general-purpose OS with virtual memory, paging, or dynamic loading as core features (an MMU may be used for protection, not for demand paging);
- tied to EmbCC, EmbStudio, or any single vendor's silicon;
- a network stack, filesystem, or USB stack vendor (these are integrated middleware with defined contracts);
- certified to any safety or security standard until a defined product scope, process, and assessment say so.

## 5. Positioning against existing systems

**PROPOSED.** This is design input, not marketing. Each system below is good at something EmbLinkRTOS should learn from and has a limitation EmbLinkRTOS should avoid.

| System | Learn from it | Avoid |
|---|---|---|
| FreeRTOS | Tiny footprint, task notifications, ubiquity, simplicity of porting | Thin semantics specification; MPU support bolted on; `FromISR` API duplication; weak observability |
| Zephyr | DeviceTree plus Kconfig hardware-as-data pipeline, driver model, native simulation, userspace memory domains, huge board catalog | Weight and complexity on tiny targets; configuration surface is hard to reason about; build system coupling |
| Eclipse ThreadX | Safety certifications, event chaining, small deterministic kernel, picokernel design | Closed-world driver ecosystem; limited isolation model |
| NuttX | POSIX breadth, filesystem and driver maturity | POSIX as the core model is heavy for small targets |
| RTEMS | Rigor, space heritage, SMP scheduler research, qualification data packages | Build and configuration ergonomics |
| seL4 | Capabilities as the universal access model; formal semantics as the spec; proof-driven confidence | Not aimed at MCU-class memory; no small-target story |
| Hubris (Oxide) | Every driver in an isolated task; supervisor restarts faulted tasks; tiny syscall surface; static everything | Rust-only; single fixed task set per image |
| Tock | Process isolation on MPU MCUs; capsule model for trusted drivers; grant-based memory | Rust-only; dynamic app loading adds complexity |
| Embassy | Async as the natural MCU programming model; no-stack tasks; excellent HAL ergonomics | Not a general RTOS; cooperative executor, no priority isolation without multiple executors |
| ARINC 653 systems (PikeOS, VxWorks 653) | Time and space partitioning as the certification primitive | Static schedules as the only model |

The synthesis EmbLinkRTOS aims for: FreeRTOS-class footprint on the smallest targets, Zephyr-class hardware pipeline, seL4-style capabilities, Hubris-style isolation and supervision on MPU targets, ARINC-style time partitioning as an option, Embassy-style asynchronous driver model underneath synchronous APIs, and evidence discipline from the safety world. All in one source tree, all compiling down to what each target can afford.

## 6. Definition of success

Inherited from v0.1 Appendix D and extended. EmbLinkRTOS is credible when all of the following hold at once:

- scheduling and synchronization semantics are precise and the conformance suite enforces them;
- at least three architecture ports plus the native port pass the same conformance suite unchanged;
- memory and timing behavior are measured and bounded where promised, with published raw data;
- the same kernel source runs on a 2 KB AVR and on an isolated, multicore 32-bit SoC;
- applications build with EmbCC, GCC, and Clang;
- a new board on a supported SoC is a hardware description file plus a board directory with no new C for peripherals that already have drivers;
- a fault in an isolated partition is contained, recorded, and recovered without a reboot on MPU targets;
- a field crash can be diagnosed from the retained crash record and trace without reproducing it;
- releases are reproducible, signed, carry a software bill of materials, and have documented support windows;
- production users can read exactly what is supported, validated, and qualified, and what is not;
- MPU, SMP, AMP, networking, and safety evidence have been added without replacing the core model.
