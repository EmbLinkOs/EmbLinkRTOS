/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_irq_priority.c
 * @brief   Generic priority limits and Cortex-M controller ordering (SPEC-002 §8).
 *
 * @author  Junior Deogracias
 * @date    2026-10-21 (created)
 */
#include <emb/arch.h>

#include <emb_test.h>

EMB_TEST(irq_priority_accepts_advertised_endpoints)
{
    EMB_TEST_REQ("KRN-IRQ-028", "KRN-IRQ-029");
    EMB_ASSERT_OK(emb_irq_set_level(0u, 0u));
    EMB_ASSERT_OK(emb_irq_set_level(0u, (uint8_t)EMB_IRQ_LEVEL_KERNEL_MAX));
}

EMB_TEST(irq_priority_rejects_out_of_range_level)
{
    EMB_TEST_REQ("API-015", "KRN-IRQ-028");
    EMB_ASSERT_MISUSE(emb_irq_set_level(0u, (uint8_t)(EMB_IRQ_LEVEL_KERNEL_MAX + 1u)), EMB_EINVAL);
    EMB_ASSERT_MISUSE(emb_irq_set_level(0u, UINT8_MAX), EMB_EINVAL);
}

EMB_TEST(irq_priority_rejects_unknown_number)
{
    EMB_TEST_REQ("API-015", "KRN-IRQ-029");
    EMB_ASSERT_MISUSE(emb_irq_set_level((emb_irq_t)EMB_ARCH_IRQ_COUNT, 0u), EMB_EINVAL);
}

EMB_TEST(irq_priority_port_rejects_invalid_inputs)
{
    /* Port calls return status even in checked builds; public entry adds the fault. */
    EMB_TEST_REQ("KRN-IRQ-028");
    EMB_ASSERT_STATUS(emb_arch_irq_set_level(0u, (uint8_t)(EMB_IRQ_LEVEL_KERNEL_MAX + 1u)),
                      EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_arch_irq_set_level((emb_irq_t)EMB_ARCH_IRQ_COUNT, 0u), EMB_EINVAL);
}

#if CONFIG_EMB_ARCH_NATIVE
#include <emb_native.h>

#if !CONFIG_EMB_CHECKED
static emb_status_t context_status; /* a checked build faults instead of returning */
#endif

static void priority_from_isr(void *arg)
{
    (void)arg;
    /* Context wins even when both arguments are invalid (API-013). */
#if CONFIG_EMB_CHECKED
    (void)emb_irq_set_level((emb_irq_t)EMB_ARCH_IRQ_COUNT, UINT8_MAX);
#else
    context_status = emb_irq_set_level((emb_irq_t)EMB_ARCH_IRQ_COUNT, UINT8_MAX);
#endif
}

#if CONFIG_EMB_CHECKED
static uint8_t expected_class;
static uint8_t expected_argument;

static void check_priority_fault(const emb_fault_info_t *info)
{
    if (info->fault_class != expected_class || info->argument != expected_argument) {
        emb_native_exit(97); /* distinguish a wrong fault from the expected fault exit */
    }
}

#define EXPECT_PRIORITY_FAULT(stmt, fault_class, argument)                    \
    do {                                                                      \
        if (emb_test_fork_begin()) {                                          \
            expected_class = (fault_class);                                   \
            expected_argument = (argument);                                   \
            emb_fault_set_hook(check_priority_fault);                         \
            stmt;                                                             \
            emb_test_fork_child_no_fault();                                   \
        }                                                                     \
        emb_test_fork_expect_fault(__LINE__, EMB_TEST_STR("priority fault")); \
    } while (0)

EMB_TEST(irq_priority_fault_argument_and_precedence)
{
    EMB_TEST_REQ("API-013", "API-015");
    EXPECT_PRIORITY_FAULT((void)emb_irq_set_level(0u, UINT8_MAX), EMB_FAULT_API_ARGUMENT, 2u);
    EXPECT_PRIORITY_FAULT((void)emb_irq_set_level((emb_irq_t)EMB_ARCH_IRQ_COUNT, UINT8_MAX),
                          EMB_FAULT_API_ARGUMENT, 1u);
}
#endif

EMB_TEST(irq_priority_context_precedes_argument_validation)
{
    EMB_TEST_REQ("API-013", "API-015");
    emb_test_irq_connect(0u, priority_from_isr, NULL);
#if CONFIG_EMB_CHECKED
    EXPECT_PRIORITY_FAULT(emb_test_irq_raise(0u), EMB_FAULT_API_CONTEXT, 0u);
#else
    context_status = EMB_OK;
    emb_test_irq_raise(0u);
    EMB_ASSERT_EQ(context_status, EMB_EPERM);
#endif
}
#endif

#if CONFIG_EMB_ARCH_CORTEX_M
#include <emb_irq_priority.h>
#include <hw_config.h>

#define PRIORITY_REG(irq)       (*(volatile uint8_t *)(uintptr_t)(0xE000E400u + (irq)))
#define SYSTEM_PRIORITY(offset) (*(volatile uint8_t *)(uintptr_t)(0xE000ED20u + (offset)))
#define PRIORITY_LOW            ((emb_irq_t)EMB_HW_TEST_IRQ)
#define PRIORITY_HIGH           ((emb_irq_t)EMB_HW_TEST_IRQ_HIGH)

static volatile bool high_seen;
static bool wait_for_preemption;

static void priority_high(void *arg)
{
    (void)arg;
    emb_test_mark(20);
    high_seen = true;
}

static void priority_low(void *arg)
{
    uint32_t spin = 0u;
    (void)arg;
    emb_test_mark(10);
    (void)emb_irq_pend(PRIORITY_HIGH);
    if (wait_for_preemption) {
        while (!high_seen && spin < 1000000u) {
            spin++;
        }
    }
    emb_test_mark(30);
}

static void priority_pending_low(void *arg)
{
    (void)arg;
    emb_test_mark(10);
}

static void priority_setup(emb_isr_fn_t low, uint8_t high_level)
{
    EMB_ASSERT_OK(emb_irq_disable(PRIORITY_LOW));
    EMB_ASSERT_OK(emb_irq_disable(PRIORITY_HIGH));
    EMB_ASSERT_OK(emb_irq_clear_pending(PRIORITY_LOW));
    EMB_ASSERT_OK(emb_irq_clear_pending(PRIORITY_HIGH));
    EMB_ASSERT_OK(emb_irq_connect(PRIORITY_LOW, low, NULL));
    EMB_ASSERT_OK(emb_irq_connect(PRIORITY_HIGH, priority_high, NULL));
    EMB_ASSERT_OK(emb_irq_set_level(PRIORITY_LOW, 0u));
    EMB_ASSERT_OK(emb_irq_set_level(PRIORITY_HIGH, high_level));
    high_seen = false;
    EMB_ASSERT_OK(emb_irq_enable(PRIORITY_LOW));
    EMB_ASSERT_OK(emb_irq_enable(PRIORITY_HIGH));
}

static void priority_cleanup(void)
{
    EMB_ASSERT_OK(emb_irq_disable(PRIORITY_LOW));
    EMB_ASSERT_OK(emb_irq_disable(PRIORITY_HIGH));
    EMB_ASSERT_OK(emb_irq_clear_pending(PRIORITY_LOW));
    EMB_ASSERT_OK(emb_irq_clear_pending(PRIORITY_HIGH));
}

static void priority_settle(unsigned marks)
{
    uint32_t spin = 0u;
    while (emb_test_marks_count() < marks && spin < 1000000u) {
        spin++;
    }
}

EMB_TEST(irq_priority_encodings_and_invalid_write_guard)
{
    unsigned level;
    uint8_t before;
    EMB_TEST_REQ("KRN-IRQ-028", "KRN-IRQ-031");
    for (level = 0u; level <= EMB_IRQ_LEVEL_MAX; level++) {
        EMB_ASSERT_OK(emb_irq_set_level(PRIORITY_LOW, (uint8_t)level));
        EMB_ASSERT_EQ(PRIORITY_REG(PRIORITY_LOW), embn_cm_irq_priority((uint8_t)level));
    }
    EMB_ASSERT_OK(emb_irq_set_level(PRIORITY_LOW, 0u));
    EMB_ASSERT_EQ(PRIORITY_REG(PRIORITY_LOW), EMB_CM_IRQ_LOWEST_PRIORITY);
    EMB_ASSERT_OK(emb_irq_set_level(PRIORITY_LOW, (uint8_t)EMB_IRQ_LEVEL_MAX));
    EMB_ASSERT_EQ(PRIORITY_REG(PRIORITY_LOW), CONFIG_EMB_CM_KERNEL_BASEPRI);
    before = PRIORITY_REG(PRIORITY_LOW);
    EMB_ASSERT_STATUS(emb_arch_irq_set_level(PRIORITY_LOW, (uint8_t)(EMB_IRQ_LEVEL_MAX + 1u)),
                      EMB_EINVAL);
    EMB_ASSERT_EQ(PRIORITY_REG(PRIORITY_LOW), before);
    EMB_ASSERT_STATUS(emb_arch_irq_set_level(PRIORITY_LOW, UINT8_MAX), EMB_EINVAL);
    EMB_ASSERT_EQ(PRIORITY_REG(PRIORITY_LOW), before);
    EMB_ASSERT_EQ(SYSTEM_PRIORITY(3u), embn_cm_irq_priority((uint8_t)CONFIG_EMB_CM_TIMER_LEVEL));
    EMB_ASSERT_EQ(SYSTEM_PRIORITY(2u) & (256u - EMB_CM_IRQ_PRIORITY_STEP),
                  256u - EMB_CM_IRQ_PRIORITY_STEP);
    EMB_ASSERT_EQ((*(volatile uint32_t *)(uintptr_t)0xE000ED0Cu >> 8) & 7u, 0u);
}

#if EMB_IRQ_LEVEL_MAX > 0
EMB_TEST(irq_priority_higher_generic_level_wins_pending_order)
{
    emb_irq_key_t key;
    EMB_TEST_REQ("KRN-IRQ-028");
    priority_setup(priority_pending_low, (uint8_t)EMB_IRQ_LEVEL_MAX);
    key = emb_irq_lock();
    EMB_ASSERT_OK(emb_irq_pend(PRIORITY_LOW));
    EMB_ASSERT_OK(emb_irq_pend(PRIORITY_HIGH));
    EMB_ASSERT_EQ(emb_test_marks_count(), 0u);
    emb_irq_unlock(key);
    priority_settle(2u);
    EMB_ASSERT_MARKS(20, 10);
    priority_cleanup();
}

#if CONFIG_EMB_IRQ_NESTING
EMB_TEST(irq_priority_higher_generic_level_preempts_lower)
{
    EMB_TEST_REQ("KRN-IRQ-028");
    priority_setup(priority_low, (uint8_t)EMB_IRQ_LEVEL_MAX);
    wait_for_preemption = true;
    EMB_ASSERT_OK(emb_irq_pend(PRIORITY_LOW));
    priority_settle(3u);
    EMB_ASSERT_MARKS(10, 20, 30);
    priority_cleanup();
}
#endif
#endif /* multiple generic levels */

#if CONFIG_EMB_IRQ_NESTING && EMB_IRQ_LEVEL_MAX > 0 && CONFIG_EMB_TIMER && CONFIG_EMB_WORKQ && \
    !defined(__ARM_ARCH_6M__)
/* A nested kernel-aware interrupt inside an EMB_TIMER_ISR_CONTEXT callback (review F6): the
 * callback pends a higher level, whose handler cancels and reschedules a delayed item due in
 * the same expiry pass. SysTick runs at the highest kernel-aware level by default, so the
 * test lowers it for its run: only then can a kernel-aware interrupt nest in a callback. */
static emb_workq_storage_t nested_wq_storage;
EMB_THREAD_STACK(nested_wq_stack, EMB_TEST_STACK_SIZE);
static emb_workq_t nested_wq;
static emb_timer_storage_t nested_timer_storage;
static emb_timer_t nested_timer;
static emb_dwork_t nested_work;
static volatile uint32_t nested_ran_at;
static volatile unsigned nested_depth;

static void nested_work_fn(emb_work_t *work)
{
    (void)work;
    nested_ran_at = EMB_TEST_NOW();
    emb_test_mark(40);
}

static void nested_high(void *arg)
{
    (void)arg;
    nested_depth = emb_irq_nesting_depth();
    emb_test_mark(20);
    (void)emb_dwork_cancel(&nested_work, NULL);
    (void)emb_dwork_schedule(nested_wq, &nested_work, EMB_TICKS(4));
}

static void nested_callback(emb_timer_t timer, void *arg)
{
    uint32_t spin = 0u;
    (void)timer;
    (void)arg;
    emb_test_mark(10);
    (void)emb_irq_pend(PRIORITY_HIGH);
    while (nested_depth == 0u && spin < 1000000u) { /* bounded: it preempts at once */
        spin++;
    }
    emb_test_mark(30);
}

EMB_TEST(irq_priority_nested_interrupt_reschedules_work_in_a_timer_callback)
{
    uint8_t systick = SYSTEM_PRIORITY(3u);
    emb_workq_attr_t qa;
    emb_timer_attr_t ta;
    uint32_t t0;
    EMB_TEST_REQ("KRN-WQ-007", "KRN-TIM-025", "KRN-IRQ-012");
    priority_setup(priority_pending_low, (uint8_t)EMB_IRQ_LEVEL_MAX);
    EMB_ASSERT_OK(emb_irq_connect(PRIORITY_HIGH, nested_high, NULL));
    emb_workq_attr_default(&qa);
    qa.stack = nested_wq_stack;
    qa.stack_size = sizeof(nested_wq_stack);
    qa.priority = 2u;
    EMB_ASSERT_OK(emb_workq_init(&nested_wq_storage, &qa, &nested_wq));
    emb_dwork_init(&nested_work, nested_work_fn);
    emb_timer_attr_default(&ta);
    ta.fn = nested_callback;
    ta.flags = EMB_TIMER_ISR_CONTEXT;
    EMB_ASSERT_OK(emb_timer_init(&nested_timer_storage, &ta, &nested_timer));
    nested_depth = 0u;
    SYSTEM_PRIORITY(3u) = embn_cm_irq_priority(0u); /* SysTick below the nested level */
    t0 = EMB_TEST_NOW();
    /* the same deadline, armed after the timer: due in the same pass, after its callback */
    EMB_ASSERT_OK(emb_timer_start(nested_timer, EMB_TICKS(2)));
    EMB_ASSERT_OK(emb_dwork_schedule(nested_wq, &nested_work, EMB_TICKS(2)));
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(9)));
    SYSTEM_PRIORITY(3u) = systick;
    EMB_ASSERT_MARKS(10, 20, 30, 40);
    EMB_ASSERT_EQ(nested_depth, 2u);                          /* inside the tick's interrupt */
    emb_test_check_elapsed(__LINE__, nested_ran_at - t0, 6u); /* the new delay, from tick 2 */
    EMB_ASSERT_OK(emb_timer_destroy(nested_timer));
    EMB_ASSERT_OK(emb_workq_destroy(nested_wq));
    priority_cleanup();
}
#endif

EMB_TEST(irq_priority_equal_levels_do_not_preempt)
{
    EMB_TEST_REQ("KRN-IRQ-028");
    priority_setup(priority_low, 0u);
    wait_for_preemption = false;
    EMB_ASSERT_OK(emb_irq_pend(PRIORITY_LOW));
    priority_settle(3u);
    EMB_ASSERT_MARKS(10, 30, 20);
    priority_cleanup();
}

EMB_TEST(irq_priority_highest_kernel_level_is_masked)
{
    emb_irq_key_t key;
    EMB_TEST_REQ("KRN-IRQ-018", "KRN-IRQ-028");
    priority_setup(priority_pending_low, (uint8_t)EMB_IRQ_LEVEL_MAX);
    key = emb_irq_lock();
    EMB_ASSERT_OK(emb_irq_pend(PRIORITY_HIGH));
    EMB_ASSERT_TRUE(!high_seen);
    emb_test_mark(1);
    emb_irq_unlock(key);
    priority_settle(2u);
    EMB_ASSERT_MARKS(1, 20);
    priority_cleanup();
}
#endif
