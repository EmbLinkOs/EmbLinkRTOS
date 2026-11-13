/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* What the controller's system description (system.yaml) names and its generated system.c
 * needs: the samples queue's item. */
#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdint.h>

typedef struct sample {
    uint32_t cycles; /* emb_arch_cycles() at the conversion's interrupt */
    uint16_t raw;    /* the ADC reading */
    uint16_t seq;    /* the conversion's number, modulo 65536 */
} sample_t;

#endif /* CONTROLLER_H */
