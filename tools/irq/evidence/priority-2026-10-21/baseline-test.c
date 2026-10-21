/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Generic priority limits and Cortex-M controller ordering (SPEC-002 §8). */
#include <emb_test.h>
#include <emb/arch.h>

EMB_TEST(irq_priority_accepts_advertised_endpoints)
{
    EMB_TEST_REQ("KRN-IRQ-028", "KRN-IRQ-029");
    EMB_ASSERT_OK(emb_irq_set_level(0u, 0u));
    EMB_ASSERT_OK(emb_irq_set_level(0u, (uint8_t)EMB_IRQ_LEVEL_KERNEL_MAX));
}

EMB_TEST(irq_priority_rejects_out_of_range_level)
{
    EMB_TEST_REQ("API-015", "KRN-IRQ-028");
    EMB_ASSERT_MISUSE(
        emb_irq_set_level(0u, (uint8_t)(EMB_IRQ_LEVEL_KERNEL_MAX + 1u)), EMB_EINVAL);
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
    EMB_ASSERT_STATUS(
        emb_arch_irq_set_level(0u, (uint8_t)(EMB_IRQ_LEVEL_KERNEL_MAX + 1u)), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_arch_irq_set_level((emb_irq_t)EMB_ARCH_IRQ_COUNT, 0u), EMB_EINVAL);
}
