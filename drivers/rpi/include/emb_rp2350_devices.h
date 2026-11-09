/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The RP2350 drivers as devices (SPEC-016): the operations and generated configurations the
 * bindings name (drivers/bindings.yaml). Thin adapters: the functions of
 * emb_rp2350_drivers.h stay the low layer the board support uses before the kernel.
 */
#ifndef EMB_RP2350_DEVICES_H
#define EMB_RP2350_DEVICES_H

#include <emb/config.h>

#include <stdint.h>

#if CONFIG_EMB_DEVICE
#include <emb/device.h>
#include <emb/drv_gpio.h>
#endif
#if CONFIG_EMB_DRV_DMA
#include <emb/drv_dma.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The clock generators: the clock tree the board support brought up. */
typedef struct emb_rp2350_clocks_config {
    uintptr_t base;
} emb_rp2350_clocks_config_t;

/* GPIO bank 0: its function select (IO_BANK0) and its pads (PADS_BANK0); its lines, the
 * GPIO class's (SPEC-017 §5a), are driven and read through the SIO. */
typedef struct emb_rp2350_io_bank_config {
    uintptr_t base;
    uint32_t lines; /* GPIO0 to GPIO<lines - 1>: 30 on the RP2350A */
} emb_rp2350_io_bank_config_t;

/* The DMA controller (datasheet §12.6): its registers and the interrupt its channels end on. */
typedef struct emb_rp2350_dma_config {
    uintptr_t base;
    uint32_t irq; /* DMA_IRQ_0 */
} emb_rp2350_dma_config_t;

#if CONFIG_EMB_DRV_DMA
#define EMB_RP2350_DMA_CHANNELS 16u
/* A channel's job: its end's callback, and the count it started with. */
typedef struct emb_rp2350_dma_channel {
    emb_drv_dma_done_t done;
    void *arg;
    uint32_t count;
    uint32_t start; /* the advancing side's first address: how far it got is what moved */
    uint8_t width;
    uint8_t side; /* which side advances: 0 none, 1 the read, 2 the write */
    uint8_t running;
} emb_rp2350_dma_channel_t;

/* The DMA controller's RAM. */
typedef struct emb_rp2350_dma_data {
    const emb_device_t *dev;
    emb_rp2350_dma_channel_t channels[EMB_RP2350_DMA_CHANNELS];
} emb_rp2350_dma_data_t;

/* The DMA class's operations (SPEC-017 §7): a job per channel, paced by its DREQ, ending on
 * DMA_IRQ_0. */
extern const emb_drv_dma_api_t emb_rp2350_dma_api;
#endif

#if CONFIG_EMB_DEVICE
/* READY when clk_ref runs from the crystal. */
extern const emb_device_api_t emb_rp2350_clocks_api;
/* Takes the bank and its pads out of reset; READY once they are. Open drain is the output
 * enable's: the pads have none of their own. */
extern const emb_drv_gpio_api_t emb_rp2350_io_bank_api;
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_RP2350_DEVICES_H */
