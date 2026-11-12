/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Arm PrimeCell SSP PL022 (Arm DDI 0194H) as an SPI controller of the SPI class (SPEC-017
 * §5), the RP2350's SPI and the one QEMU's MPS2 machines model. Motorola frames, controller
 * mode, eight frames in flight through its FIFOs.
 */
#ifndef EMB_PL022_H
#define EMB_PL022_H

#include <emb/config.h>
#include <emb/drv_dma.h>

#include <stdbool.h>
#include <stdint.h>
#if CONFIG_EMB_DRV_SPI
#include <emb/drv_spi.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The generated configuration of a PL022 device: its DMA channels where the board gives it
 * some (dma: [tx, rx]), each NULL's otherwise. */
typedef struct emb_pl022_config {
    uintptr_t base;
    uint32_t clock_hz; /* SSPCLK */
    uint32_t irq;
    emb_drv_dma_chan_t dma_tx;
    emb_drv_dma_chan_t dma_rx;
} emb_pl022_config_t;

#if CONFIG_EMB_DRV_SPI
/* A PL022 device's RAM. */
typedef struct emb_pl022_data {
    emb_drv_bus_t bus;       /* the class's, first */
    emb_drv_spi_req_t *xfer; /* the transfer in progress */
    const emb_device_t *dev; /* for the interrupt handler */
    size_t sent;             /* frames written to the transmit FIFO */
    bool stopping;           /* canceled: no more frames out, ends when those in flight are back */
    bool dma;                /* the transfer runs by DMA: the board gave the device channels */
    uint8_t fill;            /* the ones a transfer with no transmit buffer sends, DMA's source */
    uint8_t sink;            /* where a transfer with no receive buffer's frames go */
} emb_pl022_data_t;

/* The SPI class's operations. Initialization connects the interrupt; each transfer sets the
 * controller up for its target, and the receive FIFO's level and timeout interrupts carry
 * it, the transmit FIFO refilled as frames come back. */
extern const emb_device_api_t emb_pl022_api;
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_PL022_H */
