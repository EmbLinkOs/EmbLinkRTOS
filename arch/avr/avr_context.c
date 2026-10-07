/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * AVR context frame and switch (SPEC-012 §4): one 35-byte frame layout for the switch,
 * the interrupt epilogue, and the initial frame; the switch is a naked routine whose
 * whole length is the masked section. Register conventions: avr-gcc passes the first
 * pointer argument in r24:r25 and the second in r22:r23; r1 is the zero register.
 */
#include <emb/arch.h>

#include <stdint.h>

struct embk_thread *embn_avr_current;

/* Frame bytes from the saved stack pointer upward: r31 .. r2, r1, SREG, r0, PC hi, PC lo. */
#define FRAME_BYTES 35u

void emb_arch_context_init(embk_thread_t *t, void *stack_base, size_t stack_size, emb_thread_entry_t entry,
                           void *arg, bool privileged)
{
    uint8_t *sp = (uint8_t *)stack_base + stack_size - 1u; /* the first free byte is the top */
    uint16_t launch = (uint16_t)(uintptr_t)&embk_thread_launch; /* MISRA Dev CS-12: 11.1, the one site */
    uint16_t a_entry = (uint16_t)(uintptr_t)entry;
    uint16_t a_arg = (uint16_t)(uintptr_t)arg;
    unsigned r;
    (void)privileged;

    *sp = (uint8_t)(launch & 0xFFu); /* return address low byte at the higher address */
    sp--;
    *sp = (uint8_t)(launch >> 8);
    sp--;
    *sp = 0u; /* r0 */
    sp--;
    *sp = 0x80u; /* SREG: I set, the launch enables interrupts */
    sp--;
    *sp = 0u; /* r1 */
    sp--;
    for (r = 2u; r <= 31u; r++) { /* r2 .. r31, bounded */
        uint8_t v = 0u;
        if (r == 22u) {
            v = (uint8_t)(a_arg & 0xFFu); /* embk_thread_launch(entry, arg): arg in r22:r23 */
        } else if (r == 23u) {
            v = (uint8_t)(a_arg >> 8);
        } else if (r == 24u) {
            v = (uint8_t)(a_entry & 0xFFu); /* entry in r24:r25 */
        } else if (r == 25u) {
            v = (uint8_t)(a_entry >> 8);
        } else {
            v = 0u;
        }
        *sp = v;
        sp--;
    }
    ((emb_arch_tcb_t *)(void *)t)->sp = (emb_arch_sp_t)(uintptr_t)sp; /* points below r31 */
}

/* Save the running context, make @next current, restore it (SPEC-012 §4.3). The call
 * pushed the return address; r24:r25 = next. */
EMB_NAKED void emb_arch_switch_to(embk_thread_t *next)
{
    (void)next;
    __asm__ __volatile__("push r0\n\t"
                         "in r0, __SREG__\n\t"
                         "cli\n\t"
                         "push r0\n\t"
                         "push r1\n\t"
                         "clr r1\n\t"
                         "push r2\n\tpush r3\n\tpush r4\n\tpush r5\n\tpush r6\n\tpush r7\n\tpush r8\n\tpush r9\n\t"
                         "push r10\n\tpush r11\n\tpush r12\n\tpush r13\n\tpush r14\n\tpush r15\n\tpush r16\n\t"
                         "push r17\n\tpush r18\n\tpush r19\n\tpush r20\n\tpush r21\n\tpush r22\n\tpush r23\n\t"
                         "push r24\n\tpush r25\n\tpush r26\n\tpush r27\n\tpush r28\n\tpush r29\n\tpush r30\n\t"
                         "push r31\n\t" EMBN_AVR_EXCHANGE EMBN_AVR_RESTORE ::
                             : "memory");
}

/* The exiting thread's last switch: no save (SPEC-008 §5 step 5 is immediate on this port). */
EMB_NAKED void emb_arch_switch_final(embk_thread_t *next)
{
    (void)next;
    __asm__ __volatile__("cli\n\t"
                         "sts embn_avr_current, r24\n\t"
                         "sts embn_avr_current+1, r25\n\t"
                         "movw r26, r24\n\t"
                         "ld r0, x+\n\t"
                         "out __SP_L__, r0\n\t"
                         "ld r0, x\n\t"
                         "out __SP_H__, r0\n\t" EMBN_AVR_RESTORE ::
                             : "memory");
}

void emb_arch_reschedule_pend(void)
{
    /* the flag is enough: the EMB_ISR epilogue asks the kernel at exit */
}

bool emb_arch_switch_out_done(const embk_thread_t *t)
{
    (void)t;
    return true;
}

/* P3 (SPEC-012 §3): the pre-kernel context becomes the idle context. Its stack is the
 * stack main() runs on; the switch saves it into @idle, and when the scheduler later
 * picks the idle context it resumes here and runs the idle loop. */
void emb_arch_kernel_start(embk_thread_t *first, embk_thread_t *idle)
{
    embn_avr_current = idle;
    emb_arch_switch_to(first);
    /* resumed as the idle context */
    __asm__ __volatile__("sei" ::: "memory");
    embk_thread_launch(NULL, NULL);
}
