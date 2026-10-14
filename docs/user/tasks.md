# Threads, periodic work and shutdown

This tutorial uses the implemented native kernel. It creates a periodic worker, waits for its exit code, and ends the host process. The thread, time and join APIs are also present in the embedded ports; `emb_native_exit()` is a native helper, not a portable firmware shutdown API.

## A complete finite program

Save this program as `build/tutorials/tasks.c`, creating that directory first. Include `<emb/emb.h>` so the configured storage types and object-definition macros are available.

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include <emb/emb.h>
#include <emb_board.h>
#include <emb_native.h>

static void periodic(void *arg);
static void supervisor(void *arg);
static uint32_t iterations;

EMB_THREAD_DEFINE(worker, periodic, NULL, 2u, 2048u, 0u);
EMB_THREAD_DEFINE(control, supervisor, NULL, 1u, 2048u, 0u);

static void periodic(void *arg)
{
    emb_instant_t next = emb_time_now();
    (void)arg;
    for (uint32_t i = 0u; i < 5u; i++) {
        next = emb_instant_add(next, EMB_MS(10));
        EMB_CHECK(emb_thread_sleep_until(next));
        iterations++;
    }
    emb_thread_exit(7);
}

static void supervisor(void *arg)
{
    int code = 0;
    (void)arg;
    EMB_CHECK(emb_thread_join(worker, EMB_WAIT_FOREVER, &code));
    if (code != 7 || iterations != 5u) {
        emb_native_exit(1);
    }
    emb_board_puts("periodic: 5 iterations, exit=7\n");
    emb_native_exit(0);
}

int main(void)
{
    emb_board_init();
    emb_kernel_init();
    emb_kernel_start();
}
```

The initialization table constructs and autostarts both objects inside `emb_kernel_init()`. Execution begins at `emb_kernel_start()`: priority 2 runs before priority 1. The worker sleeps, the supervisor blocks in join, and idle advances native virtual time. Returning from a thread entry would also terminate it, with exit code zero. `emb_thread_exit(7)` makes the result explicit.

## Compile and run on native Linux

Activate the prepared native tools as described in [Getting started](getting-started.md). From the repository root, build the existing native libraries, then link the saved tutorial against that configuration:

```sh
cmake --preset native-gcc
cmake --build --preset native-gcc --parallel 4
cc -std=c11 -Wall -Wextra -Werror -pthread \
  -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L \
  -Iinclude -Ibuild/native-gcc/generated/include \
  -Iarch/native/include -Iboards/native/native/include \
  build/tutorials/tasks.c \
  -Wl,--start-group \
  build/native-gcc/boards/native/libemb_board.a \
  build/native-gcc/kernel/libemb_kernel.a \
  build/native-gcc/arch/native/libemb_arch.a \
  -Wl,--end-group -o build/tutorials/tasks
./build/tutorials/tasks
```

Expected output is `periodic: 5 iterations, exit=7`, followed by process exit status zero. Host execution need not take 50 milliseconds: the native scheduling clock is virtual. The library/include paths and GNU linker grouping above are for this Linux/GCC configuration; use the repository's target helpers for a port application. A native run provides no board timing or stack-size guarantee.

## Choose priorities and deadlines deliberately

Application priorities are `1 .. EMB_PRIORITY_MAX`; larger is more urgent. A high-priority thread must block when it has no work. Yield rotates equal-priority peers and cannot let a lower-priority thread run. Tiny uses unique base-priority slots, so these two priorities also avoid a slot collision there.

Advancing `next` from the previous deadline keeps the schedule anchored. Sleeping for a relative period after each iteration adds the work time to the period. If work overruns an absolute deadline, `sleep_until()` yields and the next iteration catches up; it does not count misses, skip periods or impose an execution budget. Define an application policy for overruns. In a 32-bit time configuration, compare instants using `emb_instant_before()` and keep scheduled deadlines within `EMB_TIMEOUT_MAX_TICKS` ahead. Avoid the maximum tick value as a finite object-wait deadline: it is the current forever sentinel.

## Storage and cancellation

`EMB_THREAD_DEFINE` uses static storage and a configured, aligned stack. With manual `emb_thread_init()`, keep the control-block storage, stack, entry argument and borrowed name alive for the thread/object lifetime. The attribute structure itself is copied. Generated storage sizes belong to one configuration: rebuild an application and its libraries together.

Join waits for termination; it does not itself destroy the object or free caller storage. Use `emb_thread_destroy()` after a successful join when storage must be reused. A detached thread cannot be joined. Destroy can return `EMB_EBUSY` until the native port has completed switching the terminated thread out. Keep retry logic outside a critical section and do not reuse storage before success. A copied POINTER handle must not be used after destruction/reinitialization; generation-based stale-handle protection is planned.

Cancellation is cooperative: `emb_thread_cancel()` requests it, and a cancellation point or cancelable wait returns `EMB_ECANCELED`. It does not kill the thread, destroy its objects, or repair shared state. Handle cancellation by releasing resources and choosing an exit result. In this closed example no one requests cancellation; the checked configuration faults on unexpected statuses through `EMB_CHECK`. In release, the macro returns the expression value without faulting, so a real worker must explicitly handle errors as well as expected cancellation/timeouts. See [Synchronization](synchronization.md) for ownership and [Debugging](debugging.md) for fault handling.
