# Debugging and diagnosing failures

Start with the checked native configuration and a finite reproduction. It can exercise scheduling, waits and misuse diagnostics without a board. Native virtual time, host-managed stacks and deterministic IRQ injection do not reproduce physical interrupt latency, target stack exhaustion or asynchronous hardware races.

## Narrow a failing run

From the repository root, after activating the native tools:

```sh
cmake --preset native-gcc
cmake --build --preset native-gcc --parallel 4
ctest --preset native-gcc -N
ctest --preset native-gcc -R '^test_thread$' --output-on-failure -V
```

Replace `test_thread` with `test_mutex`, `test_notify` or the affected area. `-R` selects a CTest executable, not an individual `EMB_TEST` case. Read its case-level results and skips. If no test matches, the command does not establish a pass; inspect `ctest -N` and use `--no-tests=error` in automation.

Record the revision, preset, generated `build/native-gcc/.config`, compiler version, executable, command and complete output with a reproduction. The generated JSON contains configuration origins. Reproduce in release when the difference concerns misuse: checked builds raise faults, while applicable release paths return status codes. Handle expected wait results explicitly instead of wrapping every operation in `EMB_CHECK`. That macro faults on non-OK only in checked builds; in release it evaluates to its argument, so explicitly handle failures there. Use `(void)EMB_CHECK(status)` when checking an already-computed status and intentionally discarding its value, to avoid an unused-value warning in release.

For a differential bridge failure, use the path printed after `saved failure`. The [evidence workflow](../engineering/differential-evidence.md) explains offline/live replay and bounded schedule reduction; its bundles preserve the scenario, placements, output and identities automatically.

An available GDB can run a native executable directly:

```sh
gdb --args build/native-gcc/tests/conformance/test_thread
```

Set a breakpoint at `embk_fault_raise` for misuse or at the function under investigation. Native misuse tests fork children: use `set follow-fork-mode child` when the failure is in an expected-fault child. GDB is an optional host tool; it is not required by the documentation or native build setup. Sanitizer presets need their compiler runtimes and are separate checks.

## Inspect a thread

Use `emb_thread_state()` for lifecycle and wait reason, `emb_thread_get_priority()` for base priority and `emb_thread_get_effective_priority()` for inherited/ceiling priority. `emb_thread_name()` returns a borrowed name when names are enabled. On tiny, `emb_thread_index()` is the current table slot and can change with base priority; do not use it as a durable object identity.

`emb_thread_stack_info()` reports configured bytes and, with `CONFIG_EMB_STACK_STATS`, a fill-pattern estimate. Zero with statistics disabled is not evidence that the stack uses no memory. Native stack observations do not size an MCU application's C call stack: host threads use host-managed stacks. Account for architecture frames, interrupt reserve, call paths and physical high-water measurements as described in [Timing and evidence](../engineering/timing-and-evidence.md).

`emb_thread_foreach()` exists with `CONFIG_EMB_THREAD_LIST`. Its callback runs under the scheduler lock: inspect/copy bounded data, do not block or create/destroy registry objects. The scheduler lock does not exclude interrupt handlers. Take any needed snapshot before later formatting or exporting it.

## Register bounded diagnostic hooks

The following fragment can be added to a native application's source. Call `configure_diagnostics()` after `emb_kernel_init()` and before `emb_kernel_start()`; read the recorded scalar fields later from an appropriate thread/host diagnostic path.

```c
#include <emb/emb.h>

static volatile uint16_t last_trace_id;
static volatile uint8_t last_fault_class;
static volatile uint16_t last_fault_code;
static void on_trace(const emb_trace_event_t *event, void *ctx);
static void on_fault(const emb_fault_info_t *info);
void configure_diagnostics(void);

static void on_trace(const emb_trace_event_t *event, void *ctx)
{
    (void)ctx;
    last_trace_id = event->id;
}

static void on_fault(const emb_fault_info_t *info)
{
    last_fault_class = info->fault_class;
    last_fault_code = info->code;
}

void configure_diagnostics(void)
{
    emb_trace_set_sink(on_trace, NULL);
    emb_fault_set_hook(on_fault);
}
```

The trace sink is synchronous and can execute with interrupts masked. Keep it bounded and ISR-safe; avoid blocking, allocation, console formatting and kernel calls that emit trace again. Copy the fields needed during the callback: event pointers refer to temporary records. With `CONFIG_EMB_TRACE` off, registering a sink is a no-op. The scalar stores above illustrate collection only; `volatile` does not provide a portable shared-memory protocol or retained crash storage.

The fault hook receives a temporary `emb_fault_info_t` with class/code, context, argument index where provided, and hardware fields where the port supplies them. It runs with interrupts masked before the non-returning port action; it cannot recover by returning. Current generic faults set `api_id` to zero. Retained, versioned crash records, partition fault delivery, CTF rings and host crash decoders remain planned; the presence of related structures or specifications does not make those services available.

## Common symptoms

| Symptom | Check first |
|---|---|
| Lower-priority worker never runs | Higher-priority thread spins or yields instead of blocking |
| Native time never advances | Some thread stays runnable; virtual time advances in idle |
| Notification wakes repeatedly | CLEAR was omitted, or a readiness bit caused a legitimate extra recheck |
| Bound consumer misses initially ready work | It waited before inspecting/draining the initial semaphore count |
| Checked build stops on an API call | Wrong context, ownership, handle/lifetime, storage or blocking sub-state |
| Tiny thread init/priority change returns `EMB_EBUSY` | Another initialized thread owns that base-priority slot |
| Unexpected `EMB_EOWNERDEAD` | A previous mutex owner terminated with the lock held; the new caller owns it |
| IRQ priority works differently on native and Cortex-M | Native ignores levels; Cortex-M's current direction differs from the generic target convention |

Use a passing relevant conformance run as evidence for that reproduction/configuration. It does not close the known deep differential failures or prove every API requirement. Check [Project status](../STATUS.md) for unresolved implementation and evidence gaps.
