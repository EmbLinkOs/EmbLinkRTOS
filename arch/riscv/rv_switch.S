/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    rv_switch.S
 * @brief   RISC-V context frame, switch, trap entry and reset (docs/ports/riscv.md).
 *
 * @details
 * One 128-byte frame serves the thread switch, the trap path and the initial frame:
 *
 *   word 0 mepc, 1 ra, 2-4 t0-t2, 5-6 s0-s1, 7-14 a0-a7, 15-24 s2-s11, 25-28 t3-t6,
 *   29 mstatus, 30-31 padding (16-byte alignment)
 *
 * A context is resumed by writing mstatus and mepc from its frame and executing mret, so
 * MIE comes back from the frame's MPIE: set for a thread interrupted with interrupts on
 * or a fresh one, clear for a thread that switched out inside its critical section.
 * gp is not used by this port and is not saved. tp points from reset at the one
 * thread-local block of the C library (errno, A88), which every thread shares, so no frame
 * carries it either.
 *
 * @author  Junior Deogracias
 * @date    2026-10-09 (created)
 */
#include <emb/config.h>

        .equ    FRAME, 128
        .equ    MSTATUS_MPP_M, 0x1800
        .equ    MSTATUS_MPIE,  0x80
        .equ    MSTATUS_MIE,   0x8

        .macro  SAVE_REGS
        sw      x1,   1*4(sp)
        sw      x5,   2*4(sp)
        sw      x6,   3*4(sp)
        sw      x7,   4*4(sp)
        sw      x8,   5*4(sp)
        sw      x9,   6*4(sp)
        sw      x10,  7*4(sp)
        sw      x11,  8*4(sp)
        sw      x12,  9*4(sp)
        sw      x13, 10*4(sp)
        sw      x14, 11*4(sp)
        sw      x15, 12*4(sp)
        sw      x16, 13*4(sp)
        sw      x17, 14*4(sp)
        sw      x18, 15*4(sp)
        sw      x19, 16*4(sp)
        sw      x20, 17*4(sp)
        sw      x21, 18*4(sp)
        sw      x22, 19*4(sp)
        sw      x23, 20*4(sp)
        sw      x24, 21*4(sp)
        sw      x25, 22*4(sp)
        sw      x26, 23*4(sp)
        sw      x27, 24*4(sp)
        sw      x28, 25*4(sp)
        sw      x29, 26*4(sp)
        sw      x30, 27*4(sp)
        sw      x31, 28*4(sp)
        .endm

        .text

/* void emb_arch_switch_to(embk_thread_t *next): called in the critical section (MIE
 * clear). Saves the caller as a frame that resumes at the return address with MIE still
 * clear, makes @next current, and resumes it. */
        .section .text.emb_arch_switch_to, "ax"
        .globl  emb_arch_switch_to
        .align  2
emb_arch_switch_to:
        addi    sp, sp, -FRAME
        SAVE_REGS
        sw      ra, 0(sp)                  /* mepc: back to the caller */
        csrr    t0, mstatus
        li      t1, MSTATUS_MPP_M
        or      t0, t0, t1
        li      t1, ~(MSTATUS_MPIE | MSTATUS_MIE)
        and     t0, t0, t1                 /* resumes inside its critical section */
        sw      t0, 29*4(sp)
        la      t0, embn_rv_current
        lw      t1, 0(t0)
        sw      sp, 0(t1)                  /* current->arch.sp */
        sw      a0, 0(t0)
        lw      sp, 0(a0)
        j       embn_rv_restore

/* void emb_arch_switch_final(embk_thread_t *next): the exiting thread's last switch,
 * nothing saved. */
        .section .text.emb_arch_switch_final, "ax"
        .globl  emb_arch_switch_final
        .align  2
emb_arch_switch_final:
        la      t0, embn_rv_current
        sw      a0, 0(t0)
        lw      sp, 0(a0)
        j       embn_rv_restore

/* Resume the frame at sp. */
        .section .text.embn_rv_restore, "ax"
        .globl  embn_rv_restore
        .align  2
embn_rv_restore:
        lw      t0, 0(sp)
        csrw    mepc, t0
        lw      t0, 29*4(sp)
        csrw    mstatus, t0                /* MIE stays clear until mret */
        lw      x1,   1*4(sp)
        lw      x6,   3*4(sp)
        lw      x7,   4*4(sp)
        lw      x8,   5*4(sp)
        lw      x9,   6*4(sp)
        lw      x10,  7*4(sp)
        lw      x11,  8*4(sp)
        lw      x12,  9*4(sp)
        lw      x13, 10*4(sp)
        lw      x14, 11*4(sp)
        lw      x15, 12*4(sp)
        lw      x16, 13*4(sp)
        lw      x17, 14*4(sp)
        lw      x18, 15*4(sp)
        lw      x19, 16*4(sp)
        lw      x20, 17*4(sp)
        lw      x21, 18*4(sp)
        lw      x22, 19*4(sp)
        lw      x23, 20*4(sp)
        lw      x24, 21*4(sp)
        lw      x25, 22*4(sp)
        lw      x26, 23*4(sp)
        lw      x27, 24*4(sp)
        lw      x28, 25*4(sp)
        lw      x29, 26*4(sp)
        lw      x30, 27*4(sp)
        lw      x31, 28*4(sp)
        lw      x5,   2*4(sp)
        addi    sp, sp, FRAME
        mret

/* The machine trap vector (mtvec direct mode): save the interrupted context on its own
 * stack, record it, run the C handler on the interrupt stack, resume the context the
 * handler returns (the kernel's interrupt exit decision). */
        .section .text.embn_rv_trap_entry, "ax"
        .globl  embn_rv_trap_entry
        .align  4
embn_rv_trap_entry:
        addi    sp, sp, -FRAME
        SAVE_REGS
        csrr    t0, mepc
        sw      t0, 0(sp)
        csrr    t0, mstatus
        sw      t0, 29*4(sp)
        la      t0, embn_rv_current
        lw      t1, 0(t0)
        sw      sp, 0(t1)
        mv      a3, sp                     /* the frame, for fault reports */
        la      t0, embn_rv_isr_stack_top
        lw      sp, 0(t0)
        csrr    a0, mcause
        csrr    a1, mepc
        csrr    a2, mtval
        call    embn_rv_trap               /* returns the thread to resume */
        la      t0, embn_rv_current
        sw      a0, 0(t0)
        lw      sp, 0(a0)
        j       embn_rv_restore

#if CONFIG_EMB_RV_ESP32C3
/* The ESP32-C3's vectored mtvec (TRM 1.6): exceptions at the base, interrupt n at
 * base + 4n. Each entry jumps to the one trap entry, which reads mcause; norvc keeps
 * every jump four bytes long. */
        .section .text.embn_rv_vectors, "ax"
        .globl  embn_rv_vectors
        .balign 256
        .option push
        .option norvc
embn_rv_vectors:
        .rept   32
        j       embn_rv_trap_entry
        .endr
        .option pop
#endif

/* Reset (QEMU virt with -bios none starts here in machine mode; the ESP32-C3's boot ROM
 * jumps here once it has loaded the image): interrupts off, the
 * stack, .bss cleared, then C. Everything is loaded in RAM, so .data needs no copy. */
/* A word in each thread-local section, so that both exist in every image and the TLS
 * segment starts where the linker script's __emb_tls_start says (A88). */
        .section .tdata.emb_tls_anchor, "awT", @progbits
        .align  2
embn_rv_tdata_anchor:
        .word   0
        .section .tbss.emb_tls_anchor, "awT", @nobits
        .align  2
embn_rv_tbss_anchor:
        .space  4

        .section .text.embn_rv_reset, "ax" /* the one function this name can belong to */
        .globl  embn_rv_reset
        .align  2
embn_rv_reset:
#if !CONFIG_EMB_RV_ESP32C3
        csrw    mie, zero                  /* the ESP32-C3 has no mie: its controller is off */
#endif
        csrci   mstatus, MSTATUS_MIE
        la      sp, __stack_top
        la      t0, __bss_start
        la      t1, __bss_end
1:
        bgeu    t0, t1, 2f
        sw      zero, 0(t0)
        addi    t0, t0, 4
        j       1b
2:
        la      t0, __emb_tbss_start       /* the TLS block's zero part (A88) */
        la      t1, __emb_tls_end
4:
        bgeu    t0, t1, 5f
        sb      zero, 0(t0)
        addi    t0, t0, 1
        j       4b
5:
        la      tp, __emb_tls_start
        call    embn_rv_start
3:
        wfi
        j       3b
