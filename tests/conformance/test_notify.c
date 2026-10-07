/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Notification conformance (SPEC-006 §2, §3; KRN-NOTIF-001 to 006). */
#include <emb_native.h>
#include <emb_test.h>

#define B0    EMB_NOTIFY_BIT(0)
#define B1    EMB_NOTIFY_BIT(1)
#define IRQ_N 9u

static emb_thread_t target;

EMB_TEST(notify_bits_are_level_sensitive)
{
    emb_notify_bits_t got = 0u;
    EMB_TEST_REQ("KRN-NOTIF-001", "KRN-NOTIF-002");
    EMB_ASSERT_OK(emb_notify_set(emb_thread_self(), B0));
    EMB_ASSERT_OK(emb_notify_wait(B0, EMB_NOTIFY_ANY, EMB_NO_WAIT, &got));
    EMB_ASSERT_EQ(got, B0);
    EMB_ASSERT_OK(emb_notify_wait(B0, EMB_NOTIFY_ANY, EMB_NO_WAIT, &got)); /* not consumed */
    EMB_ASSERT_OK(emb_notify_wait(B0, EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR, EMB_NO_WAIT, &got));
    EMB_ASSERT_STATUS(emb_notify_wait(B0, EMB_NOTIFY_ANY, EMB_NO_WAIT, &got), EMB_ETIMEDOUT);
    EMB_ASSERT_EQ(emb_notify_get(), 0u);
}

EMB_TEST(notify_any_and_all_modes)
{
    emb_notify_bits_t got = 0u;
    EMB_TEST_REQ("KRN-NOTIF-001");
    EMB_ASSERT_OK(emb_notify_set(emb_thread_self(), B0));
    EMB_ASSERT_STATUS(emb_notify_wait(B0 | B1, EMB_NOTIFY_ALL, EMB_NO_WAIT, &got), EMB_ETIMEDOUT);
    EMB_ASSERT_OK(emb_notify_wait(B0 | B1, EMB_NOTIFY_ANY, EMB_NO_WAIT, &got));
    EMB_ASSERT_EQ(got, B0);
    EMB_ASSERT_OK(emb_notify_set(emb_thread_self(), B1));
    EMB_ASSERT_OK(emb_notify_wait(B0 | B1, EMB_NOTIFY_ALL | EMB_NOTIFY_CLEAR, EMB_NO_WAIT, &got));
    EMB_ASSERT_EQ(got, B0 | B1);
    EMB_ASSERT_EQ(emb_notify_get(), 0u);
    EMB_ASSERT_OK(emb_notify_set(emb_thread_self(), B0 | B1));
    EMB_ASSERT_OK(emb_notify_clear(B0));
    EMB_ASSERT_EQ(emb_notify_get(), B1);
    EMB_ASSERT_OK(emb_notify_clear(B1));
}

static void waiter(void *arg)
{
    emb_notify_bits_t got = 0u;
    emb_status_t st = emb_notify_wait(B0 | B1, EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR,
                                      EMB_TIMEOUT(EMB_TICKS(5)), &got);
    (void)arg;
    emb_test_mark(st == EMB_OK ? (int)got : 99);
}

EMB_TEST(notify_wake_from_thread_hands_off_bits)
{
    EMB_TEST_REQ("KRN-NOTIF-003", "KRN-WAIT-012");
    target = emb_test_thread("w", 3u, 0u, waiter, NULL);
    EMB_ASSERT_OK(emb_notify_set(target, B1));
    EMB_ASSERT_MARKS((int)B1);
    EMB_ASSERT_OK(emb_thread_join(target, EMB_WAIT_FOREVER, NULL));
}

EMB_ISR(isr_set)
{
    (void)emb_isr_arg_;
    emb_test_mark(100);
    (void)emb_notify_set(target, B0);
}

EMB_TEST(notify_wake_from_isr)
{
    EMB_TEST_REQ("KRN-NOTIF-002", "KRN-IRQ-010");
    EMB_ASSERT_OK(emb_irq_connect(IRQ_N, isr_set, NULL));
    target = emb_test_thread("w", 3u, 0u, waiter, NULL);
    emb_native_irq_raise(IRQ_N);
    emb_test_mark(1);
    EMB_ASSERT_MARKS(100, (int)B0, 1);
}

EMB_TEST(notify_wait_times_out)
{
    uint32_t t0 = EMB_TEST_NOW();
    EMB_TEST_REQ("KRN-NOTIF-001", "KRN-WAIT-004");
    target = emb_test_thread("w", 3u, 0u, waiter, NULL);
    EMB_ASSERT_OK(emb_thread_join(target, EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_MARKS(99);
    EMB_ASSERT_EQ(EMB_TEST_NOW() - t0, 5u);
}

static void exits(void *arg)
{
    (void)arg;
}

EMB_TEST(notify_rejects_kernel_bits_and_terminated_targets)
{
    emb_thread_t h = emb_test_thread("x", 3u, 0u, exits, NULL);
    EMB_TEST_REQ("KRN-NOTIF-002", "KRN-NOTIF-006");
    EMB_ASSERT_MISUSE(emb_notify_set(emb_thread_self(), EMB_NOTIFY_K_WORK), EMB_EINVAL);
    EMB_ASSERT_MISUSE(emb_notify_set(emb_thread_self(), 0u), EMB_EINVAL);
    EMB_ASSERT_MISUSE(emb_notify_wait(0u, EMB_NOTIFY_ANY, EMB_NO_WAIT, NULL), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_notify_set(h, B0), EMB_ESTATE);
}

#if CONFIG_EMB_SEM
static emb_sem_storage_t sem_storage;

EMB_TEST(notify_semaphore_binding_signals_ready_transition)
{
    emb_sem_t sem;
    emb_notify_bits_t got = 0u;
    EMB_TEST_REQ("KRN-NOTIF-004", "KRN-NOTIF-005");
    EMB_ASSERT_OK(emb_sem_init(&sem_storage, NULL, &sem));
    EMB_ASSERT_OK(emb_sem_bind_notify(sem, emb_thread_self(), B1));
    EMB_ASSERT_STATUS(emb_sem_bind_notify(sem, emb_thread_self(), B0), EMB_EEXIST);
    EMB_ASSERT_OK(emb_sem_give(sem)); /* 0 -> 1: the binding fires */
    EMB_ASSERT_OK(emb_notify_wait(B1, EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR, EMB_NO_WAIT, &got));
    EMB_ASSERT_OK(emb_sem_give(sem)); /* 1 -> 2: no new signal */
    EMB_ASSERT_STATUS(emb_notify_wait(B1, EMB_NOTIFY_ANY, EMB_NO_WAIT, &got), EMB_ETIMEDOUT);
    EMB_ASSERT_OK(emb_sem_take(sem, EMB_NO_WAIT));
    EMB_ASSERT_OK(emb_sem_take(sem, EMB_NO_WAIT));
    EMB_ASSERT_OK(emb_sem_destroy(sem));
}
#endif
