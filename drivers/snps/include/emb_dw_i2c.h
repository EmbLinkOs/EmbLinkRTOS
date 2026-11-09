/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The Synopsys DesignWare I2C controller, DW_apb_i2c (the RP2350's I2C0 and I2C1, datasheet
 * §12.2), as a device of the I2C class (SPEC-017 §6). */
#ifndef EMB_DW_I2C_H
#define EMB_DW_I2C_H

#include <emb/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if CONFIG_EMB_DEVICE
#include <emb/device.h>
#include <emb/drv_gpio.h>
#endif
#if CONFIG_EMB_DRV_I2C
#include <emb/drv_i2c.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* A controller: its lines first (the class's recovery drives them), its registers, the clock
 * it counts SCL in, and its interrupt. */
typedef struct emb_dw_i2c_config {
    emb_drv_gpio_line_t scl;
    emb_drv_gpio_line_t sda;
    uintptr_t base;
    uint32_t clock_hz; /* ic_clk: clk_sys on the RP2350 */
    uint32_t irq;
} emb_dw_i2c_config_t;

#if CONFIG_EMB_DRV_I2C
/* A controller's RAM. A DW_apb_i2c is a controller or a target at a time: a transaction
 * while a listen waits runs in controller mode, and the target mode comes back after it. */
typedef struct emb_dw_i2c_data {
    emb_drv_bus_t bus;         /* the class's, first */
    emb_drv_i2c_req_t *xfer;   /* the transaction in progress */
    emb_drv_i2c_req_t *listen; /* the listen waiting or answering */
    const emb_device_t *dev;   /* for the interrupt handler */
    size_t seg;                /* the segment the next command comes from */
    size_t pos;                /* and the byte in it */
    size_t rseg;               /* the read segment the next byte received goes to */
    size_t rpos;
    uint32_t written;   /* write commands queued */
    uint32_t reads_out; /* read commands queued and not yet answered */
    uint32_t received;  /* bytes read back */
    bool issued;        /* every command is queued */
    bool stopping;      /* the transaction was canceled */
} emb_dw_i2c_data_t;

/* The I2C class's operations: standard, fast and fast-plus speeds, and target mode. Its
 * target cannot start refusing bytes mid-transaction (IC_SLV_DATA_NACK_ONLY is written only
 * while it is idle), so bytes beyond a listen's buffer are acknowledged and dropped. */
extern const emb_drv_i2c_api_t emb_dw_i2c_api;
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_DW_I2C_H */
