/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_cortex_m.h
 * @brief   Cortex-M port services for boards and tests (not kernel API).
 *
 * @details
 * The Arm semihosting exit that emulated boards end a run with, the halt that hardware boards end
 * it with, and the floating-point register probes of the FPU conformance test. Inline assembly
 * stays in arch/ (CODING-STANDARD CS-1.5).
 *
 * @author  Junior Deogracias
 * @date    2026-10-09 (created)
 */
#ifndef EMB_CORTEX_M_H
#define EMB_CORTEX_M_H

#include <emb/compiler.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SYS_EXIT_EXTENDED: the emulator (run with -semihosting) exits with @code. Halts a part
 * that has no debugger attached; for emulated boards only. */
EMB_NORETURN void emb_cm_semihosting_exit(int code);

/* Masks every interrupt (PRIMASK) and sleeps for good: how a board with no host to exit to
 * ends a run. A pending interrupt still wakes WFI; the loop sleeps again. */
EMB_NORETURN void emb_cm_halt(void);

/* A stretch with interrupts held off, or the core stalled, longer than a tick: a flash
 * operation (SPEC-017 §9, A122). begin() notes the processor's cycle count and where SysTick
 * is in its tick; end() counts the tick boundaries passed meanwhile, of which SysTick's one
 * pending interrupt delivers one, and has the kernel add the rest at that interrupt, so the
 * clock does not fall behind. Both with interrupts held off; the cycles are DWT CYCCNT's, which
 * an ARMv6-M core and QEMU lack (there nothing is added). */
typedef struct emb_cm_stall {
    uint32_t cycles;  /* DWT CYCCNT at begin */
    uint32_t into;    /* SysTick counts into the tick at begin */
    uint8_t pending;  /* a tick was already pending at begin */
    uint8_t counting; /* the cycle counter runs */
    uint8_t reserved_[2];
} emb_cm_stall_t;

/* PRIMASK set, every interrupt held off, fault handlers aside; the previous value returned for
 * emb_cm_primask_restore(). Inline: for code that runs while nothing may be fetched from flash
 * (the RP2350's flash routine, with XIP off). */
static EMB_ALWAYS_INLINE uint32_t emb_cm_primask_set(void)
{
    uint32_t m;
    __asm__ __volatile__("mrs %0, primask\n\tcpsid i" : "=r"(m)::"memory");
    return m;
}

static EMB_ALWAYS_INLINE void emb_cm_primask_restore(uint32_t m)
{
    __asm__ __volatile__("msr primask, %0" ::"r"(m) : "memory");
}

/* The processor's cycle count, DWT CYCCNT, started on first use; 0 always where there is none
 * (ARMv6-M, QEMU). It wraps every 2^32 cycles. */
uint32_t emb_cm_cycle_count(void);
void emb_cm_stall_begin(emb_cm_stall_t *s);
void emb_cm_stall_end(const emb_cm_stall_t *s);

#if defined(__ARM_FP)
/* s16..s31 = base, base + 1, ... as raw bit patterns. */
void emb_cm_fp_fill(uint32_t base);
/* 1 when s16..s31 still hold the emb_cm_fp_fill(base) pattern, 0 otherwise. */
uint32_t emb_cm_fp_check(uint32_t base);
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_CORTEX_M_H */
