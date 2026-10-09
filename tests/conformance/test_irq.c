/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Interrupt and critical-section conformance (SPEC-002; KRN-IRQ-001 to 036). */
#include <emb_test.h>

#define IRQ_A 0u /* software interrupt slots of the test framework */
#define IRQ_B 1u

static emb_thread_t waiter_handle;
static emb_context_t seen_context;
static unsigned seen_depth;
static emb_status_t isr_status;

EMB_TEST_ISR(isr_notify)
{
    (void)emb_isr_arg_;
    emb_test_mark(100);
    seen_context = emb_context();
    seen_depth = emb_irq_nesting_depth();
    (void)emb_notify_set(waiter_handle, EMB_NOTIFY_BIT(0));
    emb_test_mark(101);
}

static void waiter(void *arg)
{
    emb_notify_bits_t got = 0u;
    (void)arg;
    EMB_ASSERT_OK(emb_notify_wait(EMB_NOTIFY_BIT(0), EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR,
                                  EMB_WAIT_FOREVER, &got));
    emb_test_mark(3);
}

EMB_TEST(irq_lock_is_nestable_and_lifo)
{
    emb_irq_key_t outer;
    emb_irq_key_t inner;
    EMB_TEST_REQ("KRN-IRQ-001", "KRN-IRQ-002");
    EMB_ASSERT_TRUE(!emb_irq_is_locked());
    outer = emb_irq_lock();
    EMB_ASSERT_TRUE(emb_irq_is_locked());
    inner = emb_irq_lock();
    EMB_ASSERT_TRUE(emb_irq_is_locked());
    emb_irq_unlock(inner);
    EMB_ASSERT_TRUE(emb_irq_is_locked()); /* inner unlock restores a masked state */
    emb_irq_unlock(outer);
    EMB_ASSERT_TRUE(!emb_irq_is_locked());
}

EMB_TEST(irq_isr_wake_switches_at_outermost_exit)
{
    EMB_TEST_REQ("KRN-IRQ-010", "KRN-IRQ-012", "KRN-IRQ-013", "KRN-SCH-021");
    emb_test_irq_connect(IRQ_A, isr_notify, NULL);
    waiter_handle = emb_test_thread(EMB_TEST_NAME("waiter"), 3u, 0u, waiter, NULL);
    emb_test_mark(1);
    emb_test_irq_raise(IRQ_A); /* delivered now: unmasked thread context */
    emb_test_mark(2);
    EMB_ASSERT_MARKS(1, 100, 101, 3, 2); /* the switch happens after the handler returned */
    EMB_ASSERT_EQ(seen_context, EMB_CONTEXT_ISR);
    EMB_ASSERT_EQ(seen_depth, 1u);
    EMB_ASSERT_EQ(emb_irq_nesting_depth(), 0u);
}

EMB_TEST(irq_delivery_is_deferred_while_masked)
{
    emb_irq_key_t key;
    EMB_TEST_REQ("KRN-IRQ-001", "KRN-IRQ-033");
    emb_test_irq_connect(IRQ_A, isr_notify, NULL);
    waiter_handle = emb_test_thread(EMB_TEST_NAME("waiter"), 3u, 0u, waiter, NULL);
    key = emb_irq_lock();
    emb_test_irq_raise(IRQ_A);
    emb_test_mark(1); /* still pending */
    emb_irq_unlock(key);
    emb_test_irq_settle(); /* taken at the unlock on hardware; see the hook's contract */
    emb_test_mark(2);
    EMB_ASSERT_MARKS(1, 100, 101, 3, 2);
}

EMB_TEST(irq_scheduler_lock_defers_the_switch_not_the_handler)
{
    EMB_TEST_REQ("KRN-SCH-016", "KRN-SCH-018", "KRN-SCH-019");
    emb_test_irq_connect(IRQ_A, isr_notify, NULL);
    waiter_handle = emb_test_thread(EMB_TEST_NAME("waiter"), 3u, 0u, waiter, NULL);
    emb_sched_lock();
    emb_test_irq_raise(IRQ_A); /* the handler runs; the woken thread waits for the unlock */
    emb_test_mark(1);
    emb_sched_unlock();
    emb_test_mark(2);
    EMB_ASSERT_MARKS(100, 101, 1, 3, 2);
}

EMB_TEST(irq_disabled_source_stays_pending)
{
    EMB_TEST_REQ("KRN-IRQ-020");
    emb_test_irq_connect(IRQ_B, isr_notify, NULL);
    waiter_handle = emb_test_thread(EMB_TEST_NAME("waiter"), 3u, 0u, waiter, NULL);
    emb_test_irq_disable(IRQ_B);
    EMB_ASSERT_TRUE(!emb_test_irq_is_enabled(IRQ_B));
    emb_test_irq_raise(IRQ_B);
    emb_test_mark(1);
    emb_test_irq_enable(IRQ_B);
    emb_test_mark(2);
    EMB_ASSERT_MARKS(1, 100, 101, 3, 2);
}

EMB_TEST_ISR(isr_blocks)
{
    (void)emb_isr_arg_;
    isr_status = emb_thread_sleep(EMB_TICKS(1));
}

EMB_TEST(irq_blocking_call_from_isr_is_misuse)
{
    EMB_TEST_REQ("API-013", "KRN-IRQ-014");
    emb_test_irq_connect(IRQ_B, isr_blocks, NULL);
    isr_status = EMB_OK;
#if CONFIG_EMB_CHECKED
    EMB_ASSERT_FAULTS(emb_test_irq_raise(IRQ_B));
#else
    emb_test_irq_raise(IRQ_B);
    EMB_ASSERT_EQ(isr_status, EMB_EPERM);
#endif
}

#if CONFIG_EMB_IRQ_DYNAMIC
EMB_TEST(irq_connect_validates_arguments)
{
    EMB_TEST_REQ("KRN-IRQ-021");
    EMB_ASSERT_MISUSE(emb_irq_connect((emb_irq_t)EMB_ARCH_IRQ_COUNT, isr_notify, NULL), EMB_EINVAL);
    EMB_ASSERT_MISUSE(emb_irq_connect(9u, NULL, NULL), EMB_EINVAL);
}
#endif

EMB_TEST(irq_unlock_without_lock_is_misuse)
{
    EMB_TEST_REQ("KRN-IRQ-002");
#if CONFIG_EMB_CHECKED
    EMB_ASSERT_FAULTS(emb_irq_unlock(0u));
#endif
}
