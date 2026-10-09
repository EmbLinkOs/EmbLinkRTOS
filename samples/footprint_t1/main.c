/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The R-003 T1 and T2 reference program (tiny profile, release build): the scheduler,
 * notifications, sleep and timeouts, one semaphore type; no mutex, no logging. Its link
 * map is what the footprint gate measures (tools/footprint, TEST-011). The program also
 * runs: a producer and a consumer hand units through a semaphore and a notification,
 * both with timeouts, while a third thread wakes every tick. The console lines are
 * board code, not kernel code, and do not count.
 */
#include <emb/emb.h>

#include <emb_board.h>

#if CONFIG_EMB_ARCH_NATIVE
#include <emb_native.h>
#define ROUND_TARGET 200000u
#else
#define REPORT_EVERY 10000u
#endif

#define PING EMB_NOTIFY_BIT(0)

static uint32_t rounds; /* lock: written by the consumer only */
static uint32_t ticks_seen;
static emb_sem_storage_t units_storage;
static emb_sem_t units;

static void producer(void *arg);
static void consumer(void *arg);
static void ticker(void *arg);

EMB_THREAD_DEFINE(thread_p, producer, NULL, 2u, 512u, 0u);
EMB_THREAD_DEFINE(thread_c, consumer, NULL, 3u, 512u, 0u);
EMB_THREAD_DEFINE(thread_t, ticker, NULL, 4u, 512u, 0u);

static void units_init(void)
{
    emb_sem_attr_t attr;
    emb_sem_attr_default(&attr);
    attr.max = 4u;
    (void)emb_sem_init(&units_storage, &attr, &units);
}
EMB_INIT_TABLE_ENTRY(units_init);

static void producer(void *arg)
{
    (void)arg;
    for (;;) {
        /* a notification wait with a timeout: either way one unit goes out */
        (void)emb_notify_wait(PING, EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR, EMB_TIMEOUT(EMB_TICKS(5)),
                              NULL);
        (void)emb_sem_give(units);
    }
}

static void consumer(void *arg)
{
    (void)arg;
    (void)emb_notify_set(thread_p, PING); /* the first unit */
    for (;;) {
        if (emb_sem_take(units, EMB_TIMEOUT(EMB_TICKS(20))) == EMB_OK) {
            rounds++;
            if ((rounds % 1000u) == 0u) {
                emb_board_led_toggle();
            }
            if ((rounds % 100u) == 0u) {
                (void)emb_thread_sleep(EMB_TICKS(1));
            }
#if CONFIG_EMB_ARCH_NATIVE
            if (rounds >= ROUND_TARGET) {
                emb_board_puts("footprint_t1: rounds=");
                emb_board_put_u32(rounds);
                emb_board_puts(" ticks=");
                emb_board_put_u32(ticks_seen);
                emb_board_puts("\n");
                emb_native_exit(0);
            }
#else
            if ((rounds % REPORT_EVERY) == 0u) {
                emb_board_puts("rounds=");
                emb_board_put_u32(rounds);
                emb_board_puts(" ticks=");
                emb_board_put_u32(ticks_seen);
                emb_board_puts("\r\n");
            }
#endif
        }
        (void)emb_notify_set(thread_p, PING);
    }
}

static void ticker(void *arg)
{
    emb_instant_t next = emb_time_now();
    (void)arg;
    for (;;) {
        next = emb_instant_add(next, EMB_TICKS(1));
        (void)emb_thread_sleep_until(next);
        ticks_seen++;
    }
}

int main(void)
{
    emb_board_init();
    emb_kernel_init();
    emb_kernel_start();
}
