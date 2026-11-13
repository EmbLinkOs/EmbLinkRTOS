/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_arch_debug.h
 * @brief   The RISC-V port's part of the debug descriptor (PORT-012, SPEC-011 §14, A107).
 *
 * @details
 * The one 128-byte frame of rv_switch.S, which the switch, the trap path and the initial frame
 * share, and the interrupt stack. A thread resumes with sp at the saved sp plus the frame and pc at
 * mepc (word 0). Only kernel/debug/descriptor.c includes this.
 *
 * @author  Junior Deogracias
 * @date    2026-11-08 (created)
 */
#ifndef EMB_ARCH_DEBUG_H
#define EMB_ARCH_DEBUG_H

#include <emb/debug.h>

#include <stdint.h>

extern uint8_t embn_rv_isr_stack[];

#define EMB_ARCH_DEBUG_FRAME_KIND  EMB_DEBUG_KIND_TABLE
#define EMB_ARCH_DEBUG_FRAME_FLAGS 0u
#define EMB_ARCH_DEBUG_FRAME_SIZE  128u
/* GDB's RISC-V numbers: x1 to x31 are 1 to 31, pc is 32 */
#define EMB_ARCH_DEBUG_FRAME_TABLE                                                               \
    {32u, 0u}, {1u, 1u}, {5u, 2u}, {6u, 3u}, {7u, 4u}, {8u, 5u}, {9u, 6u}, {10u, 7u}, {11u, 8u}, \
        {12u, 9u}, {13u, 10u}, {14u, 11u}, {15u, 12u}, {16u, 13u}, {17u, 14u}, {18u, 15u},       \
        {19u, 16u}, {20u, 17u}, {21u, 18u}, {22u, 19u}, {23u, 20u}, {24u, 21u}, {25u, 22u},      \
        {26u, 23u}, {27u, 24u}, {28u, 25u}, {29u, 26u}, {30u, 27u},                              \
    {                                                                                            \
        31u, 28u                                                                                 \
    }
#define EMB_ARCH_DEBUG_ISR_STACK      embn_rv_isr_stack
#define EMB_ARCH_DEBUG_ISR_STACK_SIZE CONFIG_EMB_ISR_STACK_SIZE

#endif /* EMB_ARCH_DEBUG_H */
