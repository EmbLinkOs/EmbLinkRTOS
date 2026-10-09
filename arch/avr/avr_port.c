/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * AVR port, ATmega328P (SPEC-012): Timer1 periodic tick, interrupt mask table, idle,
 * faults. Tickless mode is not implemented in this milestone (the configuration
 * refuses it); the contract functions it needs exist and are never called.
 */
#include <emb/arch.h>
#include <emb/context.h>
#include <emb/irq.h>

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>

/* ---- interrupt controller: one mask bit per vector (SPEC-012 §5) ---------------------- */

typedef struct irq_mask {
    uint8_t reg; /* I/O address in the data space; 0 = no mask bit */
    uint8_t bit;
} irq_mask_t;

/* Indexed by vector number; data-space addresses of the mask registers. */
static EMB_FLASH_CONST irq_mask_t irq_masks[EMB_ARCH_IRQ_COUNT] = {
    {0u, 0u},    /* 0 RESET */
    {0x3Du, 0u}, /* 1 INT0: EIMSK */
    {0x3Du, 1u}, /* 2 INT1 */
    {0x68u, 0u}, /* 3 PCINT0: PCICR */
    {0x68u, 1u}, /* 4 PCINT1 */
    {0x68u, 2u}, /* 5 PCINT2 */
    {0x60u, 6u}, /* 6 WDT: WDTCSR.WDIE */
    {0x70u, 1u}, /* 7 TIMER2_COMPA: TIMSK2 */
    {0x70u, 2u}, /* 8 TIMER2_COMPB */
    {0x70u, 0u}, /* 9 TIMER2_OVF */
    {0x6Fu, 5u}, /* 10 TIMER1_CAPT: TIMSK1 */
    {0x6Fu, 1u}, /* 11 TIMER1_COMPA */
    {0x6Fu, 2u}, /* 12 TIMER1_COMPB */
    {0x6Fu, 0u}, /* 13 TIMER1_OVF */
    {0x6Eu, 1u}, /* 14 TIMER0_COMPA: TIMSK0 */
    {0x6Eu, 2u}, /* 15 TIMER0_COMPB */
    {0x6Eu, 0u}, /* 16 TIMER0_OVF */
    {0x4Cu, 7u}, /* 17 SPI_STC: SPCR.SPIE */
    {0xC1u, 7u}, /* 18 USART_RX: UCSR0B.RXCIE0 */
    {0xC1u, 5u}, /* 19 USART_UDRE: UDRIE0 */
    {0xC1u, 6u}, /* 20 USART_TX: TXCIE0 */
    {0x7Au, 3u}, /* 21 ADC: ADCSRA.ADIE */
    {0x3Fu, 3u}, /* 22 EE_READY: EECR.EERIE */
    {0x50u, 3u}, /* 23 ANALOG_COMP: ACSR.ACIE */
    {0xBCu, 0u}, /* 24 TWI: TWCR.TWIE */
    {0x57u, 7u}, /* 25 SPM_READY: SPMCSR.SPMIE */
};

static volatile uint8_t *mask_reg(emb_irq_t irq, uint8_t *out_bit)
{
    uint8_t reg;
    if (irq >= (emb_irq_t)EMB_ARCH_IRQ_COUNT) {
        return NULL;
    }
    reg = pgm_read_byte(&irq_masks[irq].reg);
    *out_bit = pgm_read_byte(&irq_masks[irq].bit);
    if (reg == 0u) {
        return NULL;
    }
    return (volatile uint8_t *)(uintptr_t)reg; /* memory-mapped register (CODING-STANDARD CS-5.1) */
}

emb_status_t emb_arch_irq_enable(emb_irq_t irq)
{
    uint8_t bit = 0u;
    volatile uint8_t *reg = mask_reg(irq, &bit);
    emb_irq_key_t key;
    if (reg == NULL) {
        return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT) ? EMB_ENOTSUP : EMB_EINVAL;
    }
    key = emb_arch_irq_lock();
    *reg |= (uint8_t)(1u << bit);
    emb_arch_irq_unlock(key);
    return EMB_OK;
}

emb_status_t emb_arch_irq_disable(emb_irq_t irq)
{
    uint8_t bit = 0u;
    volatile uint8_t *reg = mask_reg(irq, &bit);
    emb_irq_key_t key;
    if (reg == NULL) {
        return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT) ? EMB_ENOTSUP : EMB_EINVAL;
    }
    key = emb_arch_irq_lock();
    *reg &= (uint8_t) ~(uint8_t)(1u << bit);
    emb_arch_irq_unlock(key);
    return EMB_OK;
}

bool emb_arch_irq_is_enabled(emb_irq_t irq)
{
    uint8_t bit = 0u;
    volatile uint8_t *reg = mask_reg(irq, &bit);
    return reg != NULL && (*reg & (uint8_t)(1u << bit)) != 0u;
}

emb_status_t emb_arch_irq_set_level(emb_irq_t irq, uint8_t level)
{
    (void)level; /* one level: vector order is the priority (SPEC-012 §5) */
    return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT) ? EMB_OK : EMB_EINVAL;
}

emb_status_t emb_arch_irq_clear_pending(emb_irq_t irq)
{
    emb_irq_key_t key;
    if (irq >= (emb_irq_t)EMB_ARCH_IRQ_COUNT) {
        return EMB_EINVAL;
    }
    key = emb_arch_irq_lock();
    switch (irq) {
    case 1u:
        EIFR = (uint8_t)(1u << INTF0);
        break;
    case 2u:
        EIFR = (uint8_t)(1u << INTF1);
        break;
    case 10u:
        TIFR1 = (uint8_t)(1u << ICF1);
        break;
    case 11u:
        TIFR1 = (uint8_t)(1u << OCF1A);
        break;
    case 12u:
        TIFR1 = (uint8_t)(1u << OCF1B);
        break;
    case 13u:
        TIFR1 = (uint8_t)(1u << TOV1);
        break;
    case 14u:
        TIFR0 = (uint8_t)(1u << OCF0A);
        break;
    case 15u:
        TIFR0 = (uint8_t)(1u << OCF0B);
        break;
    case 16u:
        TIFR0 = (uint8_t)(1u << TOV0);
        break;
    default:
        emb_arch_irq_unlock(key);
        return EMB_ENOTSUP;
    }
    emb_arch_irq_unlock(key);
    return EMB_OK;
}

emb_status_t emb_arch_irq_pend_soft(emb_irq_t irq)
{
    (void)irq;
    return EMB_ENOTSUP; /* SPEC-012 §5 */
}

bool emb_arch_in_isr(void)
{
    return emb_in_isr();
}

/* ---- timer (SPEC-012 §7) ----------------------------------------------------------- */

static uint16_t tick_top; /* OCR1A + 1: counts per tick, for the cycle counter */

emb_status_t emb_arch_timer_init(void)
{
    TCCR1A = 0u;
    TCCR1B = 0u;
    TIMSK1 = 0u;
    TCNT1 = 0u;
    return EMB_OK;
}

void emb_arch_timer_start_periodic(uint32_t hz)
{
    uint32_t counts =
        ((uint32_t)CONFIG_EMB_AVR_F_CPU / (uint32_t)CONFIG_EMB_AVR_TIMER1_PRESCALER) / hz;
    uint8_t cs;
    if (counts == 0u) {
        counts = 1u;
    }
    if (counts > 65536u) {
        counts = 65536u;
    }
    tick_top = (uint16_t)counts;
    switch (CONFIG_EMB_AVR_TIMER1_PRESCALER) {
    case 1:
        cs = (uint8_t)(1u << CS10);
        break;
    case 8:
        cs = (uint8_t)(1u << CS11);
        break;
    case 64:
        cs = (uint8_t)((1u << CS11) | (1u << CS10));
        break;
    case 256:
        cs = (uint8_t)(1u << CS12);
        break;
    default:
        cs = (uint8_t)((1u << CS12) | (1u << CS10)); /* 1024 */
        break;
    }
    OCR1A = (uint16_t)(counts - 1u);
    TCNT1 = 0u;
    TCCR1A = 0u;
    TCCR1B = (uint8_t)((1u << WGM12) | cs); /* CTC on OCR1A */
    TIFR1 = (uint8_t)(1u << OCF1A);
    TIMSK1 = (uint8_t)(1u << OCIE1A);
}

emb_arch_timer_raw_t emb_arch_timer_now_raw(void)
{
    emb_irq_key_t key = emb_arch_irq_lock();
    uint16_t v = TCNT1; /* 16-bit read through the temporary register, masked */
    emb_arch_irq_unlock(key);
    return v;
}

void emb_arch_timer_set_deadline_raw(emb_arch_timer_raw_t when)
{
    (void)when; /* tickless: not in this milestone */
}

void emb_arch_timer_cancel(void)
{
}

uint32_t emb_arch_timer_hz(void)
{
    return (uint32_t)CONFIG_EMB_AVR_F_CPU / (uint32_t)CONFIG_EMB_AVR_TIMER1_PRESCALER;
}

emb_tick_t emb_arch_timer_max_ticks(void)
{
    return 32768u; /* half the 16-bit range (SPEC-012 §7) */
}

uint32_t emb_arch_timer_set_latency_ticks(void)
{
    return 0u;
}

/* The Timer1 compare vector: the kernel tick (SPEC-012 §4.5). */
EMB_ISR(TIMER1_COMPA)
{
    embk_time_timer_isr();
}

emb_cycle_t emb_arch_cycles(void)
{
    /* coarse: prescaled Timer1 counts since start (SPEC-012 §7) */
    emb_irq_key_t key = emb_arch_irq_lock();
    uint32_t ticks = (uint32_t)emb_time_now().ticks;
    uint16_t cnt = TCNT1;
    emb_arch_irq_unlock(key);
    return (emb_cycle_t)((ticks * (uint32_t)tick_top) + cnt);
}

uint32_t emb_arch_cycles_hz(void)
{
    return emb_arch_timer_hz();
}

/* ---- startup, idle, stacks, faults ----------------------------------------------------- */

void emb_arch_early_init(void)
{
    __asm__ __volatile__("cli" ::: "memory");
}

void emb_arch_init(void)
{
    SMCR = 0u; /* sleep mode idle (SM2:0 = 000), sleep disabled until emb_arch_idle */
}

void emb_arch_idle(void)
{
    /* sei; sleep is atomic: an interrupt pending at sei is taken after sleep (SPEC-011 §9) */
    SMCR = (uint8_t)(1u << SE);
    __asm__ __volatile__("sei\n\t"
                         "sleep\n\t" ::
                             : "memory");
    SMCR = 0u;
}

bool emb_arch_stack_check(const embk_thread_t *t)
{
    (void)t;
    return true; /* the kernel checks the guard words; no hardware limit here */
}

void emb_arch_stack_limit_set(void *limit)
{
    (void)limit;
}

void emb_arch_fault_halt(const emb_fault_info_t *info)
{
    (void)info;
    __asm__ __volatile__("cli" ::: "memory");
    for (;;) {
        /* halted with interrupts masked; the board may blink here in a later milestone */
    }
}

void emb_arch_reset(const emb_fault_info_t *info)
{
    (void)info; /* the .noinit crash record comes with the observability milestone */
    __asm__ __volatile__("cli" ::: "memory");
    wdt_enable(WDTO_15MS);
    for (;;) {
    }
}

void emb_arch_breakpoint(void)
{
    __asm__ __volatile__("break" ::: "memory");
}
