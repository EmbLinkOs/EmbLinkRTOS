/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    board.c
 * @brief   Board support for RP2350 boards described in YAML (A79), on the Arm cores in the Secure
 *          state, over the RP2350 drivers (drivers/rpi) and the PL011's (drivers/arm).
 *
 * @details
 * The clocks from the crystal, the pins from the generated table, the console on a PL011 UART, and
 * the run's end back in the boot ROM's loader. Bases and frequencies come from hw_config.h.
 *
 * @author  Junior Deogracias
 * @date    2026-11-04 (created)
 */
#include <emb/fault.h>

#include <emb_board.h>
#include <emb_cortex_m.h>
#include <emb_pl011.h>
#include <emb_rp2350_drivers.h>
#include <hw_config.h>

/* The boot ROM's IMAGE_DEF block is generated (boot_block.c), from the SoC's boot entry. No
 * code reads it, so this reference is what makes the linker take it from the library; the
 * linker script then keeps its section after the vector table. */
extern const uint32_t emb_boot_block[5];
EMB_USED static const uint32_t *const emb_boot_block_ref = emb_boot_block;

void emb_board_init(void)
{
    int clocked = emb_rp2350_clock_init();
    (void)emb_rp2350_unreset(EMB_RP2350_RESET_IO_BANK0 | EMB_RP2350_RESET_PADS_BANK0 |
                             EMB_RP2350_RESET_UART0 | EMB_RP2350_RESET_UART1);
    emb_rp2350_pins_apply(emb_board_pins, emb_board_pin_count);
    /* the polled console, before anything else; its device (uart0, SPEC-016) finds it running
     * at its rate and leaves it as it is */
    emb_pl011_setup(EMB_HW_CONSOLE_BASE, EMB_HW_CONSOLE_CLOCK_HZ, EMB_HW_CONSOLE_BAUD);
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

void emb_board_puts(const char *s)
{
    while (*s != '\0') { /* bounded by the string */
        emb_pl011_putc(EMB_HW_CONSOLE_BASE, *s);
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

/* set for a test image built for the board (EMB_TEST_EMULATOR=hil, the SoC's CMakeLists.txt) */
#ifndef EMB_RP2350_EXIT_TO_BOOTSEL
#define EMB_RP2350_EXIT_TO_BOOTSEL 0
#endif

#if EMB_RP2350_EXIT_TO_BOOTSEL
/* The boot ROM's reboot (RP2350 datasheet §5.4.8.24; pico-sdk 2.1.1's bootrom_constants.h and
 * picoboot_constants.h): the table lookup function is the halfword pointer at 0x16, asked for
 * code 'R','B' with RT_FLAG_FUNC_ARM_SEC (this code runs in the Secure state). */
#define ROM_TABLE_LOOKUP                  0x16u
#define ROM_FUNC_REBOOT                   ((uint32_t)'R' | ((uint32_t)'B' << 8))
#define RT_FLAG_FUNC_ARM_SEC              0x0004u
#define REBOOT2_FLAG_REBOOT_TYPE_BOOTSEL  0x2u
#define REBOOT2_FLAG_NO_RETURN_ON_SUCCESS 0x100u
/* the lookup returns the function's address; as an integer, so that ISO C converts it */
typedef uintptr_t (*rom_table_lookup_fn)(uint32_t code, uint32_t mask);
typedef int (*rom_reboot_fn)(uint32_t flags, uint32_t delay_ms, uint32_t p0, uint32_t p1);

/* Back to the boot ROM's USB loader, both interfaces on, so that picotool can load the next
 * image without the BOOTSEL button (tools/hil/run_hil.py). The table pointer is read through a
 * volatile pointer object: GCC takes a constant address this low for a null-page access. */
static void reboot_to_bootsel(void)
{
    const volatile uint16_t *volatile table =
        (const volatile uint16_t *)(uintptr_t)ROM_TABLE_LOOKUP;
    rom_table_lookup_fn lookup = (rom_table_lookup_fn)(uintptr_t)*table;
    uintptr_t fn = lookup(ROM_FUNC_REBOOT, RT_FLAG_FUNC_ARM_SEC);
    if (fn != 0u) {
        rom_reboot_fn reboot = (rom_reboot_fn)fn;
        (void)reboot(REBOOT2_FLAG_REBOOT_TYPE_BOOTSEL | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS, 10u, 0u,
                     0u);
    }
}
#endif

/* A test image built for the board (EMB_TEST_EMULATOR=hil) hands it back to the boot ROM's
 * loader when it ends; otherwise, and if the reboot fails, the CPU halts. */
void emb_board_exit(int code)
{
    (void)code; /* no host to report it to */
    emb_pl011_flush(EMB_HW_CONSOLE_BASE);
#if EMB_RP2350_EXIT_TO_BOOTSEL
    reboot_to_bootsel();
#endif
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
#if CONFIG_EMB_FAULT_ACTION_REBOOT
    emb_pl011_flush(EMB_HW_CONSOLE_BASE); /* the port resets next: the line must leave first */
#else
    emb_board_exit(2); /* the halt action: the run ends here */
#endif
}
