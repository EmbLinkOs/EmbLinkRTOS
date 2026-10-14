# Core concepts

- A **thread** is a schedulable context with its own stack. Application priorities are positive; larger values are more urgent. Yield lets equal-priority peers run and does not authorize lower-priority work.
- **Static storage** belongs to the caller. The build generates object storage size/alignment for the configuration. Initialize the kernel and objects, then start scheduling; initialization-table objects are constructed during `emb_kernel_init()`.
- A **wait** completes with one result: satisfaction, timeout, cancellation or deliberate destruction. Resource hand-off reserves the satisfied mutex/semaphore unit for the chosen waiter rather than making it race another consumer.
- A **mutex** has an owner and optional inheritance/ceiling/recursion. Inheritance and deadlock detection have depth limits. A semaphore has no owner and is not a substitute for a mutex protecting shared state.
- A **notification** is a bit set, not a counter or a message. Repeated sets coalesce. The owner waits; ISR-safe code can set application bits. Semaphore binding takes a one-bit mask; bind/check existing readiness and drain it before waiting again.
- **Duration**, **instant** and **timeout** are distinct types. Convert a duration with `EMB_TIMEOUT()` for a timeout parameter. Periodic work uses absolute deadlines; tiny's 32-bit absolute comparisons have a half-range constraint.
- **Context annotations** decide whether an operation is ISR-safe. Passing `EMB_NO_WAIT` alone does not grant ISR permission. Blocking under a caller-held critical section or scheduler lock is misuse.

The current POINTER handle model provides type/lifecycle diagnostics in checked builds and no capability-right security. Partitions, TABLE handles and restart are future protection mechanisms. [Target specifications](../specs/index.md) give detailed contracts; [Project status](../STATUS.md) distinguishes their implemented portions.
