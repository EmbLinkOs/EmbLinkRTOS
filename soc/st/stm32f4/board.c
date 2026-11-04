/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Board support for every STM32F4 board described in YAML: the emb_board.h interface over the
 * STM32F4 drivers, with the clock tree, pins, console and LED from the board's generated
 * hw_config.h and board_pins.c (SPEC-014). A hardware target: the run ends in a WFI loop with
 * interrupts masked, since a part with no debugger attached has nowhere to exit to (an emulator
 * ends the run at the console's EMB_TEST_END).
 */
#include <emb/fault.h>

#include <emb_board.h>
#include <emb_cortex_m.h>
#include <emb_stm32_drivers.h>
#include <hw_config.h>

static void console_init(uint32_t pclk_hz)
{
    emb_stm32_clock_enable(EMB_HW_CONSOLE_CLKEN_OFFSET, EMB_HW_CONSOLE_CLKEN_BIT);
    emb_stm32_usart_init(EMB_HW_CONSOLE_BASE, pclk_hz, EMB_HW_CONSOLE_BAUD);
}

void emb_board_init(void)
{
    int clocked = emb_stm32f4_clock_init();
    emb_stm32_pins_apply(emb_board_pins, emb_board_pin_count);
    if (clocked != 0) {
        console_init(EMB_HW_CONSOLE_CLOCK_HZ);
        return;
    }
    /* still on HSI: say why, then stop, since the kernel's tick assumes EMB_HW_CPU_HZ */
    console_init(EMB_HW_HSI_HZ);
    emb_board_puts("\nemb: " EMB_HW_BOARD ": no ");
    emb_board_put_u32(EMB_HW_CPU_HZ);
    emb_board_puts(" Hz clock (is the HSE input running?)\n");
    emb_board_exit(3);
}

void emb_board_led_toggle(void)
{
#ifdef EMB_HW_LED_PIN
    emb_stm32_gpio_toggle(EMB_HW_LED_PORT_BASE, EMB_HW_LED_PIN);
#endif
}

void emb_board_puts(const char *s)
{
    while (*s != '\0') { /* bounded by the string */
        emb_stm32_usart_putc(EMB_HW_CONSOLE_BASE, *s);
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
    emb_stm32_usart_flush(EMB_HW_CONSOLE_BASE);
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
