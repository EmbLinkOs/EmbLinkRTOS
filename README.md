# EmbLinkRTOS

A deterministic real-time kernel at the center of a complete embedded software platform, scaling from 8-bit microcontrollers with 2 KB of RAM to isolated, multicore 32-bit systems with one kernel source, one set of semantics, and one tool pipeline.

**Current phase: architecture complete, implementation next.** There is no kernel code yet, on purpose. The architecture (`docs/architecture/`), fifteen accepted specifications (`docs/specs/`), 356 requirements with verification methods (`docs/requirements/`), three research records comparing eleven other kernels (`docs/research/`), and an executable reference model that explores the kernel's protocols exhaustively (`tools/model/`) fix the semantics before the first source file, so the implementation never has to be rebuilt around them.

Start with [`docs/README.md`](docs/README.md).

## What makes it different

- **Isolation-ready by construction.** Capabilities and partitions exist at every scale: a pointer on a 2 KB device, hardware-enforced isolation with restartable partitions on an MPU device, same application source.
- **Temporal protection.** Fixed-priority scheduling with execution budgets and deadline monitoring, instead of silent starvation or priority aging.
- **Hardware as data.** SoCs and boards are validated descriptions; device tables, memory maps, pin and clock configuration, and linker fragments are generated.
- **Observability by default.** Deferred-format logging, Common Trace Format tracing, retained crash records, and a versioned debug descriptor any debugger can read.
- **Evidence over claims.** An executable reference model is the scheduler's oracle; every requirement maps to a test; every timing number ships with its configuration.
- **Secure lifecycle.** Signed, measured, updatable images; reproducible builds; software bill of materials; disclosure policy; long-term support windows.
- **Toolchain independent.** EmbCC first-class; GCC and Clang required.

## Target progression

Native simulation and AVR (ATmega328P) first, then Cortex-M (Armv6-M to Armv8-M with MPU and TrustZone), then RISC-V with PMP, then symmetric and asymmetric multicore.

## Licence and contributing

Apache-2.0 (see `LICENSE` and `NOTICE`). Contributions are welcome under the Developer Certificate of Origin; see `CONTRIBUTING.md`. Security reports: see `SECURITY.md`.

## Part of the EmbLink platform

EmbLinkRTOS integrates with EmbCC, EmbBuild, EmbDebug, EmbFlash, and EmbStudio while remaining fully usable with third-party compilers, debuggers, probes, and build systems.
