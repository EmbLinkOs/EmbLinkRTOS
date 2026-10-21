/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Generic priority limits and Cortex-M controller ordering (SPEC-002 §8). */
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

static emb_status_t context_status;

static void priority_from_isr(void *arg)
{
    (void)arg;
    /* Context wins even when both arguments are invalid (API-013). */
    context_status = emb_irq_set_level((emb_irq_t)EMB_ARCH_IRQ_COUNT, UINT8_MAX);
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
    context_status = EMB_OK;
#if CONFIG_EMB_CHECKED
    EXPECT_PRIORITY_FAULT(emb_test_irq_raise(0u), EMB_FAULT_API_CONTEXT, 0u);
#else
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
