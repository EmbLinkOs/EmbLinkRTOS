/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The first-execution sequence of v0.1 §41.3 (M1 exit criterion, 07 §2): reset, SoC
 * startup, architecture and kernel initialization, idle plus Task A and Task B, start
 * the scheduler, A runs, a scheduling event switches to B, repeat for very large switch
 * counts. A and B share a priority and hand over with emb_thread_yield(); a third
 * thread wakes on the timer every tick so that interrupt-driven preemption happens too.
 * The LED toggles every 1000 round trips. On the native port the run ends after the
 * configured number of switches with the counts printed.
 */
#include <emb/emb.h>

#include <emb_board.h>

#if CONFIG_EMB_ARCH_NATIVE
#include <emb_native.h>
#define SWITCH_TARGET 2000000u
#else
#define REPORT_EVERY 10000u /* a progress line on the console (SIM-002: observable under QEMU) */
#endif

static uint32_t count_a; /* lock: written by A only, read after the run */
static uint32_t count_b; /* lock: written by B only */
static uint32_t ticks_seen;

#define PING EMB_NOTIFY_BIT(0)

static void task_a(void *arg);
static void task_b(void *arg);
static void task_timer(void *arg);

/* A and B hand the CPU to each other with a notification (a scheduling event that
 * always switches), and every hundredth round trip both sleep one tick so that the
 * timer interrupt and the timer thread's preemption are part of the sequence too.
 * Priorities are unique so the sequence also runs on the tiny profile's table
 * scheduler (ADR-036); virtual time on the native port advances only in idle. */
#define ROUNDS_PER_SLEEP 100u

EMB_THREAD_DEFINE(thread_a, task_a, NULL, 2u, 512u, 0u);
EMB_THREAD_DEFINE(thread_b, task_b, NULL, 3u, 512u, 0u);
EMB_THREAD_DEFINE(thread_t, task_timer, NULL, 4u, 512u, 0u);

static void task_a(void *arg)
{
    (void)arg;
    for (;;) {
        (void)emb_notify_wait(PING, EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR, EMB_WAIT_FOREVER, NULL);
        count_a++;
        if ((count_a % 1000u) == 0u) {
            emb_board_led_toggle();
        }
        if ((count_a % ROUNDS_PER_SLEEP) == 0u) {
            (void)emb_thread_sleep(EMB_TICKS(1));
        }
        (void)emb_notify_set(thread_b, PING);
    }
}

static void task_b(void *arg)
{
    (void)arg;
    (void)emb_notify_set(thread_a, PING); /* the first ball */
    for (;;) {
        (void)emb_notify_wait(PING, EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR, EMB_WAIT_FOREVER, NULL);
        count_b++;
#if CONFIG_EMB_ARCH_NATIVE
        if (count_a + count_b >= SWITCH_TARGET) {
            emb_board_puts("first_execution: switches A=");
            emb_board_put_u32(count_a);
            emb_board_puts(" B=");
            emb_board_put_u32(count_b);
            emb_board_puts(" ticks=");
            emb_board_put_u32(ticks_seen);
            emb_board_puts("\n");
            emb_native_exit(0);
        }
#else
        if ((count_b % REPORT_EVERY) == 0u) {
            emb_board_puts("switches=");
            emb_board_put_u32(count_a + count_b);
            emb_board_puts(" ticks=");
            emb_board_put_u32(ticks_seen);
            emb_board_puts("\r\n");
        }
#endif
        (void)emb_notify_set(thread_a, PING);
    }
}

static void task_timer(void *arg)
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
