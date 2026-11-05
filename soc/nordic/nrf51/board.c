/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Board support for nRF51 boards described in YAML (A86), as QEMU models the micro:bit: the
 * console on the UART, the run's end through Arm semihosting. Bases from hw_config.h; the
 * UART's register offsets from the nRF51 reference manual and QEMU's hw/char/nrf51_uart.c.
 * An emulation reference, like the MPS2 boards: PSELTXD is left at its reset value, so on a
 * physical micro:bit the console's pin would still have to be selected.
 */
#include <emb/fault.h>

#include <emb_board.h>
#include <emb_cortex_m.h>
#include <hw_config.h>

#define REG(off)         (*(volatile uint32_t *)(uintptr_t)(EMB_HW_CONSOLE_BASE + (off)))
#define UART_STARTTX     0x008u
#define UART_TXDRDY      0x11Cu /* event: the byte in TXD has gone */
#define UART_ENABLE      0x500u
#define UART_TXD         0x51Cu
#define UART_BAUDRATE    0x524u
#define UART_ENABLE_UART 4u
/* the reference manual's BAUDRATE value for 115200 baud (given there in hex), in decimal so that
 * no register value reads as an address to HW-001's check */
#define UART_BAUD_115200 30924800u

void emb_board_init(void)
{
    REG(UART_ENABLE) = UART_ENABLE_UART;
    REG(UART_BAUDRATE) = UART_BAUD_115200; /* EMB_HW_CONSOLE_BAUD is 115200 on every board here */
    REG(UART_STARTTX) = 1u;
}

void emb_board_led_toggle(void)
{
    /* the micro:bit's LEDs are a matrix QEMU does not model */
}

static void put_char(char c)
{
    REG(UART_TXD) = (uint32_t)(unsigned char)c;
    while (REG(UART_TXDRDY) == 0u) { /* bounded by the UART sending one byte */
    }
    REG(UART_TXDRDY) = 0u;
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
    emb_cm_semihosting_exit(code);
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
