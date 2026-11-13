/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_async_irq.c
 * @brief   The kernel under asynchronous interrupts (SPEC-013 §5, A93).
 *
 * @details
 * The native port's injector lands ticks, and a test interrupt every fourth time, at whatever
 * statement the running thread is on, as a board's interrupts do, and never only at a gate
 * crossing. A producer and a consumer pass numbered items through a message queue and share a
 * mutex, a thread takes what the interrupt gives, and a timer counts in the tick's interrupt, until
 * 5,000 interrupts have landed where they hit. The run checks that nothing is lost, duplicated or
 * reordered; the invariant checker (CONFIG_EMB_NATIVE_INVARIANTS) checks the scheduler at every
 * crossing throughout.
 *
 * @author  Junior Deogracias
 * @date    2026-11-05 (created)
 */
#include <emb_native.h>
#include <emb_test.h>

#define LANDED    5000u     /* interrupts that land where they hit before the producer stops */
#define MAX_ITEMS 50000000u /* a bound on the run, whatever the host's speed */
#define TEST_IRQ  5u        /* the native platform's test line (platform_native.c) */
#define PERIOD_US 20u

EMB_MSGQ_DEFINE(items_q, sizeof(uint32_t), 8u, 0u);
EMB_MUTEX_DEFINE(shared_lock, EMB_MUTEX_INHERIT, 0u, 0u);

static emb_sem_storage_t given_storage;
static emb_sem_t given;
static emb_timer_storage_t ticker_storage;
static emb_timer_t ticker;
static volatile uint32_t isr_gives;
static volatile uint32_t timer_expiries;
static volatile bool stop_taker;
static uint32_t sent;
static uint32_t received;
static uint32_t order_errors;
static uint32_t shared_count;
static uint32_t takes;

static void give_isr(void *arg)
{
    (void)arg;
    if (emb_sem_give(given) == EMB_OK) {
        isr_gives++;
    }
}

static void tick_count(emb_timer_t timer, void *arg)
{
    (void)timer;
    (void)arg;
    timer_expiries++;
}

static void producer(void *arg)
{
    uint32_t i;
    uint32_t end = 0u;
    (void)arg;
    for (i = 1u; i <= MAX_ITEMS; i++) {
        uint64_t delivered = 0u;
        uint64_t deferred = 0u;
        if ((i % 256u) == 0u) {
            emb_native_async_stats(&delivered, &deferred);
            if (delivered >= LANDED) {
                break;
            }
        }
        EMB_ASSERT_OK(emb_msgq_send(items_q, &i, EMB_WAIT_FOREVER));
        sent++;
        EMB_ASSERT_OK(emb_mutex_lock(shared_lock, EMB_WAIT_FOREVER));
        shared_count++;
        EMB_ASSERT_OK(emb_mutex_unlock(shared_lock));
        if ((i % 512u) == 0u) {
            (void)emb_thread_sleep(EMB_TICKS(1)); /* let time and the timer run */
        }
    }
    EMB_ASSERT_OK(emb_msgq_send(items_q, &end, EMB_WAIT_FOREVER)); /* 0: the end */
}

static void consumer(void *arg)
{
    uint32_t expected = 1u;
    (void)arg;
    for (;;) {
        uint32_t v = 0u;
        EMB_ASSERT_OK(emb_msgq_receive(items_q, &v, EMB_WAIT_FOREVER));
        if (v == 0u) {
            break;
        }
        if (v != expected) {
            order_errors++;
        }
        expected = v + 1u;
        received++;
        EMB_ASSERT_OK(emb_mutex_lock(shared_lock, EMB_WAIT_FOREVER));
        shared_count++;
        EMB_ASSERT_OK(emb_mutex_unlock(shared_lock));
    }
}

static void taker(void *arg)
{
    (void)arg;
    while (!stop_taker) {
        if (emb_sem_take(given, EMB_TIMEOUT(EMB_TICKS(5))) == EMB_OK) {
            takes++;
        }
    }
}

EMB_TEST(async_interrupts_lose_nothing)
{
    emb_timer_attr_t ta;
    emb_thread_t p;
    emb_thread_t c;
    emb_thread_t t;
    uint32_t left = 0u;
    uint64_t delivered = 0u;
    uint64_t deferred = 0u;
    EMB_TEST_REQ("KRN-WAIT-001", "KRN-IRQ-022", "SIM-004");
    EMB_ASSERT_OK(emb_sem_init(&given_storage, NULL, &given));
    emb_test_irq_connect(0u, give_isr, NULL);
    emb_timer_attr_default(&ta);
    ta.fn = tick_count;
    ta.flags = EMB_TIMER_ISR_CONTEXT;
    EMB_ASSERT_OK(emb_timer_init(&ticker_storage, &ta, &ticker));
    EMB_ASSERT_OK(emb_timer_start_periodic(ticker, EMB_TICKS(1), EMB_TICKS(1)));
    emb_native_async_start(PERIOD_US, (emb_irq_t)TEST_IRQ);
    t = emb_test_thread(EMB_TEST_NAME("taker"), 4u, 0u, taker, NULL);
    c = emb_test_thread(EMB_TEST_NAME("consumer"), 2u, 0u, consumer, NULL);
    p = emb_test_thread(EMB_TEST_NAME("producer"), 3u, 0u, producer, NULL);
    EMB_ASSERT_OK(emb_thread_join(p, EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_OK(emb_thread_join(c, EMB_WAIT_FOREVER, NULL));
    emb_native_async_stop();
    stop_taker = true;
    EMB_ASSERT_OK(emb_thread_join(t, EMB_WAIT_FOREVER, NULL));
    emb_native_async_stats(&delivered, &deferred);
    EMB_ASSERT_OK(emb_timer_stop(ticker, NULL));
    EMB_ASSERT_EQ(received, sent);
    EMB_ASSERT_EQ(order_errors, 0u);
    EMB_ASSERT_EQ(shared_count, 2u * sent);
    left = emb_sem_count(given);
    EMB_ASSERT_EQ(takes + left, isr_gives); /* every give taken or still counted */
    EMB_ASSERT_TRUE(delivered >= LANDED);   /* the interrupts did land mid-statement */
    EMB_ASSERT_TRUE(timer_expiries > 0u);
    emb_test_logf("  async: %lu items, %lu interrupts handled where they landed, %lu deferred "
                  "to a crossing, %lu interrupt gives, %lu timer expiries\n",
                  (unsigned long)sent, (unsigned long)delivered, (unsigned long)deferred,
                  (unsigned long)isr_gives, (unsigned long)timer_expiries);
    EMB_ASSERT_OK(emb_timer_destroy(ticker));
    EMB_ASSERT_OK(emb_sem_destroy(given));
}
