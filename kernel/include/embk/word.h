/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The kernel's bitmap word (ADR-036, SPEC-012 §2): CONFIG_EMB_ARCH_WORD_BITS wide,
 * one bit per priority level (ready set) or per thread (tiny wait queues). The
 * highest set bit is the highest priority; finding it is one builtin on 32- and
 * 64-bit cores and a four-step nibble lookup on AVR (SPEC-012 §6).
 */
#ifndef EMBK_WORD_H
#define EMBK_WORD_H

#include <emb/compiler.h>
#include <emb/config.h>

#include <stdint.h>

#if CONFIG_EMB_ARCH_WORD_BITS == 8
typedef uint8_t embk_word_t;
#define EMBK_WORD_BITS 8u
#define EMBK_WORD_C(n) ((uint8_t)(n))
#elif CONFIG_EMB_ARCH_WORD_BITS == 16
typedef uint16_t embk_word_t;
#define EMBK_WORD_BITS 16u
#define EMBK_WORD_C(n) ((uint16_t)(n))
#elif CONFIG_EMB_ARCH_WORD_BITS == 64
typedef uint64_t embk_word_t;
#define EMBK_WORD_BITS 64u
#define EMBK_WORD_C(n) UINT64_C(n)
#else
typedef uint32_t embk_word_t;
#define EMBK_WORD_BITS 32u
#define EMBK_WORD_C(n) UINT32_C(n)
#endif

#define EMBK_WORD_BIT(n) ((embk_word_t)(EMBK_WORD_C(1) << (n)))

/* Index of the highest set bit; @w must be nonzero. */
static EMB_INLINE unsigned embk_word_highest(embk_word_t w)
{
#if CONFIG_EMB_ARCH_WORD_BITS == 8 || CONFIG_EMB_ARCH_WORD_BITS == 16
    /* Bounded: at most EMBK_WORD_BITS / 4 nibble steps plus a 16-entry lookup. */
    static EMB_FLASH_CONST uint8_t nibble_highest[16] = {0u, 0u, 1u, 1u, 2u, 2u, 2u, 2u,
                                                         3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u};
    unsigned base = 0u;
#if CONFIG_EMB_ARCH_WORD_BITS == 16
    if ((w & (embk_word_t)0xFF00u) != 0u) {
        w = (embk_word_t)(w >> 8);
        base += 8u;
    }
#endif
    if ((w & (embk_word_t)0xF0u) != 0u) {
        w = (embk_word_t)(w >> 4);
        base += 4u;
    }
    return base + EMB_FLASH_READ_U8(&nibble_highest[w & 0x0Fu]);
#elif CONFIG_EMB_ARCH_WORD_BITS == 64
    uint32_t hi = (uint32_t)(w >> 32);
    if (hi != 0u) {
        return 63u - EMB_CLZ32(hi);
    }
    return 31u - EMB_CLZ32((uint32_t)w);
#else
    return 31u - EMB_CLZ32(w);
#endif
}

/* Index of the lowest set bit; @w must be nonzero. */
static EMB_INLINE unsigned embk_word_lowest(embk_word_t w)
{
#if CONFIG_EMB_ARCH_WORD_BITS == 8 || CONFIG_EMB_ARCH_WORD_BITS == 16
    unsigned n = 0u;
    while ((w & 1u) == 0u) { /* bounded by EMBK_WORD_BITS */
        w = (embk_word_t)(w >> 1);
        n++;
    }
    return n;
#elif CONFIG_EMB_ARCH_WORD_BITS == 64
    uint32_t lo = (uint32_t)w;
    if (lo != 0u) {
        return EMB_CTZ32(lo);
    }
    return 32u + EMB_CTZ32((uint32_t)(w >> 32));
#else
    return EMB_CTZ32(w);
#endif
}

#endif /* EMBK_WORD_H */
