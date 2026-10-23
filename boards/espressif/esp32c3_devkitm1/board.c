/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * esp32c3_devkitm1 (ESP32-C3-DevKitM-1 V1.0): the ESP32-C3-MINI-1 as its boot ROM leaves
 * it after loading this image from flash offset 0 (CPU_CLK XTAL_CLK / 2, UART0 at 115200
 * on GPIO21/20); console on UART0, which the board's CP2102N carries to its USB port.
 * Register addresses and bits from the ESP32-C3 Technical Reference Manual v1.4,
 * chapter 26. The board's only LED is the SK6812 on GPIO8, an addressable one whose
 * timing the CPU cannot bit-bang at this clock: emb_board_led_toggle() keeps its state
 * only, as QEMU virt's does. A hardware target: the run ends in a WFI loop with interrupts
 * masked (an emulator ends the run at the console's EMB_TEST_END).
 */
#include <emb/fault.h>

#include <emb_arch.h>

#include <emb_board.h>
#include <hw_config.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define UART_FIFO        REG32(EMB_HW_UART0_BASE + 0x00u)
#define UART_STATUS      REG32(EMB_HW_UART0_BASE + 0x1Cu)
#define STATUS_TXFIFO(s) (((s) >> 16) & 0x3FFu)
#define TXFIFO_SIZE      128u
#define TX_BOUND         200000u /* well past one character time at 115200 baud and 20 MHz */

static unsigned led_state;

void emb_board_init(void)
{
    led_state = 0u; /* UART0 as the ROM left it: 115200 8N1 */
}

void emb_board_led_toggle(void)
{
    led_state ^= 1u;
}

static void put_char(char c)
{
    uint32_t n;
    for (n = 0u; n < TX_BOUND; n++) { /* bounded by TX_BOUND: one character time */
        if (STATUS_TXFIFO(UART_STATUS) < TXFIFO_SIZE - 1u) {
            break;
        }
    }
    UART_FIFO = (uint32_t)(unsigned char)c;
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
    uint32_t n;
    (void)code;                                     /* no host to report it to */
    for (n = 0u; n < TX_BOUND * TXFIFO_SIZE; n++) { /* bounded: the FIFO's last characters */
        if (STATUS_TXFIFO(UART_STATUS) == 0u) {
            break;
        }
    }
    emb_rv_halt();
}

void emb_board_fault_report(const emb_fault_info_t *info)
{
    emb_board_puts("\nemb: kernel fault class ");
    emb_board_put_u32((info != NULL) ? info->fault_class : 0u);
    emb_board_puts(" code ");
    emb_board_put_u32((info != NULL) ? info->code : 0u);
    emb_board_puts(" pc ");
    emb_board_put_u32((info != NULL) ? (uint32_t)info->pc : 0u);
    emb_board_puts(" address ");
    emb_board_put_u32((info != NULL) ? (uint32_t)info->address : 0u);
    emb_board_puts(" at ");
    emb_board_puts((info != NULL && info->where != NULL) ? info->where : "?");
    emb_board_puts("\n");
    emb_board_exit(2);
}
