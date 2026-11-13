/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    generic.h
 * @brief   Compiler portability layer for an unknown C11 compiler (SPEC-001 §10).
 *
 * @details
 * Defines nothing, so every macro takes the documented fallback in <emb/compiler.h>. A port built
 * this way has no naked functions and no sections; it exists so that a new compiler can start
 * compiling the kernel before its own header is written.
 *
 * @author  Junior Deogracias
 * @date    2026-10-07 (created)
 */
#ifndef EMB_COMPILER_GENERIC_H
#define EMB_COMPILER_GENERIC_H

#define EMB_COMPILER_NAME    "generic"
#define EMB_COMPILER_VERSION 0

#endif /* EMB_COMPILER_GENERIC_H */
