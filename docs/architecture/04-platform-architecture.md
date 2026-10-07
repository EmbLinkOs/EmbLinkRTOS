# 04 - Platform Architecture

**Status:** PROPOSED unless marked. This document turns the v0.1 PLANNED and FUTURE platform sections into concrete designs.

---

## 1. Hardware description pipeline

### 1.1 Decision (ADR-004)

**Status: LOCKED.** Accepted by the project owner on 2026-10-06.

Hardware is described in a **project-owned, schema-validated YAML** model with two levels: SoC descriptions and board descriptions. A generator consumes them together with the configuration and emits everything hardware-specific that would otherwise be hand-written C.

The model borrows what works from DeviceTree (the `compatible` string binding a node to a driver, hierarchical buses, phandles as references) and from CMSIS-SVD (register-level detail for tooling) without adopting their syntax. **Importers** exist for CMSIS-SVD (peripheral instances, IRQ numbers, memory map) and for DeviceTree source (to bootstrap from Zephyr or Linux board files), so that authors curate rather than transcribe.

### 1.2 Model

```
soc/<vendor>/<family>/<part>.yaml
  cpu:        arch, core, revision, fpu, mpu/pmp regions, cache, trustzone, cores[]
  memory:     regions[] {name, base, size, attrs}
  clocks:     sources, plls, dividers, gates (clock tree)
  interrupts: controller, count, priority bits
  peripherals[]:
    name, compatible, base, irqs[], clock, dma_requests[], reset_line, power_domain
  dma:        controllers, channels, request map
  flash:      geometry, erase units, write granularity
  errata[]:   id, affected revisions, workaround flag

boards/<vendor>/<board>.yaml
  soc:        reference
  oscillators: external sources and frequencies
  pins:       pinmux assignments per peripheral instance
  devices[]:  external devices {compatible, bus, address, gpios, power}
  console:    which UART or debug transport
  flash_partitions[]: bootloader, slot0, slot1, storage, crash
  leds, buttons, connectors
  defaults:   Kconfig defaults (console baud, enabled devices)
```

### 1.3 Generated outputs

| Output | Consumer |
|---|---|
| `soc_devices.c` device instance table, interrupt vector bindings, DMA bindings | drivers, kernel init |
| `soc_clocks.c` clock tree configuration sequence | SoC startup |
| `board_pins.c` pin configuration | board init |
| `regions.ld` linker fragments for memory regions, partitions, retained areas | linker |
| `partitions.c` static partition and capability tables | kernel (isolated profiles) |
| `hw_config.h` constants: core count, region bases, IRQ numbers | everything |
| `Kconfig.board` defaults | configuration |
| `board.md` reference page | documentation |
| `hw.json` normalized model | EmbStudio, EmbDebug, HIL tooling |

**HW-001** Every peripheral instance, interrupt binding, clock setting, pin assignment, memory region, and flash partition shall originate in the hardware description.
**HW-002** The hardware model shall be validated against a published schema before generation; validation errors shall name the file and node.
**HW-003** Generated files shall be reproducible and never edited by hand.
**HW-004** Importers for CMSIS-SVD and DeviceTree source shall exist to bootstrap SoC and board descriptions; the SVD importer builds on EmbCC's `embsvd` parser (09 §9).
**HW-005** The normalized model shall be exported in a stable JSON form for tooling.
**HW-006** From the same region model, the generator shall emit both a GNU ld linker script and an `embld` option set (`-Ttext`, `-Tdata`, `--rom-limit`), because `embld` accepts linker scripts on ARM and RISC-V but not on AVR (09 §7); the two outputs shall be tested for agreement.

## 2. Device model

### 2.1 Device instance

```
device:
  name, compatible
  ops            -> driver class operations table
  config         -> generated, const (addresses, IRQs, pins, clocks)
  state          -> RAM: lifecycle, power state, driver private data
  deps[]         -> devices that must be initialized first
  partition      -> owning partition (isolated profiles)
```

### 2.2 Lifecycle

```
UNINIT -> INITIALIZING -> READY <-> SUSPENDED
                 |          |
                 v          v
              FAILED     FAILED
```

Initialization order is computed by the generator from `deps` (clock before UART, bus before device-on-bus) and emitted as a static ordered table. No runtime dependency resolution. A device that fails initialization is marked FAILED, logged, and does not block unrelated devices.

**DRV-001** Device initialization order shall be computed at build time from declared dependencies.
**DRV-002** Device lifecycle states shall be observable through a common API and through the debug descriptor.
**DRV-003** A device in FAILED state shall reject operations with a defined status rather than undefined behavior.

## 3. Driver model

### 3.1 Request and completion (ADR-007)

Every driver class is specified as a **request/completion** interface. A request carries a buffer, parameters, and a **completion target**. The completion target is one of: notification bits on a thread, a semaphore, a work item, or (ISR-safe, bounded) a callback. Synchronous APIs are thin wrappers: submit, wait on a notification, return.

```
  app: emb_uart_write(dev, buf, len, EMB_WAIT_FOREVER)
         |
         v  (wrapper)
       request { buf, len, completion = notify(self, bit) }
         |
         v
     driver.submit(dev, &request)   -- programs DMA or starts ISR-driven transfer
         |
   ISR completes -> driver marks request done -> emb_notify_set(thread, bit)
         |
       wrapper wakes, returns status
```

Why this matters:
- the same driver source serves polling, interrupt, and DMA transfer modes;
- completion delivery works into an unprivileged partition (notification is a capability-checked kernel op);
- across a core boundary, the request can be forwarded over a port without changing the class API;
- cancellation and timeouts have one implementation in the wrapper.

**DRV-004** Every driver class shall define its operations as request submission with asynchronous completion; blocking variants shall be wrappers.
**DRV-005** Completion delivery shall support notification, semaphore, work item, and bounded ISR callback targets.
**DRV-006** Request cancellation and timeout shall be defined per class, including what happens to a partially completed DMA transfer.

### 3.2 Driver contract checklist (LOCKED from v0.1, extended)

Each class specifies: initialization dependencies, request types, blocking wrappers, ISR behavior, DMA ownership and cache maintenance, concurrency (per-device lock or lock-free), timeout behavior, power transitions (`suspend`, `resume`, `runtime_idle`), error reporting, capability discovery, and isolation placement (kernel partition or own partition).

### 3.3 Class catalog

1.0: GPIO and pin control, interrupt controller, clock and reset, timers and counters, UART, SPI, I2C, DMA, watchdog, flash, RTC, entropy source.
Post-1.0: ADC, DAC, PWM, CAN and CAN-FD, I2S, SD/MMC, Ethernet MAC and PHY, USB device and host controllers, sensors, displays, input, crypto accelerators, PCIe where applicable.

## 4. Power management

### 4.1 Model

```
  threads declare latency constraints  (max tolerable wake latency)
  timer subsystem exposes next deadline
  devices report runtime idle / busy
            |
            v
  power policy: pick the deepest system state whose exit latency fits
                the tightest constraint and whose entry+exit cost is
                recovered before the next deadline
            |
            v
  devices suspend (ordered) -> arch idle/sleep -> wake source -> devices resume -> scheduler
```

System power states are SoC-defined and mapped to a generic ordered set: `ACTIVE`, `IDLE` (clock gating, instant wake), `SLEEP` (core off, RAM retained), `DEEP_SLEEP` (most domains off, retained region only), `OFF` (RTC or wake pin only). Device power states: `ACTIVE`, `LOW_POWER`, `SUSPENDED`, `OFF`.

**PWR-001** Threads and drivers shall be able to declare wake latency constraints; the policy shall never choose a state violating an active constraint.
**PWR-002** Device suspend and resume order shall follow the inverse of initialization order unless a device declares otherwise.
**PWR-003** Power state residency shall be counted and exposed for energy accounting.
**PWR-004** Power management shall be a compile-time option; disabled, the idle path is the arch idle instruction only.
**PWR-005** Dynamic voltage and frequency scaling, clock gating per peripheral, and power domains are SoC capabilities expressed in the hardware description and used by the policy where present.

## 5. Boot, images, and update

### 5.1 Chain

```
ROM -> bootloader (verifies, selects, optionally decrypts) -> EmbLinkRTOS image(s) -> partitions start
```

### 5.2 Decision (ADR-008)

The default image format and manifest are **MCUboot-compatible** (image header, TLV trailer, slot semantics, swap or overwrite or direct-XIP upgrade modes). EmbLinkRTOS does not require MCUboot itself; any bootloader honoring the format works, and a project-owned minimal bootloader may be built on the same kernel later.

**BOOT-001** Images shall carry a header with version, size, flags, and a signature TLV in an MCUboot-compatible layout.
**BOOT-002** A/B slot update with confirm-or-revert, anti-rollback counters, and power-loss-safe swap shall be supported by the storage layout and the update service.
**BOOT-003** Boot measurements (hashes of each stage) shall be available to the runtime for attestation when a root of trust exists.
**BOOT-004** Multi-image boot (several cores, several partitions loaded separately) shall be expressible in the manifest.
**BOOT-005** Boot time budget shall be measurable and published per reference board.

## 6. Security architecture

### 6.1 Threat model (new)

Assets: firmware integrity and confidentiality, device identity keys, user and operational data, availability of real-time functions, debug access.
Adversaries: remote network attacker, local attacker with physical access (debug port, bus probing), malicious or compromised third-party component inside the image, supply-chain attacker on build inputs.
Out of scope for the kernel: invasive silicon attacks; these are mitigated by SoC features and product design.

### 6.2 Mechanisms

| Layer | Mechanism |
|---|---|
| Boot | Signed images, measured boot, anti-rollback, debug port lock policy |
| Kernel | Capabilities, partitions, syscall argument validation, W^X regions, stack protection (canaries, limit registers, MPU guards), pointer authentication and branch target identification where available (Armv8.1-M), per-partition budgets against denial of service |
| Crypto | PSA Crypto-shaped API over software (reviewed libraries) or hardware backends; keys referenced by handle, never by pointer; entropy source as a device class |
| Identity | Device identity and attestation service built on DICE-style measured boot where the SoC provides a unique device secret; falls back to provisioned keys |
| TrustZone (Armv8-M) | EmbLinkRTOS runs in the non-secure world; a PSA-style secure partition manager (such as TF-M) or a minimal project-owned secure firmware runs in the secure world; the call interface is the PSA Functional API shape |
| Update | Authenticated, versioned, power-loss-safe, with rollback protection |
| Process | Threat model maintained, SECURITY.md disclosure policy, advisories, SBOM per release, pinned toolchains, reproducible builds, dependency tracking |

**SEC-006** The project shall maintain a written threat model that each security requirement traces to.
**SEC-007** Cryptographic keys shall be referenced through handles with usage policy; raw key material shall not cross the public API.
**SEC-008** Every release shall ship a software bill of materials (SPDX or CycloneDX) covering kernel, middleware, and generated code inputs.
**SEC-009** Builds shall be reproducible from a pinned toolchain description; release verification shall rebuild and compare.
**SEC-010** A vulnerability disclosure process and support window per release line shall be published before 1.0.
**SEC-011** Debug access policy (open, authenticated, locked) shall be a provisioning-time decision supported by the boot chain.

## 7. Observability

### 7.1 Deferred-format logging (ADR-009)

Log calls do not format on target. The format string and metadata (level, file, line, argument types) are placed in a non-loaded ELF section and replaced by a 16- or 32-bit identifier. The target emits identifier plus raw arguments. The host decoder (EmbDebug or a standalone tool using the ELF) reconstructs the text. Typical savings are an order of magnitude in flash and bandwidth, and logging becomes cheap enough to leave on in production.

A plain-text backend remains available for the `tiny` profile without a host decoder, selected at compile time with the same call sites.

**OBS-001** Logging shall support deferred formatting with string interning in a non-loaded section, decodable from the ELF.
**OBS-002** Log severity shall be filterable at compile time per module and at runtime per module.
**OBS-003** ISR logging shall be non-blocking and bounded; drops shall be counted, not hidden.
**OBS-004** Log transports shall include UART, SWO/ITM, RTT-like memory channel, USB, retained RAM ring, and network, selected by configuration.

### 7.2 Tracing

Kernel trace points (context switch, state transitions with reasons, priority changes, inheritance, ISR enter and exit, timer events, object operations, budget events, power transitions, partition faults) emit compact binary events to a per-CPU ring buffer. The on-wire format is **Common Trace Format (CTF)** with a generated metadata description, so Trace Compass, Babeltrace, and Perfetto (through conversion) read it without a proprietary viewer. EmbDebug reads the same stream.

**OBS-005** Trace points shall compile to nothing when disabled and to a bounded store when enabled.
**OBS-006** Trace output shall be CTF with generated metadata; EmbDebug shall not be required to decode it.
**OBS-007** Trace shall support streaming (probe, UART, network) and snapshot (ring buffer dumped on fault or demand) modes.

### 7.3 Flight recorder and crash records

The last part of the trace ring lives in a retained region and is included in the crash record (03 §10.2). A boot after a fault can upload the record. This is how field failures become diagnosable.

### 7.4 Kernel debug descriptor (ADR-011)

The image exports a versioned, read-only descriptor: struct offsets for TCB fields, ready structure layout, per-CPU state, object type tags, partition table location, trace ring location, and build identity. RTOS-aware debuggers (EmbDebug, OpenOCD, pyOCD, probe-rs, commercial probes) read the descriptor instead of hard-coding offsets per kernel version.

**OBS-008** The image shall export a versioned debug descriptor sufficient to enumerate threads, states, priorities, stacks, owned locks, partitions, and devices without knowledge of private struct layout.

### 7.5 Runtime statistics

Per thread: CPU time, switch count, stack high-water, deadline misses, budget overruns. Per CPU: utilization, idle residency, max critical section, interrupt counts. Per object: contention counts. All optional, all compile-out.

## 8. Multicore

### 8.1 SMP (FUTURE, constraints LOCKED)

- Per-CPU state already exists; SMP adds spinlocks, atomics, inter-processor interrupts, affinity masks, and a cross-CPU reschedule protocol.
- Ready structure strategy: begin with a single global ready structure under one scheduler spinlock with affinity support (simplest to make correct, measurable); move to per-CPU structures with migration only if measurements justify it (ADR-012).
- Lock ordering rules and interrupt-masking-while-holding rules are documented before the first SMP line is written.

### 8.2 AMP (new)

Each core runs its own image (possibly a different profile, possibly a different kernel). Cores communicate through **ports** over a transport: shared-memory rings in a region visible to both plus a doorbell interrupt or mailbox peripheral described in the hardware model. An OpenAMP/rpmsg-compatible transport is an option for interoperability with Linux on application cores; it is not the native model.

**MC-001** Heterogeneous and asymmetric multicore shall be supported by per-core images sharing the hardware description, with inter-core ports using standard IPC semantics.
**MC-002** Shared regions, mailboxes, and doorbells shall be described in the hardware model.
**MC-003** A core may be designated as the boot master that loads and releases other cores according to the manifest.

### 8.3 Accelerators and co-processors

DSPs, NPUs, crypto engines, and secure elements are devices with request/completion interfaces. Where they run firmware, that firmware is an image in the manifest. No special kernel concept.

## 9. Native simulation and virtual platforms

Three complementary layers, all in CI:

| Layer | Tool | Purpose |
|---|---|---|
| Native port | `arch/native` | Kernel semantics, sanitizers, fuzzing, reference-model differential tests; runs in seconds |
| Instruction-level emulation | Renode (preferred for MCU peripheral models), QEMU (including EmbCC's own harness boards: `lm3s6965evb`, `mps2-an386`, `mps2-an500`, `mps2-an505`, micro:bit, RISC-V `virt`, AVR) | Real arch port code, real interrupt controllers, multi-node and multicore scenarios without hardware |
| Hardware-in-the-loop | Real boards through EmbFlash and third-party probes | Timing truth, peripherals, power |

**SIM-001** The native port shall pass the full kernel conformance suite.
**SIM-002** Each supported architecture shall have at least one emulated target in CI.
**SIM-003** Emulated results shall never be published as timing evidence.

## 10. Storage and filesystems (FUTURE, interfaces PLANNED)

Block device and flash device class APIs, a partition table consistent with the boot manifest, and filesystem integration (littlefs for raw flash, FAT for interchange) through a filesystem abstraction. Power-loss safety is a property of each layer's contract and is tested by fault injection on the native port and by power cycling on HIL.

## 11. Networking, USB, and middleware (FUTURE, contracts PLANNED)

EmbLinkRTOS integrates maintained third-party stacks (for example lwIP, mbedTLS or wolfSSL, TinyUSB or a project USB stack later) through a documented **OS abstraction contract**: threads, synchronization, memory pools, timers, and a network interface driver class with explicit packet buffer ownership. The kernel never grows protocol knowledge. Zero-copy RX and TX paths use buffer pools with ownership transfer (03 §6.5).

## 12. Compatibility layers (FUTURE)

CMSIS-RTOS2 for Arm ecosystems; a POSIX subset (threads, mutexes, condition variables, clocks, semaphores, later sockets) for middleware portability. Both are mappings; neither changes native semantics or is required on tiny targets.
