/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Compiler portability layer for GCC (SPEC-001 §10, BLD-002). Nothing includes this
 * file directly: <emb/compiler.h> selects it. Every macro here has a fallback in
 * <emb/compiler.h>; this file defines only what GCC does better than the fallback.
 */
#ifndef EMB_COMPILER_GCC_H
#define EMB_COMPILER_GCC_H

#define EMB_COMPILER_NAME    "gcc"
#define EMB_COMPILER_VERSION ((__GNUC__ * 10000) + (__GNUC_MINOR__ * 100) + __GNUC_PATCHLEVEL__)

#define EMB_INLINE             inline
#define EMB_ALWAYS_INLINE      inline __attribute__((always_inline))
#define EMB_NOINLINE           __attribute__((noinline))
#define EMB_NORETURN           __attribute__((noreturn))
#define EMB_UNUSED             __attribute__((unused))
#define EMB_USED               __attribute__((used))
#define EMB_WEAK               __attribute__((weak))
#define EMB_ALIAS(sym)         __attribute__((alias(#sym)))
#define EMB_SECTION(name)      __attribute__((section(name)))
#define EMB_ALIGNED(n)         __attribute__((aligned(n)))
#define EMB_PACKED             __attribute__((packed))
#define EMB_NAKED              __attribute__((naked))
#define EMB_DEPRECATED(msg)    __attribute__((deprecated(msg)))
#define EMB_PRINTF_LIKE(f, a)  __attribute__((format(printf, f, a)))
#define EMB_LIKELY(x)          __builtin_expect(!!(x), 1)
#define EMB_UNLIKELY(x)        __builtin_expect(!!(x), 0)
#define EMB_UNREACHABLE()      __builtin_unreachable()
#define EMB_COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")
#define EMB_TRAP()             __builtin_trap()
#define EMB_IS_CONSTANT(x)     __builtin_constant_p(x)

#if __GNUC__ >= 7
#define EMB_FALLTHROUGH __attribute__((fallthrough))
#endif

#ifndef __cplusplus
#define EMB_IF_CONSTANT(x, a, b) __builtin_choose_expr(__builtin_constant_p(x), (a), (b))
#endif

/* __builtin_clz operates on unsigned int: 16 bits on AVR, so the 32-bit forms go
 * through unsigned long there (always at least 32 bits). */
#define EMB_CLZ32(x) \
    ((unsigned)__builtin_clzl((unsigned long)(x)) - (unsigned)(sizeof(unsigned long) * 8u - 32u))
#define EMB_CTZ32(x)      ((unsigned)__builtin_ctzl((unsigned long)(x)))
#define EMB_POPCOUNT32(x) ((unsigned)__builtin_popcountl((unsigned long)(x)))

#define EMB_MEMCPY(d, s, n) __builtin_memcpy((d), (s), (n))
#define EMB_MEMSET(d, c, n) __builtin_memset((d), (c), (n))

#endif /* EMB_COMPILER_GCC_H */
