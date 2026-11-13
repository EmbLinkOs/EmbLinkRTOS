/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_arch_debug.h
 * @brief   The native port's part of the debug descriptor (PORT-012, SPEC-011 §14, A107).
 *
 * @details
 * Native threads are host threads: the host debugger shows their registers, and interrupts run on
 * the host thread's own stack (A90), so the port has no frame or interrupt stack to describe. Only
 * kernel/debug/descriptor.c includes this.
 *
 * @author  Junior Deogracias
 * @date    2026-11-08 (created)
 */
#ifndef EMB_ARCH_DEBUG_H
#define EMB_ARCH_DEBUG_H

#include <emb/debug.h>

#define EMB_ARCH_DEBUG_FRAME_KIND  EMB_DEBUG_KIND_NONE
#define EMB_ARCH_DEBUG_FRAME_FLAGS 0u
#define EMB_ARCH_DEBUG_FRAME_SIZE  0u

#endif /* EMB_ARCH_DEBUG_H */
