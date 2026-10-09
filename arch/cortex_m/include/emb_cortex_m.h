/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Cortex-M port services for boards and tests (not kernel API): the Arm semihosting exit
 * that emulated boards end a run with, and the floating-point register probes of the FPU
 * conformance test. Inline assembly stays in arch/ (CODING-STANDARD CS-1.5).
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
