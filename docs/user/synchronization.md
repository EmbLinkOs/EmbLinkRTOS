# Synchronization and notifications

Use an implemented primitive according to the information it preserves:

| Need | Current API | Important rule |
|---|---|---|
| Protect shared state | Mutex | An owner unlocks; inheritance can raise its effective priority |
| Count available units | Semaphore | A take consumes one unit; give may hand it directly to a waiter |
| Signal “inspect this source” | Thread notification | Bits persist and coalesce; repeated sets do not count events |
| Carry messages or schedule deferred callbacks | Planned queues/work queues | These services have specifications but no current public implementation |

## A complete producer/consumer program

This finite native example combines a semaphore count, a mutex-protected pair of counters, and a bound notification. Save it as `build/tutorials/synchronization.c`. Use the Linux compile command in [Threads](tasks.md), replacing `tasks.c` and the output name with `synchronization.c` and `synchronization`.

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include <emb/emb.h>
#include <emb_board.h>
#include <emb_native.h>

#define AVAILABLE EMB_NOTIFY_BIT(0)

static emb_sem_storage_t ready_storage;
static emb_mutex_storage_t state_storage;
static emb_sem_t ready;
static emb_mutex_t state;
static uint32_t produced = 1u;
static uint32_t consumed;
static void produce(void *arg);
static void consume(void *arg);
static void finish(void *arg);

EMB_THREAD_DEFINE(producer, produce, NULL, 2u, 2048u, 0u);
EMB_THREAD_DEFINE(consumer, consume, NULL, 3u, 2048u, 0u);
EMB_THREAD_DEFINE(control, finish, NULL, 1u, 2048u, 0u);

static void produce(void *arg)
{
    (void)arg;
    for (uint32_t i = 0u; i < 4u; i++) {
        EMB_CHECK(emb_mutex_lock(state, EMB_WAIT_FOREVER));
        produced++;
        EMB_CHECK(emb_mutex_unlock(state));
        EMB_CHECK(emb_sem_give(ready));
        EMB_CHECK(emb_thread_sleep(EMB_TICKS(1)));
    }
}

static void consume(void *arg)
{
    (void)arg;
    for (;;) {
        /* Inspect/drain before waiting, including the initially ready unit. */
        for (;;) {
            emb_status_t status = emb_sem_take(ready, EMB_NO_WAIT);
            if (status == EMB_ETIMEDOUT) {
                break;
            }
            (void)EMB_CHECK(status);
            EMB_CHECK(emb_mutex_lock(state, EMB_WAIT_FOREVER));
            consumed++;
            EMB_CHECK(emb_mutex_unlock(state));
        }
        if (consumed == 5u) {
            return;
        }
        EMB_CHECK(emb_notify_wait(AVAILABLE, EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR,
                                  EMB_WAIT_FOREVER, NULL));
    }
}

static void finish(void *arg)
{
    (void)arg;
    EMB_CHECK(emb_thread_join(producer, EMB_WAIT_FOREVER, NULL));
    EMB_CHECK(emb_thread_join(consumer, EMB_WAIT_FOREVER, NULL));
    /* Both writers have terminated; these final reads need no mutex. */
    if (produced != 5u || consumed != 5u) {
        emb_native_exit(1);
    }
    emb_board_puts("produced=5 consumed=5\n");
    emb_native_exit(0);
}

int main(void)
{
    emb_sem_attr_t attr;
    emb_board_init();
    emb_kernel_init();
    emb_sem_attr_default(&attr);
    attr.initial = 1u;
    attr.max = 5u;
    EMB_CHECK(emb_sem_init(&ready_storage, &attr, &ready));
    EMB_CHECK(emb_mutex_init(&state_storage, NULL, &state));
    EMB_CHECK(emb_sem_bind_notify(ready, consumer, AVAILABLE));
    emb_kernel_start();
}
```

Expected output is `produced=5 consumed=5` and exit status zero. This is a unit-counting example: it does not transmit payloads or claim to implement a message queue. The default mutex uses inheritance; this program requires `CONFIG_EMB_SEM`, `CONFIG_EMB_NOTIFY`, `CONFIG_EMB_MUTEX` and `CONFIG_EMB_MUTEX_INHERIT`. The reference native GCC configurations provide them.

## Why the bound consumer drains first

Binding takes a **one-bit mask**, such as `EMB_NOTIFY_BIT(0)`, rather than the integer bit index. It registers one target per semaphore. The implementation sets that bit on a later count transition from zero to one; binding an already-positive semaphore does not set it. The initial unit above therefore needs an initial readiness check.

Drain with `emb_sem_take(..., EMB_NO_WAIT)` until `EMB_ETIMEDOUT`, then wait on the notification with CLEAR. A give between the empty check and the wait leaves the bit set, so the wait observes it. A previously set bit can cause a harmless extra pass; recheck the semaphore after every wake. The count is the source of truth. A notification is not proof that a unit is still available, and `emb_sem_count()` does not reserve one. Direct semaphore waiters retain their direct hand-off behavior.

`emb_sem_unbind_notify()` removes the binding. While a binding names a thread, `emb_thread_destroy()` refuses it (a fault in checked builds, `EMB_EBUSY` in release), so tear down in this order: stop the producers, unbind or destroy the semaphore, let the consumer exit and join it, then destroy the thread. Neither unbinding nor destroying the semaphore clears the target's notification bits. Binding a thread that has already terminated returns `EMB_ESTATE`.

## Wait results and execution context

`EMB_NO_WAIT` returns `EMB_ETIMEDOUT` when no unit, lock or matching bits are immediately available. A finite timeout uses `EMB_TIMEOUT(EMB_MS(20))`. An absolute `_until` deadline lets multiple waits share one overall deadline. Durations and deadlines are distinct types; pass `emb_instant_t` to `_until`, not a duration.

Only functions explicitly annotated for ISR context may be called there. Semaphore give and notification set are ISR-safe; semaphore take and mutex acquisition remain thread-only even with `EMB_NO_WAIT`. A blocking timeout while holding a caller critical section or scheduler lock is misuse. An operation that wakes a higher-priority thread may cause it to run before the signaling thread continues; publish protected state before signaling.

Treat `EMB_ECANCELED`, `EMB_EDESTROYED` and `EMB_ETIMEDOUT` as application outcomes when your protocol permits them. Destroy normally requires no waiters; `EMB_OBJ_ABORT_WAITERS` deliberately wakes existing waiters with `EMB_EDESTROYED`. Establish ownership of shutdown and stop producers before invalidating storage. Checked builds can fault on misuse instead of returning a status.

## Mutex ownership and owner death

Inheritance changes effective priority, not base priority. It is bounded by `CONFIG_EMB_PI_MAX_DEPTH`; exceeding the propagation depth faults in a checked build and truncates propagation in release. Cost includes owned-mutex scans, waiter reordering and timeout work. See [Timing and evidence](../engineering/timing-and-evidence.md) before using an API's complexity annotation as a latency bound.

Unlock is owner-only. Recursion requires `EMB_MUTEX_RECURSIVE` and a matching unlock for every successful acquisition. For a ceiling mutex, choose a ceiling at least as high as each user's base priority and account for inherited effective priorities in schedulability analysis. A ceiling does not make arbitrary blocking while holding a mutex safe.

`EMB_EOWNERDEAD` is special: the caller **owns** the mutex and must repair its protected state. After successful repair, call `emb_mutex_mark_consistent()` and unlock. If repair cannot be performed, use an application recovery/shutdown policy; do not continue with corrupted state. Owner-death behavior depends on configuration, and the current implementation does not provide a persistent “not recoverable” state. The tutorial's workers terminate only after releasing their locks.

## Event flags

Event flags (`CONFIG_EMB_EVENT`) hold a word of bits that several threads can wait on. A wait names a mask and `EMB_EVENT_ANY` or `EMB_EVENT_ALL`, optionally with `EMB_EVENT_CLEAR` to consume the bits that satisfied it:

```c
static emb_event_storage_t status_storage;
static emb_event_t status;
#define LINK_UP  ((emb_event_bits_t)0x01u)
#define CONFIGURED ((emb_event_bits_t)0x02u)

EMB_CHECK(emb_event_init(&status_storage, NULL, &status));
/* producers, threads or interrupts: */
EMB_CHECK(emb_event_set(status, LINK_UP));
/* a consumer that needs both conditions: */
emb_event_bits_t got;
emb_status_t st = emb_event_wait(status, LINK_UP | CONFIGURED, EMB_EVENT_ALL,
                                 EMB_TIMEOUT(EMB_MS(500)), &got);
```

`emb_event_set()` and `emb_event_clear()` are ISR-safe; waits are thread-only. A set serves waiters in priority order (arrival order with `EMB_OBJ_FIFO`). A `CLEAR` waiter removes the bits it was handed before the next waiter is examined, so two clearing waiters on the same bit are served one per set, highest priority first. `emb_event_clear()` never wakes anyone. A woken waiter receives the bits that satisfied it in `got` and does not re-read the word. A bound notification fires only for bits still set after the direct waiters were served; bind and unbind follow the semaphore rules above.

## Condition variables

A condition variable (`CONFIG_EMB_COND`) lets a thread wait for a predicate over state that a mutex protects. Check the predicate in a loop with the mutex held; `emb_cond_wait()` releases the mutex and enqueues the caller in one step, so a signal sent after the release is never missed:

```c
EMB_CHECK(emb_mutex_lock(lock, EMB_WAIT_FOREVER));
while (!ready) {
    emb_status_t st = emb_cond_wait(changed, lock, EMB_TIMEOUT(EMB_MS(100)));
    if (st != EMB_OK && st != EMB_ETIMEDOUT) {
        break; /* canceled, destroyed, or EMB_EOWNERDEAD: still the owner except on a failed relock */
    }
}
/* ... use the protected state ... */
EMB_CHECK(emb_mutex_unlock(lock));
```

The waiter relocks the mutex before every return, including timeouts, cancellation and destruction, so the relock can take longer than the timeout. The relock is not a cancellation point. A signal with no waiter is lost, and there are no spurious wakeups, but recheck the predicate anyway: another thread can change it between the signal and the relock. The caller must hold the mutex exactly once; a recursively held mutex returns `EMB_ESTATE`. `emb_cond_signal()` wakes the highest-priority waiter and `emb_cond_broadcast()` wakes all of them. Both are thread-only.

## Message queues

A message queue (`CONFIG_EMB_MSGQ`) carries fixed-size items by value through a ring that you provide. Declare the ring with `EMB_MSGQ_RING()`, which gives it the size and alignment the kernel checks:

```c
typedef struct {
    uint8_t channel;
    uint16_t sample;
} reading_t;

static emb_msgq_storage_t readings_storage;
static EMB_MSGQ_RING(readings_ring, sizeof(reading_t), 8);
static emb_msgq_t readings;

emb_msgq_attr_t attr;
emb_msgq_attr_default(&attr);
attr.ring = readings_ring;
attr.ring_size = sizeof(readings_ring);
attr.item_size = sizeof(reading_t);
attr.capacity = 8;
EMB_CHECK(emb_msgq_init(&readings_storage, &attr, &readings));

/* a producer thread, or an interrupt with EMB_NO_WAIT: */
reading_t r = {.channel = 2, .sample = 517};
emb_status_t st = emb_msgq_send(readings, &r, EMB_NO_WAIT); /* EMB_ETIMEDOUT when full */
/* a consumer: */
reading_t got;
st = emb_msgq_receive(readings, &got, EMB_TIMEOUT(EMB_MS(100)));
```

Items are received in send order, except that `emb_msgq_send_front()` puts an urgent item at the head. A send to a queue with a waiting receiver copies straight into that receiver's buffer. A receive from a full queue with a waiting sender moves that sender's item into the freed slot before the sender wakes, so neither side re-checks. A full queue never drops an item silently: `send` waits or returns `EMB_ETIMEDOUT`. Interrupts may call `send`, `send_front`, `receive` and `peek` with `EMB_NO_WAIT` only. Each copy runs with interrupts masked, so `CONFIG_EMB_MSGQ_MAX_ITEM` (64 bytes, 16 on tiny) bounds the item size; pass larger payloads by pointer under a protocol of your own until buffer pools exist. Destroying a queue drops its items, and with `EMB_OBJ_ABORT_WAITERS` wakes both receivers and senders with `EMB_EDESTROYED`. A bound notification fires when the queue goes from empty to nonempty, not when an item is handed to a waiting receiver; bind and unbind follow the semaphore rules above.
