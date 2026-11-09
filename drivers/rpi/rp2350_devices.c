/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The RP2350 drivers as devices (SPEC-016): thin adapters over emb_rp2350_drivers.h. */
#include <emb_rp2350_devices.h>
#include <emb_rp2350_drivers.h>

#if CONFIG_EMB_DEVICE
#define REG32(a)         (*(volatile uint32_t *)(uintptr_t)(a))
#define CLK_REF_SELECTED 0x38u
#define CLK_REF_SRC_XOSC 2u

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

const emb_device_api_t emb_rp2350_clocks_api = {clocks_init, NULL, NULL, NULL, NULL};
const emb_device_api_t emb_rp2350_io_bank_api = {io_bank_init, NULL, NULL, NULL, NULL};
#else
/* ISO C: a translation unit declares something */
typedef int emb_rp2350_devices_off_t;
#endif
