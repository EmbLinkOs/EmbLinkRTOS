# 02 - System Architecture

**Status:** LOCKED layering from v0.1; PROPOSED additions marked.

---

## 1. Layered view

```
+------------------------------------------------------------------+
|  Applications                                                    |
+------------------------------------------------------------------+
|  Middleware / Services   (net, fs, usb, crypto, protocols, ...)  |
+------------------------------------------------------------------+
|  Compatibility layers    (CMSIS-RTOS2, POSIX subset, C++ RAII)   |
+------------------------------------------------------------------+
|  Public RTOS API         (emb_*)  -- the stable contract         |
+------------------------------------------------------------------+
|  Kernel                                                          |
|    exec | sched | wait | time | sync | ipc | object/cap | part   |
|    mem  | fault | work | notif| power-core | trace-core          |
+------------------------------------------------------------------+
|  Architecture Port       (emb_arch_*)  avr | cortex_m | riscv |  |
|                                        native | ...              |
+------------------------------------------------------------------+
|  SoC Support             generated tables + SoC startup + errata |
+------------------------------------------------------------------+
|  Board Support           generated wiring + board init           |
+------------------------------------------------------------------+
|  Device Drivers          class API + implementations             |
+------------------------------------------------------------------+
|  Hardware                                                        |
+------------------------------------------------------------------+

 Cross-cutting (data, not layers):
   Hardware Description -> Generator -> SoC/Board tables, linker
   Configuration (Kconfig) -> generated config headers
   Requirements <-> Tests <-> Evidence
```

The layering is **LOCKED**. The new elements relative to v0.1 are: the explicit *compatibility layer* position, the kernel subsystems `object/cap`, `part` (partitions), `work`, `notif`, `power-core`, `trace-core`, and the *native* architecture port as a peer of hardware ports.

## 2. Layer responsibilities

| Layer | Owns | Must never |
|---|---|---|
| **Kernel** | Thread lifecycle, scheduling classes, wait and wake protocol, time and timeouts, synchronization, IPC, object and capability model, partitions, memory policy, fault policy, work queues, notifications, trace and power hooks | Include an architecture, SoC, or board header; know a register address; allocate dynamically on a real-time path |
| **Architecture port** | CPU context, first-thread launch, context switch, interrupt and exception entry and exit, critical sections and masking, atomics and barriers, privilege transitions, MPU or MMU programming primitives, architecture timer hooks, idle instruction, fault context decode, SMP boot and inter-processor interrupts | Decide scheduling policy; touch a ready queue; know which thread is highest priority |
| **SoC support** | Memory map, clock tree, reset controller, interrupt controller instance, peripheral instances with addresses, IRQ numbers, DMA requests, flash geometry, errata workarounds, SoC startup | Contain a driver implementation; know about a board's pins or external devices |
| **Board support** | Oscillators, pin routing, external devices, connectors, LEDs and buttons, console wiring, boot straps, flash partitions, board init sequence | Contain a kernel or SoC copy |
| **Device drivers** | Class interfaces (operations tables) and concrete implementations for peripheral types | Hard-code an instance address; assume a specific board |
| **Public API** | Stable C contract, documented per function: blocking class, ISR safety, timing class, ownership rules | Expose internal structure layout |
| **Compatibility** | Mapping of foreign APIs onto native services | Change native semantics |
| **Middleware** | Protocol and service stacks integrated via a documented OS abstraction contract | Reach below the public API or driver class API |
| **Toolchain layer** | Compiler, assembler, linker invocation; attributes; section handling; LTO; startup and linker script integration | Leak compiler-specific syntax into generic kernel source |

## 3. Concept vocabulary

This vocabulary is used consistently across all documents. New concepts are **PROPOSED**.

| Concept | Definition | Scale-down form |
|---|---|---|
| **Thread** | Independently schedulable execution context with its own stack, scheduling state, wait state, and identity | Unchanged |
| **Partition** *(new)* | Unit of isolation: a set of threads, a memory map, a capability table, an optional CPU budget or schedule window, and a fault policy | Exactly one partition, zero code |
| **Capability** *(new)* | Unforgeable reference to a kernel object plus a rights mask; the public handle *is* a capability | A pointer; rights checks compile out |
| **Kernel object** | Thread, mutex, semaphore, queue, timer, event, notification target, work queue, port, partition, memory region, device | Unchanged |
| **Scheduling class** *(new)* | Policy that orders READY threads: `FIXED_PRIORITY` (mandatory), `DEADLINE`, `TIME_TABLE`, `IDLE` | Only `FIXED_PRIORITY` and `IDLE` |
| **Budget** *(new)* | Execution-time allowance per thread or partition with a replenishment period and an overrun policy | Compiles out |
| **Notification** *(new)* | Per-thread set of event bits, settable from any context, waited by the owner | Unchanged; the cheapest primitive |
| **Work queue** *(new)* | Kernel thread that executes queued, intrusive work items in order | One system work queue; may be disabled |
| **Wait object** | Internal queue of blocked threads with deterministic ordering and wake results | Unchanged |
| **Port** *(new)* | Message endpoint for IPC across a partition or core boundary with copy or ownership-transfer semantics | Compiles out |
| **Device** | A driver bound to one hardware instance with generated configuration, lifecycle state, and power state | Unchanged |
| **Region** *(new)* | A named range of memory with attributes (executable, DMA-capable, cacheable, retained, secure) known to linker and kernel | A linker symbol |
| **Domain, clock domain, power domain, time domain** | Hardware groupings; never used for isolation | n/a |

## 4. Kernel subsystem map

```
                       +-----------+
                       |  public   |
                       |   API     |
                       +-----+-----+
                             |
   +------------+------------+-------------+-------------+
   |            |            |             |             |
+--v---+   +----v----+  +----v----+   +----v-----+  +----v-----+
| sync |   |   ipc   |  |  timer  |   |  thread  |  |  object  |
|mutex |   | queue   |  | sw timer|   | lifecycle|  |  / cap   |
|sem   |   | port    |  | timeout |   | join/can |  |  tables  |
|event |   | notif   |  +----+----+   +----+-----+  +----+-----+
+--+---+   +----+----+       |             |             |
   |            |            |             |             |
   +------+-----+------------+------+------+-------------+
          |                         |
     +----v-----+              +----v------+          +----------+
     |   wait   |<-------------+   sched   +--------->|   part   |
     | protocol |              | classes   |          | budgets  |
     +----+-----+              | runqueues |          | regions  |
          |                    +----+------+          +----------+
          |                         |
     +----v-------------------------v------+
     |          per-CPU state              |
     | current | idle | lock | irq | resch |
     +----------------+--------------------+
                      |
              +-------v--------+
              | emb_arch_* port|
              +----------------+

 side services:  work queues | fault manager | trace core | power core | mem (pools, regions)
```

Dependency direction is strict: `sync`, `ipc`, `timer`, and `thread` may *call* `wait` and `sched`; they never touch run-queue or wait-queue internals. `sched` depends only on `arch` abstractions and `part` for eligibility. `object/cap` is used by every API entry point to resolve handles.

## 5. Dependency rules

**LOCKED**, with additions:

1. `kernel/` includes only `kernel/`, `include/emb/`, and `arch/<arch>/include/` through the `emb_arch_*` contract.
2. `arch/` never includes `soc/` or `boards/`. SoC startup calls into the arch port; not the reverse.
3. `drivers/` depends on the driver class API and generated SoC tables; never on `boards/`.
4. `subsys/` (middleware) depends on the public API and driver class APIs only.
5. `compatibility/` depends on the public API only.
6. Generated code is never edited by hand and is reproducible from the hardware description and configuration.
7. Dependency direction is enforced by the build (include path isolation per layer) and by a CI check, not by convention.

## 6. Execution contexts

Four execution contexts exist. Every public API documents in which it may be called.

| Context | Description | May block | May call ISR-safe API | May call thread-only API |
|---|---|---|---|---|
| **Thread** | Normal scheduled execution | yes | yes | yes |
| **ISR** | Interrupt or exception handler | never | yes | never (kernel fault in checked builds) |
| **Kernel-independent ISR** | High-priority interrupt above the kernel masking level (where hardware supports it) | never | never | never |
| **Pre-kernel** | Before the scheduler starts | never | subset: object init | never |

Work queue callbacks and software timer callbacks run in thread context (on a kernel worker thread) and may block, subject to the documented latency consequences for that queue.

## 7. Target profiles

**PROPOSED.** A profile is a named, tested configuration bundle. It is how support claims and footprint claims are stated. Profiles are configuration presets, not forks.

| Profile | Typical target | Partitions | Capabilities | Classes | Trace | Notes |
|---|---|---|---|---|---|---|
| **tiny** | ATmega328P, Cortex-M0+ with 4 to 8 KB RAM | 1 | pointer | FP + IDLE | off or minimal | 8 or 16 priority levels, 32-bit time option |
| **base** | Cortex-M3/M4/M33, RISC-V RV32IMAC, 32 to 256 KB RAM | 1 | pointer with generation check | FP + IDLE | logging + light trace | 64-bit time, work queues, software timers |
| **isolated** | Cortex-M with MPU, RISC-V with PMP, Cortex-M33 with TrustZone | N | table-indexed, validated | FP + IDLE (+ budgets) | full | Syscall boundary, supervisor partition |
| **multicore** | Dual Cortex-M7/M4, RP2350, SMP RISC-V | N | validated | FP + IDLE, optional DEADLINE / TIME_TABLE | full | SMP or AMP or both |
| **native** | Host process (Linux, macOS, Windows) | N (simulated) | validated | all | full | Virtual time, fault injection, sanitizers |

Every profile passes the same conformance suite. Profile-specific tests add coverage; they never replace it.

## 8. The native architecture port

**PROPOSED.** `arch/native` implements the `emb_arch_*` contract on a host operating system:

- each EmbLinkRTOS thread maps to a host thread, with a gate ensuring exactly one is running per simulated CPU, which preserves kernel scheduling semantics exactly;
- interrupts are injected as signals or events that preempt the running simulated thread at the gate;
- time is virtual by default (advances only when all simulated CPUs are idle), so tests are deterministic and fast; wall-clock mode is available for interactive use;
- fault injection, sanitizers (address, undefined behavior, thread), coverage, and fuzzing run here;
- the host abstraction is minimal (threads, a condition variable, a monotonic clock) so that non-POSIX hosts are supported, matching EmbCC's own host strategy.

The native port is a real port, held to the same conformance suite as hardware ports. It is never a substitute for hardware validation (v0.1 TEST-007 stands).

## 9. How a new board arrives

This is the operational test of the architecture.

1. If the SoC is new: import its CMSIS-SVD or vendor data into a SoC description, curate it, and add SoC startup and errata. Drivers for peripheral types already supported need no changes.
2. Write the board description: oscillators, pin routing, external devices, console, flash partitions.
3. Run the generator. It emits device instance tables, interrupt bindings, clock configuration, pin configuration, memory regions, linker fragments, Kconfig defaults, and the board's reference documentation page.
4. Add the board's smoke test to the hardware-in-the-loop matrix.
5. No kernel, arch, or driver source changes.

If step 5 is violated for a board on an already-supported SoC, the architecture has a bug.
