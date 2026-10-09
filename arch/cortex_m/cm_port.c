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

/* ---- system control registers (Armv7-M ARM B3.2) ---------------------------------- */

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a)) /* CODING-STANDARD CS-5.1 */
#define REG8(a)  (*(volatile uint8_t *)(uintptr_t)(a))

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
#define PRIO_SYSTICK ((uint8_t)CONFIG_EMB_CM_IRQ_PRIORITY)

/* ---- switching state --------------------------------------------------------------- */

struct embk_thread *embn_cm_running; /* registers on the CPU; NULL before the first switch */
struct embk_thread *embn_cm_next;    /* the kernel's latest decision */
struct embk_thread *embn_cm_exiting; /* an exited thread whose stack PendSV has not left */

void embn_cm_irq_entry(void);
void embn_cm_systick(void);

static EMB_ALWAYS_INLINE void pend_switch(void)
{
    REG32(SCB_ICSR) = ICSR_PENDSVSET;
    __asm__ __volatile__("dsb" ::: "memory");
}

/* Lets PendSV run: BASEPRI 0 for the instant between the `isb` and the re-mask. */
static EMB_ALWAYS_INLINE void open_window(void)
{
    __asm__ __volatile__("msr basepri, %0\n\t"
                         "isb"
                         :
                         : "r"(0u)
                         : "memory");
}

/* Frame words from the saved stack pointer upward: r4..r11, EXC_RETURN, then the
 * hardware frame r0, r1, r2, r3, r12, lr, pc, xPSR (Armv7-M ARM B1.5.6). */
#define FRAME_WORDS    17u
#define EXC_RETURN_PSP 0xFFFFFFFDu /* thread mode, PSP, no floating-point context */
#define XPSR_THUMB     0x01000000u

void emb_arch_context_init(embk_thread_t *t, void *stack_base, size_t stack_size,
                           emb_thread_entry_t entry, void *arg, bool privileged)
{
    uintptr_t top = ((uintptr_t)stack_base + stack_size) & ~(uintptr_t)7u;
    uint32_t *f = (uint32_t *)top - FRAME_WORDS;
    unsigned i;
    (void)privileged; /* every thread is privileged until the MPU milestone (SPEC-010) */
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
    ((emb_arch_tcb_t *)(void *)t)->sp = (emb_arch_sp_t)(uintptr_t)f;
}

/* P2: called with the kernel's critical section held; returns, in the same section,
 * when the scheduler resumes this thread. The held BASEPRI is a local of this frame,
 * so each thread gets its own back. */
void emb_arch_switch_to(embk_thread_t *next)
{
    emb_irq_key_t held;
    __asm__ __volatile__("mrs %0, basepri" : "=r"(held));
    embn_cm_next = next;
    pend_switch();
    open_window(); /* PendSV runs here; this thread continues below once resumed */
    __asm__ __volatile__("msr basepri, %0\n\t"
                         "isb"
                         :
                         : "r"(held)
                         : "memory");
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

/* Level 0 is the most urgent kernel-aware level; each level is one step of the three
 * priority bits every Armv7-M part implements. Zero-latency priorities (below the
 * kernel's BASEPRI) are set by the board, not through this call (SPEC-002 §3). */
emb_status_t emb_arch_irq_set_level(emb_irq_t irq, uint8_t level)
{
    uint32_t prio = (uint32_t)CONFIG_EMB_CM_KERNEL_BASEPRI + 32u * (uint32_t)level;
    if (!valid_irq(irq)) {
        return EMB_EINVAL;
    }
    if (prio > 0xC0u) {
        prio = 0xC0u; /* 0xE0 and above belong to PendSV */
    }
    REG8(NVIC_IPR + irq) = (uint8_t)prio;
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

emb_status_t emb_arch_timer_init(void)
{
    REG32(SYST_CSR) = 0u;
    REG8(SCB_SHPR3 + 3u) = PRIO_SYSTICK;
    return EMB_OK;
}

void emb_arch_timer_start_periodic(uint32_t hz)
{
    uint32_t counts = (uint32_t)CONFIG_EMB_CM_CPU_HZ / hz;
    if (counts == 0u) {
        counts = 1u;
    }
    if (counts > 0x01000000u) {
        counts = 0x01000000u; /* 24-bit reload */
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

/* ---- startup and kernel start (SPEC-011 §4) ------------------------------------------- */

void emb_arch_early_init(void)
{
    __asm__ __volatile__("cpsid i" ::: "memory");
}

void emb_arch_init(void)
{
    unsigned i;
    REG8(SCB_SHPR3 + 2u) = PRIO_PENDSV;
    REG8(SCB_SHPR3 + 3u) = PRIO_SYSTICK;
    REG32(SCB_SHCSR) |= (1u << 16) | (1u << 17) | (1u << 18); /* MemManage, BusFault, Usage */
    for (i = 0u; i < (unsigned)EMB_ARCH_IRQ_COUNT; i++) {
        REG8(NVIC_IPR + i) = (uint8_t)CONFIG_EMB_CM_IRQ_PRIORITY; /* kernel-aware by default */
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

bool emb_arch_stack_check(const embk_thread_t *t)
{
    (void)t;
    return true; /* the kernel's guard words; PSPLIM comes with Armv8-M (M3-B) */
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
    __asm__ __volatile__("dsb" ::: "memory");
    REG32(SCB_AIRCR) = (0x5FAu << 16) | (1u << 2); /* SYSRESETREQ */
    __asm__ __volatile__("dsb" ::: "memory");
    for (;;) {
    }
}

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
void embn_cm_fault(const uint32_t *frame, uint32_t exc)
{
    uint32_t cfsr = REG32(SCB_CFSR);
    uintptr_t addr = cfsr;
    if ((cfsr & (1u << 7)) != 0u) {
        addr = REG32(SCB_MMFAR); /* MMARVALID */
    } else if ((cfsr & (1u << 15)) != 0u) {
        addr = REG32(SCB_BFAR); /* BFARVALID */
    }
    embk_fault_raise_hw((uint16_t)exc, addr, (uintptr_t)frame[6], (uintptr_t)frame);
}
