/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_rp2350_drivers.h
 * @brief   Minimal RP2350 drivers: the clock tree from the crystal, the reset controller and the
 *          pins.
 *
 * @details
 * Register offsets and bits are driver knowledge (pico-sdk 2.1.1's hardware/regs headers and the
 * RP2350 datasheet); every base address, frequency and PLL factor comes from the board's generated
 * hw_config.h (SPEC-014, A79).
 *
 * @author  Junior Deogracias
 * @date    2026-11-09 (created)
 */
#ifndef EMB_RP2350_DRIVERS_H
#define EMB_RP2350_DRIVERS_H

#include <stdint.h>

#include <emb_rp2350_pins.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RESETS_RESET bits (datasheet §7.5) */
#define EMB_RP2350_RESET_IO_BANK0   (1u << 6)
#define EMB_RP2350_RESET_PADS_BANK0 (1u << 9)
#define EMB_RP2350_RESET_PLL_SYS    (1u << 14)
#define EMB_RP2350_RESET_SPI0       (1u << 18)
#define EMB_RP2350_RESET_SPI1       (1u << 19)
#define EMB_RP2350_RESET_DMA        (1u << 2)
#define EMB_RP2350_RESET_I2C0       (1u << 4)
#define EMB_RP2350_RESET_I2C1       (1u << 5)
#define EMB_RP2350_RESET_UART0      (1u << 26)
#define EMB_RP2350_RESET_UART1      (1u << 27)
#define EMB_RP2350_RESET_TRNG       ((uint32_t)1u << 25)

/* clk_ref from the crystal, clk_sys from clk_ref and then, where the board asks for more,
 * from PLL_SYS at EMB_HW_CPU_HZ, the flash interface slowed first; clk_peri follows clk_sys.
 * Zero once done; nonzero when the crystal or the PLL never became ready. */
int emb_rp2350_clock_init(void);
/* Takes the blocks out of reset and waits, bounded, until they are. Zero once they are. */
int emb_rp2350_unreset(uint32_t blocks);
/* Applies the board's pin table: each line's function, then its pad out of isolation. */
void emb_rp2350_pins_apply(const emb_rp2350_pin_t *pins, unsigned count);

#ifdef __cplusplus
}
#endif

#endif /* EMB_RP2350_DRIVERS_H */
