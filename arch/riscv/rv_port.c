/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * RISC-V port, RV32 machine mode (docs/ports/riscv.md; contract SPEC-011): the trap
 * handler, the CLINT machine timer as the periodic tick, the machine software interrupt
 * as interrupt 0, startup, idle, faults. The frame and the switch are in rv_switch.S.
 *
 * Switching is direct, as on AVR: emb_arch_switch_to() saves the caller's frame and
 * resumes the next one with mret; the trap entry saves the interrupted frame, runs the
 * handler on the interrupt stack, and resumes the context the kernel's interrupt exit
 * returns. Handlers run with MIE clear (no nesting in this milestone).
 */
#include <emb/arch.h>
#include <emb/context.h>
#include <emb/irq.h>

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a)) /* CODING-STANDARD CS-5.1 */

#define CLINT_MSIP        (CONFIG_EMB_RV_CLINT_BASE + 0x0000u)
#define CLINT_MTIMECMP_LO (CONFIG_EMB_RV_CLINT_BASE + 0x4000u)
#define CLINT_MTIMECMP_HI (CONFIG_EMB_RV_CLINT_BASE + 0x4004u)
#define CLINT_MTIME_LO    (CONFIG_EMB_RV_CLINT_BASE + 0xBFF8u)
#define CLINT_MTIME_HI    (CONFIG_EMB_RV_CLINT_BASE + 0xBFFCu)

#define MIE_MSIE      (1u << 3)
#define MIE_MTIE      (1u << 7)
#define MCAUSE_IRQ    0x80000000u
#define CAUSE_SOFT    3u
#define CAUSE_TIMER   7u
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

void embn_rv_trap_entry(void);
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

/* ---- interrupts (SPEC-002 §8): number 0 is the machine software interrupt ------------------- */

static uint32_t tick_period;
static uint64_t tick_next;

static uint64_t mtime_read(void)
{
    uint32_t hi;
    uint32_t lo;
    uint32_t hi2;
    do { /* bounded: the high word changes once every 2^32 counts */
        hi = REG32(CLINT_MTIME_HI);
        lo = REG32(CLINT_MTIME_LO);
        hi2 = REG32(CLINT_MTIME_HI);
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
}

static void mtimecmp_write(uint64_t v)
{
    /* the RV32 sequence that never shows a smaller intermediate value (privileged spec) */
    REG32(CLINT_MTIMECMP_LO) = 0xFFFFFFFFu;
    REG32(CLINT_MTIMECMP_HI) = (uint32_t)(v >> 32);
    REG32(CLINT_MTIMECMP_LO) = (uint32_t)v;
}

embk_thread_t *embn_rv_trap(uint32_t mcause, uintptr_t mepc, uintptr_t mtval, const uint32_t *frame)
{
    if ((mcause & MCAUSE_IRQ) == 0u) {
        /* an exception: the code is mcause, the address mtval, the pc mepc */
        embk_fault_raise_hw((uint16_t)mcause, mtval, mepc, (uintptr_t)frame);
    }
    embk_isr_enter();
    switch (mcause & ~MCAUSE_IRQ) {
    case CAUSE_TIMER:
        tick_next += tick_period;
        mtimecmp_write(tick_next);
        embk_time_timer_isr();
        break;
    case CAUSE_SOFT:
        REG32(CLINT_MSIP) = 0u;
        embk_isr_dispatch(EMB_RV_IRQ_SOFT);
        break;
    default:
        embk_isr_dispatch((emb_irq_t)(mcause & 0xFFFFu));
        break;
    }
    return embk_isr_exit();
}

emb_status_t emb_arch_irq_enable(emb_irq_t irq)
{
    if (irq == EMB_RV_IRQ_SOFT) {
        __asm__ __volatile__("csrs mie, %0" : : "r"(MIE_MSIE) : "memory");
        return EMB_OK;
    }
    return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT) ? EMB_ENOTSUP : EMB_EINVAL;
}

emb_status_t emb_arch_irq_disable(emb_irq_t irq)
{
    if (irq == EMB_RV_IRQ_SOFT) {
        __asm__ __volatile__("csrc mie, %0" : : "r"(MIE_MSIE) : "memory");
        return EMB_OK;
    }
    return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT) ? EMB_ENOTSUP : EMB_EINVAL;
}

bool emb_arch_irq_is_enabled(emb_irq_t irq)
{
    uint32_t mie;
    if (irq != EMB_RV_IRQ_SOFT) {
        return false;
    }
    __asm__ __volatile__("csrr %0, mie" : "=r"(mie));
    return (mie & MIE_MSIE) != 0u;
}

emb_status_t emb_arch_irq_set_level(emb_irq_t irq, uint8_t level)
{
    (void)level; /* one level until the PLIC driver (no nesting in this milestone) */
    return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT) ? EMB_OK : EMB_EINVAL;
}

emb_status_t emb_arch_irq_clear_pending(emb_irq_t irq)
{
    if (irq == EMB_RV_IRQ_SOFT) {
        REG32(CLINT_MSIP) = 0u;
        return EMB_OK;
    }
    return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT) ? EMB_ENOTSUP : EMB_EINVAL;
}

emb_status_t emb_arch_irq_pend_soft(emb_irq_t irq)
{
    if (irq == EMB_RV_IRQ_SOFT) {
        REG32(CLINT_MSIP) = 1u;
        __asm__ __volatile__("fence" ::: "memory");
        return EMB_OK;
    }
    return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT) ? EMB_ENOTSUP : EMB_EINVAL;
}

bool emb_arch_in_isr(void)
{
    return emb_in_isr();
}

/* ---- machine timer (SPEC-003 §4) ------------------------------------------------------------ */

emb_status_t emb_arch_timer_init(void)
{
    __asm__ __volatile__("csrc mie, %0" : : "r"(MIE_MTIE) : "memory");
    mtimecmp_write(UINT64_MAX);
    return EMB_OK;
}

void emb_arch_timer_start_periodic(uint32_t hz)
{
    tick_period = (uint32_t)CONFIG_EMB_RV_MTIME_HZ / hz;
    if (tick_period == 0u) {
        tick_period = 1u;
    }
    tick_next = mtime_read() + tick_period;
    mtimecmp_write(tick_next);
    __asm__ __volatile__("csrs mie, %0" : : "r"(MIE_MTIE) : "memory");
}

emb_arch_timer_raw_t emb_arch_timer_now_raw(void)
{
    return REG32(CLINT_MTIME_LO);
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
    return (uint32_t)CONFIG_EMB_RV_MTIME_HZ;
}

emb_tick_t emb_arch_timer_max_ticks(void)
{
    return 1u;
}

uint32_t emb_arch_timer_set_latency_ticks(void)
{
    return 0u;
}

emb_cycle_t emb_arch_cycles(void)
{
    return (emb_cycle_t)mtime_read(); /* mtime: mcycle is not a timing source under QEMU */
}

uint32_t emb_arch_cycles_hz(void)
{
    return (uint32_t)CONFIG_EMB_RV_MTIME_HZ;
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
    /* direct mode: every trap enters embn_rv_trap_entry (4-byte aligned in rv_switch.S) */
    __asm__ __volatile__("csrw mtvec, %0" : : "r"((uintptr_t)&embn_rv_trap_entry) : "memory");
}

void emb_arch_init(void)
{
    REG32(CLINT_MSIP) = 0u;
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

/* No breakpoint: machine mode cannot tell whether a debugger is attached, and an ebreak
 * without one traps into the fault path again. */
void emb_arch_breakpoint(void)
{
}
