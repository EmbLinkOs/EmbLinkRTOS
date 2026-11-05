/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Xtensa LX6 vectors, the level-1 entry and reset (docs/ports/xtensa.md, ADR-054): the classic
 * ESP32's PRO_CPU, the windowed ABI. VECBASE takes the table, 1 KB aligned at the start of IRAM;
 * the offsets are the LX6's as the ESP32's core-isa.h gives them.
 *
 * Every context switch happens in the level-1 entry, so one frame serves the interrupt, the
 * switch and the initial frame. It is 128 bytes below the interrupted stack pointer:
 *
 *   0 a0, 4 a1, 8-60 a2-a15, 64 pc, 68 ps, 72 sar, 76 lbeg, 80 lend, 84 lcount, 88-127 unused
 *
 * The unused top words keep the frame off the 16 bytes below the interrupted stack pointer,
 * where the windowed ABI spills the interrupted function's caller's a0-a3.
 *
 * The entry saves the frame, then spills every other register window of the interrupted thread
 * to that thread's own stack, while a1 still holds the interrupted stack pointer, which the
 * caller's spill address is computed from. Only then does it move a1 to the interrupt stack and
 * call the C handler, embn_xt_irq(), which returns the frame to resume: the same one, or another
 * thread's, whose windows were spilled the same way when it was switched out. With
 * WINDOWSTART reduced to the current window, the resumed thread's returns refill its windows
 * from its own stack (window underflow).
 */
#include <emb/config.h>

        .equ    F_A0, 0
        .equ    F_A1, 4
        .equ    F_A2, 8
        .equ    F_PC, 64
        .equ    F_PS, 68
        .equ    F_SAR, 72
        .equ    F_LBEG, 76
        .equ    F_LEND, 80
        .equ    F_LCOUNT, 84
        .equ    FRAME, 128

        .equ    PS_WOE, 0x40000
        .equ    PS_UM, 0x20
        .equ    PS_EXCM, 0x10
        .equ    EXCCAUSE_LEVEL1, 4

/* Every register window except the current one to its stack (the Xtensa ISA's window
 * overflow, taken once per live frame): a no-op write to a12 of a rotated window raises the
 * overflow of the frame that owns those registers. Four rotations of three quads and one of
 * four make the 64 registers' full turn. Needs PS.WOE set, PS.EXCM clear; keeps a0-a3. */
        .macro  SPILL_WINDOWS
        and     a12, a12, a12
        rotw    3
        and     a12, a12, a12
        rotw    3
        and     a12, a12, a12
        rotw    3
        and     a12, a12, a12
        rotw    3
        and     a12, a12, a12
        rotw    4
        .endm

/* WINDOWSTART: only the current window's frame is live. Uses a2, a3. */
        .macro  WINDOW_RESET
        rsr     a2, windowbase
        ssl     a2
        movi    a3, 1
        sll     a3, a3
        wsr     a3, windowstart
        rsync
        .endm

        .section .text.embn_xt_vectors, "ax"
        .align  1024
        .global embn_xt_vectors
embn_xt_vectors:

/* ---- window overflow and underflow (the Xtensa ISA's standard handlers) --------------- */
        .org    embn_xt_vectors + 0x000
        s32e    a0, a5, -16             /* WindowOverflow4 */
        s32e    a1, a5, -12
        s32e    a2, a5, -8
        s32e    a3, a5, -4
        rfwo

        .org    embn_xt_vectors + 0x040
        l32e    a0, a5, -16             /* WindowUnderflow4 */
        l32e    a1, a5, -12
        l32e    a2, a5, -8
        l32e    a3, a5, -4
        rfwu

        .org    embn_xt_vectors + 0x080
        s32e    a0, a9, -16             /* WindowOverflow8 */
        l32e    a0, a1, -12
        s32e    a1, a9, -12
        s32e    a2, a9, -8
        s32e    a3, a9, -4
        s32e    a4, a0, -32
        s32e    a5, a0, -28
        s32e    a6, a0, -24
        s32e    a7, a0, -20
        rfwo

        .org    embn_xt_vectors + 0x0C0
        l32e    a1, a9, -12             /* WindowUnderflow8 */
        l32e    a0, a9, -16
        l32e    a7, a1, -12
        l32e    a2, a9, -8
        l32e    a4, a7, -32
        l32e    a3, a9, -4
        l32e    a5, a7, -28
        l32e    a6, a7, -24
        l32e    a7, a7, -20
        rfwu

        .org    embn_xt_vectors + 0x100
        s32e    a0, a13, -16            /* WindowOverflow12 */
        l32e    a0, a1, -12
        s32e    a1, a13, -12
        s32e    a2, a13, -8
        s32e    a3, a13, -4
        s32e    a4, a0, -48
        s32e    a5, a0, -44
        s32e    a6, a0, -40
        s32e    a7, a0, -36
        s32e    a8, a0, -32
        s32e    a9, a0, -28
        s32e    a10, a0, -24
        s32e    a11, a0, -20
        rfwo

        .org    embn_xt_vectors + 0x140
        l32e    a1, a13, -12            /* WindowUnderflow12 */
        l32e    a0, a13, -16
        l32e    a11, a1, -12
        l32e    a2, a13, -8
        l32e    a4, a11, -48
        l32e    a8, a11, -32
        l32e    a3, a13, -4
        l32e    a5, a11, -44
        l32e    a6, a11, -40
        l32e    a7, a11, -36
        l32e    a9, a11, -28
        l32e    a10, a11, -24
        l32e    a11, a11, -20
        rfwu

/* ---- levels 2 to 5, debug, NMI: never enabled; stop where a debugger can see it -------- */
        .org    embn_xt_vectors + 0x180
1:      j       1b
        .org    embn_xt_vectors + 0x1C0
1:      j       1b
        .org    embn_xt_vectors + 0x200
1:      j       1b
        .org    embn_xt_vectors + 0x240
1:      j       1b
        .org    embn_xt_vectors + 0x280
1:      j       1b
        .org    embn_xt_vectors + 0x2C0
1:      j       1b

/* ---- kernel exception (PS.UM clear: the port always runs with UM set) ------------------ */
        .org    embn_xt_vectors + 0x300
        wsr     a0, excsave1
        call0   embn_xt_fault_entry

/* ---- user exception: level-1 interrupts and every other exception ---------------------- */
        .org    embn_xt_vectors + 0x340
        wsr     a0, excsave1
        call0   embn_xt_user_entry

/* ---- double exception: an exception with PS.EXCM set; the cause stays in the registers -- */
        .org    embn_xt_vectors + 0x3C0
1:      j       1b
        .org    embn_xt_vectors + 0x400

/* ---- the level-1 entry ------------------------------------------------------------------- */

        .section .text.embn_xt_user_entry, "ax"
        .align  4
embn_xt_user_entry:
        rsr     a0, exccause
        beqi    a0, EXCCAUSE_LEVEL1, 1f
        j       embn_xt_fault_entry
1:      addi    a0, a1, -FRAME          /* a0: the frame; a1 untouched until the spill */
        s32i    a1, a0, F_A1
        s32i    a2, a0, F_A2
        s32i    a3, a0, F_A2 + 4
        s32i    a4, a0, F_A2 + 8
        s32i    a5, a0, F_A2 + 12
        s32i    a6, a0, F_A2 + 16
        s32i    a7, a0, F_A2 + 20
        s32i    a8, a0, F_A2 + 24
        s32i    a9, a0, F_A2 + 28
        s32i    a10, a0, F_A2 + 32
        s32i    a11, a0, F_A2 + 36
        s32i    a12, a0, F_A2 + 40
        s32i    a13, a0, F_A2 + 44
        s32i    a14, a0, F_A2 + 48
        s32i    a15, a0, F_A2 + 52
        rsr     a2, excsave1
        s32i    a2, a0, F_A0
        rsr     a2, epc1
        s32i    a2, a0, F_PC
        rsr     a2, ps
        s32i    a2, a0, F_PS
        rsr     a2, sar
        s32i    a2, a0, F_SAR
        rsr     a2, lbeg
        s32i    a2, a0, F_LBEG
        rsr     a2, lend
        s32i    a2, a0, F_LEND
        rsr     a2, lcount
        s32i    a2, a0, F_LCOUNT
        /* EPC1 and PS are saved: window exceptions on, level-1 interrupts still masked */
        movi    a2, PS_WOE | PS_UM | 1
        wsr     a2, ps
        rsync
        SPILL_WINDOWS
        movi    a1, embn_xt_isr_stack_top
        l32i    a1, a1, 0
        mov     a6, a0
        call4   embn_xt_irq             /* returns the frame to resume */
        mov     a0, a6
        /* EXCM again: no window exceptions, EPC1 is ours until the rfe */
        movi    a2, PS_WOE | PS_UM | PS_EXCM | 1
        wsr     a2, ps
        rsync
        WINDOW_RESET
        j       embn_xt_resume

/* Resume the frame at a0 with PS.EXCM set; a0 to a15 come from the frame. */
        .section .text.embn_xt_resume, "ax"
        .align  4
        .global embn_xt_resume
embn_xt_resume:
        l32i    a2, a0, F_LCOUNT
        wsr     a2, lcount
        l32i    a2, a0, F_LEND
        wsr     a2, lend
        l32i    a2, a0, F_LBEG
        wsr     a2, lbeg
        l32i    a2, a0, F_SAR
        wsr     a2, sar
        l32i    a2, a0, F_PC
        wsr     a2, epc1
        l32i    a2, a0, F_PS            /* EXCM set in every saved PS: still masked */
        wsr     a2, ps
        l32i    a2, a0, F_A2
        l32i    a3, a0, F_A2 + 4
        l32i    a4, a0, F_A2 + 8
        l32i    a5, a0, F_A2 + 12
        l32i    a6, a0, F_A2 + 16
        l32i    a7, a0, F_A2 + 20
        l32i    a8, a0, F_A2 + 24
        l32i    a9, a0, F_A2 + 28
        l32i    a10, a0, F_A2 + 32
        l32i    a11, a0, F_A2 + 36
        l32i    a12, a0, F_A2 + 40
        l32i    a13, a0, F_A2 + 44
        l32i    a14, a0, F_A2 + 48
        l32i    a15, a0, F_A2 + 52
        l32i    a1, a0, F_A1
        l32i    a0, a0, F_A0
        rsync
        rfe

/* ---- faults ------------------------------------------------------------------------------ */

/* Any exception but a level-1 interrupt: no return. The frames of the faulting context are
 * dropped (WINDOWSTART reduced to the current window), and the kernel's hardware fault entry
 * runs on the interrupt stack: embk_fault_raise_hw(EXCCAUSE, EXCVADDR, EPC1, sp). */
        .section .text.embn_xt_fault_entry, "ax"
        .align  4
        .global embn_xt_fault_entry
embn_xt_fault_entry:
        WINDOW_RESET
        mov     a9, a1
        rsr     a6, exccause
        rsr     a7, excvaddr
        rsr     a8, epc1
        movi    a1, embn_xt_isr_stack_top
        l32i    a1, a1, 0
        movi    a2, PS_WOE | PS_UM | 1
        wsr     a2, ps
        rsync
        movi    a0, 0
        call4   embk_fault_raise_hw
1:      j       1b

/* ---- reset ------------------------------------------------------------------------------- */

/* The boot ROM calls the entry once it has loaded the image's segments, on its own stack and
 * vectors. Its frames are dropped (nothing returns to it), level-1 interrupts are masked, the
 * stack moves to the linker script's, .bss is cleared, then C. Touching only a0-a3 before
 * WINDOWSTART is reduced keeps the ROM's live windows from overflowing. */
        .section .text.call_start_cpu0, "ax"
        .align  4
        .global call_start_cpu0
call_start_cpu0:
        rsil    a2, 1
        WINDOW_RESET
        movi    a2, PS_WOE | PS_UM | 1
        wsr     a2, ps
        rsync
        movi    a1, __stack_top
        movi    a2, __bss_start
        movi    a3, __bss_end
        movi    a0, 0
1:      bgeu    a2, a3, 2f
        s32i    a0, a2, 0
        addi    a2, a2, 4
        j       1b
2:      call4   embn_xt_start
3:      waiti   1
        j       3b
