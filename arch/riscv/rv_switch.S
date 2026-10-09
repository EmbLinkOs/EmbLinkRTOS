/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * RISC-V context frame, switch, trap entry and reset (docs/ports/riscv.md). One 128-byte
 * frame serves the thread switch, the trap path and the initial frame:
 *
 *   word 0 mepc, 1 ra, 2-4 t0-t2, 5-6 s0-s1, 7-14 a0-a7, 15-24 s2-s11, 25-28 t3-t6,
 *   29 mstatus, 30-31 padding (16-byte alignment)
 *
 * A context is resumed by writing mstatus and mepc from its frame and executing mret, so
 * MIE comes back from the frame's MPIE: set for a thread interrupted with interrupts on
 * or a fresh one, clear for a thread that switched out inside its critical section.
 * gp and tp are not used by this port and are not saved.
 */
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

/* Reset (QEMU virt with -bios none starts here in machine mode): interrupts off, the
 * stack, .bss cleared, then C. Everything is loaded in RAM, so .data needs no copy. */
        .section .text.reset, "ax"
        .globl  embn_rv_reset
        .align  2
embn_rv_reset:
        csrw    mie, zero
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
        call    embn_rv_start
3:
        wfi
        j       3b
