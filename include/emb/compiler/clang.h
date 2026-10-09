/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Compiler portability layer for Clang (SPEC-001 §10, BLD-002). Clang accepts the GCC
 * attribute set used here; the differences are the version macro and the
 * fallthrough spelling.
 */
#ifndef EMB_COMPILER_CLANG_H
#define EMB_COMPILER_CLANG_H

#define EMB_COMPILER_NAME "clang"
#define EMB_COMPILER_VERSION \
    ((__clang_major__ * 10000) + (__clang_minor__ * 100) + __clang_patchlevel__)

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
#define EMB_FALLTHROUGH        __attribute__((fallthrough))

#ifndef __cplusplus
#define EMB_IF_CONSTANT(x, a, b) __builtin_choose_expr(__builtin_constant_p(x), (a), (b))
#endif

#define EMB_CLZ32(x) \
    ((unsigned)__builtin_clzl((unsigned long)(x)) - (unsigned)(sizeof(unsigned long) * 8u - 32u))
#define EMB_CTZ32(x)      ((unsigned)__builtin_ctzl((unsigned long)(x)))
#define EMB_POPCOUNT32(x) ((unsigned)__builtin_popcountl((unsigned long)(x)))

#define EMB_MEMCPY(d, s, n) __builtin_memcpy((d), (s), (n))
#define EMB_MEMSET(d, c, n) __builtin_memset((d), (c), (n))

#ifdef __AVR__
/* Harvard core: const tables stay in program memory and are read with the LPM forms
 * (SPEC-012 §8). Registration tables go to .progmem.<name>, which the board's linker
 * script brackets with __start_<name>/__stop_<name>. */
#include <avr/pgmspace.h>
#define EMB_FLASH_CONST          const __attribute__((__progmem__))
#define EMB_FLASH_READ_U8(p)     pgm_read_byte(p)
#define EMB_FLASH_READ_U16(p)    pgm_read_word(p)
#define EMB_FLASH_READ_PTR(p)    ((void *)pgm_read_word(p))
#define EMB_FLASH_READ_FNPTR(pp) ((uintptr_t)pgm_read_word(pp))
#define EMB_TABLE_SECTION(name)  ".progmem." name
#endif

/* Writes "->EMB_PROBE <name> <size> <align>" into the object file as plain text: the
 * "i" constraints require compile-time constants, %c prints them as bare decimals on
 * every target (no '$' or '#'). Read by tools/storage/gen_storage.py. */
#define EMB_LAYOUT_MARKER(name, size, align) \
    __asm__ __volatile__("\n.ascii \"->EMB_PROBE " #name " %c0 %c1\"\n" : : "i"(size), "i"(align))

#endif /* EMB_COMPILER_CLANG_H */
