/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    relay.c
 * @brief   The NUCLEO-F446RE as a two-way 3.3 V USB-UART for another board, with bench controls
 *          (docs/engineering/hil.md, M4.3).
 *
 * @details
 * Both ways at 115200 8N1, every byte as it is: the other board's TX on PA10 (USART1 RX, CN10 pin
 * 33) goes out of USART2 (PA2) to the ST-LINK's virtual COM port, and what the host sends to the
 * port (USART2 RX, PA3) goes out of PA9 (USART1 TX, CN10 pin 21, Arduino D8) to the other board's
 * RX. Each way has a ring of its own and the main loop moves a byte only when its transmitter is
 * free, so a busy direction never holds up the other. The header pins named PA2 and PA3 (CN10
 * pins 35 and 37, Arduino D1 and D0) are not connected to the STM32 on a stock board (SB62 and
 * SB63 open), which is why the relay exists. PA10 idles high through a pull-up when nothing drives
 * it; a break (the line held low) is not relayed.
 *
 * Bench controls: the relay watches the host's stream for SPEC-015 §7 frames on channel 15 and
 * acts on their text, which it also passes on (an image under test leaves channel 15 alone):
 *   "reset"          PA8 (Arduino D7) low for 20 ms: the other board's RUN or NRST
 *   "power off|on"   PB5 (Arduino D4) low, or released: the other board's regulator enable
 *   "power cycle N"  PB5 low for N ms, then released
 *   "wake N"         PB4 (Arduino D5) high for N us: a wake-up pin
 *   "ping"           nothing but the reply
 * Each control pin is open drain when released (the other board's pull-up holds it), except
 * the wake pin, which is driven. The reply is a channel-15 frame, "ok <command>" or "? <command>",
 * sent toward the host only between two frames of the other board, so it never cuts one. The
 * green LED (PA5) toggles on each byte. build.sh links it with the board's own clock, pin and
 * USART drivers and its regions.ld.
 *
 * @author  Junior Deogracias
 * @date    2026-11-07 (created)
 */
#include <stdbool.h>
#include <stdint.h>

#include <emb_stm32_drivers.h>
#include <hw_config.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define USART_SR  0x00u
#define USART_DR  0x04u
#define USART_CR1 0x0Cu
#define SR_TXE    (1u << 7)
#define SR_RXNE   (1u << 5)
#define SR_ORE    (1u << 3)
#define SR_FE     (1u << 1)
#define CR1_RE    (1u << 2)
#define CR1_TE    (1u << 3)

#define GPIO_MODER 0x00u
#define GPIO_PUPDR 0x0Cu
#define GPIO_BSRR  0x18u

#define DEMCR         0xE000EDFCu
#define DWT_CTRL      0xE0001000u
#define DWT_CYCCNT    0xE0001004u
#define CYCLES_PER_MS (EMB_HW_CPU_HZ / 1000u)
#define CYCLES_PER_US (EMB_HW_CPU_HZ / 1000000u)

#define RING      512u /* a power of two: two frames' worth either way */
#define CMD_MAX   48u
#define CH_BENCH  15u
#define CRC_POLY  0x1021u

extern uint32_t __data_load[], __data_start[], __data_end[], __bss_start[], __bss_end[];
void embn_cm_reset(void);
int main(void);

static uint8_t msp_stack[1024] __attribute__((aligned(8), section(".emb_isr_stack")));

static void hang(void)
{
    for (;;) {
    }
}

typedef void (*vector_t)(void);
__attribute__((section(".vectors"), used)) static const vector_t vectors[16] = {
    [0] = (vector_t)(uintptr_t)&msp_stack[sizeof(msp_stack)],
    [1] = embn_cm_reset,
    [2] = hang,
    [3] = hang,
    [4] = hang,
    [5] = hang,
    [6] = hang,
};

static const emb_stm32_pin_t pins[] = {
    {EMB_STM32_PORT(A), 2u, EMB_STM32_PIN_AF, 7u, 0u, 0u},  /* USART2_TX to the ST-LINK */
    {EMB_STM32_PORT(A), 3u, EMB_STM32_PIN_AF, 7u, 0u, 0u},  /* USART2_RX from the ST-LINK */
    {EMB_STM32_PORT(A), 9u, EMB_STM32_PIN_AF, 7u, 0u, 0u},  /* USART1_TX to the other board */
    {EMB_STM32_PORT(A), 10u, EMB_STM32_PIN_AF, 7u, 0u, 0u}, /* USART1_RX from the other board */
    {EMB_STM32_PORT(A), 5u, EMB_STM32_PIN_OUTPUT, 0u, 0u, 0u},
};

void embn_cm_reset(void)
{
    uint32_t *src = __data_load;
    for (uint32_t *dst = __data_start; dst < __data_end; dst++) {
        *dst = *src++;
    }
    for (uint32_t *dst = __bss_start; dst < __bss_end; dst++) {
        *dst = 0u;
    }
    (void)main();
    hang();
}

/* ---- a ring per direction ----------------------------------------------------------- */

typedef struct ring {
    uint8_t buf[RING];
    uint32_t head;
    uint32_t tail;
    uint32_t lost; /* bytes dropped when it was full */
} ring_t;

static ring_t to_host;  /* the other board -> the ST-LINK */
static ring_t to_board; /* the host -> the other board */

static void ring_put(ring_t *r, uint8_t b)
{
    if ((r->head - r->tail) < RING) {
        r->buf[r->head & (RING - 1u)] = b;
        r->head++;
    } else {
        r->lost++;
    }
}

static bool ring_get(ring_t *r, uint8_t *b)
{
    if (r->head == r->tail) {
        return false;
    }
    *b = r->buf[r->tail & (RING - 1u)];
    r->tail++;
    return true;
}

/* ---- pins and time ------------------------------------------------------------------ */

static uint32_t now(void)
{
    return REG32(DWT_CYCCNT);
}

/* Output low (or high), or released: an input, so the other board's pull-up holds the line. */
static void pin_drive(uintptr_t port, unsigned pin, bool high)
{
    REG32(port + GPIO_BSRR) = high ? (1u << pin) : (1u << (pin + 16u));
    REG32(port + GPIO_MODER) = (REG32(port + GPIO_MODER) & ~(3u << (2u * pin))) | (1u << (2u * pin));
}

static void pin_release(uintptr_t port, unsigned pin)
{
    REG32(port + GPIO_MODER) &= ~(3u << (2u * pin));
}

/* A pin to release later, without stopping the bytes meanwhile. */
typedef struct pulse {
    uintptr_t port;
    unsigned pin;
    uint32_t until;
    bool active;
    bool drive_low_after; /* the wake pin is driven, low, between pulses */
} pulse_t;

static pulse_t reset_pulse = {EMB_HW_GPIOA_BASE, 8u, 0u, false, false};
static pulse_t power_pulse = {EMB_HW_GPIOB_BASE, 5u, 0u, false, false};
static pulse_t wake_pulse = {EMB_HW_GPIOB_BASE, 4u, 0u, false, true};

static void pulse_start(pulse_t *p, bool high, uint32_t cycles)
{
    pin_drive(p->port, p->pin, high);
    p->until = now() + cycles;
    p->active = true;
}

static void pulse_poll(pulse_t *p)
{
    if (p->active && ((int32_t)(now() - p->until) >= 0)) {
        if (p->drive_low_after) {
            pin_drive(p->port, p->pin, false);
        } else {
            pin_release(p->port, p->pin);
        }
        p->active = false;
    }
}

/* ---- the host's bench commands: frames on channel 15 ----------------------------------- */

static uint16_t crc16(uint16_t crc, uint8_t byte)
{
    crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)byte << 8));
    for (unsigned i = 0u; i < 8u; i++) {
        crc = ((crc & 0x8000u) != 0u) ? (uint16_t)((uint16_t)(crc << 1) ^ CRC_POLY) : (uint16_t)(crc << 1);
    }
    return crc;
}

static uint8_t cmd_raw[CMD_MAX + 8u]; /* the COBS bytes since the last zero */
static uint32_t cmd_len;
static bool cmd_overrun;

static char reply[CMD_MAX + 8u];
static uint32_t reply_len; /* waiting to be sent toward the host */

static bool starts(const char *s, uint32_t n, const char *word)
{
    uint32_t i = 0u;
    while (word[i] != '\0') {
        if ((i >= n) || (s[i] != word[i])) {
            return false;
        }
        i++;
    }
    return true;
}

static uint32_t number(const char *s, uint32_t n, uint32_t at)
{
    uint32_t v = 0u;
    while ((at < n) && (s[at] >= '0') && (s[at] <= '9')) {
        v = (v * 10u) + (uint32_t)(s[at] - '0');
        at++;
    }
    return v;
}

static void act(const char *c, uint32_t n)
{
    bool ok = true;
    if (starts(c, n, "reset")) {
        pulse_start(&reset_pulse, false, 20u * CYCLES_PER_MS);
    } else if (starts(c, n, "power off")) {
        power_pulse.active = false;
        pin_drive(power_pulse.port, power_pulse.pin, false);
    } else if (starts(c, n, "power on")) {
        power_pulse.active = false;
        pin_release(power_pulse.port, power_pulse.pin);
    } else if (starts(c, n, "power cycle ")) {
        pulse_start(&power_pulse, false, number(c, n, 12u) * CYCLES_PER_MS);
    } else if (starts(c, n, "wake ")) {
        pulse_start(&wake_pulse, true, number(c, n, 5u) * CYCLES_PER_US);
    } else if (!starts(c, n, "ping")) {
        ok = false;
    }
    reply_len = 0u;
    reply[reply_len++] = ok ? 'o' : '?';
    if (ok) {
        reply[reply_len++] = 'k';
    }
    reply[reply_len++] = ' ';
    for (uint32_t i = 0u; (i < n) && (reply_len < CMD_MAX); i++) {
        reply[reply_len++] = c[i];
    }
}

/* A zero ended the bytes in cmd_raw: a channel-15 frame with a good CRC is a command. */
static void cmd_end(void)
{
    uint8_t dec[CMD_MAX + 8u];
    uint32_t n = 0u;
    uint32_t i = 0u;
    uint16_t crc = 0xFFFFu;
    if (cmd_overrun || (cmd_len < 2u)) {
        return;
    }
    while (i < cmd_len) { /* COBS: a code byte, then its run */
        uint32_t code = cmd_raw[i++];
        for (uint32_t k = 1u; (k < code) && (i < cmd_len); k++) {
            dec[n++] = cmd_raw[i++];
        }
        if ((code < 0xFFu) && (i < cmd_len)) {
            dec[n++] = 0u;
        }
    }
    if ((n < 3u) || (dec[0] != CH_BENCH)) {
        return;
    }
    for (i = 0u; i < (n - 2u); i++) {
        crc = crc16(crc, dec[i]);
    }
    if ((dec[n - 2u] != (uint8_t)crc) || (dec[n - 1u] != (uint8_t)(crc >> 8))) {
        return;
    }
    act((const char *)&dec[1], n - 3u);
}

static void watch_host_byte(uint8_t b)
{
    if (b == 0u) {
        cmd_end();
        cmd_len = 0u;
        cmd_overrun = false;
    } else if (cmd_len < sizeof(cmd_raw)) {
        cmd_raw[cmd_len++] = b;
    } else {
        cmd_overrun = true; /* longer than any command: not one */
    }
}

/* The reply, framed, toward the host: only between two of the other board's frames (the last
 * byte relayed was a zero) or once that way has been quiet for 2 ms. */
static void reply_send(bool boundary)
{
    uint8_t raw[CMD_MAX + 8u];
    uint32_t n = 0u;
    uint16_t crc = 0xFFFFu;
    uint32_t code_at;
    uint8_t code = 1u;
    if ((reply_len == 0u) || !boundary || (to_host.head != to_host.tail)) {
        return;
    }
    raw[n++] = CH_BENCH;
    for (uint32_t i = 0u; i < reply_len; i++) {
        raw[n++] = (uint8_t)reply[i];
    }
    for (uint32_t i = 0u; i < n; i++) {
        crc = crc16(crc, raw[i]);
    }
    raw[n++] = (uint8_t)crc;
    raw[n++] = (uint8_t)(crc >> 8);
    ring_put(&to_host, 0u); /* a zero first: the decoder starts clean */
    code_at = to_host.head;
    ring_put(&to_host, 0u); /* the first code byte, filled below */
    for (uint32_t i = 0u; i < n; i++) {
        if (raw[i] == 0u) {
            to_host.buf[code_at & (RING - 1u)] = code;
            code_at = to_host.head;
            ring_put(&to_host, 0u);
            code = 1u;
        } else {
            ring_put(&to_host, raw[i]);
            code++;
        }
    }
    to_host.buf[code_at & (RING - 1u)] = code;
    ring_put(&to_host, 0u);
    reply_len = 0u;
}

static void puts_vcp(const char *s)
{
    while (*s != '\0') {
        emb_stm32_usart_putc(EMB_HW_USART2_BASE, *s++);
    }
}

int main(void)
{
    uint32_t quiet_since;
    bool last_zero = true;
    if (emb_stm32f4_clock_init() == 0) {
        hang();
    }
    emb_stm32_pins_apply(pins, sizeof(pins) / sizeof(pins[0]));
    emb_stm32_clock_enable(EMB_HW_GPIOB_CLKEN_OFFSET, EMB_HW_GPIOB_CLKEN_BIT);
    emb_stm32_clock_enable(EMB_HW_USART2_CLKEN_OFFSET, EMB_HW_USART2_CLKEN_BIT);
    emb_stm32_clock_enable(EMB_HW_USART1_CLKEN_OFFSET, EMB_HW_USART1_CLKEN_BIT);
    emb_stm32_usart_init(EMB_HW_USART2_BASE, EMB_HW_APB1_HZ, 115200u);
    emb_stm32_usart_init(EMB_HW_USART1_BASE, EMB_HW_APB2_HZ, 115200u);
    REG32(EMB_HW_GPIOA_BASE + GPIO_PUPDR) =
        (REG32(EMB_HW_GPIOA_BASE + GPIO_PUPDR) & ~(3u << 20)) | (1u << 20); /* PA10 idles high */
    REG32(EMB_HW_USART1_BASE + USART_CR1) |= CR1_RE | CR1_TE;
    REG32(EMB_HW_USART2_BASE + USART_CR1) |= CR1_RE | CR1_TE;
    REG32(DEMCR) |= 1u << 24; /* TRCENA: the cycle counter times the bench pulses */
    REG32(DWT_CYCCNT) = 0u;
    REG32(DWT_CTRL) |= 1u;
    pin_drive(wake_pulse.port, wake_pulse.pin, false); /* driven low between wake pulses */
    puts_vcp("\r\n[nucleo uart relay: PA10 <-> VCP <-> PA9, 115200; bench on channel 15]\r\n");
    quiet_since = now();
    for (;;) {
        uint32_t sr1 = REG32(EMB_HW_USART1_BASE + USART_SR);
        uint32_t sr2 = REG32(EMB_HW_USART2_BASE + USART_SR);
        uint8_t b;
        if ((sr1 & (SR_RXNE | SR_ORE)) != 0u) {
            b = (uint8_t)REG32(EMB_HW_USART1_BASE + USART_DR); /* also clears ORE, FE */
            if ((sr1 & SR_FE) == 0u) {
                ring_put(&to_host, b);
                emb_stm32_gpio_toggle(EMB_HW_GPIOA_BASE, 5u);
            }
        }
        if ((sr2 & (SR_RXNE | SR_ORE)) != 0u) {
            b = (uint8_t)REG32(EMB_HW_USART2_BASE + USART_DR);
            if ((sr2 & SR_FE) == 0u) {
                ring_put(&to_board, b);
                watch_host_byte(b);
            }
        }
        if (((sr2 & SR_TXE) != 0u) && ring_get(&to_host, &b)) {
            REG32(EMB_HW_USART2_BASE + USART_DR) = b;
            last_zero = b == 0u;
            quiet_since = now();
        }
        if (((sr1 & SR_TXE) != 0u) && ring_get(&to_board, &b)) {
            REG32(EMB_HW_USART1_BASE + USART_DR) = b;
        }
        reply_send(last_zero || ((now() - quiet_since) > (2u * CYCLES_PER_MS)));
        pulse_poll(&reset_pulse);
        pulse_poll(&power_pulse);
        pulse_poll(&wake_pulse);
    }
}
