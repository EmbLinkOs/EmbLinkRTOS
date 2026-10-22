/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * RISC-V port, RV32 machine mode (docs/ports/riscv.md; contract SPEC-011): the trap
 * handler, startup, idle, faults. The frame and the switch are in rv_switch.S; the tick,
 * the interrupt controller and the cycle counter are the SoC's: the CLINT (rv_clint.c,
 * QEMU virt) or the ESP32-C3's SYSTIMER and interrupt matrix (rv_esp32c3.c), behind
 * rv_soc.h.
 *
 * Switching is direct, as on AVR: emb_arch_switch_to() saves the caller's frame and
 * resumes the next one with mret; the trap entry saves the interrupted frame, runs the
 * handler on the interrupt stack, and resumes the context the kernel's interrupt exit
 * returns. Handlers run with MIE clear (no nesting in this milestone).
 */
#include "rv_soc.h"

#include <emb/arch.h>
#include <emb/context.h>
#include <emb/irq.h>

#include <stdint.h>

#define MCAUSE_IRQ    0x80000000u
#define MSTATUS_MPP_M 0x1800u
#define MSTATUS_MPIE  0x80u

/* ---- context ---------------------------------------------------------------------------- */

/* The context the trap entry saves into and resumes from (rv_switch.S). Before the
 * kernel starts it is the boot record, which is never resumed. */
static emb_arch_tcb_t boot_context;
struct embk_thread *embn_rv_current = (struct embk_thread *)(void *)&boot_context;

/* The interrupt stack and its top, read by the trap entry. */
static uint8_t embn_rv_isr_stack[CONFIG_EMB_ISR_STACK_SIZE] EMB_ALIGNED(16);
uint8_t *const embn_rv_isr_stack_top = &embn_rv_isr_stack[CONFIG_EMB_ISR_STACK_SIZE];

void embn_rv_start(void);
embk_thread_t *embn_rv_trap(uint32_t mcause, uintptr_t mepc, uintptr_t mtval,
                            const uint32_t *frame);
int main(void);

#define FRAME_WORDS  32u
#define SLOT_MEPC    0u
#define SLOT_A0      7u
#define SLOT_A1      8u
#define SLOT_MSTATUS 29u

void emb_arch_context_init(embk_thread_t *t, void *stack_base, size_t stack_size,
                           emb_thread_entry_t entry, void *arg, bool privileged)
{
    uintptr_t top = ((uintptr_t)stack_base + stack_size) & ~(uintptr_t)15u;
    uint32_t *f = (uint32_t *)top - FRAME_WORDS;
    unsigned i;
    (void)privileged; /* machine mode only until the PMP milestone (SPEC-010) */
    for (i = 0u; i < FRAME_WORDS; i++) {
        f[i] = 0u;
    }
    /* MISRA Dev CS-12: 11.1, the one function-to-integer conversion of the port */
    f[SLOT_MEPC] = (uint32_t)(uintptr_t)&embk_thread_launch;
    f[SLOT_A0] = (uint32_t)(uintptr_t)entry; /* embk_thread_launch(entry, arg) */
    f[SLOT_A1] = (uint32_t)(uintptr_t)arg;
    f[SLOT_MSTATUS] = MSTATUS_MPP_M | MSTATUS_MPIE; /* the launch runs with interrupts on */
    ((emb_arch_tcb_t *)(void *)t)->sp = (emb_arch_sp_t)(uintptr_t)f;
}

void emb_arch_reschedule_pend(void)
{
    /* the trap exit asks the kernel; nothing to record here */
}

bool emb_arch_switch_out_done(const embk_thread_t *t)
{
    return t != embn_rv_current; /* the switch is synchronous on this port */
}

/* ---- traps ------------------------------------------------------------------------------- */

embk_thread_t *embn_rv_trap(uint32_t mcause, uintptr_t mepc, uintptr_t mtval, const uint32_t *frame)
{
    if ((mcause & MCAUSE_IRQ) == 0u) {
        /* an exception: the code is mcause, the address mtval, the pc mepc */
        embk_fault_raise_hw((uint16_t)mcause, mtval, mepc, (uintptr_t)frame);
    }
    embk_isr_enter();
    embn_rv_soc_irq(mcause & ~MCAUSE_IRQ);
    return embk_isr_exit();
}

/* ---- startup and kernel start (SPEC-011 §4) ---------------------------------------------- */

void embn_rv_start(void)
{
    emb_arch_early_init();
    (void)main();
    for (;;) {
        /* main returned before the kernel started */
    }
}

void emb_arch_early_init(void)
{
    __asm__ __volatile__("csrci mstatus, 8" ::: "memory");
    embn_rv_soc_early_init(); /* mtvec, and what the SoC needs before C runs on */
}

void emb_arch_init(void)
{
    embn_rv_soc_init();
}

/* P3. Without an idle thread the pre-kernel context becomes the idle context, as on AVR:
 * the first switch saves it into @idle, and when the scheduler picks idle it resumes
 * here and runs the idle loop. With an idle thread the boot record is saved and never
 * resumed. */
void emb_arch_kernel_start(embk_thread_t *first, embk_thread_t *idle)
{
#if CONFIG_EMB_IDLE_THREAD
    (void)idle;
#else
    embn_rv_current = idle;
#endif
    emb_arch_switch_to(first);
    /* resumed as the idle context */
    __asm__ __volatile__("csrsi mstatus, 8" ::: "memory");
    embk_thread_launch(NULL, NULL);
}

/* ---- idle, stacks, faults ---------------------------------------------------------------- */

void emb_arch_idle(void)
{
    __asm__ __volatile__("wfi" ::: "memory");
}

bool emb_arch_stack_check(const embk_thread_t *t)
{
    (void)t;
    return true; /* the kernel's guard words; PMP stack guards come with SPEC-010 */
}

void emb_arch_stack_limit_set(void *limit)
{
    (void)limit;
}

/* The board may report and end the run (an emulator exit, a crash record). */
EMB_WEAK void emb_board_fault_report(const emb_fault_info_t *info);
EMB_WEAK void emb_board_fault_report(const emb_fault_info_t *info)
{
    (void)info;
}

void emb_arch_fault_halt(const emb_fault_info_t *info)
{
    __asm__ __volatile__("csrci mstatus, 8" ::: "memory");
    emb_board_fault_report(info);
    for (;;) {
        __asm__ __volatile__("wfi");
    }
}

void emb_arch_reset(const emb_fault_info_t *info)
{
    __asm__ __volatile__("csrci mstatus, 8" ::: "memory");
    emb_board_fault_report(info);
    for (;;) {
        /* no architectural reset in machine mode: the board's watchdog, when it has one */
    }
}

void emb_rv_halt(void)
{
    __asm__ __volatile__("csrci mstatus, 8" ::: "memory");
    for (;;) { /* the panic path (CS-6.2): the run is over */
        __asm__ __volatile__("wfi" ::: "memory");
    }
}

/* No breakpoint: machine mode cannot tell whether a debugger is attached, and an ebreak
 * without one traps into the fault path again. */
void emb_arch_breakpoint(void)
{
}
