/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Internal NVIC encoding; callers validate the advertised generic domain first. */
#ifndef EMB_CORTEX_M_IRQ_PRIORITY_H
#define EMB_CORTEX_M_IRQ_PRIORITY_H

#include <emb/config.h>

#include <stdint.h>

#define EMB_CM_IRQ_PRIORITY_STEP   (1u << (8u - CONFIG_EMB_CM_IRQ_PRIORITY_BITS))
#define EMB_CM_IRQ_LOWEST_PRIORITY (256u - 2u * EMB_CM_IRQ_PRIORITY_STEP)

static inline uint8_t embn_cm_irq_priority(uint8_t level)
{
    return (uint8_t)(EMB_CM_IRQ_LOWEST_PRIORITY - EMB_CM_IRQ_PRIORITY_STEP * (uint32_t)level);
}

#endif /* EMB_CORTEX_M_IRQ_PRIORITY_H */
