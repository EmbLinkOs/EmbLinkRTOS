/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Xtensa port, the classic ESP32's PRO_CPU (docs/ports/xtensa.md, ADR-054; contract SPEC-011).
 * The vectors, the level-1 entry and reset are in xt_vectors.S.
 *
 * Every switch happens in the level-1 entry, as Cortex-M's happen in PendSV: a thread's switch
 * (emb_arch_switch_to) records the kernel's decision, raises the core's software interrupt
 * (internal interrupt 7, level 1) and opens its critical section for the instant the
 * interrupt needs; a kernel-aware interrupt takes the decision of the kernel's interrupt exit.
 * embn_xt_irq() runs every pending source in one entry, then resumes the decided thread.
 *
 *   - the tick: the core's CCOMPARE0 against CCOUNT, internal interrupt 6, level 1;
 *   - the interrupt numbers are the interrupt matrix's sources (TRM 7): an enabled source is
 *     routed to CPU interrupt 1 (level 1, level-triggered), a disabled one to internal
 *     interrupt 6, where an external source has no effect (ESP-IDF's ETS_INVALID_INUM);
 *     the dispatcher reads PRO_INTR_STATUS_0..2;
 *   - EMB_XT_IRQ_FROM_CPU0 (source 24, FROM_CPU_INTR0) is the one software can raise, by
 *     writing 1 to DPORT_CPU_INTR_FROM_CPU_0, and the dispatcher clears it by writing 0;
 *   - the cycle counter: CCOUNT, at the CPU clock.
 *
 * Register addresses from ESP-IDF v5.4's components/soc/esp32/register/soc/ (dport_reg.h,
 * rtc_cntl_reg.h, timer_group_reg.h).
 */
#include <emb/arch.h>
#include <emb/context.h>
#include <emb/irq.h>

#include <stdint.h>

#if defined(EMBN_XT_NEWLIB)
#include <sys/reent.h> /* declares __getreent() */
#endif

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a)) /* CODING-STANDARD CS-5.1 */

/* DPORT: the interrupt matrix and the cross-core software interrupts */
#define DPORT_INTR_FROM_CPU0 0x3FF000DCu
#define DPORT_PRO_INTR_STAT  0x3FF000ECu /* _0, _1, _2: sources 0-31, 32-63, 64-95 */
#define DPORT_PRO_MAP(src)   (0x3FF00104u + 4u * (src))

/* the watchdogs the ROM leaves guarding a flash boot */
#define RTC_WDT_CONFIG0   0x3FF4808Cu
#define RTC_WDT_PROTECT   0x3FF480A4u
#define TIMG_WDT_CONFIG0  0x3FF5F048u /* + 0x1000 per group */
#define TIMG_WDT_PROTECT  0x3FF5F064u
#define WDT_KEY           0x50D83AA1u
#define RTC_CNTL_OPTIONS0 0x3FF48000u
#define OPTIONS0_SYS_RST  (1u << 31) /* RTC_CNTL_SW_SYS_RST: the digital system, RAM kept */

#define LINE_MATRIX 1u  /* CPU interrupt 1: level 1, level-triggered */
#define LINE_TICK   6u  /* internal: CCOMPARE0 */
#define LINE_SOFT   7u  /* internal: the software interrupt the switch raises */
#define LINE_PARKED 6u  /* where a disabled source goes */
#define TICK_MARGIN 64u /* cycles: a compare value closer than this to CCOUNT could be missed */

#define PS_INTLEVEL 0xFu

/* ---- switching state ------------------------------------------------------------------- */

struct embk_thread *embn_xt_running; /* registers on the CPU; NULL before the first switch */
struct embk_thread *embn_xt_next;    /* the kernel's latest decision */
struct embk_thread *embn_xt_exiting; /* an exited thread whose stack the switch has not left */

static uint32_t irq_enabled[3]; /* the sources emb_arch_irq_enable() routed to LINE_MATRIX */
static uint32_t tick_period;
static uint32_t tick_due; /* the CCOUNT the next tick is for */

/* the interrupt stack and its top, read by the level-1 entry and the fault entry */
static uint8_t embn_xt_isr_stack[CONFIG_EMB_ISR_STACK_SIZE] EMB_ALIGNED(16);
uint8_t *const embn_xt_isr_stack_top = &embn_xt_isr_stack[CONFIG_EMB_ISR_STACK_SIZE];

extern const uint8_t embn_xt_vectors[];
void embn_xt_start(void);
uint32_t *embn_xt_irq(uint32_t *frame);
int main(void);

uint8_t *emb_arch_isr_stack(size_t *size)
{
    *size = sizeof(embn_xt_isr_stack); /* A90: filled at init, its guard checked at exit */
    return embn_xt_isr_stack;
}

static EMB_ALWAYS_INLINE uint32_t ccount(void)
{
    uint32_t v;
    __asm__ __volatile__("rsr %0, ccount" : "=a"(v));
    return v;
}

static EMB_ALWAYS_INLINE void intenable_set(uint32_t bits)
{
    uint32_t v;
    emb_irq_key_t key = emb_arch_irq_lock();
    __asm__ __volatile__("rsr %0, intenable" : "=a"(v));
    v |= bits;
    __asm__ __volatile__("wsr %0, intenable\n\trsync" : : "a"(v) : "memory");
    emb_arch_irq_unlock(key);
}

static EMB_ALWAYS_INLINE void pend_switch(void)
{
    __asm__ __volatile__("wsr %0, intset\n\trsync" : : "a"(1u << LINE_SOFT) : "memory");
}

static EMB_ALWAYS_INLINE bool switch_pending(void)
{
    uint32_t v;
    __asm__ __volatile__("rsr %0, interrupt" : "=a"(v));
    return (v & (1u << LINE_SOFT)) != 0u;
}

/* Lets the software interrupt run: PS.INTLEVEL 0 until the pending switch has been taken,
 * then @held again. A thread switched out here resumes here. */
static EMB_ALWAYS_INLINE void open_window(emb_irq_key_t held)
{
    do {
        __asm__ __volatile__("wsr %0, ps\n\trsync" : : "a"(held & ~PS_INTLEVEL) : "memory");
    } while (switch_pending());
    __asm__ __volatile__("wsr %0, ps\n\trsync" : : "a"(held) : "memory");
}

/* ---- context ------------------------------------------------------------------------------ */

/* Frame words from the saved stack pointer (xt_vectors.S): 0 a0, 1 a1, 2-15 a2-a15, 16 pc,
 * 17 ps, 18 sar, 19 lbeg, 20 lend, 21 lcount, 22-31 unused. */
#define FRAME_WORDS 32u
#define SLOT_A1     1u
#define SLOT_A6     6u
#define SLOT_A7     7u
#define SLOT_PC     16u
#define SLOT_PS     17u
/* PS of a fresh thread: CALLINC 1 (embk_thread_launch's entry rotates as after a call4, so its
 * arguments are the frame's a6 and a7), WOE, UM, EXCM for the resume's rfe, INTLEVEL 0 */
#define PS_INITIAL 0x00050030u

void emb_arch_context_init(embk_thread_t *t, void *stack_base, size_t stack_size,
                           emb_thread_entry_t entry, void *arg, bool privileged)
{
    uintptr_t top = ((uintptr_t)stack_base + stack_size) & ~(uintptr_t)15u;
    uint32_t *f = (uint32_t *)top - FRAME_WORDS;
    unsigned i;
    (void)privileged; /* one privilege level until the MPU milestone (SPEC-010) */
    for (i = 0u; i < FRAME_WORDS; i++) {
        f[i] = 0u;
    }
    /* the launch's caller is no one: a0 0 ends a backtrace, and a1 is the stack's top, the
     * 16 bytes below it the base save area that caller would spill into */
    f[SLOT_A1] = (uint32_t)top;
    /* MISRA Dev CS-12: 11.1, the one function-to-integer conversion of the port */
    f[SLOT_PC] = (uint32_t)(uintptr_t)&embk_thread_launch;
    f[SLOT_PS] = PS_INITIAL;
    f[SLOT_A6] = (uint32_t)(uintptr_t)entry; /* embk_thread_launch(entry, arg) */
    f[SLOT_A7] = (uint32_t)(uintptr_t)arg;
    ((emb_arch_tcb_t *)(void *)t)->sp = (emb_arch_sp_t)(uintptr_t)f;
}

/* P2: called with the kernel's critical section held; returns, in the same section, when the
 * scheduler resumes this thread. */
void emb_arch_switch_to(embk_thread_t *next)
{
    emb_irq_key_t held;
    __asm__ __volatile__("rsr %0, ps" : "=a"(held));
    embn_xt_next = next;
    pend_switch();
    open_window(held); /* the switch happens here; this thread continues once resumed */
}

/* The exiting thread's last switch: its context is not saved. Until the switch has left its
 * stack, emb_arch_switch_out_done() reports it as in use. */
void emb_arch_switch_final(embk_thread_t *next)
{
    emb_irq_key_t held;
    __asm__ __volatile__("rsr %0, ps" : "=a"(held));
    embn_xt_exiting = embn_xt_running;
    embn_xt_running = NULL;
    embn_xt_next = next;
    pend_switch();
    open_window(held);
    for (;;) {
        /* not reached: the switch never resumes this context */
    }
}

void emb_arch_reschedule_pend(void)
{
    /* the interrupt exit asks the kernel; nothing to record here */
}

bool emb_arch_switch_out_done(const embk_thread_t *t)
{
    return t != embn_xt_exiting && t != embn_xt_running;
}

/* ---- the level-1 handler --------------------------------------------------------------------- */

static void tick(void)
{
    uint32_t now = ccount();
    uint32_t when;
    tick_due += tick_period;
    /* a due time already past or too close is replaced by one just ahead: the missed ticks
     * follow one after the other, none is lost */
    when = ((int32_t)(tick_due - now) > (int32_t)TICK_MARGIN) ? tick_due : now + TICK_MARGIN;
    __asm__ __volatile__("wsr %0, ccompare0\n\tisync" : : "a"(when) : "memory"); /* clears it */
    embk_time_timer_isr();
}

static void matrix(void)
{
    unsigned w;
    for (w = 0u; w < 3u; w++) {
        uint32_t pending = REG32(DPORT_PRO_INTR_STAT + 4u * w) & irq_enabled[w];
        while (pending != 0u) { /* bounded: one pass per pending source */
            unsigned bit = (unsigned)__builtin_ctz(pending);
            emb_irq_t src = (emb_irq_t)(32u * w + bit);
            pending &= pending - 1u;
            if (src == EMB_XT_IRQ_FROM_CPU0) {
                REG32(DPORT_INTR_FROM_CPU0) = 0u;
            }
            embk_isr_dispatch(src);
        }
    }
}

/* Called by the level-1 entry on the interrupt stack, level-1 interrupts masked, with the
 * interrupted context's frame; returns the frame to resume. */
uint32_t *embn_xt_irq(uint32_t *frame)
{
    uint32_t pending;
    uint32_t enabled;
    __asm__ __volatile__("rsr %0, interrupt" : "=a"(pending));
    __asm__ __volatile__("rsr %0, intenable" : "=a"(enabled));
    pending &= enabled;
    if (embn_xt_running != NULL) {
        ((emb_arch_tcb_t *)(void *)embn_xt_running)->sp = (emb_arch_sp_t)(uintptr_t)frame;
    }
    if ((pending & (1u << LINE_SOFT)) != 0u) {
        __asm__ __volatile__("wsr %0, intclear\n\trsync" : : "a"(1u << LINE_SOFT) : "memory");
    }
    if ((pending & ((1u << LINE_TICK) | (1u << LINE_MATRIX))) != 0u) {
        embk_isr_enter();
        if ((pending & (1u << LINE_TICK)) != 0u) {
            tick();
        }
        if ((pending & (1u << LINE_MATRIX)) != 0u) {
            matrix();
        }
        embn_xt_next = embk_isr_exit(); /* P1 */
    }
    if (embn_xt_next == NULL) {
        return frame; /* before the kernel's first switch: nothing decided */
    }
    embn_xt_exiting = NULL; /* the exiting thread's stack is left now */
    embn_xt_running = embn_xt_next;
    return (uint32_t *)(uintptr_t)((emb_arch_tcb_t *)(void *)embn_xt_running)->sp;
}

/* ---- interrupts (SPEC-002 §8): the interrupt matrix's sources ------------------------------ */

static emb_status_t route(emb_irq_t irq, bool on)
{
    unsigned w = (unsigned)irq / 32u;
    uint32_t bit = 1u << ((unsigned)irq % 32u);
    emb_irq_key_t key;
    if (irq >= (emb_irq_t)EMB_ARCH_IRQ_COUNT) {
        return EMB_EINVAL;
    }
    key = emb_arch_irq_lock();
    REG32(DPORT_PRO_MAP(irq)) = on ? LINE_MATRIX : LINE_PARKED;
    if (on) {
        irq_enabled[w] |= bit;
    } else {
        irq_enabled[w] &= ~bit;
    }
    emb_arch_irq_unlock(key);
    return EMB_OK;
}

emb_status_t emb_arch_irq_enable(emb_irq_t irq)
{
    return route(irq, true);
}

emb_status_t emb_arch_irq_disable(emb_irq_t irq)
{
    return route(irq, false);
}

bool emb_arch_irq_is_enabled(emb_irq_t irq)
{
    if (irq >= (emb_irq_t)EMB_ARCH_IRQ_COUNT) {
        return false;
    }
    return (irq_enabled[(unsigned)irq / 32u] & (1u << ((unsigned)irq % 32u))) != 0u;
}

emb_status_t emb_arch_irq_set_level(emb_irq_t irq, uint8_t level)
{
    /* one level: every source runs at level 1 without nesting */
    return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT && level == 0u) ? EMB_OK : EMB_EINVAL;
}

emb_status_t emb_arch_irq_clear_pending(emb_irq_t irq)
{
    if (irq == EMB_XT_IRQ_FROM_CPU0) {
        REG32(DPORT_INTR_FROM_CPU0) = 0u;
        return EMB_OK;
    }
    /* a peripheral's pending state is the peripheral's to clear */
    return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT) ? EMB_ENOTSUP : EMB_EINVAL;
}

emb_status_t emb_arch_irq_pend_soft(emb_irq_t irq)
{
    if (irq == EMB_XT_IRQ_FROM_CPU0) {
        REG32(DPORT_INTR_FROM_CPU0) = 1u;
        __asm__ __volatile__("memw" ::: "memory");
        return EMB_OK;
    }
    return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT) ? EMB_ENOTSUP : EMB_EINVAL;
}

bool emb_arch_in_isr(void)
{
    return emb_in_isr();
}

/* ---- the tick and the cycle counter (SPEC-003 §4): CCOUNT, CCOMPARE0 ------------------------ */

emb_status_t emb_arch_timer_init(void)
{
    return EMB_OK; /* CCOMPARE0's line stays off in INTENABLE until the start */
}

void emb_arch_timer_start_periodic(uint32_t hz)
{
    uint32_t when;
    tick_period = (uint32_t)CONFIG_EMB_XT_CPU_HZ / hz;
    if (tick_period < 2u * TICK_MARGIN) {
        tick_period = 2u * TICK_MARGIN;
    }
    tick_due = ccount() + tick_period;
    when = tick_due;
    __asm__ __volatile__("wsr %0, ccompare0\n\tisync" : : "a"(when) : "memory");
    intenable_set(1u << LINE_TICK);
}

emb_arch_timer_raw_t emb_arch_timer_now_raw(void)
{
    return ccount();
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
    return (uint32_t)CONFIG_EMB_XT_CPU_HZ;
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
    return ccount();
}

uint32_t emb_arch_cycles_hz(void)
{
    return (uint32_t)CONFIG_EMB_XT_CPU_HZ;
}

/* ---- startup and kernel start (SPEC-011 §4) ---------------------------------------------- */

static void wdt_off(uint32_t protect, uint32_t config0)
{
    REG32(protect) = WDT_KEY;
    REG32(config0) = 0u;
    REG32(protect) = 0u;
}

void embn_xt_start(void)
{
    emb_arch_early_init();
    (void)main();
    for (;;) {
        /* main returned before the kernel started */
    }
}

void emb_arch_early_init(void)
{
    emb_irq_t src;
    (void)emb_arch_irq_lock();
    wdt_off(RTC_WDT_PROTECT, RTC_WDT_CONFIG0);
    wdt_off(TIMG_WDT_PROTECT, TIMG_WDT_CONFIG0);
    wdt_off(TIMG_WDT_PROTECT + 0x1000u, TIMG_WDT_CONFIG0 + 0x1000u);
    __asm__ __volatile__("wsr %0, intenable\n\trsync" : : "a"(0u) : "memory");
    for (src = 0u; src < (emb_irq_t)EMB_ARCH_IRQ_COUNT; src++) {
        REG32(DPORT_PRO_MAP(src)) = LINE_PARKED; /* at reset every source is on line 0 */
    }
    REG32(DPORT_INTR_FROM_CPU0) = 0u;
    __asm__ __volatile__("wsr %0, intclear\n\trsync" : : "a"(1u << LINE_SOFT) : "memory");
    /* the port's vectors in place of the ROM's */
    __asm__ __volatile__("wsr %0, vecbase\n\tisync" : : "a"(embn_xt_vectors) : "memory");
}

void emb_arch_init(void)
{
    intenable_set((1u << LINE_SOFT) | (1u << LINE_MATRIX));
}

#if !CONFIG_EMB_IDLE_THREAD
/* Without an idle thread the idle context gets this stack, as on Cortex-M: the pre-kernel
 * stack is left for good at the first switch. */
EMB_THREAD_STACK(embn_xt_idle_stack, 512);
#endif

/* P3: the first switch is the software interrupt's, from the boot context, which is not
 * saved. */
void emb_arch_kernel_start(embk_thread_t *first, embk_thread_t *idle)
{
    emb_irq_key_t held;
#if !CONFIG_EMB_IDLE_THREAD
    emb_arch_context_init(idle, embn_xt_idle_stack, sizeof(embn_xt_idle_stack), NULL, NULL,
                          true); /* embk_thread_launch(NULL, NULL) runs the idle loop */
#else
    (void)idle; /* the kernel's idle thread has its own initial frame */
#endif
    __asm__ __volatile__("rsr %0, ps" : "=a"(held));
    embn_xt_running = NULL;
    embn_xt_exiting = NULL;
    embn_xt_next = first;
    pend_switch();
    open_window(held | 1u);
    for (;;) {
        /* not reached */
    }
}

/* ---- idle, stacks, faults ---------------------------------------------------------------- */

void emb_arch_idle(void)
{
    __asm__ __volatile__("waiti 0" ::: "memory"); /* PS.INTLEVEL 0, as the idle loop runs */
}

bool emb_arch_stack_check(const embk_thread_t *t)
{
    (void)t;
    return true; /* the kernel's guard words; no stack limit register on the LX6 */
}

void emb_arch_stack_limit_set(void *limit)
{
    (void)limit;
}

/* The board may report and end the run (a console line, the crash record). */
EMB_WEAK void emb_board_fault_report(const emb_fault_info_t *info);
EMB_WEAK void emb_board_fault_report(const emb_fault_info_t *info)
{
    (void)info;
}

void emb_arch_fault_halt(const emb_fault_info_t *info)
{
    (void)emb_arch_irq_lock();
    emb_board_fault_report(info);
    emb_xt_halt();
}

void emb_arch_reset(const emb_fault_info_t *info)
{
    (void)emb_arch_irq_lock();
    emb_board_fault_report(info);
    emb_arch_system_reset();
}

/* What the board must finish before the reset: its console's transmit FIFO, which the reset
 * would discard with the last line in it. */
EMB_WEAK void emb_board_reset_prepare(void);
EMB_WEAK void emb_board_reset_prepare(void)
{
}

void emb_arch_system_reset(void)
{
    (void)emb_arch_irq_lock();
    emb_board_reset_prepare();
    REG32(RTC_CNTL_OPTIONS0) |= OPTIONS0_SYS_RST; /* the ROM boots the image again */
    for (;;) {
        /* until the reset takes the CPU */
    }
}

#if CONFIG_EMB_CRASH_RECORD
/* The linker script's area above the stack, outside every loaded segment (A73). */
extern uint8_t __emb_retained_start[];
extern uint8_t __emb_retained_end[];

uint8_t *emb_arch_retained(uint16_t *size)
{
    *size = (uint16_t)(__emb_retained_end - __emb_retained_start);
    return __emb_retained_start;
}

void emb_arch_retained_sync(bool store)
{
    (void)store; /* the memory itself survives a software reset */
}

/* A hardware fault's state for the crash record (A74): EXCCAUSE, EXCVADDR and EPC1, as the
 * fault entry passed them. The faulting frames are dropped, so no general registers. */
uint8_t emb_arch_crash_regs(const emb_fault_info_t *info, uint32_t *regs, uint8_t max,
                            uint8_t *status_count, uint32_t *lr)
{
    *status_count = 0u;
    *lr = 0u;
    if (info->fault_class != EMB_FAULT_HARDWARE || max < 3u) {
        return 0u;
    }
    regs[0] = (uint32_t)info->code;
    regs[1] = (uint32_t)info->address;
    regs[2] = (uint32_t)info->pc;
    *status_count = 3u;
    return 3u;
}
#endif

#if defined(EMBN_XT_NEWLIB)
/* Newlib's reentrancy block with Espressif's GCC: its newlib is built with __DYNAMIC_REENT__ and
 * asks the system for the running task's block through __getreent(), which ESP-IDF supplies.
 * The weak default found none outside ESP-IDF, and the first libc call that set errno stored
 * to address 0 (test_smoke on the board, 2026-11-05). As on RISC-V (A88), one block serves
 * every thread: errno is shared. Here, not in a file of its own, so that every image links it
 * ahead of the C library's weak definition. Zero-initialised, not _REENT_INIT: the kernel uses
 * no stdio, and _REENT_INIT's streams would link it with its unimplemented syscalls. */
static struct _reent embn_xt_reent;

struct _reent *__getreent(void)
{
    return &embn_xt_reent;
}
#endif

void emb_xt_halt(void)
{
    (void)emb_arch_irq_lock();
    for (;;) { /* the panic path (CS-6.2): the run is over */
        __asm__ __volatile__("waiti 1" ::: "memory");
    }
}

/* No breakpoint: without a debugger attached a break instruction takes the debug vector,
 * which this port parks. */
void emb_arch_breakpoint(void)
{
}
