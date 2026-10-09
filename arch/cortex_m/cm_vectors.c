/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Cortex-M vector table, reset entry, PendSV, and fault entry (docs/ports/cortex_m.md).
 * The board's linker script places .vectors at the boot address and defines the
 * symbols used here: __stack_top (the MSP), __data_load, __data_start, __data_end,
 * __bss_start, __bss_end.
 */
#include <emb/arch.h>

#include <stdint.h>

extern uint32_t __stack_top[];
extern uint32_t __data_load[];
extern uint32_t __data_start[];
extern uint32_t __data_end[];
extern uint32_t __bss_start[];
extern uint32_t __bss_end[];

int main(void);

void embn_cm_reset(void);
void embn_cm_pendsv(void);
void embn_cm_fault_entry(void);
void embn_cm_nmi(void);
void embn_cm_unused(void);
void embn_cm_irq_entry(void);
void embn_cm_systick(void);
void embn_cm_fault(const uint32_t *frame, uint32_t exc);

#define EMBN_STR2_(x) #x
#define EMBN_STR_(x)  EMBN_STR2_(x)

typedef void (*embn_vector_t)(void);

/* The table holds the configured lines rounded up to a multiple of 16; the lines past
 * the device's last one are never raised. */
#define EMBN_CM_IRQ_SLOTS (((EMB_ARCH_IRQ_COUNT) + 15) / 16 * 16)

typedef struct embn_cm_vectors {
    uint32_t *initial_sp;
    embn_vector_t system[15];
    embn_vector_t irq[EMBN_CM_IRQ_SLOTS];
} embn_cm_vectors_t;

/* External vectors all enter the common entry, which reads IPSR (CONFIG_EMB_IRQ_DYNAMIC). */
#define V4(f)  f, f, f, f
#define V16(f) V4(f), V4(f), V4(f), V4(f)
#define E      embn_cm_irq_entry
#if EMBN_CM_IRQ_SLOTS == 16
#define EMBN_CM_IRQS V16(E)
#elif EMBN_CM_IRQ_SLOTS == 32
#define EMBN_CM_IRQS V16(E), V16(E)
#elif EMBN_CM_IRQ_SLOTS == 48
#define EMBN_CM_IRQS V16(E), V16(E), V16(E)
#elif EMBN_CM_IRQ_SLOTS == 64
#define EMBN_CM_IRQS V16(E), V16(E), V16(E), V16(E)
#elif EMBN_CM_IRQ_SLOTS == 80
#define EMBN_CM_IRQS V16(E), V16(E), V16(E), V16(E), V16(E)
#elif EMBN_CM_IRQ_SLOTS == 96
#define EMBN_CM_IRQS V16(E), V16(E), V16(E), V16(E), V16(E), V16(E)
#elif EMBN_CM_IRQ_SLOTS == 112
#define EMBN_CM_IRQS V16(E), V16(E), V16(E), V16(E), V16(E), V16(E), V16(E)
#elif EMBN_CM_IRQ_SLOTS == 128
#define EMBN_CM_IRQS V16(E), V16(E), V16(E), V16(E), V16(E), V16(E), V16(E), V16(E)
#else
#error "CONFIG_EMB_CM_IRQ_COUNT above 128: extend the vector initializer"
#endif

EMB_USED EMB_SECTION(".vectors") const embn_cm_vectors_t embn_cm_vector_table = {
    __stack_top,
    {
        embn_cm_reset,       /* 1 Reset */
        embn_cm_nmi,         /* 2 NMI */
        embn_cm_fault_entry, /* 3 HardFault */
        embn_cm_fault_entry, /* 4 MemManage */
        embn_cm_fault_entry, /* 5 BusFault */
        embn_cm_fault_entry, /* 6 UsageFault */
        NULL,                /* 7 */
        NULL,                /* 8 */
        NULL,                /* 9 */
        NULL,                /* 10 */
        embn_cm_unused,      /* 11 SVCall: no system calls before SPEC-010 */
        embn_cm_unused,      /* 12 DebugMonitor */
        NULL,                /* 13 */
        embn_cm_pendsv,      /* 14 PendSV */
        embn_cm_systick,     /* 15 SysTick */
    },
    {EMBN_CM_IRQS},
};

/* Reset: enable the FPU before any floating-point instruction, copy .data, clear .bss,
 * run main() (which never returns once the kernel starts). */
void embn_cm_reset(void)
{
    uint32_t *src = __data_load;
    uint32_t *dst = __data_start;
#if defined(__ARM_FP)
    *(volatile uint32_t *)(uintptr_t)0xE000ED88u |= (0xFu << 20); /* CPACR: CP10, CP11 */
    __asm__ __volatile__("dsb\n\t"
                         "isb" ::
                             : "memory");
#endif
    /* bounded by the linker-defined section sizes */
    while (dst < __data_end) {
        *dst = *src;
        dst++;
        src++;
    }
    dst = __bss_start;
    while (dst < __bss_end) {
        *dst = 0u;
        dst++;
    }
    emb_arch_early_init();
    (void)main();
    for (;;) {
        /* main returned before the kernel started */
    }
}

/* PendSV: the one place a context changes (see cm_port.c). Masks the kernel's
 * interrupts, saves the running context unless it is NULL (the first switch, or an
 * exiting thread), makes the decision current, restores it, and returns with BASEPRI 0.
 * Floating-point registers s16-s31 move only for a thread with an active FP context
 * (EXC_RETURN bit 4 clear); the hardware lazily stacks s0-s15. */
#if defined(__ARM_FP)
#define EMBN_CM_SAVE_FP \
    "tst lr, #0x10\n\t" \
    "it eq\n\t"         \
    "vstmdbeq r0!, {s16-s31}\n\t"
#define EMBN_CM_RESTORE_FP \
    "tst lr, #0x10\n\t"    \
    "it eq\n\t"            \
    "vldmiaeq r0!, {s16-s31}\n\t"
#else
#define EMBN_CM_SAVE_FP
#define EMBN_CM_RESTORE_FP
#endif

EMB_NAKED void embn_cm_pendsv(void)
{
    __asm__ __volatile__("mov r0, #" EMBN_STR_(
        CONFIG_EMB_CM_KERNEL_BASEPRI) "\n\t"
                                      "msr basepri, r0\n\t"
                                      "isb\n\t"
                                      "movw r2, #:lower16:embn_cm_running\n\t"
                                      "movt r2, #:upper16:embn_cm_running\n\t"
                                      "ldr r1, [r2]\n\t"
                                      "cbz r1, 1f\n\t"
                                      "mrs r0, psp\n\t" EMBN_CM_SAVE_FP
                                      "stmdb r0!, {r4-r11, lr}\n\t"
                                      "str r0, [r1]\n\t"
                                      "b 2f\n\t"
                                      "1:\n\t"
                                      "movw r3, #:lower16:embn_cm_exiting\n\t"
                                      "movt r3, #:upper16:embn_cm_exiting\n\t"
                                      "str r1, [r3]\n\t" /* r1 is 0: the exited thread's stack is
                                                            left */
                                      "2:\n\t"
                                      "movw r3, #:lower16:embn_cm_next\n\t"
                                      "movt r3, #:upper16:embn_cm_next\n\t"
                                      "ldr r1, [r3]\n\t"
                                      "str r1, [r2]\n\t"
                                      "ldr r0, [r1]\n\t"
                                      "ldmia r0!, {r4-r11, lr}\n\t" EMBN_CM_RESTORE_FP
                                      "msr psp, r0\n\t"
                                      "mov r0, #0\n\t"
                                      "msr basepri, r0\n\t"
                                      "isb\n\t"
                                      "bx lr\n\t");
}

/* Fault entry for HardFault, MemManage, BusFault and UsageFault: the stacked frame is
 * on PSP or MSP by EXC_RETURN bit 2; the exception number from IPSR. */
EMB_NAKED void embn_cm_fault_entry(void)
{
    __asm__ __volatile__("tst lr, #4\n\t"
                         "ite eq\n\t"
                         "mrseq r0, msp\n\t"
                         "mrsne r0, psp\n\t"
                         "mrs r1, ipsr\n\t"
                         "b embn_cm_fault\n\t");
}

void embn_cm_nmi(void)
{
    embk_fault_raise_hw(2u, 0u, 0u, 0u);
}

void embn_cm_unused(void)
{
    uint32_t ipsr;
    __asm__ __volatile__("mrs %0, ipsr" : "=r"(ipsr));
    embk_fault_raise_hw((uint16_t)(ipsr & 0x1FFu), 0u, 0u, 0u);
}
