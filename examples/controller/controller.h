/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    controller.h
 * @brief   What the controller's system description (system.yaml) names and its generated system.c
 *          needs.
 *
 * @details
 * The samples queue's item.
 *
 * @author  Junior Deogracias
 * @date    2026-11-13 (created)
 */
#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdint.h>

typedef struct sample {
    uint32_t cycles; /* emb_arch_cycles() at the conversion's interrupt */
    uint16_t raw;    /* the ADC reading */
    uint16_t seq;    /* the conversion's number, modulo 65536 */
} sample_t;

#endif /* CONTROLLER_H */
