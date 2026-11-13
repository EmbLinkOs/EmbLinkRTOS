/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    stm32_flash.c
 * @brief   The STM32F4's flash interface (RM0390 Rev 6 §3) as a flash class device (SPEC-017 §9,
 *          A122).
 *
 * @details
 * Its one bank stalls the CPU's every fetch and read from flash while it erases or programs, so
 * each operation runs with interrupts held off and the kernel's clock catches up after it:
 * interrupts would only wait on the stall too. Erases are a sector at a time and programs a word at
 * a time where aligned, at the 32-bit parallelism 2.7 to 3.6 V allows; then the flash caches are
 * reset, since they may hold what was there before.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/irq.h>

#include <emb_cortex_m.h>
#include <emb_stm32_devices.h>

#if CONFIG_EMB_DRV_FLASH
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define FLASH_ACR  0x00u
#define FLASH_KEYR 0x04u
#define FLASH_SR   0x0Cu
#define FLASH_CR   0x10u

#define ACR_ICEN  ((uint32_t)1u << 9)
#define ACR_DCEN  ((uint32_t)1u << 10)
#define ACR_ICRST ((uint32_t)1u << 11)
#define ACR_DCRST ((uint32_t)1u << 12)

#define SR_EOP    ((uint32_t)1u << 0)
#define SR_OPERR  ((uint32_t)1u << 1)
#define SR_WRPERR ((uint32_t)1u << 4)
#define SR_PGAERR ((uint32_t)1u << 5)
#define SR_PGPERR ((uint32_t)1u << 6)
#define SR_PGSERR ((uint32_t)1u << 7)
#define SR_RDERR  ((uint32_t)1u << 8)
#define SR_ERRORS (SR_OPERR | SR_WRPERR | SR_PGAERR | SR_PGPERR | SR_PGSERR | SR_RDERR)
#define SR_BSY    ((uint32_t)1u << 16)

#define CR_PG        ((uint32_t)1u << 0)
#define CR_SER       ((uint32_t)1u << 1)
#define CR_SNB       3u /* the sector, bits 3 to 6 */
#define CR_PSIZE_X8  ((uint32_t)0u << 8)
#define CR_PSIZE_X32 ((uint32_t)2u << 8)
#define CR_STRT      ((uint32_t)1u << 16)
#define CR_LOCK      ((uint32_t)1u << 31)

/* RM0390 §3.7.1, FLASH_KEYR's KEY1 and KEY2, in decimal */
#define KEY1 1164378403u
#define KEY2 3455027627u

/* bounded: a 128 KiB sector erases in at most 4 s, and the CPU stalls on its flash meanwhile,
 * so each pass of the loop waits on the operation itself */
#define BUSY_POLL 100000000u

static const emb_stm32_flash_config_t *stm32_flash_config(const emb_device_t *dev)
{
    return (const emb_stm32_flash_config_t *)dev->config;
}

static emb_status_t stm32_flash_init(const emb_device_t *dev)
{
    return emb_drv_flash_lock_init(dev);
}

static const emb_drv_flash_geometry_t *stm32_flash_geometry(const emb_device_t *dev)
{
    return &stm32_flash_config(dev)->geometry;
}

static emb_status_t stm32_flash_read(const emb_device_t *dev, uint32_t offset, void *buf,
                                     size_t len)
{
    const volatile uint8_t *src =
        (const volatile uint8_t *)(stm32_flash_config(dev)->geometry.base + offset);
    uint8_t *dst = (uint8_t *)buf;
    size_t i;
    for (i = 0u; i < len; i++) { /* bounded: len */
        dst[i] = src[i];
    }
    return EMB_OK;
}

/* Waits until the interface is idle, then reads and clears what it reported. */
static emb_status_t stm32_flash_done(uintptr_t base)
{
    uint32_t n;
    uint32_t sr;
    for (n = 0u; n < BUSY_POLL; n++) {
        if ((REG32(base + FLASH_SR) & SR_BSY) == 0u) {
            break;
        }
    }
    sr = REG32(base + FLASH_SR);
    REG32(base + FLASH_SR) = SR_ERRORS | SR_EOP;
    if ((sr & SR_BSY) != 0u) {
        return EMB_EIO;
    }
    if ((sr & SR_WRPERR) != 0u) {
        return EMB_EWRPROT;
    }
    return ((sr & SR_ERRORS) != 0u) ? EMB_EIO : EMB_OK;
}

/* Unlocked, with interrupts held off and the stall measured; the caller's steps run here. */
typedef struct stm32_flash_session {
    emb_irq_key_t key;
    emb_cm_stall_t stall;
    uint32_t acr;
} stm32_flash_session_t;

static void stm32_flash_begin(uintptr_t base, stm32_flash_session_t *s)
{
    s->key = emb_irq_lock();
    emb_cm_stall_begin(&s->stall);
    s->acr = REG32(base + FLASH_ACR);
    if ((REG32(base + FLASH_CR) & CR_LOCK) != 0u) {
        REG32(base + FLASH_KEYR) = KEY1;
        REG32(base + FLASH_KEYR) = KEY2;
    }
    REG32(base + FLASH_SR) = SR_ERRORS | SR_EOP; /* a previous operation's flags */
}

/* Locked again; the caches reset, since they may hold what was there before (§3.5.2). */
static void stm32_flash_end(uintptr_t base, stm32_flash_session_t *s)
{
    REG32(base + FLASH_CR) = CR_LOCK;
    REG32(base + FLASH_ACR) = s->acr & ~(ACR_ICEN | ACR_DCEN);
    REG32(base + FLASH_ACR) = (s->acr & ~(ACR_ICEN | ACR_DCEN)) | ACR_ICRST | ACR_DCRST;
    REG32(base + FLASH_ACR) = s->acr;
    emb_cm_stall_end(&s->stall);
    emb_irq_unlock(s->key);
}

/* The sector number of the sector starting at @offset (the class checked it starts one). */
static uint32_t stm32_flash_snb(const emb_drv_flash_geometry_t *g, uint32_t offset)
{
    uint32_t at = 0u;
    uint32_t i;
    for (i = 0u; i < g->sector_count; i++) { /* bounded: the sectors */
        if (at == offset) {
            break;
        }
        at += g->sectors[i];
    }
    return i;
}

static emb_status_t stm32_flash_erase(const emb_device_t *dev, uint32_t offset, uint32_t len)
{
    const emb_stm32_flash_config_t *cfg = stm32_flash_config(dev);
    stm32_flash_session_t s;
    emb_status_t st;
    (void)len; /* one sector: the class erases sector by sector */
    stm32_flash_begin(cfg->base, &s);
    REG32(cfg->base + FLASH_CR) = CR_SER | (stm32_flash_snb(&cfg->geometry, offset) << CR_SNB) |
                                  CR_PSIZE_X32;
    REG32(cfg->base + FLASH_CR) |= CR_STRT;
    st = stm32_flash_done(cfg->base);
    stm32_flash_end(cfg->base, &s);
    return st;
}

static emb_status_t stm32_flash_program(const emb_device_t *dev, uint32_t offset, const void *buf,
                                        size_t len)
{
    const emb_stm32_flash_config_t *cfg = stm32_flash_config(dev);
    const uint8_t *src = (const uint8_t *)buf;
    uintptr_t at = cfg->geometry.base + offset;
    size_t done = 0u;
    stm32_flash_session_t s;
    emb_status_t st = EMB_OK;
    stm32_flash_begin(cfg->base, &s);
    while ((done < len) && (st == EMB_OK)) { /* bounded: len */
        if ((((at + done) & 3u) == 0u) && ((len - done) >= 4u)) {
            uint32_t word = (uint32_t)src[done] | ((uint32_t)src[done + 1u] << 8) |
                            ((uint32_t)src[done + 2u] << 16) | ((uint32_t)src[done + 3u] << 24);
            REG32(cfg->base + FLASH_CR) = CR_PG | CR_PSIZE_X32;
            REG32(at + done) = word;
            done += 4u;
        } else {
            REG32(cfg->base + FLASH_CR) = CR_PG | CR_PSIZE_X8;
            *(volatile uint8_t *)(at + done) = src[done];
            done += 1u;
        }
        st = stm32_flash_done(cfg->base);
    }
    stm32_flash_end(cfg->base, &s);
    return st;
}

const emb_drv_flash_api_t emb_stm32_flash_api = {
    {stm32_flash_init, NULL, NULL, NULL, NULL, 0u},
    stm32_flash_geometry,
    stm32_flash_read,
    stm32_flash_program,
    stm32_flash_erase,
};
#else
/* ISO C: a translation unit declares something */
typedef int emb_stm32_flash_off_t;
#endif
