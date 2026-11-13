/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    rp2350_trng.c
 * @brief   The RP2350's true random number generator (datasheet §12.12; registers as pico-sdk
 *          2.1.1's hardware/regs/trng.h names them) as an entropy class device (SPEC-017 §11,
 *          A124).
 *
 * @details
 * A ring oscillator sampled into 192-bit blocks, each passing the block's own health tests (the von
 * Neumann corrector, the continuous test, the autocorrelation test), which pico-sdk's pico_rand
 * bypasses and this driver keeps. A block's end, or a test's failure, raises the TRNG's interrupt,
 * which wakes the read. A failed block is discarded: after the von Neumann or the continuous test
 * the source starts again; after the autocorrelation test, which only a reset of the block clears,
 * the TRNG is reset and set up again.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/irq.h>
#include <emb/sem.h>

#include <emb_rp2350_devices.h>
#include <emb_rp2350_drivers.h>

#if CONFIG_EMB_DRV_ENTROPY
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define RNG_IMR            0x100u
#define RNG_ISR            0x104u
#define RNG_ICR            0x108u
#define TRNG_CONFIG        0x10Cu
#define EHR_DATA0          0x114u /* to EHR_DATA5, a word apart */
#define RND_SOURCE_ENABLE  0x12Cu
#define SAMPLE_CNT1        0x130u
#define TRNG_DEBUG_CONTROL 0x138u
#define TRNG_SW_RESET      0x140u
#define TRNG_BUSY          0x1B8u

#define ISR_EHR_VALID    (1u << 0)
#define ISR_AUTOCORR_ERR (1u << 1)
#define ISR_CRNGT_ERR    (1u << 2)
#define ISR_VN_ERR       (1u << 3)
#define ISR_ALL          0xFu
#define ISR_RESTARTS     (ISR_CRNGT_ERR | ISR_VN_ERR)
#define EHR_WORDS        6u
#define BLOCK            24u    /* 192 bits */
#define BUSY_POLL        10000u /* bounded: a reset settles in a few clocks */

static const emb_rp2350_trng_config_t *rp2350_trng_config(const emb_device_t *dev)
{
    return (const emb_rp2350_trng_config_t *)dev->config;
}

static emb_rp2350_trng_data_t *rp2350_trng_data(const emb_device_t *dev)
{
    return (emb_rp2350_trng_data_t *)dev->data;
}

/* Set up, every health test on, collecting. */
static void rp2350_trng_setup(const emb_rp2350_trng_config_t *cfg)
{
    REG32(cfg->base + RND_SOURCE_ENABLE) = 0u;
    REG32(cfg->base + RNG_IMR) = ISR_ALL; /* masked until a read waits */
    REG32(cfg->base + TRNG_CONFIG) = 0u;  /* the shortest inverter chain */
    REG32(cfg->base + SAMPLE_CNT1) = cfg->sample_count;
    REG32(cfg->base + TRNG_DEBUG_CONTROL) = 0u; /* no test bypassed */
    REG32(cfg->base + RNG_ICR) = ISR_ALL;
    REG32(cfg->base + RND_SOURCE_ENABLE) = 1u;
}

/* A reset of the block: the autocorrelation test's failure clears no other way. */
static void rp2350_trng_reset(const emb_rp2350_trng_config_t *cfg)
{
    uint32_t n;
    REG32(cfg->base + TRNG_SW_RESET) = 1u;
    for (n = 0u; n < BUSY_POLL; n++) {
        if (REG32(cfg->base + TRNG_BUSY) == 0u) {
            break;
        }
    }
    rp2350_trng_setup(cfg);
}

static void rp2350_trng_isr(void *arg)
{
    emb_rp2350_trng_data_t *d = (emb_rp2350_trng_data_t *)arg;
    REG32(rp2350_trng_config(d->dev)->base + RNG_IMR) = ISR_ALL; /* the read takes it from here */
    (void)emb_sem_give(d->ready);
}

static emb_status_t rp2350_trng_init(const emb_device_t *dev)
{
    const emb_rp2350_trng_config_t *cfg = rp2350_trng_config(dev);
    emb_rp2350_trng_data_t *d = rp2350_trng_data(dev);
    emb_sem_attr_t attr;
    emb_status_t st;
    d->dev = dev;
    if (emb_rp2350_unreset(EMB_RP2350_RESET_TRNG) != 0) {
        return EMB_EIO;
    }
    st = emb_drv_entropy_lock_init(dev);
    if (st != EMB_OK) {
        return st;
    }
    emb_sem_attr_default(&attr);
    attr.name = dev->name;
    attr.initial = 0u;
    attr.max = 1u;
    attr.flags = EMB_SEM_SATURATE; /* the interrupt's gives never pile up */
    st = emb_sem_init(&d->ready_storage, &attr, &d->ready);
    if (st != EMB_OK) {
        return st;
    }
    rp2350_trng_reset(cfg);
    if ((emb_irq_connect((emb_irq_t)cfg->irq, rp2350_trng_isr, d) != EMB_OK) ||
        (emb_irq_enable((emb_irq_t)cfg->irq) != EMB_OK)) {
        return EMB_EINVAL;
    }
    return EMB_OK;
}

static emb_status_t rp2350_trng_block(const emb_device_t *dev, uint8_t *buf, emb_timeout_t timeout)
{
    const emb_rp2350_trng_config_t *cfg = rp2350_trng_config(dev);
    emb_rp2350_trng_data_t *d = rp2350_trng_data(dev);
    uint32_t isr = REG32(cfg->base + RNG_ISR);
    uint32_t i;
    if ((isr & ISR_ALL) == 0u) {
        emb_status_t st;
        (void)emb_sem_take(d->ready, EMB_NO_WAIT); /* a give an earlier read timed out on */
        REG32(cfg->base + RNG_IMR) = 0u;           /* its end, or a failure, wakes this */
        st = emb_sem_take(d->ready, timeout);
        REG32(cfg->base + RNG_IMR) = ISR_ALL;
        if (st != EMB_OK) {
            return st; /* EMB_ETIMEDOUT */
        }
        isr = REG32(cfg->base + RNG_ISR);
    }
    if ((isr & ISR_AUTOCORR_ERR) != 0u) {
        d->failures++;
        rp2350_trng_reset(cfg);
        return EMB_EHEALTH;
    }
    if ((isr & ISR_RESTARTS) != 0u) {
        d->failures++;
        REG32(cfg->base + RND_SOURCE_ENABLE) = 0u; /* the block discarded */
        REG32(cfg->base + RNG_ICR) = ISR_ALL;
        REG32(cfg->base + RND_SOURCE_ENABLE) = 1u;
        return EMB_EHEALTH;
    }
    if ((isr & ISR_EHR_VALID) == 0u) {
        return EMB_EIO; /* woken with nothing to show */
    }
    for (i = 0u; i < EHR_WORDS; i++) { /* the last read starts the next block */
        uint32_t w = REG32(cfg->base + EHR_DATA0 + (4u * i));
        buf[(4u * i) + 0u] = (uint8_t)w;
        buf[(4u * i) + 1u] = (uint8_t)(w >> 8);
        buf[(4u * i) + 2u] = (uint8_t)(w >> 16);
        buf[(4u * i) + 3u] = (uint8_t)(w >> 24);
    }
    REG32(cfg->base + RNG_ICR) = ISR_EHR_VALID;
    return EMB_OK;
}

static emb_status_t rp2350_trng_suspend(const emb_device_t *dev)
{
    REG32(rp2350_trng_config(dev)->base + RND_SOURCE_ENABLE) = 0u;
    return EMB_OK;
}

static emb_status_t rp2350_trng_resume(const emb_device_t *dev)
{
    rp2350_trng_reset(rp2350_trng_config(dev)); /* a fresh start: the next block is new */
    return EMB_OK;
}

const emb_drv_entropy_api_t emb_rp2350_trng_api = {
    {rp2350_trng_init, NULL, NULL, rp2350_trng_suspend, rp2350_trng_resume,
     EMB_DRV_ENTROPY_CAP_HEALTH_TEST},
    rp2350_trng_block,
    BLOCK,
    {0u, 0u, 0u},
};
#else
/* ISO C: a translation unit declares something */
typedef int emb_rp2350_trng_off_t;
#endif
