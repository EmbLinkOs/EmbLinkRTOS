/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Interrupt management API (SPEC-002 §8.2): controller operations through the port. */
#include <emb/irq.h>

#include <embk/kernel.h>

emb_status_t emb_irq_enable(emb_irq_t irq)
{
    return emb_arch_irq_enable(irq);
}

emb_status_t emb_irq_disable(emb_irq_t irq)
{
    return emb_arch_irq_disable(irq);
}

bool emb_irq_is_enabled(emb_irq_t irq)
{
    return emb_arch_irq_is_enabled(irq);
}

emb_status_t emb_irq_set_level(emb_irq_t irq, uint8_t level)
{
    EMBK_REQUIRE_NOT_ISR();
    return emb_arch_irq_set_level(irq, level);
}

emb_status_t emb_irq_pend(emb_irq_t irq)
{
    return emb_arch_irq_pend_soft(irq);
}

emb_status_t emb_irq_clear_pending(emb_irq_t irq)
{
    return emb_arch_irq_clear_pending(irq);
}

#if CONFIG_EMB_IRQ_DYNAMIC

typedef struct irq_entry {
    emb_isr_fn_t fn;
    void *arg;
} irq_entry_t;

static irq_entry_t irq_table[EMB_ARCH_IRQ_COUNT]; /* lock: critical section */

emb_status_t emb_irq_connect(emb_irq_t irq, emb_isr_fn_t fn, void *arg)
{
    emb_irq_key_t key;
    EMBK_REQUIRE_NOT_ISR();
    EMBK_REQUIRE(irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_REQUIRE(fn != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    key = embk_irq_lock();
    irq_table[irq].fn = fn;
    irq_table[irq].arg = arg;
    embk_irq_unlock(key);
    return EMB_OK;
}

void embk_isr_dispatch(emb_irq_t irq)
{
    if (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT && irq_table[irq].fn != NULL) {
        irq_table[irq].fn(irq_table[irq].arg);
    } else {
        embk_fault_raise(EMB_FAULT_SPURIOUS_IRQ, irq, 0u, EMBK_WHERE);
    }
}

#endif
