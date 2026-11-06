/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The interrupt stack (SPEC-002 §7.2, PORT-016, A90): its size and high-water mark, the
 * fill check of its lowest bytes at the outermost interrupt exit, the same check of the
 * interrupted thread's guard, and on Armv8-M Mainline MSPLIM stopping an interrupt that
 * overflows. The checks fault, so they run where fault checks run (fork, or a board's reset).
 */
#include <emb/arch.h> /* emb_arch_isr_stack: where the guard bytes are */

#include <emb_test.h>

#define SLOT 0u

EMB_TEST_ISR(touch_isr)
{
    (void)emb_isr_arg_;
}

EMB_TEST(isr_stack_info_reports_size_and_use)
{
    size_t size = 0u;
    size_t used = 0u;
    emb_status_t st = emb_isr_stack_info(&size, &used);
    EMB_TEST_REQ("PORT-016", "KRN-MEM-009");
    emb_test_irq_connect(SLOT, touch_isr, NULL);
#if CONFIG_EMB_ARCH_NATIVE
    emb_test_irq_raise(SLOT);
    EMB_ASSERT_STATUS(st, EMB_ENOTSUP); /* interrupts run on the host thread's stack */
#else
    size_t used_after = 0u;
    EMB_ASSERT_OK(st);
#if CONFIG_EMB_ARCH_HAS_INTERRUPT_STACK
    EMB_ASSERT_EQ(size, CONFIG_EMB_ISR_STACK_SIZE);
#else
    EMB_ASSERT_TRUE(size > 0u); /* AVR: the pre-kernel stack */
#endif
    emb_test_irq_raise(SLOT);
    emb_test_irq_settle();
    EMB_ASSERT_OK(emb_isr_stack_info(NULL, &used_after));
#if CONFIG_EMB_STACK_CHECK || CONFIG_EMB_STACK_STATS
    EMB_ASSERT_TRUE(used_after > 0u);
    EMB_ASSERT_TRUE(used_after < size);
    EMB_ASSERT_TRUE(used_after >= used); /* a high-water mark */
#else
    EMB_ASSERT_EQ(used_after, 0u);
#endif
#endif
}

#if CONFIG_EMB_CHECKED && CONFIG_EMB_STACK_CHECK
/* An overflow that reached the stack's lowest bytes, made by hand. */
EMB_TEST_ISR(spoil_isr_stack_guard)
{
    size_t size = 0u;
    uint8_t *base = emb_arch_isr_stack(&size);
    (void)emb_isr_arg_;
    base[0] = (uint8_t)~EMB_STACK_FILL;
}

EMB_TEST(isr_stack_guard_is_checked_at_the_outermost_exit)
{
    size_t size = 0u;
    EMB_TEST_REQ("PORT-016", "KRN-MEM-010");
    if (emb_arch_isr_stack(&size) == NULL) {
        emb_test_skip(EMB_TEST_STR("no interrupt stack to check"));
        return;
    }
    emb_test_irq_connect(SLOT, spoil_isr_stack_guard, NULL);
    /* the byte is put back after the interrupt: only the check at its exit can see it */
    EMB_ASSERT_FAULTS({
        emb_test_irq_raise(SLOT);
        emb_test_irq_settle();
        emb_arch_isr_stack(&size)[0] = EMB_STACK_FILL;
    });
}

static uint8_t *guarded_stack;

EMB_TEST_ISR(spoil_thread_guard)
{
    (void)emb_isr_arg_;
    guarded_stack[0] = (uint8_t)~EMB_STACK_FILL; /* the interrupted thread's lowest byte */
}

static void interrupted(void *arg)
{
    (void)arg;
    emb_test_irq_raise(SLOT); /* runs on this thread's stack or takes its exception frame */
    emb_test_irq_settle();
    guarded_stack[0] = EMB_STACK_FILL; /* put back: the switch-out check sees nothing */
}

/* An interrupt on a thread whose stack is nearly full: the thread's guard is checked at the
 * outermost exit too, not only when the thread next switches out. */
EMB_TEST(thread_guard_is_checked_at_the_outermost_exit)
{
    emb_thread_attr_t attr;
    emb_thread_t h;
    EMB_TEST_REQ("KRN-MEM-010", "PORT-016");
    emb_thread_attr_default(&attr);
    attr.name = EMB_TEST_NAME("guarded");
    attr.stack = emb_test_scratch_stack(&attr.stack_size);
    attr.priority = 3u;
    guarded_stack = (uint8_t *)attr.stack;
    emb_test_irq_connect(SLOT, spoil_thread_guard, NULL);
    EMB_ASSERT_FAULTS({
        EMB_ASSERT_OK(emb_thread_init(emb_test_scratch_storage(), &attr, interrupted, NULL, &h));
        EMB_ASSERT_OK(emb_thread_start(h)); /* preempts: the interrupt comes at once */
    });
}
#endif

#if CONFIG_EMB_CHECKED && CONFIG_EMB_ARCH_HAS_HW_STACK_LIMIT && CONFIG_EMB_ARCH_CORTEX_M
/* far more frames than the interrupt stack holds; volatile, so it is not folded away */
static volatile uint32_t depth_bound = 1000000u;

static uint32_t dive(uint32_t n)
{
    volatile uint8_t pad[64];
    pad[0] = (uint8_t)n;
    if (n >= depth_bound) {
        return pad[0];
    }
    return dive(n + 1u) + pad[0];
}

EMB_TEST_ISR(overflow_isr)
{
    (void)emb_isr_arg_;
    (void)dive(0u);
}

/* MSPLIM stops it before it leaves the stack (A90): a stack overflow fault, not a lockup. A
 * fault check expects no fault in a release build, so the case is checked-only. */
EMB_TEST(isr_stack_limit_stops_an_overflowing_interrupt)
{
    EMB_TEST_REQ("PORT-016", "KRN-MEM-010");
    emb_test_irq_connect(SLOT, overflow_isr, NULL);
    EMB_ASSERT_FAULTS(emb_test_irq_raise(SLOT));
}
#endif
