/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    rp2350_gpio.c
 * @brief   RP2350 resets (datasheet §7.5) and pins: IO_BANK0's function select and PADS_BANK0's
 *          pads.
 *
 * @author  Junior Deogracias
 * @date    2026-11-09 (created)
 */
#include <emb_rp2350_drivers.h>
#include <hw_config.h>

#define REG(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#define CLR_ALIAS 0x3000u /* write ones to clear bits */

#define RESETS_RESET      (EMB_HW_RESETS_BASE + 0x00u)
#define RESETS_RESET_DONE (EMB_HW_RESETS_BASE + 0x08u)
#define POLL_LIMIT        1000000u

#define IO_GPIO_CTRL(n) (EMB_HW_IO_BANK0_BASE + 0x04u + 8u * (n))
#define PADS_GPIO(n)    (EMB_HW_PADS_BANK0_BASE + 0x04u + 4u * (n))
#define PAD_ISO         (1u << 8)
#define PAD_OD          (1u << 7)
#define FUNCSEL_NULL    31u /* IO_BANK0 GPIOn_CTRL: no function, an analog input */
#define PAD_IE          (1u << 6)
#define PAD_PUE         (1u << 3)
#define PAD_PDE         (1u << 2)
/* the SIO's output registers (datasheet §3.1.11) */
#define SIO_GPIO_OUT_SET (EMB_HW_SIO_BASE + 0x018u)
#define SIO_GPIO_OUT_CLR (EMB_HW_SIO_BASE + 0x020u)
#define SIO_GPIO_OE_SET  (EMB_HW_SIO_BASE + 0x038u)

int emb_rp2350_unreset(uint32_t blocks)
{
    REG(RESETS_RESET + CLR_ALIAS) = blocks;
    for (uint32_t i = 0u; i < POLL_LIMIT; i++) { /* bounded */
        if ((REG(RESETS_RESET_DONE) & blocks) == blocks) {
            return 0;
        }
    }
    return -1;
}

void emb_rp2350_pins_apply(const emb_rp2350_pin_t *pins, unsigned count)
{
    for (unsigned i = 0u; i < count; i++) { /* bounded by the table */
        const emb_rp2350_pin_t *p = &pins[i];
        if (p->out != EMB_RP2350_OUT_NONE) { /* the level, then the driver, before the pin */
            REG((p->out == EMB_RP2350_OUT_HIGH) ? SIO_GPIO_OUT_SET : SIO_GPIO_OUT_CLR) = 1u
                                                                                         << p->gpio;
            REG(SIO_GPIO_OE_SET) = 1u << p->gpio;
        }
        REG(IO_GPIO_CTRL(p->gpio)) = p->funcsel;
        uint32_t pad = REG(PADS_GPIO(p->gpio)) & ~(PAD_ISO | PAD_OD | PAD_IE);
        switch (p->pull) {
        case EMB_RP2350_PULL_UP:
            pad = (pad & ~PAD_PDE) | PAD_PUE;
            break;
        case EMB_RP2350_PULL_NONE:
            pad &= ~(PAD_PDE | PAD_PUE);
            break;
        default: /* the pad's reset pull-down stays */
            break;
        }
        if (p->funcsel == FUNCSEL_NULL) {
            pad |= PAD_OD; /* an analog input: no digital output either, as the ADC chapter asks */
        }
        REG(PADS_GPIO(p->gpio)) = pad |
                                  ((p->input_en != 0u) ? PAD_IE : 0u); /* then out of isolation */
    }
}
