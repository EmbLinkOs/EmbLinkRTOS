/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The RISC-V port on a CLINT (QEMU virt; docs/ports/riscv.md): the machine timer (mtime,
 * mtimecmp) as the periodic tick and the cycle counter, the machine software interrupt
 * (msip) as interrupt 0, mtvec in direct mode. The PLIC's sources come later.
 */
#include "rv_soc.h"

#include <emb/arch.h>
#include <emb/context.h>
#include <emb/irq.h>

#include <stdint.h>

#define REG32(a) EMBN_RV_REG32(a)

#define CLINT_MSIP        (CONFIG_EMB_RV_CLINT_BASE + 0x0000u)
#define CLINT_MTIMECMP_LO (CONFIG_EMB_RV_CLINT_BASE + 0x4000u)
#define CLINT_MTIMECMP_HI (CONFIG_EMB_RV_CLINT_BASE + 0x4004u)
#define CLINT_MTIME_LO    (CONFIG_EMB_RV_CLINT_BASE + 0xBFF8u)
#define CLINT_MTIME_HI    (CONFIG_EMB_RV_CLINT_BASE + 0xBFFCu)

#define MIE_MSIE    (1u << 3)
#define MIE_MTIE    (1u << 7)
#define CAUSE_SOFT  3u
#define CAUSE_TIMER 7u

void embn_rv_soc_early_init(void)
{
    /* direct mode: every trap enters embn_rv_trap_entry (4-byte aligned in rv_switch.S) */
    __asm__ __volatile__("csrw mtvec, %0" : : "r"((uintptr_t)&embn_rv_trap_entry) : "memory");
}

void embn_rv_soc_init(void)
{
    REG32(CLINT_MSIP) = 0u;
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

void embn_rv_soc_irq(uint32_t cause)
{
    switch (cause) {
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
        embk_isr_dispatch((emb_irq_t)(cause & 0xFFFFu));
        break;
    }
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
    /* One configurable level until a programmable interrupt controller is added. */
    return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT && level == 0u) ? EMB_OK : EMB_EINVAL;
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
