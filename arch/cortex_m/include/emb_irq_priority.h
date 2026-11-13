/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_irq_priority.h
 * @brief   Internal NVIC encoding; callers validate the advertised generic domain first.
 *
 * @author  Junior Deogracias
 * @date    2026-10-21 (created)
 */
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
