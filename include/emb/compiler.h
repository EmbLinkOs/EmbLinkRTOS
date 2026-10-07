/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The compiler portability macros of SPEC-001 §10 (BLD-002, PORT-ABI-002). Generic
 * code uses only these names for compiler-specific behavior. The per-compiler header
 * defines what it can; everything left undefined takes the fallback below, so a new
 * compiler compiles the kernel with <emb/compiler/generic.h> and loses only
 * optimizations and diagnostics, never correctness. Additions beyond the SPEC-001
 * list, recorded here: EMB_FALLTHROUGH (CODING-STANDARD CS-7.3), EMB_TRAP,
 * EMB_IS_CONSTANT and EMB_IF_CONSTANT (compile-time time conversions, SPEC-001 §7.3),
 * EMB_MEMCPY and EMB_MEMSET (freestanding builtins, CS-1.2), EMB_FLASH_CONST and
 * EMB_FLASH_READ_* (SPEC-012 §8, defined by the AVR port header, no-ops elsewhere).
 */
#ifndef EMB_COMPILER_H
#define EMB_COMPILER_H

#include <stddef.h>
#include <stdint.h>

#if defined(__EMBCC__)
#include <emb/compiler/embcc.h>
#elif defined(__clang__)
#include <emb/compiler/clang.h>
#elif defined(__GNUC__)
#include <emb/compiler/gcc.h>
#else
#include <emb/compiler/generic.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- fallbacks: each has a defined, possibly empty, meaning --------------------- */
#ifndef EMB_INLINE
#define EMB_INLINE inline
#endif
#ifndef EMB_ALWAYS_INLINE
#define EMB_ALWAYS_INLINE inline
#endif
#ifndef EMB_NOINLINE
#define EMB_NOINLINE
#endif
#ifndef EMB_NORETURN
#ifdef __cplusplus
#define EMB_NORETURN [[noreturn]]
#else
#define EMB_NORETURN _Noreturn
#endif
#endif
#ifndef EMB_UNUSED
#define EMB_UNUSED
#endif
#ifndef EMB_USED
#define EMB_USED
#endif
#ifndef EMB_WEAK
#define EMB_WEAK
#endif
#ifndef EMB_ALIAS
#define EMB_ALIAS(sym)
#endif
#ifndef EMB_SECTION
#define EMB_SECTION(name)
#endif
#ifndef EMB_ALIGNED
#ifdef __cplusplus
#define EMB_ALIGNED(n) alignas(n)
#else
#define EMB_ALIGNED(n) _Alignas(n)
#endif
#endif
#ifndef EMB_PACKED
#define EMB_PACKED
#endif
#ifndef EMB_NAKED
#define EMB_NAKED
#endif
#ifndef EMB_DEPRECATED
#define EMB_DEPRECATED(msg)
#endif
#ifndef EMB_PRINTF_LIKE
#define EMB_PRINTF_LIKE(f, a)
#endif
#ifndef EMB_LIKELY
#define EMB_LIKELY(x) (x)
#endif
#ifndef EMB_UNLIKELY
#define EMB_UNLIKELY(x) (x)
#endif
#ifndef EMB_UNREACHABLE
#define EMB_UNREACHABLE() \
    do {                  \
    } while (0)
#endif
#ifndef EMB_COMPILER_BARRIER
#define EMB_COMPILER_BARRIER() \
    do {                       \
    } while (0)
#endif
#ifndef EMB_TRAP
#define EMB_TRAP() \
    do {           \
    } while (0)
#endif
#ifndef EMB_FALLTHROUGH
#ifdef __cplusplus
#define EMB_FALLTHROUGH [[fallthrough]]
#else
#define EMB_FALLTHROUGH \
    do {                \
    } while (0)
#endif
#endif
#ifndef EMB_IS_CONSTANT
#define EMB_IS_CONSTANT(x) 0
#endif
#ifndef EMB_IF_CONSTANT
#define EMB_IF_CONSTANT(x, a, b) (b)
#endif
#ifndef EMB_FLASH_CONST
#define EMB_FLASH_CONST       const
#define EMB_FLASH_READ_U8(p)  (*(p))
#define EMB_FLASH_READ_U16(p) (*(p))
#define EMB_FLASH_READ_PTR(p) (*(p))
#endif

#ifdef __cplusplus
#define EMB_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define EMB_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

/* Recover the containing object of an intrusive member. MISRA Dev CS-12: rules 11.4
 * and 18.4, the one definition the kernel's intrusive lists are allowed to use. */
#define EMB_CONTAINER_OF(ptr, type, member) \
    ((type *)(void *)((char *)(ptr) - offsetof(type, member)))
#define EMB_CONTAINER_OF_CONST(ptr, type, member) \
    ((const type *)(const void *)((const char *)(const void *)(ptr) - offsetof(type, member)))

/* Portable bit scans (fallbacks; the compiler headers replace them with builtins).
 * Each is a bounded loop of at most 32 steps; the result for x == 0 is undefined,
 * as for the builtins. */
static EMB_INLINE unsigned emb_clz32_soft(uint32_t x)
{
    unsigned n = 0u;
    while ((x & UINT32_C(0x80000000)) == 0u) {
        x <<= 1;
        n++;
    }
    return n;
}

static EMB_INLINE unsigned emb_ctz32_soft(uint32_t x)
{
    unsigned n = 0u;
    while ((x & 1u) == 0u) {
        x >>= 1;
        n++;
    }
    return n;
}

static EMB_INLINE unsigned emb_popcount32_soft(uint32_t x)
{
    unsigned n = 0u;
    while (x != 0u) {
        x &= x - 1u;
        n++;
    }
    return n;
}

#ifndef EMB_CLZ32
#define EMB_CLZ32(x) emb_clz32_soft((uint32_t)(x))
#endif
#ifndef EMB_CTZ32
#define EMB_CTZ32(x) emb_ctz32_soft((uint32_t)(x))
#endif
#ifndef EMB_POPCOUNT32
#define EMB_POPCOUNT32(x) emb_popcount32_soft((uint32_t)(x))
#endif

#ifndef EMB_MEMCPY
static EMB_INLINE void *emb_memcpy_soft(void *d, const void *s, size_t n)
{
    unsigned char *dp = (unsigned char *)d;
    const unsigned char *sp = (const unsigned char *)s;
    size_t i;
    for (i = 0u; i < n; i++) {
        dp[i] = sp[i];
    }
    return d;
}
#define EMB_MEMCPY(d, s, n) emb_memcpy_soft((d), (s), (n))
#endif
#ifndef EMB_MEMSET
static EMB_INLINE void *emb_memset_soft(void *d, int c, size_t n)
{
    unsigned char *dp = (unsigned char *)d;
    size_t i;
    for (i = 0u; i < n; i++) {
        dp[i] = (unsigned char)c;
    }
    return d;
}
#define EMB_MEMSET(d, c, n) emb_memset_soft((d), (c), (n))
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_COMPILER_H */
