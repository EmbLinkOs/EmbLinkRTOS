/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The RISC-V port on the ESP32-C3 (docs/ports/riscv.md; Espressif's ESP32-C3 Technical
 * Reference Manual v1.4, chapters 1, 8, 10, 11 and 12). The part has no mtime, no
 * mtimecmp and no mie: its interrupts come through the interrupt matrix to CPU
 * interrupts 1-31, each with a type, a priority and an enable bit, above a threshold,
 * and mtvec is vectored.
 *
 *   - the tick: SYSTIMER's comparator 0 in period mode on unit 0 (16 MHz), its source
 *     (37) on CPU interrupt 2, level-triggered, cleared through INT_CLR;
 *   - interrupt 0: SYSTEM's FROM_CPU_INTR0 (source 50) on CPU interrupt 1, level-
 *     triggered, raised by writing 1 and cleared by writing 0;
 *   - both at priority 1 over a threshold of 1;
 *   - the cycle counter: unit 0's 52-bit count, latched by UPDATE.
 *
 * The boot ROM leaves two watchdogs running to guard a flash boot (TRM 12.2.2): the RTC
 * watchdog and timer group 0's. A second-stage bootloader stops them, and so does
 * embn_rv_soc_early_init(), first; the super watchdog is set to feed itself as
 * ESP-IDF's startup sets it.
 */
#include "rv_soc.h"

#include <emb/arch.h>
#include <emb/context.h>
#include <emb/irq.h>

#include <stdint.h>

#define REG32(a) EMBN_RV_REG32(a)

/* the interrupt matrix and the CPU's interrupt controller (TRM 8.4) */
#define INTMTX_BASE           0x600C2000u
#define INTMTX_MAP(src)       (INTMTX_BASE + 4u * (src))
#define INTMTX_CPU_INT_EN     (INTMTX_BASE + 0x104u)
#define INTMTX_CPU_INT_TYPE   (INTMTX_BASE + 0x108u)
#define INTMTX_CPU_INT_PRI(n) (INTMTX_BASE + 0x114u + 4u * (n))
#define INTMTX_CPU_INT_THRESH (INTMTX_BASE + 0x194u)
#define SRC_SYSTIMER_TARGET0  37u
#define SRC_FROM_CPU_INTR0    50u
#define CPU_INT_SOFT          1u
#define CPU_INT_TICK          2u

/* SYSTEM (TRM 16) */
#define SYSTEM_PERIP_CLK_EN0      0x600C0010u
#define SYSTEM_PERIP_RST_EN0      0x600C0018u
#define SYSTEM_SYSTIMER_BIT       (1u << 29)
#define SYSTEM_CPU_INTR_FROM_CPU0 0x600C0028u

/* SYSTIMER (TRM 10) */
#define SYSTIMER_BASE         0x60023000u
#define SYSTIMER_CONF         (SYSTIMER_BASE + 0x00u)
#define SYSTIMER_UNIT0_OP     (SYSTIMER_BASE + 0x04u)
#define SYSTIMER_TARGET0_CONF (SYSTIMER_BASE + 0x34u)
#define SYSTIMER_UNIT0_HI     (SYSTIMER_BASE + 0x40u)
#define SYSTIMER_UNIT0_LO     (SYSTIMER_BASE + 0x44u)
#define SYSTIMER_COMP0_LOAD   (SYSTIMER_BASE + 0x50u)
#define SYSTIMER_INT_ENA      (SYSTIMER_BASE + 0x64u)
#define SYSTIMER_INT_CLR      (SYSTIMER_BASE + 0x6Cu)
#define CONF_TARGET0_WORK_EN  (1u << 24)
#define OP_UPDATE             (1u << 30)
#define OP_VALUE_VALID        (1u << 29)
#define TARGET_PERIOD_MODE    (1u << 30)
#define TARGET_PERIOD_MAX     0x03FFFFFFu
#define INT_TARGET0           1u

/* the watchdogs the ROM leaves running (TRM 12) */
#define RTC_CNTL_WDTCONFIG0   0x60008090u
#define RTC_CNTL_WDTWPROTECT  0x600080A8u
#define RTC_CNTL_SWD_CONF     0x600080ACu
#define RTC_CNTL_SWD_WPROTECT 0x600080B0u
#define RTC_WDT_WKEY          0x50D83AA1u
#define RTC_SWD_WKEY          0x8F1D312Au
#define SWD_AUTO_FEED_EN      (1u << 31)
#define TIMG_WDTCONFIG0(g)    (0x6001F048u + 0x1000u * (g))
#define TIMG_WDTWPROTECT(g)   (0x6001F064u + 0x1000u * (g))
#define TIMG_WDT_WKEY         0x50D83AA1u
#define TIMG_WDT_CONF_UPDATE  (1u << 22) /* the configuration takes effect */

/* the vector table (rv_switch.S): 32 jumps to the trap entry on a 256-byte boundary */
extern const uint32_t embn_rv_vectors[32];

static void watchdogs_stop(void)
{
    unsigned g;
    REG32(RTC_CNTL_WDTWPROTECT) = RTC_WDT_WKEY;
    REG32(RTC_CNTL_WDTCONFIG0) = 0u;
    REG32(RTC_CNTL_WDTWPROTECT) = 0u;
    REG32(RTC_CNTL_SWD_WPROTECT) = RTC_SWD_WKEY;
    REG32(RTC_CNTL_SWD_CONF) |= SWD_AUTO_FEED_EN;
    REG32(RTC_CNTL_SWD_WPROTECT) = 0u;
    for (g = 0u; g < 2u; g++) {
        REG32(TIMG_WDTWPROTECT(g)) = TIMG_WDT_WKEY;
        REG32(TIMG_WDTCONFIG0(g)) = 0u; /* WDT_EN and FLASHBOOT_MOD_EN off */
        REG32(TIMG_WDTCONFIG0(g)) = TIMG_WDT_CONF_UPDATE;
        REG32(TIMG_WDTWPROTECT(g)) = 0u;
    }
}

void embn_rv_soc_early_init(void)
{
    watchdogs_stop();
    /* vectored mode: exceptions at the base, interrupt n at base + 4n, each a jump to the
     * trap entry, which reads mcause */
    __asm__ __volatile__("csrw mtvec, %0" : : "r"((uintptr_t)&embn_rv_vectors[0] | 1u) : "memory");
}

void embn_rv_soc_init(void)
{
    uint32_t bit = (1u << CPU_INT_SOFT) | (1u << CPU_INT_TICK);
    REG32(INTMTX_CPU_INT_EN) &= ~bit;
    REG32(SYSTEM_CPU_INTR_FROM_CPU0) = 0u;
    REG32(INTMTX_MAP(SRC_FROM_CPU_INTR0)) = CPU_INT_SOFT;
    REG32(INTMTX_MAP(SRC_SYSTIMER_TARGET0)) = CPU_INT_TICK;
    REG32(INTMTX_CPU_INT_TYPE) &= ~bit; /* level */
    REG32(INTMTX_CPU_INT_PRI(CPU_INT_SOFT)) = 1u;
    REG32(INTMTX_CPU_INT_PRI(CPU_INT_TICK)) = 1u;
    REG32(INTMTX_CPU_INT_THRESH) = 1u;
}

/* ---- interrupts (SPEC-002 §8): number 0 is FROM_CPU_INTR0 ------------------------------ */

void embn_rv_soc_irq(uint32_t cause)
{
    switch (cause) {
    case CPU_INT_TICK:
        REG32(SYSTIMER_INT_CLR) = INT_TARGET0;
        embk_time_timer_isr();
        break;
    case CPU_INT_SOFT:
        REG32(SYSTEM_CPU_INTR_FROM_CPU0) = 0u;
        embk_isr_dispatch(EMB_RV_IRQ_SOFT);
        break;
    default:
        embk_isr_dispatch((emb_irq_t)(cause & 0xFFFFu));
        break;
    }
}

emb_status_t emb_arch_irq_enable(emb_irq_t irq)
{
    if (irq == EMB_RV_IRQ_SOFT) {
        REG32(INTMTX_CPU_INT_EN) |= 1u << CPU_INT_SOFT;
        return EMB_OK;
    }
    return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT) ? EMB_ENOTSUP : EMB_EINVAL;
}

emb_status_t emb_arch_irq_disable(emb_irq_t irq)
{
    if (irq == EMB_RV_IRQ_SOFT) {
        REG32(INTMTX_CPU_INT_EN) &= ~(1u << CPU_INT_SOFT);
        return EMB_OK;
    }
    return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT) ? EMB_ENOTSUP : EMB_EINVAL;
}

bool emb_arch_irq_is_enabled(emb_irq_t irq)
{
    if (irq != EMB_RV_IRQ_SOFT) {
        return false;
    }
    return (REG32(INTMTX_CPU_INT_EN) & (1u << CPU_INT_SOFT)) != 0u;
}

emb_status_t emb_arch_irq_set_level(emb_irq_t irq, uint8_t level)
{
    /* one configurable level: every interrupt runs at priority 1 without nesting */
    return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT && level == 0u) ? EMB_OK : EMB_EINVAL;
}

emb_status_t emb_arch_irq_clear_pending(emb_irq_t irq)
{
    if (irq == EMB_RV_IRQ_SOFT) {
        REG32(SYSTEM_CPU_INTR_FROM_CPU0) = 0u;
        return EMB_OK;
    }
    return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT) ? EMB_ENOTSUP : EMB_EINVAL;
}

emb_status_t emb_arch_irq_pend_soft(emb_irq_t irq)
{
    if (irq == EMB_RV_IRQ_SOFT) {
        REG32(SYSTEM_CPU_INTR_FROM_CPU0) = 1u;
        __asm__ __volatile__("fence" ::: "memory");
        return EMB_OK;
    }
    return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT) ? EMB_ENOTSUP : EMB_EINVAL;
}

bool emb_arch_in_isr(void)
{
    return emb_in_isr();
}

/* ---- SYSTIMER (SPEC-003 §4) ---------------------------------------------------------------- */

static uint64_t systimer_read(void)
{
    uint32_t n;
    uint32_t hi;
    uint32_t lo;
    REG32(SYSTIMER_UNIT0_OP) = OP_UPDATE;
    for (n = 0u; n < 64u; n++) { /* bounded: the value is valid within a few APB cycles */
        if ((REG32(SYSTIMER_UNIT0_OP) & OP_VALUE_VALID) != 0u) {
            break;
        }
    }
    hi = REG32(SYSTIMER_UNIT0_HI);
    lo = REG32(SYSTIMER_UNIT0_LO);
    return ((uint64_t)hi << 32) | lo;
}

emb_status_t emb_arch_timer_init(void)
{
    REG32(SYSTEM_PERIP_CLK_EN0) |= SYSTEM_SYSTIMER_BIT;
    REG32(SYSTEM_PERIP_RST_EN0) &= ~SYSTEM_SYSTIMER_BIT;
    REG32(INTMTX_CPU_INT_EN) &= ~(1u << CPU_INT_TICK);
    REG32(SYSTIMER_CONF) &= ~CONF_TARGET0_WORK_EN;
    REG32(SYSTIMER_INT_ENA) &= ~INT_TARGET0;
    REG32(SYSTIMER_INT_CLR) = INT_TARGET0;
    return EMB_OK;
}

void emb_arch_timer_start_periodic(uint32_t hz)
{
    uint32_t period = (uint32_t)CONFIG_EMB_RV_MTIME_HZ / hz;
    if (period == 0u) {
        period = 1u;
    }
    if (period > TARGET_PERIOD_MAX) {
        period = TARGET_PERIOD_MAX;
    }
    /* period mode on unit 0 (TRM 10.5.2): the period, then COMP0_LOAD, then the work
     * enable; the first alarm one period from now */
    REG32(SYSTIMER_TARGET0_CONF) = TARGET_PERIOD_MODE | period;
    REG32(SYSTIMER_COMP0_LOAD) = 1u;
    REG32(SYSTIMER_CONF) |= CONF_TARGET0_WORK_EN;
    REG32(SYSTIMER_INT_CLR) = INT_TARGET0;
    REG32(SYSTIMER_INT_ENA) |= INT_TARGET0;
    REG32(INTMTX_CPU_INT_EN) |= 1u << CPU_INT_TICK;
}

emb_arch_timer_raw_t emb_arch_timer_now_raw(void)
{
    return (emb_arch_timer_raw_t)systimer_read();
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
    return (emb_cycle_t)systimer_read();
}

uint32_t emb_arch_cycles_hz(void)
{
    return (uint32_t)CONFIG_EMB_RV_MTIME_HZ;
}
