/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Board support for RP2350 boards described in YAML (A79), on the Arm cores in the Secure
 * state: the clocks from the crystal, the pins from the
 * generated table, and the console on a PL011 UART. Bases and frequencies come from
 * hw_config.h; register offsets and bits from pico-sdk 2.1.1's hardware/regs headers and the
 * RP2350 datasheet.
 */
#include <emb/fault.h>

#include <emb_board.h>
#include <emb_cortex_m.h>
#include <emb_rp2350_pins.h>
#include <hw_config.h>

#define REG(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
/* every peripheral has atomic aliases: write ones to set (+0x2000) or clear (+0x3000) bits */
#define SET_ALIAS 0x2000u
#define CLR_ALIAS 0x3000u

/* The boot ROM's IMAGE_DEF block is generated (boot_block.c), from the SoC's boot entry. No
 * code reads it, so this reference is what makes the linker take it from the library; the
 * linker script then keeps its section after the vector table. */
extern const uint32_t emb_boot_block[5];
EMB_USED static const uint32_t *const emb_boot_block_ref = emb_boot_block;

/* ---- RESETS, XOSC, CLOCKS -------------------------------------------------------------- */
#define RESETS_RESET      (EMB_HW_RESETS_BASE + 0x00u)
#define RESETS_RESET_DONE (EMB_HW_RESETS_BASE + 0x08u)
#define RESET_IO_BANK0    (1u << 6)
#define RESET_PADS_BANK0  (1u << 9)
#define RESET_UART0       (1u << 26)
#define RESET_UART1       (1u << 27)

#define XOSC_CTRL       (EMB_HW_XOSC_BASE + 0x00u)
#define XOSC_STATUS     (EMB_HW_XOSC_BASE + 0x04u)
#define XOSC_STARTUP    (EMB_HW_XOSC_BASE + 0x0Cu)
#define XOSC_RANGE_1_15 0xAA0u
#define XOSC_ENABLE     (0xFABu << 12)
#define XOSC_STABLE     (1u << 31)
/* pico-sdk's delay, 1 ms of crystal cycles in units of 256, times a margin of 64 */
#define XOSC_DELAY ((((EMB_HW_CPU_HZ / 1000u) + 128u) / 256u) * 64u)

#define CLK_REF_CTRL     (EMB_HW_CLOCKS_BASE + 0x30u)
#define CLK_REF_DIV      (EMB_HW_CLOCKS_BASE + 0x34u)
#define CLK_REF_SELECTED (EMB_HW_CLOCKS_BASE + 0x38u)
#define CLK_SYS_CTRL     (EMB_HW_CLOCKS_BASE + 0x3Cu)
#define CLK_SYS_DIV      (EMB_HW_CLOCKS_BASE + 0x40u)
#define CLK_SYS_SELECTED (EMB_HW_CLOCKS_BASE + 0x44u)
#define CLK_PERI_CTRL    (EMB_HW_CLOCKS_BASE + 0x48u)
#define CLK_PERI_DIV     (EMB_HW_CLOCKS_BASE + 0x4Cu)
#define CLK_REF_SRC_XOSC 2u
#define CLK_SYS_SRC_REF  0u
#define CLK_PERI_ENABLE  (1u << 11) /* AUXSRC 0: clk_sys */
#define CLK_DIV_ONE      (1u << 16) /* INT 1, FRAC 0 */

#define POLL_LIMIT 1000000u

/* ---- the pins ------------------------------------------------------------------------------ */
#define IO_GPIO_CTRL(n) (EMB_HW_IO_BANK0_BASE + 0x04u + 8u * (n))
#define PADS_GPIO(n)    (EMB_HW_PADS_BANK0_BASE + 0x04u + 4u * (n))
#define PAD_ISO         (1u << 8)
#define PAD_OD          (1u << 7)
#define PAD_IE          (1u << 6)

/* ---- PL011 ---------------------------------------------------------------------------------- */
#define UART_DR    (EMB_HW_CONSOLE_BASE + 0x00u)
#define UART_FR    (EMB_HW_CONSOLE_BASE + 0x18u)
#define UART_IBRD  (EMB_HW_CONSOLE_BASE + 0x24u)
#define UART_FBRD  (EMB_HW_CONSOLE_BASE + 0x28u)
#define UART_LCR_H (EMB_HW_CONSOLE_BASE + 0x2Cu)
#define UART_CR    (EMB_HW_CONSOLE_BASE + 0x30u)
#define FR_TXFF    (1u << 5)
#define FR_BUSY    (1u << 3)
#define LCR_8N1    0x60u /* WLEN 8 bits, one stop bit, no parity */
#define LCR_FEN    0x10u
#define CR_ENABLE  0x301u /* UARTEN | TXE | RXE */
/* pico-sdk's divisor: 64ths of the 16x oversampled bit time, rounded */
#define BAUD_DIV  ((8u * EMB_HW_CONSOLE_CLOCK_HZ / EMB_HW_CONSOLE_BAUD) + 1u)
#define BAUD_IBRD (BAUD_DIV >> 7)
#define BAUD_FBRD ((BAUD_DIV & 0x7Fu) >> 1)

static int poll_set(uint32_t addr, uint32_t mask, uint32_t want)
{
    for (uint32_t i = 0u; i < POLL_LIMIT; i++) { /* bounded */
        if ((REG(addr) & mask) == want) {
            return 0;
        }
    }
    return -1;
}

/* clk_ref from the crystal, clk_sys from clk_ref, clk_peri (the UARTs') from clk_sys: all at the
 * crystal's frequency, EMB_HW_CPU_HZ. The PLL is not used in this first increment. */
static int clock_init(void)
{
    REG(XOSC_STARTUP) = XOSC_DELAY;
    REG(XOSC_CTRL) = XOSC_ENABLE | XOSC_RANGE_1_15;
    if (poll_set(XOSC_STATUS, XOSC_STABLE, XOSC_STABLE) != 0) {
        return -1;
    }
    REG(CLK_REF_DIV) = CLK_DIV_ONE;
    REG(CLK_REF_CTRL) = CLK_REF_SRC_XOSC; /* glitchless: SELECTED says when it took */
    if (poll_set(CLK_REF_SELECTED, 0xFu, 1u << CLK_REF_SRC_XOSC) != 0) {
        return -1;
    }
    REG(CLK_SYS_DIV) = CLK_DIV_ONE;
    REG(CLK_SYS_CTRL) = CLK_SYS_SRC_REF;
    if (poll_set(CLK_SYS_SELECTED, 0x3u, 1u << CLK_SYS_SRC_REF) != 0) {
        return -1;
    }
    REG(CLK_PERI_DIV) = CLK_DIV_ONE;
    REG(CLK_PERI_CTRL) = CLK_PERI_ENABLE;
    return 0;
}

static void pins_apply(void)
{
    for (unsigned i = 0u; i < emb_board_pin_count; i++) { /* bounded by the table */
        const emb_rp2350_pin_t *p = &emb_board_pins[i];
        REG(IO_GPIO_CTRL(p->gpio)) = p->funcsel;
        uint32_t pad = REG(PADS_GPIO(p->gpio)) & ~(PAD_ISO | PAD_OD | PAD_IE);
        REG(PADS_GPIO(p->gpio)) = pad |
                                  ((p->input_en != 0u) ? PAD_IE : 0u); /* then out of isolation */
    }
}

static void console_init(void)
{
    REG(UART_CR) = 0u;
    REG(UART_IBRD) = BAUD_IBRD;
    REG(UART_FBRD) = BAUD_FBRD;
    REG(UART_LCR_H) = LCR_8N1 | LCR_FEN; /* also latches the divisors */
    REG(UART_CR) = CR_ENABLE;
}

void emb_board_init(void)
{
    int clocked = clock_init();
    uint32_t blocks = RESET_IO_BANK0 | RESET_PADS_BANK0 | RESET_UART0 | RESET_UART1;
    REG(RESETS_RESET + CLR_ALIAS) = blocks;
    (void)poll_set(RESETS_RESET_DONE, blocks, blocks);
    pins_apply();
    console_init();
    if (clocked != 0) {
        /* the console runs at whatever clk_peri the ROM left, so this may print garbled */
        emb_board_puts("\nemb: the crystal oscillator did not start; halting\n");
        emb_board_exit(3);
    }
}

void emb_board_led_toggle(void)
{
    /* the Pico 2 W's LED is on its wireless chip, which has no driver yet */
}

static void put_char(char c)
{
    while ((REG(UART_FR) & FR_TXFF) != 0u) { /* bounded by the UART draining a character */
    }
    REG(UART_DR) = (uint32_t)(unsigned char)c;
}

void emb_board_puts(const char *s)
{
    while (*s != '\0') { /* bounded by the string */
        put_char(*s);
        s++;
    }
}

void emb_board_put_u32(uint32_t v)
{
    char buf[11];
    unsigned i = sizeof(buf) - 1u;
    buf[i] = '\0';
    do { /* at most ten digits */
        i--;
        buf[i] = (char)('0' + (char)(v % 10u));
        v /= 10u;
    } while (v != 0u && i > 0u);
    emb_board_puts(&buf[i]);
}

void emb_board_exit(int code)
{
    (void)code; /* no host to report it to */
    (void)poll_set(UART_FR, FR_BUSY, 0u);
    emb_cm_halt();
}

void emb_board_fault_report(const emb_fault_info_t *info)
{
    emb_board_puts("\nemb: kernel fault class ");
    emb_board_put_u32((info != NULL) ? info->fault_class : 0u);
    emb_board_puts(" code ");
    emb_board_put_u32((info != NULL) ? info->code : 0u);
    emb_board_puts(" pc ");
    emb_board_put_u32((info != NULL) ? (uint32_t)info->pc : 0u);
    emb_board_puts(" at ");
    emb_board_puts((info != NULL && info->where != NULL) ? info->where : "?");
    emb_board_puts("\n");
    emb_board_exit(2);
}
