# Port: native (`arch/native`)

**Specification:** SPEC-013 (port), SPEC-011 (contract).
**Status (M1):** POSIX backend; one host thread per EmbLinkRTOS thread behind a gate; deterministic interrupt delivery at gate crossings; virtual time advancing only in idle; periodic and tickless modes; host fault handling and the asynchronous signal mode come with later milestones.
**Verified with:** GCC 13 and Clang 18 on Linux x86-64, checked and release build kinds, base and tiny profiles, AddressSanitizer, UndefinedBehaviorSanitizer, and ThreadSanitizer, each running the whole conformance suite and the first-execution sample (two million switches).

## Gate and threads

The gate is a host mutex held by whichever host thread runs simulated code, plus one condition variable per thread in the control block's 128-byte port extension. `emb_arch_switch_to(next)` records the running thread's mask state, wakes `next`'s host thread (creating it on first dispatch), and parks the caller on its own condition variable; exactly one simulated thread runs at any time, so the kernel's scheduling decisions are exact (SPEC-013 §4). A fresh host thread enters `embk_thread_launch(entry, arg)` with interrupts enabled. Thread exit hands the gate over and ends the host thread (`emb_arch_switch_final`); the gate release is the switch-out completion. The host main thread becomes the idle context at kernel start; with `CONFIG_EMB_IDLE_THREAD=y` the kernel's idle entry runs on it.

Host stacks are 256 KB (`CONFIG_EMB_NATIVE_HOST_STACK`) and unrelated to the configured thread stack sizes; stack statistics report a high-water mark of zero (SPEC-013 §13 decision 7).

## Interrupts

`emb_arch_irq_lock()` sets the gate's masked flag and returns the previous value. Interrupts are events in a per-source pending table (`CONFIG_EMB_NATIVE_MAX_IRQS` sources, number 0 reserved for the virtual timer); they are delivered, lowest number first, at every gate crossing while unmasked: `emb_irq_unlock()` to an unmasked state, `emb_arch_idle()`, `emb_native_yield_point()`, `emb_native_irq_raise()` itself, and the resumption of a thread. A handler runs on the current host thread with the mask set and the kernel's nesting depth incremented; the switch the kernel decides at exit happens before the interrupted thread continues (P1). Handlers are bound with `emb_irq_connect()` (`CONFIG_EMB_IRQ_DYNAMIC` is the native default); `EMB_ISR(name)` declares a static handler function taking the connect argument.

## Time

Virtual time is a raw counter in tick units (`emb_arch_timer_hz()` is the tick rate). It advances only in `emb_arch_idle()`: with pending interrupts they are delivered; otherwise the counter jumps to the programmed deadline and the timer interrupt is raised; with no deadline and nothing pending the run is finished or deadlocked and the port reports it and exits with status 3 (SIM-005) unless `emb_native_set_idle_hook()` installed a hook. Timeout tests therefore run in microseconds of wall time with exact tick arithmetic (`EMB_TEST_EXACT_TIME`). Wall-clock mode is not implemented in M1.

## Harness facilities

`emb_native_set_idle_hook(hook)` runs the hook when nothing is runnable, nothing is pending and no deadline is programmed (instead of the deadlock exit); `emb_native_set_deadline_filter(filter)` lets a harness refuse the idle-time jump to a deadline (the differential runner keeps a "sleep forever" asleep that way); `emb_native_tick()` advances virtual time by one tick and raises the timer interrupt, pending if called with interrupts masked; `emb_native_irq_raise(irq)` raises a software interrupt. The differential runner (`tests/differential/`) uses all four to execute the reference model's scenarios under a chosen schedule (`tools/model/README.md`).

## Faults and exits

A kernel fault prints its class, code, argument, thread and location to stderr and ends the process with status 2 (`EMB_NATIVE_EXIT_FAULT`); the test framework's fault expectations fork the process and check that status (TEST-010). `emb_native_exit(code)` ends a run with the given status.

## Conformance

All nine suites of `tests/conformance/` pass on every leg listed above; `ctest --preset <leg>` runs them. Sanitizer legs use GCC's runtimes by default (`EMB_SANITIZER_CC` selects Clang where `libclang-rt` is installed), and ThreadSanitizer runs with `die_after_fork=0` because misuse tests fork.
