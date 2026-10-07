/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * AVR port header (SPEC-012; contract SPEC-011 §3, §6): critical sections on SREG.I,
 * the EMB_ISR vector macros, and the port's type and size constants. Vector numbers
 * come from the board's hw_config.h (EMB_VECTOR_<name>).
 */
#ifndef EMB_AVR_EMB_ARCH_H
#define EMB_AVR_EMB_ARCH_H

#include <emb/compiler.h>
#include <emb/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <hw_config.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t emb_irq_key_t;         /* the saved SREG */
typedef uint16_t emb_arch_timer_raw_t; /* TCNT1 */

#define EMB_ARCH_TCB_EXTENSION 0
/* The clock is read inside the critical section (SPEC-003 §3.1). */
#define EMB_ARCH_CLOCK_SEQLOCK 0
#define EMB_ARCH_IRQ_COUNT     26

static EMB_ALWAYS_INLINE emb_irq_key_t emb_arch_irq_lock(void)
{
    uint8_t s;
    __asm__ __volatile__("in %0, __SREG__\n\t"
                         "cli"
                         : "=r"(s)
                         :
                         : "memory");
    return s;
}

/* Restores the I bit only: the other SREG flags are meaningless across a call, and a
 * `sei` ends QEMU's translation block so that a pending interrupt is taken at the
 * unlock exactly as the hardware does after one instruction (`out SREG` would be
 * serviced only at the next indirect jump under emulation; SPEC-012 amendment). */
static EMB_ALWAYS_INLINE void emb_arch_irq_unlock(emb_irq_key_t key)
{
    __asm__ __volatile__("sbrc %0, 7\n\t"
                         "sei"
                         :
                         : "r"(key)
                         : "memory");
}

static EMB_ALWAYS_INLINE bool emb_arch_irq_locked(void)
{
    uint8_t s;
    __asm__ __volatile__("in %0, __SREG__" : "=r"(s));
    return (s & 0x80u) == 0u;
}

/* ---- handlers (SPEC-002 §8.3, SPEC-012 §4.4) ---------------------------------------
 * EMB_ISR(name) defines the naked vector __vector_<n>: it saves the full register file
 * on the interrupted stack with I forced set in the saved SREG (set; bld r0,7: ori
 * needs r16 and up), enters the kernel,
 * calls the C body, asks the kernel which thread resumes, exchanges the stack pointer
 * when it is another thread (the saved frame is the context frame), restores, and
 * returns with `ret`: the restored SREG carries the resumed context's own I bit, which
 * `reti` would override inside a switched-to thread's critical section.
 * EMB_ISR_RAW(name) is a plain signal handler that may not call the kernel. */

#define EMBN_AVR_SAVE_ISR                                                                      \
    "push r0\n\t"                                                                              \
    "in r0, __SREG__\n\t"                                                                      \
    "set\n\t"                                                                                  \
    "bld r0, 7\n\t"                                                                            \
    "push r0\n\t"                                                                              \
    "push r1\n\t"                                                                              \
    "clr r1\n\t"                                                                               \
    "push r2\n\tpush r3\n\tpush r4\n\tpush r5\n\tpush r6\n\tpush r7\n\tpush r8\n\tpush r9\n\t" \
    "push r10\n\tpush r11\n\tpush r12\n\tpush r13\n\tpush r14\n\tpush r15\n\tpush r16\n\t"     \
    "push r17\n\tpush r18\n\tpush r19\n\tpush r20\n\tpush r21\n\tpush r22\n\tpush r23\n\t"     \
    "push r24\n\tpush r25\n\tpush r26\n\tpush r27\n\tpush r28\n\tpush r29\n\tpush r30\n\t"     \
    "push r31\n\t"

/* r24:r25 holds the thread to resume (never NULL): store SP into the port's current,
 * make it current, load its SP. Unconditional: no branch, no label (SPEC-011 §15). */
#define EMBN_AVR_EXCHANGE             \
    "lds r26, embn_avr_current\n\t"   \
    "lds r27, embn_avr_current+1\n\t" \
    "in r0, __SP_L__\n\t"             \
    "st x+, r0\n\t"                   \
    "in r0, __SP_H__\n\t"             \
    "st x, r0\n\t"                    \
    "sts embn_avr_current, r24\n\t"   \
    "sts embn_avr_current+1, r25\n\t" \
    "movw r26, r24\n\t"               \
    "ld r0, x+\n\t"                   \
    "out __SP_L__, r0\n\t"            \
    "ld r0, x\n\t"                    \
    "out __SP_H__, r0\n\t"

#define EMBN_AVR_RESTORE                                                                       \
    "pop r31\n\tpop r30\n\tpop r29\n\tpop r28\n\tpop r27\n\tpop r26\n\tpop r25\n\tpop r24\n\t" \
    "pop r23\n\tpop r22\n\tpop r21\n\tpop r20\n\tpop r19\n\tpop r18\n\tpop r17\n\tpop r16\n\t" \
    "pop r15\n\tpop r14\n\tpop r13\n\tpop r12\n\tpop r11\n\tpop r10\n\tpop r9\n\tpop r8\n\t"   \
    "pop r7\n\tpop r6\n\tpop r5\n\tpop r4\n\tpop r3\n\tpop r2\n\t"                             \
    "pop r1\n\t"                                                                               \
    "pop r0\n\t"                                                                               \
    "out __SREG__, r0\n\t"                                                                     \
    "pop r0\n\t"                                                                               \
    "ret\n\t"

#define EMBN_STR2_(x) #x
#define EMBN_STR_(x)  EMBN_STR2_(x)

#define EMBN_ISR_DEFINE_(num, body)                                                                \
    static void body(void) __attribute__((used));                                                  \
    void __vector_##num(void) __attribute__((naked, used, externally_visible));                    \
    void __vector_##num(void)                                                                      \
    {                                                                                              \
        __asm__ __volatile__(                                                                      \
            EMBN_AVR_SAVE_ISR                                                                      \
            "call embk_isr_enter\n\t"                                                              \
            "call " EMBN_STR_(body) "\n\t"                                                         \
                                    "call embk_isr_exit\n\t" EMBN_AVR_EXCHANGE EMBN_AVR_RESTORE :: \
                                        : "memory");                                               \
    }                                                                                              \
    static void body(void)
#define EMBN_ISR_EXPAND_(num, body) EMBN_ISR_DEFINE_(num, body)
#define EMB_ISR(name)               EMBN_ISR_EXPAND_(EMB_VECTOR_##name, embk_isr_body_##name)

#define EMBN_ISR_RAW_DEFINE_(num)                                                \
    void __vector_##num(void) __attribute__((signal, used, externally_visible)); \
    void __vector_##num(void)
#define EMBN_ISR_RAW_EXPAND_(num) EMBN_ISR_RAW_DEFINE_(num)
#define EMB_ISR_RAW(name)         EMBN_ISR_RAW_EXPAND_(EMB_VECTOR_##name)

/* The port's view of the running thread; written only by the switch routines. */
struct embk_thread;
extern struct embk_thread *embn_avr_current;

#ifdef __cplusplus
}
#endif

#endif /* EMB_AVR_EMB_ARCH_H */
