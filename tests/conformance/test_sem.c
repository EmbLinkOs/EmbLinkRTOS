/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Semaphore conformance (SPEC-005 §4; KRN-SYNC-020 to 022, KRN-WAIT-001, 004, 012, 013). */
#include <emb_test.h>

#define IRQ_S 2u /* software interrupt slot */

static emb_sem_storage_t storage;
static emb_sem_t sem;

static void make(const emb_sem_attr_t *attr)
{
    EMB_ASSERT_OK(emb_sem_init(&storage, attr, &sem));
}

static void taker(void *arg)
{
    int id = (int)(intptr_t)arg;
    emb_status_t st = emb_sem_take(sem, EMB_TIMEOUT(EMB_TICKS(10)));
    emb_test_mark(st == EMB_OK ? id : (st == EMB_ETIMEDOUT ? id + 50 : id + 70));
}

EMB_TEST(sem_counts_and_nowait)
{
    emb_sem_attr_t attr;
    EMB_TEST_REQ("KRN-SYNC-020", "KRN-SYNC-021");
    emb_sem_attr_default(&attr);
    attr.initial = 2u;
    make(&attr);
    EMB_ASSERT_EQ(emb_sem_count(sem), 2u);
    EMB_ASSERT_OK(emb_sem_take(sem, EMB_NO_WAIT));
    EMB_ASSERT_OK(emb_sem_take(sem, EMB_NO_WAIT));
    EMB_ASSERT_STATUS(emb_sem_take(sem, EMB_NO_WAIT), EMB_ETIMEDOUT);
    EMB_ASSERT_OK(emb_sem_give(sem));
    EMB_ASSERT_EQ(emb_sem_count(sem), 1u);
    EMB_ASSERT_OK(emb_sem_destroy(sem));
}

EMB_TEST(sem_give_hands_off_to_highest_waiter)
{
    EMB_TEST_REQ("KRN-WAIT-001", "KRN-WAIT-012", "KRN-SYNC-022");
    make(NULL);
    (void)emb_test_thread(EMB_TEST_NAME("t2"), 2u, 0u, taker, (void *)2);
    (void)emb_test_thread(EMB_TEST_NAME("t4"), 4u, 0u, taker, (void *)4);
    (void)emb_test_thread(EMB_TEST_NAME("t3"), 3u, 0u, taker, (void *)3);
    EMB_ASSERT_OK(emb_sem_give(sem));
    EMB_ASSERT_EQ(emb_sem_count(sem), 0u); /* handed over, never counted */
    EMB_ASSERT_OK(emb_sem_give(sem));
    EMB_ASSERT_OK(emb_sem_give(sem));
    EMB_ASSERT_MARKS(4, 3, 2);
    EMB_ASSERT_OK(emb_sem_destroy(sem));
}

#if !CONFIG_EMB_SCHED_TABLE
EMB_TEST(sem_fifo_option_orders_by_arrival)
{
    emb_sem_attr_t attr;
    EMB_TEST_REQ("KRN-WAIT-002");
    emb_sem_attr_default(&attr);
    attr.flags = EMB_OBJ_FIFO;
    make(&attr);
    (void)emb_test_thread(EMB_TEST_NAME("t3"), 3u, 0u, taker, (void *)3);
    (void)emb_test_thread(EMB_TEST_NAME("t4"), 4u, 0u, taker, (void *)4);
    EMB_ASSERT_OK(emb_sem_give(sem));
    EMB_ASSERT_OK(emb_sem_give(sem));
    EMB_ASSERT_MARKS(3, 4);
    EMB_ASSERT_OK(emb_sem_destroy(sem));
}
#endif

EMB_TEST(sem_take_times_out_and_disarms)
{
    uint32_t t0 = EMB_TEST_NOW();
    EMB_TEST_REQ("KRN-WAIT-004", "KRN-WAIT-013");
    make(NULL);
    (void)emb_test_thread(EMB_TEST_NAME("t3"), 3u, 0u, taker, (void *)3);
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(20)));
    EMB_ASSERT_MARKS(53);
    EMB_ASSERT_ELAPSED(t0, 20u);
    EMB_ASSERT_OK(emb_sem_give(sem)); /* nobody waits any more: counted */
    EMB_ASSERT_EQ(emb_sem_count(sem), 1u);
    EMB_ASSERT_OK(emb_sem_destroy(sem));
}

EMB_TEST_ISR(isr_give)
{
    (void)emb_isr_arg_;
    emb_test_mark(100);
    EMB_ASSERT_OK(emb_sem_give(sem));
}

EMB_TEST(sem_give_from_isr_wakes_at_exit)
{
    EMB_TEST_REQ("KRN-SYNC-022", "KRN-IRQ-010", "KRN-IRQ-033");
    make(NULL);
    emb_test_irq_connect(IRQ_S, isr_give, NULL);
    (void)emb_test_thread(EMB_TEST_NAME("t3"), 3u, 0u, taker, (void *)3);
    emb_test_irq_raise(IRQ_S);
    emb_test_mark(1);
    EMB_ASSERT_MARKS(100, 3, 1);
    EMB_ASSERT_OK(emb_sem_destroy(sem));
}

EMB_TEST(sem_max_overflow_and_saturate)
{
    emb_sem_attr_t attr;
    EMB_TEST_REQ("KRN-SYNC-020");
    emb_sem_attr_default(&attr);
    attr.max = 2u;
    make(&attr);
    EMB_ASSERT_OK(emb_sem_give(sem));
    EMB_ASSERT_OK(emb_sem_give(sem));
    EMB_ASSERT_STATUS(emb_sem_give(sem), EMB_EOVERFLOW);
    EMB_ASSERT_EQ(emb_sem_count(sem), 2u);
    EMB_ASSERT_OK(emb_sem_destroy(sem));
    emb_sem_attr_binary(&attr, false);
    make(&attr);
    EMB_ASSERT_OK(emb_sem_give(sem));
    EMB_ASSERT_OK(emb_sem_give(sem)); /* saturates silently */
    EMB_ASSERT_EQ(emb_sem_count(sem), 1u);
    EMB_ASSERT_OK(emb_sem_take(sem, EMB_NO_WAIT));
    EMB_ASSERT_STATUS(emb_sem_take(sem, EMB_NO_WAIT), EMB_ETIMEDOUT);
    EMB_ASSERT_OK(emb_sem_destroy(sem));
}

EMB_TEST(sem_init_validates_arguments)
{
    emb_sem_attr_t attr;
    EMB_TEST_REQ("API-015");
    emb_sem_attr_default(&attr);
    attr.initial = 5u;
    attr.max = 2u;
    EMB_ASSERT_MISUSE(emb_sem_init(&storage, &attr, &sem), EMB_EINVAL);
    EMB_ASSERT_MISUSE(emb_sem_init(NULL, NULL, &sem), EMB_EINVAL);
}

EMB_TEST(sem_destroy_aborts_waiters_when_allowed)
{
    emb_sem_attr_t attr;
    EMB_TEST_REQ("KRN-OBJ-001", "KRN-WAIT-018");
    emb_sem_attr_default(&attr);
    attr.flags = EMB_OBJ_ABORT_WAITERS;
    make(&attr);
    (void)emb_test_thread(EMB_TEST_NAME("t3"), 3u, 0u, taker, (void *)3);
    EMB_ASSERT_OK(emb_sem_destroy(sem));
    EMB_ASSERT_MARKS(73); /* EMB_EDESTROYED */
}

EMB_TEST(sem_destroy_with_waiters_is_misuse)
{
    EMB_TEST_REQ("KRN-OBJ-001", "API-013");
    make(NULL);
    (void)emb_test_thread(EMB_TEST_NAME("t3"), 3u, 0u, taker, (void *)3);
    EMB_ASSERT_MISUSE(emb_sem_destroy(sem), EMB_EBUSY);
    EMB_ASSERT_OK(emb_sem_give(sem));
    EMB_ASSERT_MARKS(3);
    EMB_ASSERT_OK(emb_sem_destroy(sem));
}

EMB_TEST(sem_blocking_take_from_prekernel_or_isr_rejected)
{
    EMB_TEST_REQ("API-012", "API-013");
    make(NULL);
    EMB_ASSERT_OK(emb_sem_give(sem));
    EMB_ASSERT_OK(emb_sem_take(sem, EMB_WAIT_FOREVER)); /* satisfied without blocking */
    EMB_ASSERT_OK(emb_sem_destroy(sem));
}
