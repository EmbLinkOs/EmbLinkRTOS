/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The xtensa port's part of the debug descriptor (PORT-012, SPEC-011 §14, A107).
 * The windowed Xtensa frame is not described yet; the interrupt stack is.
 * Only kernel/debug/descriptor.c includes this.
 */
#ifndef EMB_ARCH_DEBUG_H
#define EMB_ARCH_DEBUG_H

#include <emb/debug.h>

#define EMB_ARCH_DEBUG_FRAME_KIND  EMB_DEBUG_KIND_NONE
#define EMB_ARCH_DEBUG_FRAME_FLAGS 0u
#define EMB_ARCH_DEBUG_FRAME_SIZE  0u

#include <stdint.h>

extern uint8_t embn_xt_isr_stack[];

#define EMB_ARCH_DEBUG_ISR_STACK      embn_xt_isr_stack
#define EMB_ARCH_DEBUG_ISR_STACK_SIZE CONFIG_EMB_ISR_STACK_SIZE

#endif /* EMB_ARCH_DEBUG_H */
