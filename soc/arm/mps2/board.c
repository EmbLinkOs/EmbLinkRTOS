/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Board support for the MPS2 boards as QEMU models them (mps2-an385, mps2-an386), each
 * described in YAML: console on the CMSDK APB UART0, LED0 in the FPGA I/O block, run end
 * through Arm semihosting (QEMU runs with -semihosting); addresses from hw_config.h. A reference
 * board for emulation (SIM-002), not a hardware target: semihosting halts a part with
 * no debugger attached.
 */
#include <emb/fault.h>

#include <emb_board.h>
#include <emb_cmsdk_uart.h>
#include <emb_cortex_m.h>
#include <hw_config.h>

#define FPGAIO_LED (*(volatile uint32_t *)(uintptr_t)(EMB_HW_FPGAIO_BASE + 0x00u))

static unsigned led_state;

void emb_board_init(void)
{
    /* the polled console, before anything else; its device (uart0, SPEC-016) is
     * initialized with the board's EARLY devices and sets up the same registers */
    emb_cmsdk_uart_setup(EMB_HW_CONSOLE_BASE, EMB_HW_CONSOLE_CLOCK_HZ, EMB_HW_CONSOLE_BAUD);
    led_state = 0u;
}

void emb_board_led_toggle(void)
{
    led_state ^= 1u;
    FPGAIO_LED = led_state; /* LED0 (QEMU models the register) */
}

void emb_board_puts(const char *s)
{
    while (*s != '\0') { /* bounded by the string */
        emb_cmsdk_uart_putc(EMB_HW_CONSOLE_BASE, *s);
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
