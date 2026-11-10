/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Cortex-M port, Armv7-M and Armv7E-M (docs/ports/cortex_m.md; contract SPEC-011).
 *
 * Switching. Threads run in thread mode on PSP; handlers run on MSP. Every switch is
 * done by PendSV, at the lowest priority, so it happens only when no other handler is
 * active. The port keeps two pointers: embn_cm_running, the thread whose registers are
 * on the CPU, and embn_cm_next, the kernel's latest decision. A thread-context switch
 * (emb_arch_switch_to) stores the decision, pends PendSV and opens its critical section
 * for the instant PendSV needs; a kernel-aware interrupt stores the decision of the
 * kernel's interrupt exit and pends PendSV when it differs from the running thread.
 * PendSV saves r4-r11, EXC_RETURN and, for a thread that used the FPU, s16-s31 on the
 * outgoing PSP, and restores the incoming thread's. One frame layout serves the switch,
 * the interrupt path, and the initial frame.
 *
 * Critical sections are BASEPRI (emb_arch.h); PendSV is masked by any nonzero BASEPRI,
 * so it always runs with BASEPRI 0 and every context it resumes was saved at 0.
 */
#include <emb/arch.h>
#include <emb/context.h>
#include <emb/irq.h>

#include <stdint.h>

#include <emb_cortex_m.h>
#include <emb_irq_priority.h>

/* ---- system control registers (Armv7-M ARM B3.2) ---------------------------------- */

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a)) /* CODING-STANDARD CS-5.1 */
#if defined(__ARM_ARCH_6M__)
#define EMBN_CM_V6M 1 /* Armv6-M (A86): PRIMASK, word-only priority registers, HardFault only */
#else
#define EMBN_CM_V6M 0
#endif
#define REG8(a) (*(volatile uint8_t *)(uintptr_t)(a))

#define SCB_ICSR       0xE000ED04u
#define SCB_SHPR2      0xE000ED1Cu
#define SCB_SHPR3      0xE000ED20u
#define SCB_SHCSR      0xE000ED24u
#define SCB_CFSR       0xE000ED28u
#define SCB_HFSR       0xE000ED2Cu
#define SCB_AIRCR      0xE000ED0Cu
#define ICSR_PENDSVSET (1u << 28)
#define SYST_CSR       0xE000E010u
#define SYST_RVR       0xE000E014u
#define SYST_CVR       0xE000E018u
#define NVIC_ISER      0xE000E100u
#define NVIC_ICER      0xE000E180u
#define NVIC_ISPR      0xE000E200u
#define NVIC_ICPR      0xE000E280u
#define NVIC_IPR       0xE000E400u

#define PRIO_PENDSV  0xFFu /* the lowest: reads back as the lowest implemented */
#define PRIO_SYSTICK embn_cm_irq_priority((uint8_t)CONFIG_EMB_CM_TIMER_LEVEL)

/* ---- switching state --------------------------------------------------------------- */

/* PendSV's assembly reads and writes these three, the vector table's initial SP is the interrupt
 * stack's top and the fault entry's assembly calls embn_cm_fault: none is visible to LTO, so
 * EMB_USED keeps them (ADR-056). */
EMB_USED struct embk_thread
    *embn_cm_running;                      /* registers on the CPU; NULL before the first switch */
EMB_USED struct embk_thread *embn_cm_next; /* the kernel's latest decision */
EMB_USED struct embk_thread *embn_cm_exiting; /* an exited thread whose stack PendSV has not left */

void embn_cm_irq_entry(void);
void embn_cm_systick(void);

static EMB_ALWAYS_INLINE void pend_switch(void)
{
    REG32(SCB_ICSR) = ICSR_PENDSVSET;
    __asm__ __volatile__("dsb" ::: "memory");
}

/* Lets PendSV run: BASEPRI (Armv6-M: PRIMASK) 0 for the instant between the `isb` and the
 * re-mask. */
static EMB_ALWAYS_INLINE void open_window(void)
{
#if EMBN_CM_V6M
    __asm__ __volatile__("cpsie i\n\t"
                         "isb" ::
                             : "memory");
#else
    __asm__ __volatile__("msr basepri, %0\n\t"
                         "isb"
                         :
                         : "r"(0u)
                         : "memory");
#endif
}

/* A priority byte: Armv7-M and later address them one by one; Armv6-M takes word accesses
 * only (Armv6-M ARM B3.4), so the byte is read, modified and written back in its word, masked
 * against an interrupt that writes another byte of the same word. */
#if EMBN_CM_V6M
static void prio_write(uint32_t base, unsigned index, uint8_t value)
{
    uint32_t a = base + (index & ~3u);
    unsigned shift = 8u * (index & 3u);
    emb_irq_key_t key = emb_arch_irq_lock();
    REG32(a) = (REG32(a) & ~(0xFFu << shift)) | ((uint32_t)value << shift);
    emb_arch_irq_unlock(key);
}
#else
static void prio_write(uint32_t base, unsigned index, uint8_t value)
{
    REG8(base + index) = value; /* the byte itself */
}
#endif

/* Frame words from the saved stack pointer upward: on Armv8-M Mainline the thread's PSPLIM
 * (A80), then r4..r11, EXC_RETURN, then the hardware frame r0, r1, r2, r3, r12, lr, pc, xPSR
 * (Armv7-M ARM B1.5.6). */
#if defined(__ARM_ARCH_8M_MAIN__)
#define FRAME_LIMIT 1u
#else
#define FRAME_LIMIT 0u
#endif
#define FRAME_WORDS    (17u + FRAME_LIMIT)
#define EXC_RETURN_PSP 0xFFFFFFFDu /* thread mode, PSP, no floating-point context */
#define XPSR_THUMB     0x01000000u
#define CFSR_STKOF     (1u << 20) /* UsageFault: a stack limit check failed (Armv8-M) */

void emb_arch_context_init(embk_thread_t *t, void *stack_base, size_t stack_size,
                           emb_thread_entry_t entry, void *arg, bool privileged)
{
    uintptr_t top = ((uintptr_t)stack_base + stack_size) & ~(uintptr_t)7u;
    uint32_t *f = (uint32_t *)top - FRAME_WORDS;
    unsigned i;
    (void)privileged; /* every thread is privileged until the MPU milestone (SPEC-010) */
#if FRAME_LIMIT
    /* PSPLIM: the stack's lowest 8-aligned address; a push below it is a STKOF UsageFault */
    f[0] = (uint32_t)(((uintptr_t)stack_base + 7u) & ~(uintptr_t)7u);
    f++;
#endif
    for (i = 0u; i < 8u; i++) {
        f[i] = 0u; /* r4..r11 */
    }
    f[8] = EXC_RETURN_PSP;
    f[9] = (uint32_t)(uintptr_t)entry; /* r0: embk_thread_launch(entry, arg) */
    f[10] = (uint32_t)(uintptr_t)arg;  /* r1 */
    f[11] = 0u;                        /* r2 */
    f[12] = 0u;                        /* r3 */
    f[13] = 0u;                        /* r12 */
    f[14] = 0u;                        /* lr: the launch never returns */
    /* MISRA Dev CS-12: 11.1, the one function-to-integer conversion of the port */
    f[15] = (uint32_t)(uintptr_t)&embk_thread_launch & ~1u; /* pc, Thumb bit in xPSR */
    f[16] = XPSR_THUMB;
    ((emb_arch_tcb_t *)(void *)t)->sp = (emb_arch_sp_t)(uintptr_t)(f - FRAME_LIMIT);
}

/* P2: called with the kernel's critical section held; returns, in the same section,
 * when the scheduler resumes this thread. The held BASEPRI is a local of this frame,
 * so each thread gets its own back. */
void emb_arch_switch_to(embk_thread_t *next)
{
    emb_irq_key_t held;
#if EMBN_CM_V6M
    __asm__ __volatile__("mrs %0, primask" : "=r"(held));
#else
    __asm__ __volatile__("mrs %0, basepri" : "=r"(held));
#endif
    embn_cm_next = next;
    pend_switch();
    open_window(); /* PendSV runs here; this thread continues below once resumed */
#if EMBN_CM_V6M
    __asm__ __volatile__("msr primask, %0\n\t"
                         "isb"
                         :
                         : "r"(held)
                         : "memory");
#else
    __asm__ __volatile__("msr basepri, %0\n\t"
                         "isb"
                         :
                         : "r"(held)
                         : "memory");
#endif
}

/* The exiting thread's last switch: its context is not saved. Until PendSV has left
 * its stack, emb_arch_switch_out_done() reports it as in use. */
void emb_arch_switch_final(embk_thread_t *next)
{
    embn_cm_exiting = embn_cm_running;
    embn_cm_running = NULL;
    embn_cm_next = next;
    pend_switch();
    open_window();
    for (;;) {
        /* not reached: PendSV never resumes this context */
    }
}

void emb_arch_reschedule_pend(void)
{
    /* the interrupt exit asks the kernel; nothing to record here */
}

bool emb_arch_switch_out_done(const embk_thread_t *t)
{
    return t != embn_cm_exiting && t != embn_cm_running;
}

/* P1 at the end of every kernel-aware handler: take the kernel's decision and pend the
 * switch when it is not the running thread. The kernel's exit runs masked through the
 * port primitive, which leaves the checked build's critical-section count alone. */
static void isr_exit(void)
{
    emb_irq_key_t key = emb_arch_irq_lock();
    embk_thread_t *ctx = embk_isr_exit();
    embn_cm_next = ctx;
    if (ctx != embn_cm_running) {
        pend_switch();
    }
    emb_arch_irq_unlock(key);
}

static void isr_enter(void)
{
    emb_irq_key_t key = emb_arch_irq_lock();
    embk_isr_enter();
    emb_arch_irq_unlock(key);
}

/* Every external vector: the interrupt number from IPSR, the connected handler. */
void embn_cm_irq_entry(void)
{
    uint32_t ipsr;
    __asm__ __volatile__("mrs %0, ipsr" : "=r"(ipsr));
    isr_enter();
    embk_isr_dispatch((emb_irq_t)((ipsr & 0x1FFu) - 16u));
    isr_exit();
}

/* ---- interrupt controller (SPEC-002 §8) ----------------------------------------------- */

static bool valid_irq(emb_irq_t irq)
{
    return irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT;
}

emb_status_t emb_arch_irq_enable(emb_irq_t irq)
{
    if (!valid_irq(irq)) {
        return EMB_EINVAL;
    }
    REG32(NVIC_ISER + 4u * (irq / 32u)) = 1u << (irq % 32u);
    return EMB_OK;
}

emb_status_t emb_arch_irq_disable(emb_irq_t irq)
{
    if (!valid_irq(irq)) {
        return EMB_EINVAL;
    }
    REG32(NVIC_ICER + 4u * (irq / 32u)) = 1u << (irq % 32u);
    __asm__ __volatile__("dsb\n\t"
                         "isb" ::
                             : "memory");
    return EMB_OK;
}

bool emb_arch_irq_is_enabled(emb_irq_t irq)
{
    return valid_irq(irq) && (REG32(NVIC_ISER + 4u * (irq / 32u)) & (1u << (irq % 32u))) != 0u;
}

/* Larger generic levels encode smaller NVIC values: more urgent (SPEC-002 §8).
 * Only kernel-aware levels are currently exposed; PendSV's encoding is reserved. */
emb_status_t emb_arch_irq_set_level(emb_irq_t irq, uint8_t level)
{
    if (!valid_irq(irq) || level > EMB_IRQ_LEVEL_KERNEL_MAX) {
        return EMB_EINVAL;
    }
    prio_write(NVIC_IPR, irq, embn_cm_irq_priority(level));
    return EMB_OK;
}

emb_status_t emb_arch_irq_clear_pending(emb_irq_t irq)
{
    if (!valid_irq(irq)) {
        return EMB_EINVAL;
    }
    REG32(NVIC_ICPR + 4u * (irq / 32u)) = 1u << (irq % 32u);
    return EMB_OK;
}

emb_status_t emb_arch_irq_pend_soft(emb_irq_t irq)
{
    if (!valid_irq(irq)) {
        return EMB_EINVAL;
    }
    REG32(NVIC_ISPR + 4u * (irq / 32u)) = 1u << (irq % 32u);
    __asm__ __volatile__("dsb\n\t"
                         "isb" ::
                             : "memory");
    return EMB_OK;
}

bool emb_arch_in_isr(void)
{
    uint32_t ipsr;
    __asm__ __volatile__("mrs %0, ipsr" : "=r"(ipsr));
    return (ipsr & 0x1FFu) != 0u;
}

/* ---- SysTick (SPEC-003 §4) -------------------------------------------------------------- */

static uint32_t tick_reload; /* counts per tick minus one */

/* The reload is 24 bits: a tick longer than 2^24 processor cycles cannot be counted, and a
 * clamp would run a wrong tick rate without a word (the kernel's only caller passes the
 * configured rate, so the check is the configuration's). */
#define EMBN_CM_TICK_COUNTS \
    ((uint32_t)CONFIG_EMB_CM_CPU_HZ / (1000000000u / (uint32_t)CONFIG_EMB_TICK_NS))
EMB_STATIC_ASSERT(EMBN_CM_TICK_COUNTS >= 1u && EMBN_CM_TICK_COUNTS <= 0x01000000u,
                  "CONFIG_EMB_TICK_NS needs between 1 and 2^24 SysTick counts per tick at "
                  "CONFIG_EMB_CM_CPU_HZ: shorten the tick or lower the clock");

emb_status_t emb_arch_timer_init(void)
{
    REG32(SYST_CSR) = 0u;
    prio_write(SCB_SHPR3, 3u, PRIO_SYSTICK);
    return EMB_OK;
}

void emb_arch_timer_start_periodic(uint32_t hz)
{
    uint32_t counts = (uint32_t)CONFIG_EMB_CM_CPU_HZ / hz;
    if (counts == 0u || counts > 0x01000000u) {
        /* only a rate other than the configured one gets here (the assertion above) */
        emb_fault(EMB_FAULT_KERNEL_INVARIANT, 0x5715u, "SysTick reload out of range");
    }
    tick_reload = counts - 1u;
    REG32(SYST_RVR) = tick_reload;
    REG32(SYST_CVR) = 0u;
    REG32(SYST_CSR) = 7u; /* processor clock, interrupt, enable */
}

void embn_cm_systick(void)
{
    isr_enter();
    embk_time_timer_isr();
    isr_exit();
}

emb_arch_timer_raw_t emb_arch_timer_now_raw(void)
{
    return tick_reload - REG32(SYST_CVR); /* counts into the current tick */
}

void emb_arch_timer_set_deadline_raw(emb_arch_timer_raw_t when)
{
    (void)when; /* tickless: not in this milestone (the configuration refuses it) */
}

void emb_arch_timer_cancel(void)
{
}

uint32_t emb_arch_timer_hz(void)
{
    return (uint32_t)CONFIG_EMB_CM_CPU_HZ;
}

emb_tick_t emb_arch_timer_max_ticks(void)
{
    return 1u;
}

uint32_t emb_arch_timer_set_latency_ticks(void)
{
    return 0u;
}

/* Processor clock cycles since the kernel started, from the tick count and SysTick (DWT
 * CYCCNT is preferred on hardware; QEMU does not model it). */
emb_cycle_t emb_arch_cycles(void)
{
    emb_irq_key_t key = emb_arch_irq_lock();
    uint64_t ticks = (uint64_t)emb_time_now().ticks;
    uint32_t cvr = REG32(SYST_CVR);
    if ((REG32(SCB_ICSR) & (1u << 26)) != 0u) { /* PENDSTSET: a tick not yet counted */
        ticks++;
        cvr = REG32(SYST_CVR);
    }
    emb_arch_irq_unlock(key);
    return (emb_cycle_t)((ticks * ((uint64_t)tick_reload + 1u)) + (uint64_t)(tick_reload - cvr));
}

uint32_t emb_arch_cycles_hz(void)
{
    return (uint32_t)CONFIG_EMB_CM_CPU_HZ;
}

/* ---- a stall longer than a tick (A122) ---------------------------------------------- */

#define DEMCR          0xE000EDFCu
#define DEMCR_TRCENA   ((uint32_t)1u << 24)
#define DWT_CTRL       0xE0001000u
#define DWT_CYCCNT     0xE0001004u
#define DWT_CYCCNTENA  ((uint32_t)1u << 0)
#define DWT_NOCYCCNT   ((uint32_t)1u << 25) /* the counter is not implemented */
#define ICSR_PENDSTSET ((uint32_t)1u << 26)

/* Starts DWT CYCCNT where there is one; whether it counts. */
static bool cycle_counter(void)
{
#if (__ARM_ARCH >= 7) || defined(__ARM_ARCH_8M_MAIN__)
    REG32(DEMCR) |= DEMCR_TRCENA;
    if ((REG32(DWT_CTRL) & DWT_NOCYCCNT) == 0u) {
        REG32(DWT_CTRL) |= DWT_CYCCNTENA;
    }
    return (REG32(DWT_CTRL) & DWT_CYCCNTENA) != 0u;
#else
    return false; /* ARMv6-M: no cycle counter */
#endif
}

uint32_t emb_cm_cycle_count(void)
{
    return cycle_counter() ? REG32(DWT_CYCCNT) : 0u;
}

void emb_cm_stall_begin(emb_cm_stall_t *s)
{
    s->counting = (uint8_t)(cycle_counter() ? 1u : 0u);
    s->cycles = (s->counting != 0u) ? REG32(DWT_CYCCNT) : 0u;
    s->into = tick_reload - REG32(SYST_CVR);
    s->pending = (uint8_t)(((REG32(SCB_ICSR) & ICSR_PENDSTSET) != 0u) ? 1u : 0u);
}

void emb_cm_stall_end(const emb_cm_stall_t *s)
{
    uint64_t spent;
    uint64_t passed;
    if ((s->counting == 0u) || (tick_reload == 0u)) {
        return;
    }
    spent = (uint64_t)s->into + (uint64_t)(uint32_t)(REG32(DWT_CYCCNT) - s->cycles);
    passed = spent / ((uint64_t)tick_reload + 1u); /* the tick boundaries passed */
    if ((passed != 0u) && (s->pending == 0u)) {
        passed--; /* SysTick's pending interrupt delivers one of them */
    }
    embk_time_ticks_missed((emb_tick_t)passed);
}

/* ---- startup and kernel start (SPEC-011 §4) ------------------------------------------- */

void emb_arch_early_init(void)
{
    __asm__ __volatile__("cpsid i" ::: "memory");
}

void emb_arch_init(void)
{
    unsigned i;
    uint32_t aircr = REG32(SCB_AIRCR);
    /* All advertised bits are preemption bits; preserve unrelated AIRCR fields. */
    aircr &= ~((0xFFFFu << 16) | (7u << 8));
    REG32(SCB_AIRCR) = aircr | (0x5FAu << 16);
    __asm__ __volatile__("dsb\n\tisb" ::: "memory");
    prio_write(SCB_SHPR3, 2u, PRIO_PENDSV);
    prio_write(SCB_SHPR3, 3u, PRIO_SYSTICK);
#if !EMBN_CM_V6M                                              /* Armv6-M has HardFault only */
    REG32(SCB_SHCSR) |= (1u << 16) | (1u << 17) | (1u << 18); /* MemManage, BusFault, Usage */
#endif
    for (i = 0u; i < (unsigned)EMB_ARCH_IRQ_COUNT; i++) {
        prio_write(NVIC_IPR, i, (uint8_t)CONFIG_EMB_CM_IRQ_PRIORITY); /* kernel-aware by default */
    }
}

#if !CONFIG_EMB_IDLE_THREAD
/* The inline idle context (ADR-036) cannot run on the pre-kernel stack here: that stack
 * becomes the handler stack (MSP). It gets this small stack of its own instead; the idle
 * loop and the interrupt frames it takes fit with room to spare. */
EMB_THREAD_STACK(embn_cm_idle_stack, 256);
#endif

/* P3: the first switch is PendSV's, from no context. main()'s stack stays as the
 * handler stack (MSP). */
void emb_arch_kernel_start(embk_thread_t *first, embk_thread_t *idle)
{
#if !CONFIG_EMB_IDLE_THREAD
    emb_arch_context_init(idle, embn_cm_idle_stack, sizeof(embn_cm_idle_stack), NULL, NULL,
                          true); /* embk_thread_launch(NULL, NULL) runs the idle loop */
#else
    (void)idle; /* the kernel's idle thread has its own initial frame */
#endif
    embn_cm_running = NULL;
    embn_cm_exiting = NULL;
    embn_cm_next = first;
    pend_switch();
    open_window();
    __asm__ __volatile__("cpsie i\n\t"
                         "isb" ::
                             : "memory");
    for (;;) {
        /* not reached */
    }
}

/* ---- idle, stacks, faults ---------------------------------------------------------------- */

void emb_arch_idle(void)
{
    __asm__ __volatile__("dsb\n\t"
                         "wfi" ::
                             : "memory");
}

/* The interrupt stack (MSP, A90): sized by CONFIG_EMB_ISR_STACK_SIZE, placed by the linker
 * script first in RAM (.emb_isr_stack), its top the vector table's initial SP. */
EMB_USED uint8_t embn_cm_isr_stack[CONFIG_EMB_ISR_STACK_SIZE] EMB_ALIGNED(8)
    EMB_SECTION(".emb_isr_stack");

uint8_t *emb_arch_isr_stack(size_t *size)
{
    *size = sizeof(embn_cm_isr_stack);
    return embn_cm_isr_stack;
}

bool emb_arch_stack_check(const embk_thread_t *t)
{
    (void)t;
    return true; /* the guard words; on Armv8-M Mainline also PSPLIM, which faults at once */
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
    __asm__ __volatile__("cpsid i" ::: "memory");
    emb_board_fault_report(info);
    for (;;) {
        __asm__ __volatile__("wfi");
    }
}

void emb_arch_reset(const emb_fault_info_t *info)
{
    __asm__ __volatile__("cpsid i" ::: "memory");
    emb_board_fault_report(info);
    emb_arch_system_reset();
}

/* The board's last words before a reset: a console with a transmit FIFO lets its bytes out
 * (the RP2350's PL011 holds 32, which a reset would cut off mid-line). */
EMB_WEAK void emb_board_reset_prepare(void);
EMB_WEAK void emb_board_reset_prepare(void)
{
}

void emb_arch_system_reset(void)
{
    __asm__ __volatile__("cpsid i" ::: "memory");
    emb_board_reset_prepare();
    __asm__ __volatile__("dsb" ::: "memory");
    REG32(SCB_AIRCR) = (0x5FAu << 16) | (1u << 2); /* SYSRESETREQ */
    __asm__ __volatile__("dsb" ::: "memory");
    for (;;) {
    }
}

#if CONFIG_EMB_CRASH_RECORD
/* The linker script's area above the stack, outside every loaded segment (A73). */
extern uint8_t __emb_retained_start[];
extern uint8_t __emb_retained_end[];

uint8_t *emb_arch_retained(uint16_t *size)
{
    /* MISRA Dev CS-12: 18.2, the bounds of one linker area */
    /* cppcheck-suppress subtractPointers */
    *size = (uint16_t)(__emb_retained_end - __emb_retained_start);
    return __emb_retained_start;
}

void emb_arch_retained_sync(bool store)
{
    (void)store; /* the memory itself survives */
}
#endif

/* Only with a debugger attached (DHCSR.C_DEBUGEN): without one a `bkpt` escalates to
 * HardFault, and inside a fault handler to lockup. */
void emb_arch_breakpoint(void)
{
    if ((REG32(0xE000EDF0u) & 1u) != 0u) {
        __asm__ __volatile__("bkpt 0" ::: "memory");
    }
}

/* An architecture fault (HardFault, MemManage, BusFault, UsageFault): the exception
 * number as the code, the fault status registers' address word (MMFAR/BFAR when valid,
 * else CFSR), the stacked pc and the faulting stack pointer. */
#define SCB_MMFAR 0xE000ED34u
#define SCB_BFAR  0xE000ED38u
void embn_cm_fault(const uint32_t *frame, uint32_t exc);
EMB_USED void embn_cm_fault(const uint32_t *frame, uint32_t exc)
{
#if EMBN_CM_V6M
    /* Armv6-M has no fault status registers: the exception number and the stacked pc */
    embk_fault_raise_hw((uint16_t)exc, 0u, (uintptr_t)frame[6], (uintptr_t)frame);
#else
    uint32_t cfsr = REG32(SCB_CFSR);
    uintptr_t addr = cfsr;
    if ((cfsr & CFSR_STKOF) != 0u) {
        /* the thread's stack reached PSPLIM: the exception frame was not stacked, so there is
         * no pc to report (Armv8-M ARM, stack limit checks) */
        embk_fault_raise_stack_limit((uint16_t)exc, cfsr, (uintptr_t)frame);
    }
    if ((cfsr & (1u << 7)) != 0u) {
        addr = REG32(SCB_MMFAR); /* MMARVALID */
    } else if ((cfsr & (1u << 15)) != 0u) {
        addr = REG32(SCB_BFAR); /* BFARVALID */
    }
    embk_fault_raise_hw((uint16_t)exc, addr, (uintptr_t)frame[6], (uintptr_t)frame);
#endif
}

#if CONFIG_EMB_CRASH_RECORD
/* A hardware fault's state for the crash record (A74): CFSR, HFSR, MMFAR and BFAR, then the
 * stacked r0 to r3, r12, lr, pc and xPSR, unless stacking itself failed (CFSR STKERR, MSTKERR)
 * or hit the stack limit (STKOF, A80), when the frame cannot be trusted. A stack limit fault
 * records the status registers too. Software faults have nothing to add. */
uint8_t emb_arch_crash_regs(const emb_fault_info_t *info, uint32_t *regs, uint8_t max,
                            uint8_t *status_count, uint32_t *lr)
{
    const uint32_t *frame = (const uint32_t *)info->sp;
    uint32_t cfsr;
    uint8_t n = 0u;
    uint8_t i;
    *status_count = 0u;
    *lr = 0u;
    bool limit = info->fault_class == EMB_FAULT_STACK_OVERFLOW && info->sp != 0u;
    if ((info->fault_class != EMB_FAULT_HARDWARE && !limit) || max < 4u) {
        return 0u;
    }
#if EMBN_CM_V6M
    /* no fault status registers: the stacked frame only */
    (void)cfsr;
    if (frame != NULL) {
        for (i = 0u; i < 8u && n < max; i++) {
            regs[n++] = frame[i];
        }
        *lr = frame[5];
    }
    return n;
#else
    cfsr = REG32(SCB_CFSR);
    regs[n++] = cfsr;
    regs[n++] = REG32(SCB_HFSR);
    regs[n++] = REG32(SCB_MMFAR);
    regs[n++] = REG32(SCB_BFAR);
    *status_count = n;
    if (frame != NULL && (cfsr & ((1u << 12) | (1u << 4) | CFSR_STKOF)) == 0u) {
        for (i = 0u; i < 8u && n < max; i++) { /* the eight stacked words */
            regs[n++] = frame[i];
        }
        *lr = frame[5];
    }
    return n;
#endif
}
#endif
