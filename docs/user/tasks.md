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

Application priorities are `1 .. EMB_PRIORITY_MAX`; larger is more urgent. A high-priority thread must block when it has no work. Yield rotates equal-priority peers and cannot let a lower-priority thread run. With `CONFIG_EMB_TIME_SLICING`, a thread at or below `CONFIG_EMB_TIMESLICE_PRIORITY` that runs for a whole quantum without blocking also moves behind its READY equal-priority peers; it never lets a lower priority run either. Tiny uses unique base-priority slots, so these two priorities also avoid a slot collision there.

Advancing `next` from the previous deadline keeps the schedule anchored. Sleeping for a relative period after each iteration adds the work time to the period. If work overruns an absolute deadline, `sleep_until()` yields and the next iteration catches up; it does not count misses, skip periods or impose an execution budget. Define an application policy for overruns. In a 32-bit time configuration, compare instants using `emb_instant_before()` and keep scheduled deadlines within `EMB_TIMEOUT_MAX_TICKS` ahead. Avoid the maximum tick value as a finite object-wait deadline: it is the current forever sentinel.

## Storage and cancellation

`EMB_THREAD_DEFINE` uses static storage and a configured, aligned stack. With manual `emb_thread_init()`, keep the control-block storage, stack, entry argument and borrowed name alive for the thread/object lifetime. The attribute structure itself is copied. Generated storage sizes belong to one configuration: rebuild an application and its libraries together.

Join waits for termination; it does not itself destroy the object or free caller storage. Use `emb_thread_destroy()` after a successful join when storage must be reused. A detached thread cannot be joined. Destroy can return `EMB_EBUSY` until the native port has completed switching the terminated thread out. Keep retry logic outside a critical section and do not reuse storage before success. A copied POINTER handle must not be used after destruction/reinitialization; generation-based stale-handle protection is planned.

Cancellation is cooperative: `emb_thread_cancel()` requests it, and a cancellation point or cancelable wait returns `EMB_ECANCELED`. It does not kill the thread, destroy its objects, or repair shared state. Handle cancellation by releasing resources and choosing an exit result. In this closed example no one requests cancellation; the checked configuration faults on unexpected statuses through `EMB_CHECK`. In release, the macro returns the expression value without faulting, so a real worker must explicitly handle errors as well as expected cancellation/timeouts. See [Synchronization](synchronization.md) for ownership and [Debugging](debugging.md) for fault handling.

## Deferred work on a work queue

A work queue (`CONFIG_EMB_WORKQ`, not available with the tiny profile's table scheduler in this milestone) runs short jobs on a kernel thread, one at a time and in submission order. Submitting is O(1) and ISR-safe, so an interrupt can hand its slow part to a thread:

```c
typedef struct {
    emb_work_t work;
    uint16_t sample;
} sensor_job_t;

static emb_workq_storage_t jobs_storage;
EMB_THREAD_STACK(jobs_stack, 1024);
static emb_workq_t jobs;
static sensor_job_t job;

static void process(emb_work_t *work)
{
    sensor_job_t *j = EMB_CONTAINER_OF(work, sensor_job_t, work);
    /* thread context, at the queue's priority: may block, delays later jobs on this queue */
    (void)j->sample;
}

emb_workq_attr_t attr;
emb_workq_attr_default(&attr);
attr.stack = jobs_stack;
attr.stack_size = sizeof(jobs_stack);
attr.priority = 4;
EMB_CHECK(emb_workq_init(&jobs_storage, &attr, &jobs));
emb_work_init(&job.work, process);
/* in the interrupt handler: */
(void)emb_work_submit(jobs, &job.work); /* EMB_EEXIST if it is already queued */
```

An item is never queued twice and never runs concurrently with itself; submitting it while its handler runs queues it once more for after the handler returns. `emb_work_cancel()` reports whether the item was pending (now removed), running (it completes) or idle. Before reusing an item's storage, call `emb_work_cancel_sync()`, which also waits for a running handler. `emb_workq_flush()` waits until everything queued before the call has run. Calling either from the queue's own handlers would wait forever and is misuse, as is returning from a handler with a mutex held. The queue's thread belongs to the kernel: thread calls on it are refused and it cannot be notified directly. `emb_workq_destroy()` needs an idle queue and returns after the thread has exited. Referencing `emb_workq_system()` links in a shared system queue at a high priority; keep its handlers short.

For work that should run later, an `emb_dwork_t` adds a delay: `emb_dwork_init(&dw, fn)`, then `emb_dwork_schedule(queue, &dw, EMB_MS(50))` submits it when the delay expires. Scheduling it again while the delay runs returns `EMB_EEXIST`; `emb_dwork_reschedule()` replaces the delay instead. `emb_dwork_cancel()` stops the delay or removes the queued item, and `emb_dwork_cancel_sync()` also waits for a running handler. The handler receives `&dw.work`.

## Periodic work with a software timer

A software timer (`CONFIG_EMB_TIMER`, like work queues not available with the tiny profile in this milestone) calls a function after a delay, once or periodically. By default the callback runs on a work queue you name, in thread context; with `EMB_TIMER_ISR_CONTEXT` it runs in the timer interrupt and must be short and ISR-safe:

```c
static emb_timer_storage_t blink_storage;
static emb_timer_t blink;

static void toggle(emb_timer_t timer, void *arg)
{
    (void)timer;
    (void)arg;
    /* thread context on the system work queue */
}

emb_timer_attr_t attr;
emb_timer_attr_default(&attr);
attr.fn = toggle;
attr.workq = emb_workq_system(); /* or a queue of your own */
EMB_CHECK(emb_timer_init(&blink_storage, &attr, &blink));
EMB_CHECK(emb_timer_start_periodic(blink, EMB_MS(100), EMB_MS(500)));
```

A periodic timer re-arms from its previous deadline, so expiry k falls at the first delay plus k periods however late the callbacks run. Periods missed entirely, for example while interrupts were masked, are skipped and counted, and so is an expiry that finds the previous callback still waiting; read both with `emb_timer_get_counts()`. `emb_timer_stop()` reports whether the callback was idle, pending or running, and is how a callback ends its own periodic timer. Before destroying a timer, call `emb_timer_stop_sync()`, which also waits for a running callback. A queue that a timer names cannot be destroyed until the timer is.
