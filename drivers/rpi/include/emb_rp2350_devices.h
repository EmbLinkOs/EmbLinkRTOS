/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_rp2350_devices.h
 * @brief   The RP2350 drivers as devices (SPEC-016).
 *
 * @details
 * The operations and generated configurations the bindings name (drivers/bindings.yaml). Thin
 * adapters: the functions of emb_rp2350_drivers.h stay the low layer the board support uses before
 * the kernel.
 *
 * @author  Junior Deogracias
 * @date    2026-11-09 (created)
 */
#ifndef EMB_RP2350_DEVICES_H
#define EMB_RP2350_DEVICES_H

#include <emb/config.h>

#include <stdbool.h>
#include <stdint.h>

#if CONFIG_EMB_DEVICE
#include <emb/device.h>
#include <emb/drv_counter.h>
#include <emb/drv_entropy.h>
#include <emb/drv_flash.h>
#include <emb/drv_gpio.h>
#include <emb/drv_rtc.h>
#include <emb/drv_wdt.h>
#include <emb/sem.h>
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

/* TIMER0 or TIMER1 (datasheet §12.8): its registers, its tick generator in the TICKS block
 * (index), its rate, and its four alarms' interrupts (A129). */
typedef struct emb_rp2350_timer_config {
    uintptr_t base;
    uint32_t index;
    uint32_t rate_hz; /* counts per second: 1 MHz unless the board says */
    uint32_t irq0;
    uint32_t irq1;
    uint32_t irq2;
    uint32_t irq3;
} emb_rp2350_timer_config_t;

#if CONFIG_EMB_DEVICE && CONFIG_EMB_DRV_COUNTER
/* An alarm's interrupt: its timer and its number. */
typedef struct emb_rp2350_alarm {
    const emb_device_t *dev;
    uint8_t channel;
    uint8_t reserved_[3];
} emb_rp2350_alarm_t;

/* A timer's RAM: the class's, first; each alarm's whole count, the hardware comparing the low
 * 32 bits; each alarm interrupt's argument. */
typedef struct emb_rp2350_timer_data {
    emb_drv_counter_data_t counter;
    uint64_t target[EMB_DRV_COUNTER_CHANNELS_MAX];
    emb_rp2350_alarm_t alarm[EMB_DRV_COUNTER_CHANNELS_MAX];
} emb_rp2350_timer_data_t;
#endif

#if CONFIG_EMB_DEVICE && CONFIG_EMB_DRV_ENTROPY
/* The TRNG (datasheet §12.12): its registers, its interrupt, and the clocks between samples. */
typedef struct emb_rp2350_trng_config {
    uintptr_t base;
    uint32_t irq;
    uint32_t sample_count;
} emb_rp2350_trng_config_t;

/* Its RAM: the class's lock, first; the semaphore its interrupt gives; health failures seen. */
typedef struct emb_rp2350_trng_data {
    emb_drv_bus_t lock;
    emb_sem_storage_t ready_storage;
    emb_sem_t ready;
    const emb_device_t *dev;
    uint32_t failures;
} emb_rp2350_trng_data_t;
#endif

#if CONFIG_EMB_DEVICE
/* The always-on timer in POWMAN (datasheet §6.5): its registers and its alarm's interrupt. */
typedef struct emb_rp2350_aon_config {
    uintptr_t base;
    uint32_t irq;
} emb_rp2350_aon_config_t;

/* Its RAM: the class's, first, and the device for the alarm's interrupt. */
typedef struct emb_rp2350_aon_data {
    emb_drv_rtc_data_t rtc;
    const emb_device_t *dev;
} emb_rp2350_aon_data_t;
#endif

#if CONFIG_EMB_DEVICE
/* The board's QSPI flash, through the QMI and the boot ROM's flash routines (datasheet §5.5.8,
 * §12.14): the QMI's registers, and the geometry. */
typedef struct emb_rp2350_flash_config {
    uintptr_t base;
    emb_drv_flash_geometry_t geometry;
} emb_rp2350_flash_config_t;

/* Its RAM: the class's lock, first. */
typedef struct emb_rp2350_flash_data {
    emb_drv_bus_t lock;
} emb_rp2350_flash_data_t;
#endif

/* The watchdog (datasheet §12.9): its registers. */
typedef struct emb_rp2350_watchdog_config {
    uintptr_t base;
} emb_rp2350_watchdog_config_t;

/* Its RAM: whether it was started, and whether it caused this boot's reset. */
typedef struct emb_rp2350_watchdog_data {
    uint32_t load; /* the count it was started with: LOAD is write-only */
    bool started;
    bool caused;
} emb_rp2350_watchdog_data_t;

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

#if CONFIG_EMB_DRV_WDT
/* The watchdog class's operations (SPEC-017 §8): a microsecond tick from clk_ref, the reset
 * reaching all but the oscillators, and the reset's cause from its REASON register. */
extern const emb_drv_wdt_api_t emb_rp2350_watchdog_api;
#endif

#if CONFIG_EMB_DRV_COUNTER
/* The counter class's operations (SPEC-017 §11a): TIMER0 or TIMER1, 64-bit counts of their
 * tick, an alarm on each of the four alarms. */
extern const emb_drv_counter_api_t emb_rp2350_timer_api;
#endif

#if CONFIG_EMB_DRV_ENTROPY
/* The entropy class's operations (SPEC-017 §11): 192-bit blocks from the TRNG, each through its
 * health tests, a failed one discarded. */
extern const emb_drv_entropy_api_t emb_rp2350_trng_api;
#endif
#if CONFIG_EMB_DRV_RTC
/* The RTC class's operations (SPEC-017 §10): the always-on timer as milliseconds since the
 * epoch, on a 1 kHz tick from the crystal; no calendar, no backup registers. */
extern const emb_drv_rtc_api_t emb_rp2350_aon_api;
#endif
#if CONFIG_EMB_DRV_FLASH
/* The flash class's operations (SPEC-017 §9): the boot ROM's erase and program, run from RAM
 * with interrupts off while XIP is off, the clock catching up after (A122). */
extern const emb_drv_flash_api_t emb_rp2350_flash_api;
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
