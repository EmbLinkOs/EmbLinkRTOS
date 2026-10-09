/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The architecture port contract (SPEC-011): the emb_arch_* functions every port
 * implements and the kernel calls. Not public API (SPEC-001 §1); the umbrella header
 * does not include it. The port's own header <emb_arch.h> supplies the inline
 * primitives (critical sections), the EMB_ISR macros, and EMB_ARCH_TCB_EXTENSION.
 *
 * Deviation from SPEC-011 §4 recorded here: emb_arch_kernel_start() receives the
 * first thread and the idle context instead of a bare stack pointer, so that a port
 * without an idle thread (CONFIG_EMB_IDLE_THREAD=n) can turn the pre-kernel stack
 * into the idle context's stack itself (SPEC-012 §3).
 */
#ifndef EMB_ARCH_H
#define EMB_ARCH_H

#include <emb/fault.h>
#include <emb/status.h>
#include <emb/thread.h>
#include <emb/time.h>
#include <emb/types.h>

#include <emb_arch.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct embk_thread embk_thread_t; /* kernel-private; the port touches only ->arch */

typedef uintptr_t emb_arch_sp_t;

typedef struct emb_arch_tcb {
    emb_arch_sp_t
        sp; /* saved stack pointer of a switched-out thread; first for the debug descriptor */
#if EMB_ARCH_TCB_EXTENSION > 0
    EMB_ALIGNED(EMB_ARCH_TCB_EXTENSION_ALIGN) unsigned char ext[EMB_ARCH_TCB_EXTENSION];
#endif
} emb_arch_tcb_t;

/* ---- startup and kernel start (§4) ------------------------------------------- */
void emb_arch_early_init(void);
void emb_arch_init(void);
EMB_NORETURN void emb_arch_kernel_start(embk_thread_t *first, embk_thread_t *idle);

/* ---- context (§5) ------------------------------------------------------------ */
/* Build the initial frame so that the first dispatch enters embk_thread_launch(entry, arg)
 * (the kernel's launch hook, which resets the fresh thread's kernel state, calls
 * entry(arg), and exits the thread with code 0: KRN-THR-005). */
void emb_arch_context_init(embk_thread_t *t, void *stack_base, size_t stack_size,
                           emb_thread_entry_t entry, void *arg, bool privileged);
void emb_arch_switch_to(embk_thread_t *next); /* P2: called with the critical section held */
EMB_NORETURN void emb_arch_switch_final(
    embk_thread_t *next); /* the exiting thread's last switch; addition to SPEC-011 §5 */
void emb_arch_reschedule_pend(void);
bool emb_arch_switch_out_done(const embk_thread_t *t);

/* ---- interrupts (§6): the inline primitives are in <emb_arch.h> --------------- */
bool emb_arch_in_isr(void);
emb_status_t emb_arch_irq_enable(emb_irq_t irq);
emb_status_t emb_arch_irq_disable(emb_irq_t irq);
bool emb_arch_irq_is_enabled(emb_irq_t irq);
emb_status_t emb_arch_irq_set_level(emb_irq_t irq, uint8_t level);
emb_status_t emb_arch_irq_clear_pending(emb_irq_t irq);
emb_status_t emb_arch_irq_pend_soft(emb_irq_t irq);

/* ---- timer and cycle counter (§7) --------------------------------------------- */
emb_status_t emb_arch_timer_init(void);
void emb_arch_timer_start_periodic(uint32_t hz);
emb_arch_timer_raw_t emb_arch_timer_now_raw(void);
void emb_arch_timer_set_deadline_raw(emb_arch_timer_raw_t when);
void emb_arch_timer_cancel(void);
uint32_t emb_arch_timer_hz(void);
emb_tick_t emb_arch_timer_max_ticks(void);
uint32_t emb_arch_timer_set_latency_ticks(void);
emb_cycle_t emb_arch_cycles(void);
uint32_t emb_arch_cycles_hz(void);

/* ---- idle (§9) ---------------------------------------------------------------- */
void emb_arch_idle(void);

/* ---- stacks (§10) ------------------------------------------------------------- */
bool emb_arch_stack_check(const embk_thread_t *t);
void emb_arch_stack_limit_set(void *limit);

/* ---- faults and debug (§11, §14) ---------------------------------------------- */
EMB_NORETURN void emb_arch_fault_halt(
    const emb_fault_info_t *info); /* FAULT_ACTION=HALT; info may be NULL */
EMB_NORETURN void emb_arch_reset(const emb_fault_info_t *info); /* FAULT_ACTION=REBOOT */
void emb_arch_breakpoint(void);

/* ---- kernel hooks the port calls ---------------------------------------------- */
void embk_isr_enter(void);             /* kernel-aware interrupt entry: depth++ */
embk_thread_t *embk_isr_exit(void);    /* outermost exit: the thread to switch to, or NULL */
void embk_time_timer_isr(void);        /* SPEC-003 §4.3 */
void embk_time_on_wake(void);          /* tickless: at the outermost exit of a wake-up interrupt */
void embk_isr_dispatch(emb_irq_t irq); /* CONFIG_EMB_IRQ_DYNAMIC: run the connected handler */
EMB_NORETURN void embk_thread_launch(emb_thread_entry_t entry,
                                     void *arg); /* every initial frame enters here;
                                                    entry == NULL runs the idle loop */
EMB_NORETURN void embk_idle_loop(void);          /* the idle context's body */
/* An architecture fault entry (SPEC-002 §9): @code is the port's cause (exception number
 * or mcause), @address the faulting address where known, @pc and @sp the faulting
 * context's. Records EMB_FAULT_HARDWARE and takes the configured action. */
EMB_NORETURN void embk_fault_raise_hw(uint16_t code, uintptr_t address, uintptr_t pc, uintptr_t sp);

#ifdef __cplusplus
}
#endif

#endif /* EMB_ARCH_H */
