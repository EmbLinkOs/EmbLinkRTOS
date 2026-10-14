/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Compiler portability layer for EmbCC (SPEC-001 §10; constraints from document 09).
 * EmbCC accepts the GCC attribute set listed in 09 §4 and the builtins named there.
 * Current EmbCC supports typedef alignment and constructor priorities; the RTOS
 * keeps object/member alignment and generated initialization tables as policy.
 * The predefined macro __EMBCC__ was observed in the 2026-10-14 capability probes.
 * Full RTOS qualification remains separate from the compiler capability probes.
 */
#ifndef EMB_COMPILER_EMBCC_H
#define EMB_COMPILER_EMBCC_H

#define EMB_COMPILER_NAME "embcc"
#ifdef __EMBCC_VERSION__
#define EMB_COMPILER_VERSION __EMBCC_VERSION__
#else
#define EMB_COMPILER_VERSION 0
#endif

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
#define EMB_LIKELY(x)          __builtin_expect(!!(x), 1)
#define EMB_UNLIKELY(x)        __builtin_expect(!!(x), 0)
#define EMB_COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")
#define EMB_TRAP()             __builtin_trap()

#define EMB_MEMCPY(d, s, n) __builtin_memcpy((d), (s), (n))
#define EMB_MEMSET(d, c, n) __builtin_memset((d), (c), (n))

/* EMB_CLZ32 and friends: the availability of __builtin_clz is verified by a probe at
 * configure time (09 §4); until then the portable fallbacks of <emb/compiler.h> apply. */

/* The storage probe writes sizes/alignment as text in the object, never executing
 * these bytes. Immediate operands and %c keep the marker target-independent;
 * realign subsequent instructions after the variable-length text. */
#define EMB_LAYOUT_MARKER(name, size, align)                                        \
    __asm__ __volatile__("\n.ascii \"->EMB_PROBE " #name " %c0 %c1\"\n.p2align 2\n" \
                         :                                                          \
                         : "i"(size), "i"(align))

#endif /* EMB_COMPILER_EMBCC_H */
