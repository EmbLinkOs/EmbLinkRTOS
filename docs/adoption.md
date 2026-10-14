# First application and migration guidance

**Status:** Development guidance, 2026-10-14. This is not an implemented FreeRTOS or CMSIS-RTOS2 compatibility layer. The first supported release is scoped by the [roadmap](architecture/07-roadmap.md).

## Initial use case

Prepare one interrupt-driven controller on the NUCLEO-F446RE: periodic acquisition, a control thread, UART command/telemetry, contention on a shared peripheral, and retained failure diagnostics. Begin with trusted threads in the base profile. Use the same application later to demonstrate a protected/restartable acquisition subsystem after isolation is implemented.

The baseline must run continuously, handle overload and missed deadlines explicitly, produce diagnostic artifacts for an injected failure, and build with the documented reference toolchains. It is a concrete development target; no such complete application is currently shipped. Physical board availability is not established by the repository.

## Native development today

From the repository root, with CMake, Ninja, GCC and Python/PyYAML available:

```sh
cmake --preset native-gcc
cmake --build --preset native-gcc --parallel 4
ctest --preset native-gcc --output-on-failure --no-tests=error
(cd tools/model && python3 -m unittest discover -v)
```

Hypothesis enables additional randomized model coverage; pytest can run the same model suite. Release and tiny presets exercise different feature/validation configurations. Samples show initialization and first execution, not a complete HAL or product. The virtual-time native port is useful for semantics and cannot supply hardware timing evidence.

## Mapping existing applications

| Existing concept | EmbLinkRTOS direction | Migration checks |
|---|---|---|
| FreeRTOS task | `emb_thread_init`, `start`, `join`/`detach` | Static storage, stack alignment/size, larger number means higher priority, defined exit and reuse |
| Delay/delay-until | Typed duration and `emb_thread_sleep_until` | Wrap half-range on tiny; absolute deadlines; late-job policy |
| Counting/binary semaphore | `emb_sem_take`, `give` | Hand-off prevents barging; unsatisfied NO_WAIT returns `EMB_ETIMEDOUT`; take is thread-only in M1 |
| Mutex | `emb_mutex_lock`, `unlock` | Transitive inheritance within configured depth, multi-lock restore, recursion opt-in, owner-death result |
| Task notification | Notification bits | Bits coalesce; no count/value mode; only owner waits; kernel bits reserved |
| Queue set or poll | Planned source bindings plus one notification wait | One binding per object; one-bit masks; bind/check before waiting and drain readiness; only semaphore binding exists today |
| Queue/event group/timer | SPEC-007/SPEC-005/SPEC-003 target APIs | These APIs are unimplemented; there is no drop-in replacement yet |
| `FromISR` operation | Only functions explicitly annotated ISR-safe | NO_WAIT alone does not authorize an ISR call; rescheduling is automatic at legal exit points |
| Forced task delete | Cooperative cancel and cleanup | No arbitrary asynchronous kill; partition restart is a separate future mechanism |
| Vendor HAL/peripheral calls | Future thin class driver around the supported HAL | Preserve buffer ownership, completion context, partial-transfer and cancellation rules |

A compatibility adapter must list each changed behavior; passing an application's existing tests is necessary but insufficient if it relies on ordering, notification counters, timer command queues or forced deletion. Run upstream validation suites where available plus targeted semantic-difference tests. Do not advertise unchanged middleware until it has actually run.

## Early adoption deliverables

Deliver the migration guide, one application and one focused adapter at a time after the underlying APIs exist. CMSIS-RTOS2 is first because the architecture selects it for vendor middleware; FreeRTOS API migration follows. Initial adapters can be preview scope before 1.0, outside any future qualification profile. Document implemented subsets and unsupported operations explicitly; adapters must not change native semantics.

The hardware-data proof begins with one STM32F4 SoC and two boards, a small GPIO/UART/timer/clock set, reproducible generated layout and errors naming the failing node. A board on an already supported SoC should need description data rather than peripheral C changes; a genuinely different SoC may require drivers/startup. Importers produce drafts requiring review: SVD does not encode all pin, clock, DMA, power or errata facts. No fixed half-day porting promise is made.
