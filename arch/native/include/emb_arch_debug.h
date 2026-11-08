/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The native port's part of the debug descriptor (PORT-012, SPEC-011 §14, A107).
 * Native threads are host threads: the host debugger shows their registers, and interrupts run on
 * the host thread's own stack (A90), so the port has no frame or interrupt stack to describe. Only
 * kernel/debug/descriptor.c includes this.
 */
#ifndef EMB_ARCH_DEBUG_H
#define EMB_ARCH_DEBUG_H

#include <emb/debug.h>

#define EMB_ARCH_DEBUG_FRAME_KIND  EMB_DEBUG_KIND_NONE
#define EMB_ARCH_DEBUG_FRAME_FLAGS 0u
#define EMB_ARCH_DEBUG_FRAME_SIZE  0u

#endif /* EMB_ARCH_DEBUG_H */
