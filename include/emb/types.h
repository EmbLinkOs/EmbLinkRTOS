/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Basic types shared by every public header (SPEC-001 §6.1, §7.1): the tick scalar,
 * the maximal-alignment type for object storage, handle declaration and the handle
 * helper macros. Handles are one machine word in a per-object struct so the compiler
 * keeps object types apart (ADR-014); nothing in application code dereferences one.
 */
#ifndef EMB_TYPES_H
#define EMB_TYPES_H

#include <emb/compiler.h>
#include <emb/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_EMB_TICK_32BIT
typedef uint32_t emb_tick_t;
#define EMB_TICK_MAX  UINT32_MAX
#define EMB_TICK_C(n) UINT32_C(n)
#else
typedef uint64_t emb_tick_t;
#define EMB_TICK_MAX  UINT64_MAX
#define EMB_TICK_C(n) UINT64_C(n)
#endif

/* Alignment of any kernel object storage (SPEC-001 §6.2). */
typedef union emb_max_align {
    long long ll_;
    void *p_;
    void (*fp_)(void);
} emb_max_align_t;

/* MISRA Dev CS-12: rule 20.7, `name` is a token. */
#define EMB_DECLARE_HANDLE(name) \
    typedef struct name {        \
        uintptr_t raw;           \
    } name

#define EMB_HANDLE_NULL \
    {                   \
        0               \
    }
#define EMB_HANDLE_IS_NULL(h) ((h).raw == 0u)
#define EMB_HANDLE_EQ(a, b)   ((a).raw == (b).raw)

typedef uint16_t emb_irq_t;   /* interrupt number from the generated SoC table (SPEC-002 §8.2) */
typedef uint32_t emb_cycle_t; /* cycle counter width is 32 bits on every 1.0 port (SPEC-011 §3) */

/* Object attribute flags shared by every object type (SPEC-001 §6.3, SPEC-004 §6.7,
 * ADR-020). Object-specific flags use the low bits of the same byte. */
#define EMB_OBJ_ABORT_WAITERS ((uint8_t)0x80u) /* destroy wakes waiters with EMB_EDESTROYED */
#define EMB_OBJ_FIFO          ((uint8_t)0x40u) /* wait queue in arrival order instead of priority */

#ifdef __cplusplus
}
#endif

#endif /* EMB_TYPES_H */
