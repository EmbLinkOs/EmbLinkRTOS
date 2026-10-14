# Choosing current APIs

This guide describes the current 0.2 development implementation. The authoritative [Project status](../STATUS.md) records evidence and limits; specifications describe both current mechanisms and future targets. Requirement cards marked Accepted record a design decision, not a passing implementation test.

| Goal | Available now | Still planned or limited |
|---|---|---|
| Start and coordinate threads | Static storage, priorities, start, sleep, join/detach, cooperative cancellation | Isolated partitions, budgets, restart, SMP |
| Protect or signal work | Mutexes, semaphores, per-thread notifications | Event groups, condition variables, barriers |
| Transfer payloads | Application-owned data with an explicit synchronization protocol | Message queues, pipes, buffer pools |
| Run periodic work | A thread using absolute sleep deadlines | Software timers and system work queues |
| Wait on several readiness sources | Semaphore binding plus notification bits and rechecking | General object bindings, unbind, restart-safe target lifetime |
| Diagnose failures | State/priority queries, optional stack estimates, synchronous trace and fault hooks | Retained crash formats, CTF rings, full host tooling |
| Develop without a board | Native Linux/POSIX virtual time and conformance suite | Physical-board timing, peripheral drivers and HIL evidence |
| Deploy a controller platform | Embedded kernel ports and QEMU configurations | Complete board/peripheral/application integration and release qualification |

The public C API reference is extracted from actual headers. Some declarations depend on configuration or link to a broader requirement than the current subset implements. Missing services are not supplied by enabling a configuration flag or copying a signature from a specification. Use [Configuration](configuration.md) to select available features and the API's `@config`/`@ctx` contract before using it.

The first user tutorials target native development: [threads and periodic work](tasks.md), [synchronization and notifications](synchronization.md), and [debugging](debugging.md). They avoid assuming a driver stack, an isolated profile or a released board support guarantee. Porting an application to a physical board requires its startup, console, clock, interrupt, stack and peripheral integration to be validated separately.
