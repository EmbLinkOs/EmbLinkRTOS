/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    rp2350_devices.c
 * @brief   The RP2350 drivers as devices (SPEC-016): thin adapters over emb_rp2350_drivers.h.
 *
 * @author  Junior Deogracias
 * @date    2026-11-09 (created)
 */
#include <emb_rp2350_devices.h>
#include <emb_rp2350_drivers.h>
#include <emb_rp2350_pins.h>
#include <hw_config.h>

#if CONFIG_EMB_DEVICE
#define REG32(a)         (*(volatile uint32_t *)(uintptr_t)(a))
#define CLK_REF_SELECTED 0x38u
#define CLK_REF_SRC_XOSC 2u
/* the SIO's GPIO registers (datasheet §3.1.11) and a pad's (§9.11.3) */
#define SIO_GPIO_IN      (EMB_HW_SIO_BASE + 0x004u)
#define SIO_GPIO_OUT_SET (EMB_HW_SIO_BASE + 0x018u)
#define SIO_GPIO_OUT_CLR (EMB_HW_SIO_BASE + 0x020u)
#define SIO_GPIO_OE_SET  (EMB_HW_SIO_BASE + 0x038u)
#define SIO_GPIO_OE_CLR  (EMB_HW_SIO_BASE + 0x040u)
#define IO_CTRL(b, n)    ((b) + 0x04u + (8u * (n)))
#define PADS_GPIO(n)     (EMB_HW_PADS_BANK0_BASE + 0x04u + (4u * (n)))
#define PAD_ISO          (1u << 8)
#define PAD_OD           (1u << 7)
#define PAD_IE           (1u << 6)
#define PAD_PUE          (1u << 3)
#define PAD_PDE          (1u << 2)
#define FUNCSEL_SIO      5u

static emb_status_t clocks_init(const emb_device_t *dev)
{
    const emb_rp2350_clocks_config_t *cfg = (const emb_rp2350_clocks_config_t *)dev->config;
    /* datasheet §8.1.3: SELECTED shows the source the glitchless mux runs from, one-hot */
    return (REG32(cfg->base + CLK_REF_SELECTED) == (1u << CLK_REF_SRC_XOSC)) ? EMB_OK : EMB_EIO;
}

static emb_status_t io_bank_init(const emb_device_t *dev)
{
    (void)dev; /* the reset controller's bits name the bank */
    return (emb_rp2350_unreset(EMB_RP2350_RESET_IO_BANK0 | EMB_RP2350_RESET_PADS_BANK0) == 0)
               ? EMB_OK
               : EMB_EIO;
}

/* The lines set up open drain: the pads have none, so the output enable does it. Low drives
 * the line low; high lets go of it, and its pull or the bus's resistor raises it. Bank 0 is
 * the RP2350's one GPIO bank, so the set is the driver's. */
static uint32_t io_open_drain;

static void io_write(const emb_device_t *dev, uint32_t line, bool high)
{
    uint32_t bit = 1u << line;
    (void)dev;
    if ((io_open_drain & bit) != 0u) {
        REG32(high ? SIO_GPIO_OE_CLR : SIO_GPIO_OE_SET) = bit; /* its level stays 0 */
        return;
    }
    REG32(high ? SIO_GPIO_OUT_SET : SIO_GPIO_OUT_CLR) = bit;
}

static bool io_read(const emb_device_t *dev, uint32_t line)
{
    (void)dev;
    return ((REG32(SIO_GPIO_IN) >> line) & 1u) != 0u;
}

/* In the class's critical section: the pad's register holds its pulls with its other bits. */
static emb_status_t io_configure(const emb_device_t *dev, uint32_t line, uint32_t flags)
{
    const emb_rp2350_io_bank_config_t *cfg = (const emb_rp2350_io_bank_config_t *)dev->config;
    bool output = (flags & EMB_DRV_GPIO_OUTPUT) != 0u;
    uint32_t pad;
    if (line >= cfg->lines) {
        return EMB_EINVAL;
    }
    if ((flags & EMB_DRV_GPIO_FUNCTION) != 0u) {
        unsigned i;
        for (i = 0u; i < emb_board_pin_count; i++) { /* bounded by the board's pin table */
            if (emb_board_pins[i].gpio == line) {
                io_open_drain &= ~(1u << line);
                REG32(SIO_GPIO_OE_CLR) = 1u << line; /* the peripheral drives it, not the SIO */
                emb_rp2350_pins_apply(&emb_board_pins[i], 1u);
                return EMB_OK;
            }
        }
        return EMB_EINVAL; /* the board's table does not name it */
    }
    if (output && ((flags & EMB_DRV_GPIO_OPEN_DRAIN) != 0u)) {
        io_open_drain |= 1u << line;
        REG32(SIO_GPIO_OUT_CLR) = 1u << line; /* only ever driven low */
        io_write(dev, line, (flags & EMB_DRV_GPIO_INIT_HIGH) != 0u);
    } else if (output) { /* the level first, so the line never shows another */
        io_open_drain &= ~(1u << line);
        io_write(dev, line, (flags & EMB_DRV_GPIO_INIT_HIGH) != 0u);
        REG32(SIO_GPIO_OE_SET) = 1u << line;
    } else {
        io_open_drain &= ~(1u << line);
        REG32(SIO_GPIO_OE_CLR) = 1u << line;
    }
    /* the input stays enabled, so read() shows an output's level too */
    pad = REG32(PADS_GPIO(line)) & ~(PAD_ISO | PAD_OD | PAD_PUE | PAD_PDE);
    pad |= PAD_IE;
    if ((flags & EMB_DRV_GPIO_PULL_UP) != 0u) {
        pad |= PAD_PUE;
    }
    if ((flags & EMB_DRV_GPIO_PULL_DOWN) != 0u) {
        pad |= PAD_PDE;
    }
    REG32(PADS_GPIO(line)) = pad;
    REG32(IO_CTRL(cfg->base, line)) = FUNCSEL_SIO; /* then the SIO takes the pin */
    return EMB_OK;
}

const emb_device_api_t emb_rp2350_clocks_api = {clocks_init, NULL, NULL, NULL, NULL, 0u};
const emb_drv_gpio_api_t emb_rp2350_io_bank_api = {
    {io_bank_init, NULL, NULL, NULL, NULL,
     EMB_DRV_GPIO_CAP_OPEN_DRAIN | EMB_DRV_GPIO_CAP_PULL_DOWN},
    io_configure,
    io_write,
    io_read,
};
#else
/* ISO C: a translation unit declares something */
typedef int emb_rp2350_devices_off_t;
#endif
