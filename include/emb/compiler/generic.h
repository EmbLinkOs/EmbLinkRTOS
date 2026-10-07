/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Compiler portability layer for an unknown C11 compiler (SPEC-001 §10): defines
 * nothing, so every macro takes the documented fallback in <emb/compiler.h>. A port
 * built this way has no naked functions and no sections; it exists so that a new
 * compiler can start compiling the kernel before its own header is written.
 */
#ifndef EMB_COMPILER_GENERIC_H
#define EMB_COMPILER_GENERIC_H

#define EMB_COMPILER_NAME    "generic"
#define EMB_COMPILER_VERSION 0

#endif /* EMB_COMPILER_GENERIC_H */
