/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Event flag conformance (SPEC-005 §5; KRN-SYNC-029, KRN-SYNC-035, KRN-WAIT-013). */
#include <emb_test.h>

#define E0    ((emb_event_bits_t)0x01u)
#define E1    ((emb_event_bits_t)0x02u)
#define E2    ((emb_event_bits_t)0x04u)
#define IRQ_E 2u /* software interrupt slot */

static emb_event_storage_t storage;
static emb_event_t ev;

static void make(emb_event_bits_t initial, uint8_t flags)
{
    emb_event_attr_t attr;
    emb_event_attr_default(&attr);
    attr.initial = initial;
    attr.flags = flags;
    EMB_ASSERT_OK(emb_event_init(&storage, &attr, &ev));
}

/* Waits for the mask and mode packed in arg in 12 bits, so that a 16-bit uintptr_t (AVR)
 * holds them: mask in bits 0 to 3, mode in bits 4 to 7, id in bits 8 to 11. */
static void waiter(void *arg)
{
    uintptr_t a = (uintptr_t)arg;
    emb_event_bits_t got = 0u;
    emb_status_t st = emb_event_wait(ev, (emb_event_bits_t)(a & 0x0Fu), (uint8_t)((a >> 4) & 0x0Fu),
                                     EMB_TIMEOUT(EMB_TICKS(10)), &got);
    int id = (int)((a >> 8) & 0x0Fu);
    emb_test_mark(st == EMB_OK ? id * 10 + (int)got : (st == EMB_ETIMEDOUT ? id + 500 : id + 700));
}

#define WAIT_ARG(mask, mode, id) ((void *)(uintptr_t)((mask) | ((mode) << 4) | ((id) << 8)))

EMB_TEST(event_any_all_and_clear)
{
    emb_event_bits_t got = 0u;
    EMB_TEST_REQ("KRN-SYNC-029");
    make(E0, 0u);
    EMB_ASSERT_EQ(emb_event_get(ev), E0);
    EMB_ASSERT_OK(emb_event_wait(ev, E0 | E1, EMB_EVENT_ANY, EMB_NO_WAIT, &got));
    EMB_ASSERT_EQ(got, E0);
    EMB_ASSERT_STATUS(emb_event_wait(ev, E0 | E1, EMB_EVENT_ALL, EMB_NO_WAIT, &got), EMB_ETIMEDOUT);
    EMB_ASSERT_OK(emb_event_set(ev, E1));
    EMB_ASSERT_OK(emb_event_wait(ev, E0 | E1, EMB_EVENT_ALL | EMB_EVENT_CLEAR, EMB_NO_WAIT, &got));
    EMB_ASSERT_EQ(got, E0 | E1);
    EMB_ASSERT_EQ(emb_event_get(ev), 0u); /* CLEAR consumed exactly the satisfying bits */
    EMB_ASSERT_OK(emb_event_set(ev, E2));
    EMB_ASSERT_OK(emb_event_clear(ev, E2));
    EMB_ASSERT_OK(emb_event_set(ev, 0u)); /* no-ops */
    EMB_ASSERT_OK(emb_event_clear(ev, 0u));
    EMB_ASSERT_EQ(emb_event_get(ev), 0u);
    EMB_ASSERT_OK(emb_event_destroy(ev));
}

EMB_TEST(event_overlapping_clear_serves_waiters_in_order)
{
    EMB_TEST_REQ("KRN-SYNC-029", "KRN-WAIT-001");
    make(0u, 0u);
    (void)emb_test_thread(EMB_TEST_NAME("w3"), 3u, 0u, waiter, WAIT_ARG(E0, EMB_EVENT_CLEAR, 3u));
    (void)emb_test_thread(EMB_TEST_NAME("w4"), 4u, 0u, waiter, WAIT_ARG(E0, EMB_EVENT_CLEAR, 4u));
    EMB_ASSERT_OK(emb_event_set(ev, E0)); /* the higher waiter consumes the bit */
    EMB_ASSERT_MARKS(41);
    EMB_ASSERT_EQ(emb_event_get(ev), 0u);
    EMB_ASSERT_OK(emb_event_set(ev, E0)); /* the next set serves the other */
    EMB_ASSERT_MARKS(41, 31);
    EMB_ASSERT_OK(emb_event_destroy(ev));
}

EMB_TEST(event_set_wakes_every_satisfied_waiter_in_one_pass)
{
    EMB_TEST_REQ("KRN-SYNC-029", "KRN-WAIT-013");
    make(0u, 0u);
    (void)emb_test_thread(EMB_TEST_NAME("w2"), 2u, 0u, waiter, WAIT_ARG(E0, EMB_EVENT_ANY, 2u));
    (void)emb_test_thread(EMB_TEST_NAME("w3"), 3u, 0u, waiter,
                          WAIT_ARG(E0 | E1, EMB_EVENT_ALL, 3u));
    (void)emb_test_thread(EMB_TEST_NAME("w4"), 4u, 0u, waiter, WAIT_ARG(E1, EMB_EVENT_ANY, 4u));
    EMB_ASSERT_OK(emb_event_set(ev, E0 | E1)); /* all three, by priority, bits kept */
    EMB_ASSERT_MARKS(42, 33, 21);
    EMB_ASSERT_EQ(emb_event_get(ev), E0 | E1);
    EMB_ASSERT_OK(emb_event_destroy(ev));
}

EMB_TEST_ISR(isr_set)
{
    (void)emb_isr_arg_;
    emb_test_mark(100);
    EMB_ASSERT_OK(emb_event_set(ev, E2));
}

EMB_TEST(event_set_from_isr_wakes_at_exit)
{
    EMB_TEST_REQ("KRN-SYNC-029", "KRN-SYNC-027");
    make(0u, 0u);
    emb_test_irq_connect(IRQ_E, isr_set, NULL);
    (void)emb_test_thread(EMB_TEST_NAME("w3"), 3u, 0u, waiter, WAIT_ARG(E2, EMB_EVENT_CLEAR, 3u));
    emb_test_irq_raise(IRQ_E);
    emb_test_mark(1);
    EMB_ASSERT_MARKS(100, 34, 1);
    EMB_ASSERT_OK(emb_event_destroy(ev));
}

EMB_TEST(event_wait_times_out_cancels_and_ignores_clear)
{
    emb_thread_t h;
    uint32_t t0 = EMB_TEST_NOW();
    EMB_TEST_REQ("KRN-SYNC-029", "KRN-WAIT-004", "KRN-WAIT-017");
    make(0u, 0u);
    (void)emb_test_thread(EMB_TEST_NAME("w3"), 3u, 0u, waiter, WAIT_ARG(E0, EMB_EVENT_ANY, 3u));
    EMB_ASSERT_OK(emb_event_clear(ev, E0)); /* clear never wakes */
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(20)));
    EMB_ASSERT_MARKS(503);
    EMB_ASSERT_ELAPSED(t0, 20u);
    h = emb_test_thread(EMB_TEST_NAME("w4"), 4u, 0u, waiter, WAIT_ARG(E0, EMB_EVENT_ANY, 4u));
    EMB_ASSERT_OK(emb_thread_cancel(h));
    EMB_ASSERT_MARKS(503, 704); /* EMB_ECANCELED */
    EMB_ASSERT_OK(emb_event_destroy(ev));
}

EMB_TEST(event_destroy_aborts_waiters_when_allowed)
{
    EMB_TEST_REQ("KRN-OBJ-001", "KRN-WAIT-018");
    make(0u, EMB_OBJ_ABORT_WAITERS);
    (void)emb_test_thread(EMB_TEST_NAME("w3"), 3u, 0u, waiter, WAIT_ARG(E0, EMB_EVENT_ANY, 3u));
    EMB_ASSERT_OK(emb_event_destroy(ev));
    EMB_ASSERT_MARKS(703); /* EMB_EDESTROYED, reported as id + 700 */
}

EMB_TEST(event_destroy_with_waiters_is_misuse)
{
    EMB_TEST_REQ("KRN-OBJ-001", "API-013");
    make(0u, 0u);
    (void)emb_test_thread(EMB_TEST_NAME("w3"), 3u, 0u, waiter, WAIT_ARG(E0, EMB_EVENT_ANY, 3u));
    EMB_ASSERT_MISUSE(emb_event_destroy(ev), EMB_EBUSY);
    EMB_ASSERT_OK(emb_event_set(ev, E0));
    EMB_ASSERT_MARKS(31);
    EMB_ASSERT_OK(emb_event_destroy(ev));
}

EMB_TEST(event_wait_validates_arguments)
{
    emb_event_t other;
    EMB_TEST_REQ("KRN-SYNC-029", "API-015");
    make(0u, 0u);
    EMB_ASSERT_MISUSE(emb_event_wait(ev, 0u, EMB_EVENT_ANY, EMB_NO_WAIT, NULL), EMB_EINVAL);
    EMB_ASSERT_MISUSE(emb_event_wait(ev, E0, 0x10u, EMB_NO_WAIT, NULL), EMB_EINVAL);
    EMB_ASSERT_MISUSE(emb_event_init(NULL, NULL, &other), EMB_EINVAL); /* clears other only */
    EMB_ASSERT_OK(emb_event_destroy(ev));
}

#if CONFIG_EMB_NOTIFY
EMB_TEST(event_binding_fires_after_direct_waiters)
{
    emb_notify_bits_t got = 0u;
    EMB_TEST_REQ("KRN-SYNC-035", "KRN-NOTIF-004", "KRN-NOTIF-005", "KRN-NOTIF-013");
    make(0u, 0u);
    EMB_ASSERT_OK(emb_event_bind_notify(ev, emb_thread_self(), EMB_NOTIFY_BIT(0)));
    EMB_ASSERT_STATUS(emb_event_bind_notify(ev, emb_thread_self(), EMB_NOTIFY_BIT(1)), EMB_EEXIST);
    (void)emb_test_thread(EMB_TEST_NAME("w3"), 3u, 0u, waiter, WAIT_ARG(E0, EMB_EVENT_CLEAR, 3u));
    EMB_ASSERT_OK(emb_event_set(ev, E0)); /* consumed by the waiter: no bits left, no signal */
    EMB_ASSERT_MARKS(31);
    EMB_ASSERT_STATUS(emb_notify_wait(EMB_NOTIFY_BIT(0), EMB_NOTIFY_ANY, EMB_NO_WAIT, &got),
                      EMB_ETIMEDOUT);
    EMB_ASSERT_OK(emb_event_set(ev, E1)); /* left set: the binding fires */
    EMB_ASSERT_OK(
        emb_notify_wait(EMB_NOTIFY_BIT(0), EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR, EMB_NO_WAIT, &got));
    EMB_ASSERT_OK(emb_event_set(ev, E1)); /* unchanged: no new signal */
    EMB_ASSERT_STATUS(emb_notify_wait(EMB_NOTIFY_BIT(0), EMB_NOTIFY_ANY, EMB_NO_WAIT, &got),
                      EMB_ETIMEDOUT);
    EMB_ASSERT_OK(emb_event_unbind_notify(ev));
    EMB_ASSERT_STATUS(emb_event_unbind_notify(ev), EMB_ENOENT);
    EMB_ASSERT_OK(emb_event_destroy(ev));
}
#endif

#if CONFIG_EMB_OBJ_LIST
EMB_TEST(event_storage_reuse_requires_destroy)
{
    emb_event_t other;
    EMB_TEST_REQ("KRN-OBJ-007", "KRN-OBJ-011");
    make(0u, 0u);
    EMB_ASSERT_MISUSE(emb_event_init(&storage, NULL, &other), EMB_EBUSY);
    EMB_ASSERT_OK(emb_event_destroy(ev));
    make(0u, 0u);
    EMB_ASSERT_OK(emb_event_destroy(ev));
}
#endif
