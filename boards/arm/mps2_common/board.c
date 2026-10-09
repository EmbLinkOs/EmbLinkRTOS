/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * MPS2 boards as QEMU models them (mps2-an385, mps2-an386): console on the CMSDK APB
 * UART0, run end through Arm semihosting (QEMU runs with -semihosting). A reference
 * board for emulation (SIM-002), not a hardware target: semihosting halts a part with
 * no debugger attached.
 */
#include <emb/fault.h>

#include <emb_board.h>
#include <emb_cortex_m.h>
#include <hw_config.h>

#define UART_DATA     (*(volatile uint32_t *)(uintptr_t)(EMB_HW_UART0_BASE + 0x00u))
#define UART_STATE    (*(volatile uint32_t *)(uintptr_t)(EMB_HW_UART0_BASE + 0x04u))
#define UART_CTRL     (*(volatile uint32_t *)(uintptr_t)(EMB_HW_UART0_BASE + 0x08u))
#define UART_BAUDDIV  (*(volatile uint32_t *)(uintptr_t)(EMB_HW_UART0_BASE + 0x10u))
#define STATE_TX_FULL 1u
#define CTRL_TX_EN    1u

static unsigned led_state;

void emb_board_init(void)
{
    UART_BAUDDIV = EMB_HW_CPU_HZ / 115200u;
    UART_CTRL = CTRL_TX_EN;
    led_state = 0u;
}

void emb_board_led_toggle(void)
{
    led_state ^= 1u;
    /* FPGAIO LED0 (QEMU models the register) */
    *(volatile uint32_t *)(uintptr_t)0x40028000u = led_state;
}

static void put_char(char c)
{
    /* bounded by the UART draining one character */
    while ((UART_STATE & STATE_TX_FULL) != 0u) {
    }
    UART_DATA = (uint32_t)(unsigned char)c;
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
