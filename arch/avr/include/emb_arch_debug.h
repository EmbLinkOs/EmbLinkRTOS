/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The avr port's part of the debug descriptor (PORT-012, SPEC-011 §14, A107).
 * The AVR frame is not described yet: the descriptor is off by default on AVR, whose constants live
 * in RAM. Interrupts run on the interrupted context's stack (A90). Only kernel/debug/descriptor.c
 * includes this.
 */
#ifndef EMB_ARCH_DEBUG_H
#define EMB_ARCH_DEBUG_H

#include <emb/debug.h>

#define EMB_ARCH_DEBUG_FRAME_KIND  EMB_DEBUG_KIND_NONE
#define EMB_ARCH_DEBUG_FRAME_FLAGS 0u
#define EMB_ARCH_DEBUG_FRAME_SIZE  0u

#endif /* EMB_ARCH_DEBUG_H */
