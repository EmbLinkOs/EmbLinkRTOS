/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * esp32_devkitc (an ESP32 development board with the ESP-WROOM-32 module: the DevKitC and its
 * "ESP-32S" copies) as its boot ROM leaves it after loading this image from flash offset
 * 0x1000: the CPU on the 40 MHz crystal, UART0 at 115200 on GPIO1/3, which the board's
 * USB-serial bridge carries. Register addresses from Espressif's ESP32 Technical Reference
 * Manual (UART, chapter 13) and ESP-IDF v5.4's soc/esp32 headers. The board has no LED the
 * CPU drives on every maker's copy: emb_board_led_toggle() keeps its state only. A hardware
 * target: the run ends in a waiti loop with interrupts masked.
 *
 * The CPU stays at 40 MHz in this milestone: the PLL (160 or 240 MHz, as ESP-IDF's startup
 * sets it) comes with the board's clock driver. Every measurement counts CCOUNT cycles, so
 * comparisons with other kernels on this board are in cycles.
 */
#include <emb/fault.h>

#include <emb_arch.h>

#include <emb_board.h>
#include <hw_config.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define UART_FIFO        REG32(EMB_HW_UART0_BASE + 0x00u)
#define UART_STATUS      REG32(EMB_HW_UART0_BASE + 0x1Cu)
#define STATUS_TXFIFO(s) (((s) >> 16) & 0xFFu)
#define STATUS_TX_BUSY   (0xFu << 24) /* ST_UTX_OUT: the transmitter's state, 0 when idle */
#define TXFIFO_SIZE      128u
#define TX_BOUND         200000u /* well past one character time at 115200 baud and 40 MHz */

static unsigned led_state;

static void tx_wait_idle(void)
{
    uint32_t n;
    for (n = 0u; n < TX_BOUND * TXFIFO_SIZE; n++) { /* bounded: the FIFO's last characters */
        if (STATUS_TXFIFO(UART_STATUS) == 0u && (UART_STATUS & STATUS_TX_BUSY) == 0u) {
            break;
        }
    }
}

void emb_board_init(void)
{
    led_state = 0u; /* UART0 stays as the ROM set it: 115200 8N1 */
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
    (void)code; /* no host to report it to */
    tx_wait_idle();
    emb_xt_halt();
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
