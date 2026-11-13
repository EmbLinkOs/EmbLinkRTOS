/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    cm_services.c
 * @brief   Cortex-M port services for boards and tests (emb_cortex_m.h).
 *
 * @author  Junior Deogracias
 * @date    2026-10-09 (created)
 */
#include <emb_cortex_m.h>

void emb_cm_semihosting_exit(int code)
{
    volatile uint32_t block[2];
    block[0] = 0x20026u; /* ADP_Stopped_ApplicationExit */
    block[1] = (uint32_t)code;
#if defined(__ARM_ARCH_6M__)
    __asm__ __volatile__(".syntax unified\n\t"
                         "movs r0, #0x20\n\t" /* SYS_EXIT_EXTENDED; Thumb-1 has movs only */
                         "mov r1, %0\n\t"
                         "bkpt 0xab" ::"r"(block)
                         : "r0", "r1", "cc", "memory");
#else
    __asm__ __volatile__("mov r0, #0x20\n\t" /* SYS_EXIT_EXTENDED */
                         "mov r1, %0\n\t"
                         "bkpt 0xab" ::"r"(block)
                         : "r0", "r1", "memory");
#endif
    for (;;) {
    }
}

void emb_cm_halt(void)
{
    __asm__ __volatile__("cpsid i" ::: "memory");
    for (;;) { /* the panic path (CS-6.2): the run is over */
        __asm__ __volatile__("dsb\n\t"
                             "wfi" ::
                                 : "memory");
    }
}

#if defined(__ARM_FP)
/* Naked: assembly only. The parameter is in r0, so it is marked unused rather than cast to void,
 * a C statement Clang refuses in a naked function. */
EMB_NAKED void emb_cm_fp_fill(uint32_t base EMB_UNUSED)
{
    __asm__ __volatile__("vmov s16, r0\n\tadds r0, #1\n\tvmov s17, r0\n\tadds r0, #1\n\t"
                         "vmov s18, r0\n\tadds r0, #1\n\tvmov s19, r0\n\tadds r0, #1\n\t"
                         "vmov s20, r0\n\tadds r0, #1\n\tvmov s21, r0\n\tadds r0, #1\n\t"
                         "vmov s22, r0\n\tadds r0, #1\n\tvmov s23, r0\n\tadds r0, #1\n\t"
                         "vmov s24, r0\n\tadds r0, #1\n\tvmov s25, r0\n\tadds r0, #1\n\t"
                         "vmov s26, r0\n\tadds r0, #1\n\tvmov s27, r0\n\tadds r0, #1\n\t"
                         "vmov s28, r0\n\tadds r0, #1\n\tvmov s29, r0\n\tadds r0, #1\n\t"
                         "vmov s30, r0\n\tadds r0, #1\n\tvmov s31, r0\n\t"
                         "bx lr\n\t");
}

#define FP_CMP(reg) "vmov r1, " reg "\n\tcmp r1, r0\n\tbne 1f\n\tadds r0, #1\n\t"

EMB_NAKED uint32_t emb_cm_fp_check(uint32_t base EMB_UNUSED)
{
    __asm__ __volatile__(FP_CMP("s16") FP_CMP("s17") FP_CMP("s18") FP_CMP("s19") FP_CMP("s20")
                             FP_CMP("s21") FP_CMP("s22") FP_CMP("s23") FP_CMP("s24") FP_CMP("s25")
                                 FP_CMP("s26") FP_CMP("s27") FP_CMP("s28") FP_CMP("s29")
                                     FP_CMP("s30") FP_CMP("s31") "movs r0, #1\n\t"
                                                                 "bx lr\n\t"
                                                                 "1:\n\t"
                                                                 "movs r0, #0\n\t"
                                                                 "bx lr\n\t");
}
#endif
