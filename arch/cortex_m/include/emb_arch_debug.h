/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_arch_debug.h
 * @brief   The Cortex-M port's part of the debug descriptor (PORT-012, SPEC-011 §14, A107).
 *
 * @details
 * The switch frame PendSV leaves on a thread's stack (cm_vectors.c) and the interrupt stack. From
 * the saved sp upward: the thread's PSPLIM on Armv8-M Mainline, r4 to r11, EXC_RETURN, s16 to s31
 * when EXC_RETURN bit 4 is clear, then the exception's hardware frame (r0 to r3, r12, lr, pc, xPSR,
 * and s0 to s15, FPSCR and a reserved word when the frame is the extended one), and one word more
 * when xPSR bit 9 says the hardware aligned the stack. Only kernel/debug/descriptor.c includes
 * this.
 *
 * @author  Junior Deogracias
 * @date    2026-11-08 (created)
 */
#ifndef EMB_ARCH_DEBUG_H
#define EMB_ARCH_DEBUG_H

#include <emb/debug.h>

#include <stdint.h>

extern uint8_t embn_cm_isr_stack[];

#if defined(__ARM_ARCH_8M_MAIN__)
#define EMBN_CM_DEBUG_LIMIT   1u
#define EMBN_CM_DEBUG_F_LIMIT EMB_DEBUG_FRAME_F_PSPLIM
#else
#define EMBN_CM_DEBUG_LIMIT   0u
#define EMBN_CM_DEBUG_F_LIMIT 0u
#endif
#if defined(__ARM_FP)
#define EMBN_CM_DEBUG_F_FPU EMB_DEBUG_FRAME_F_FPU
#else
#define EMBN_CM_DEBUG_F_FPU 0u
#endif

#define EMB_ARCH_DEBUG_FRAME_KIND  EMB_DEBUG_KIND_CORTEX_M
#define EMB_ARCH_DEBUG_FRAME_FLAGS (EMBN_CM_DEBUG_F_LIMIT | EMBN_CM_DEBUG_F_FPU)
#define EMB_ARCH_DEBUG_FRAME_SIZE  (4u * (9u + EMBN_CM_DEBUG_LIMIT))
/* GDB's Arm numbers: r4 to r11 are 4 to 11 */
#define EMB_ARCH_DEBUG_FRAME_TABLE                                        \
    {4u, 0u + EMBN_CM_DEBUG_LIMIT}, {5u, 1u + EMBN_CM_DEBUG_LIMIT},       \
        {6u, 2u + EMBN_CM_DEBUG_LIMIT}, {7u, 3u + EMBN_CM_DEBUG_LIMIT},   \
        {8u, 4u + EMBN_CM_DEBUG_LIMIT}, {9u, 5u + EMBN_CM_DEBUG_LIMIT},   \
        {10u, 6u + EMBN_CM_DEBUG_LIMIT}, {11u, 7u + EMBN_CM_DEBUG_LIMIT}, \
    {                                                                     \
        EMB_DEBUG_REG_EXC_RETURN, 8u + EMBN_CM_DEBUG_LIMIT                \
    }
#define EMB_ARCH_DEBUG_ISR_STACK      embn_cm_isr_stack
#define EMB_ARCH_DEBUG_ISR_STACK_SIZE CONFIG_EMB_ISR_STACK_SIZE

#endif /* EMB_ARCH_DEBUG_H */
