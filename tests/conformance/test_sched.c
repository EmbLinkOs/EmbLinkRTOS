/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Scheduler conformance (03 §2, SPEC-002 §5, §6; KRN-SCH-001 to 008, 016 to 019, 041). */
#include <emb_native.h>
#include <emb_test.h>

static void mark_and_exit(void *arg)
{
    emb_test_mark((int)(intptr_t)arg);
}

EMB_TEST(sched_higher_priority_runs_first)
{
    EMB_TEST_REQ("KRN-SCH-001", "KRN-SCH-002");
    (void)emb_test_thread("h3", 3u, 0u, mark_and_exit, (void *)3);
    emb_test_mark(1); /* the runner continues only after h3 ran to completion */
    EMB_ASSERT_MARKS(3, 1);
}

EMB_TEST(sched_lock_defers_preemption_until_unlock)
{
    EMB_TEST_REQ("KRN-SCH-016", "KRN-SCH-017", "KRN-SCH-018");
    emb_sched_lock();
    (void)emb_test_thread("h3", 3u, 0u, mark_and_exit, (void *)3);
    emb_test_mark(1); /* h3 is READY and pending, not running */
    emb_sched_lock();
    emb_sched_unlock(); /* inner unlock: still locked (depth 1) */
    emb_test_mark(2);
    EMB_ASSERT_EQ(emb_sched_lock_depth(), 1u);
    emb_sched_unlock(); /* depth 0: the pending reschedule runs h3 now */
    emb_test_mark(4);
    EMB_ASSERT_MARKS(1, 2, 3, 4);
}

static void yielder(void *arg)
{
    int id = (int)(intptr_t)arg;
    emb_test_mark(id);
    emb_thread_yield();
    emb_test_mark(id + 10);
}

EMB_TEST(sched_yield_never_to_lower_priority)
{
    EMB_TEST_REQ("KRN-SCH-008");
    (void)emb_test_thread("y3", 3u, 0u, yielder, (void *)3);
    emb_test_mark(1);
    EMB_ASSERT_MARKS(3, 13, 1); /* the yield found no peer: no switch to the runner */
}

#if !CONFIG_EMB_SCHED_TABLE
EMB_TEST(sched_fifo_within_level_and_yield_rotates)
{
    EMB_TEST_REQ("KRN-SCH-003", "KRN-SCH-008");
    emb_sched_lock();
    (void)emb_test_thread("y3a", 3u, 0u, yielder, (void *)1);
    (void)emb_test_thread("y3b", 3u, 0u, yielder, (void *)2);
    emb_sched_unlock();
    emb_test_mark(9);
    /* a runs first (FIFO), yields to b, b yields back to a, a finishes, b finishes */
    EMB_ASSERT_MARKS(1, 2, 11, 12, 9);
}

static emb_thread_t victim_handle;

EMB_ISR(wake_victim_isr)
{
    (void)emb_isr_arg_;
    (void)emb_thread_resume(victim_handle);
}

static void preempted_worker(void *arg)
{
    int id = (int)(intptr_t)arg;
    emb_test_mark(id);
    if (id == 1) {
        emb_native_irq_raise(7u); /* an ISR wakes a higher thread: this thread is preempted */
    }
    emb_test_mark(id + 10);
}

static void suspended_high(void *arg)
{
    (void)arg;
    emb_test_mark(5);
}

EMB_TEST(sched_preempted_thread_goes_ahead_of_its_peers)
{
    EMB_TEST_REQ("KRN-SCH-041", "KRN-IRQ-013");
    EMB_ASSERT_OK(emb_irq_connect(7u, wake_victim_isr, NULL));
    emb_sched_lock();
    victim_handle = emb_test_thread("high", 5u, 0u, suspended_high, NULL);
    EMB_ASSERT_OK(emb_thread_suspend(victim_handle));
    (void)emb_test_thread("w1", 3u, 0u, preempted_worker, (void *)1);
    (void)emb_test_thread("w2", 3u, 0u, preempted_worker, (void *)2);
    emb_sched_unlock();
    emb_test_mark(9);
    /* w1 runs, the ISR resumes high (priority 5) which preempts w1; at its exit w1
     * continues ahead of w2, which has not run yet */
    EMB_ASSERT_MARKS(1, 5, 11, 2, 12, 9);
}
#endif

EMB_TEST(sched_blocking_under_scheduler_lock_is_misuse)
{
    EMB_TEST_REQ("KRN-SCH-019", "API-013");
    emb_sched_lock();
    EMB_ASSERT_MISUSE(emb_thread_sleep(EMB_TICKS(1)), EMB_EPERM);
    emb_sched_unlock();
}

EMB_TEST(sched_idle_runs_when_nothing_is_ready)
{
    uint32_t t0 = EMB_TEST_NOW();
    EMB_TEST_REQ("KRN-SCH-043", "SIM-005");
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(3))); /* only the idle context can advance time */
    EMB_ASSERT_EQ(EMB_TEST_NOW() - t0, 3u);
}

EMB_TEST(sched_thread_self_and_priorities)
{
    emb_thread_t me = emb_thread_self();
    EMB_TEST_REQ("KRN-THR-002", "KRN-SCH-009");
    EMB_ASSERT_TRUE(!EMB_HANDLE_IS_NULL(me));
    EMB_ASSERT_EQ(emb_thread_get_priority(me), EMB_TEST_RUNNER_PRIORITY);
    EMB_ASSERT_EQ(emb_thread_get_effective_priority(me), EMB_TEST_RUNNER_PRIORITY);
    EMB_ASSERT_EQ(emb_context(), EMB_CONTEXT_THREAD);
}
