/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    rp2350_flash.c
 * @brief   The RP2350's QSPI flash as a flash class device (SPEC-017 §9, A122), through the boot
 *          ROM's routines (datasheet §5.5.8; the sequence pico-sdk 2.1.1's hardware_flash uses).
 *
 * @details
 * Connect the flash, leave XIP, erase or program, flush the XIP cache, then run the XIP setup
 * function the boot ROM left in boot RAM, copied to RAM at initialization. While XIP is off nothing
 * may be fetched from flash: not this code, not the bytes being programmed, not an interrupt's
 * vector. So the routine runs from RAM (.ramfunc) with PRIMASK set, a page at a time from a RAM
 * copy, and the kernel's clock catches up after each page or sector.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/irq.h>

#include <emb_cortex_m.h>
#include <emb_rp2350_devices.h>
#include <hw_config.h>

#if CONFIG_EMB_DRV_FLASH
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* The boot ROM's table lookup (datasheet §5.4): its address at 0x16, functions by two-letter
 * code, those for the Secure Arm state. */
#define ROM_TABLE_LOOKUP     0x16u
#define RT_FLAG_FUNC_ARM_SEC 0x0004u
#define ROM_CODE(a, b)       ((uint32_t)(a) | ((uint32_t)(b) << 8))

#define QMI_M0_TIMING   0x0Cu
#define QMI_M1_TIMING   0x20u /* CS1's window: PSRAM where a board has it */
#define QMI_M1_RFMT     0x24u
#define QMI_M1_RCMD     0x28u
#define XIP_SETUP_WORDS 64u    /* the ROM's XIP setup function: boot RAM's first 256 bytes */
#define PAGE            256u   /* the program unit */
#define BLOCK           65536u /* the erase command's block, where a range covers one */
#define BLOCK_ERASE_CMD 0xD8u  /* 64 KiB block erase; the ROM erases 4 KiB sectors otherwise */
#define OP_ERASE        0u
#define OP_PROGRAM      1u

typedef uintptr_t (*rp2350_rom_lookup_t)(uint32_t code, uint32_t mask);
typedef void (*rp2350_rom_void_t)(void);
typedef void (*rp2350_rom_erase_t)(uint32_t offset, size_t count, uint32_t block, uint8_t cmd);
typedef void (*rp2350_rom_program_t)(uint32_t offset, const uint8_t *data, size_t count);

/* The ROM's routines, the XIP setup copy and the page being programmed: all in RAM. */
typedef struct rp2350_flash_rom {
    rp2350_rom_void_t connect;
    rp2350_rom_void_t exit_xip;
    rp2350_rom_void_t flush;
    rp2350_rom_erase_t erase;
    rp2350_rom_program_t program;
} rp2350_flash_rom_t;

static rp2350_flash_rom_t rp2350_flash_rom;
static uint32_t rp2350_flash_xip_setup[XIP_SETUP_WORDS];
static uint8_t rp2350_flash_page[PAGE];

static const emb_rp2350_flash_config_t *rp2350_flash_config(const emb_device_t *dev)
{
    return (const emb_rp2350_flash_config_t *)dev->config;
}

static uintptr_t rp2350_flash_lookup(uint32_t code)
{
    const volatile uint16_t *volatile table =
        (const volatile uint16_t *)(uintptr_t)ROM_TABLE_LOOKUP;
    /* MISRA Dev CS-12: rule 11.1, the ROM's own function table */
    rp2350_rom_lookup_t lookup = (rp2350_rom_lookup_t)(uintptr_t)*table;
    return lookup(code, RT_FLAG_FUNC_ARM_SEC);
}

static emb_status_t rp2350_flash_init(const emb_device_t *dev)
{
    const volatile uint32_t *setup = (const volatile uint32_t *)(uintptr_t)EMB_HW_BOOTRAM_BASE;
    uintptr_t fn[5];
    uint32_t i;
    fn[0] = rp2350_flash_lookup(ROM_CODE('I', 'F'));
    fn[1] = rp2350_flash_lookup(ROM_CODE('E', 'X'));
    fn[2] = rp2350_flash_lookup(ROM_CODE('F', 'C'));
    fn[3] = rp2350_flash_lookup(ROM_CODE('R', 'E'));
    fn[4] = rp2350_flash_lookup(ROM_CODE('R', 'P'));
    for (i = 0u; i < 5u; i++) { /* bounded: five routines */
        if (fn[i] == 0u) {
            return EMB_ENOTSUP;
        }
    }
    /* MISRA Dev CS-12: rule 11.1, the ROM's routines */
    rp2350_flash_rom.connect = (rp2350_rom_void_t)fn[0];
    rp2350_flash_rom.exit_xip = (rp2350_rom_void_t)fn[1];
    rp2350_flash_rom.flush = (rp2350_rom_void_t)fn[2];
    rp2350_flash_rom.erase = (rp2350_rom_erase_t)fn[3];
    rp2350_flash_rom.program = (rp2350_rom_program_t)fn[4];
    for (i = 0u; i < XIP_SETUP_WORDS; i++) { /* bounded: 64 words */
        rp2350_flash_xip_setup[i] = setup[i];
    }
    return emb_drv_flash_lock_init(dev);
}

/* With XIP off: from RAM, interrupts off, calling only the ROM and the RAM copy of the XIP setup
 * function. The QMI's flash timing, which the clock driver set for clk_sys, is put back after
 * it, and CS1's window, which flash_exit_xip() resets, as pico-sdk does for a board with nothing
 * on CS1 (the Pico 2 W). */
EMB_SECTION(".ramfunc")
EMB_NOINLINE static void rp2350_flash_ram(uint32_t op, uint32_t offset, uint32_t count,
                                          uintptr_t qmi)
{
    uint32_t primask;
    uint32_t timing = REG32(qmi + QMI_M0_TIMING);
    uint32_t m1_timing = REG32(qmi + QMI_M1_TIMING);
    uint32_t m1_rfmt = REG32(qmi + QMI_M1_RFMT);
    uint32_t m1_rcmd = REG32(qmi + QMI_M1_RCMD);
    primask = emb_cm_primask_set();
    rp2350_flash_rom.connect();
    rp2350_flash_rom.exit_xip();
    if (op == OP_ERASE) {
        rp2350_flash_rom.erase(offset, count, BLOCK, BLOCK_ERASE_CMD);
    } else {
        rp2350_flash_rom.program(offset, rp2350_flash_page, count);
    }
    rp2350_flash_rom.flush();
    /* MISRA Dev CS-12: rule 11.1, the ROM's code copied to RAM, entered in Thumb state */
    ((rp2350_rom_void_t)((uintptr_t)rp2350_flash_xip_setup + 1u))();
    REG32(qmi + QMI_M0_TIMING) = timing;
    REG32(qmi + QMI_M1_TIMING) = m1_timing;
    REG32(qmi + QMI_M1_RFMT) = m1_rfmt;
    REG32(qmi + QMI_M1_RCMD) = m1_rcmd;
    emb_cm_primask_restore(primask);
}

/* One ROM operation, the kernel's clock caught up after it. */
static void rp2350_flash_run(const emb_device_t *dev, uint32_t op, uint32_t offset, uint32_t count)
{
    emb_cm_stall_t stall;
    emb_irq_key_t key = emb_irq_lock();
    emb_cm_stall_begin(&stall);
    rp2350_flash_ram(op, offset, count, rp2350_flash_config(dev)->base);
    emb_cm_stall_end(&stall);
    emb_irq_unlock(key);
}

static const emb_drv_flash_geometry_t *rp2350_flash_geometry(const emb_device_t *dev)
{
    return &rp2350_flash_config(dev)->geometry;
}

static emb_status_t rp2350_flash_read(const emb_device_t *dev, uint32_t offset, void *buf,
                                      size_t len)
{
    const volatile uint8_t *src =
        (const volatile uint8_t *)(rp2350_flash_config(dev)->geometry.base + offset);
    uint8_t *dst = (uint8_t *)buf;
    size_t i;
    for (i = 0u; i < len; i++) { /* bounded: len */
        dst[i] = src[i];
    }
    return EMB_OK;
}

static emb_status_t rp2350_flash_erase(const emb_device_t *dev, uint32_t offset, uint32_t len)
{
    rp2350_flash_run(dev, OP_ERASE, offset, len); /* a 4 KiB sector: the class goes by sector */
    return EMB_OK;
}

/* A page at a time, from a RAM copy: the caller's bytes may be in flash. */
static emb_status_t rp2350_flash_program(const emb_device_t *dev, uint32_t offset, const void *buf,
                                         size_t len)
{
    const uint8_t *src = (const uint8_t *)buf;
    size_t done;
    for (done = 0u; done < len; done += PAGE) { /* bounded: len, a multiple of the page */
        size_t i;
        for (i = 0u; i < PAGE; i++) { /* bounded: a page */
            rp2350_flash_page[i] = src[done + i];
        }
        rp2350_flash_run(dev, OP_PROGRAM, offset + (uint32_t)done, PAGE);
    }
    return EMB_OK;
}

const emb_drv_flash_api_t emb_rp2350_flash_api = {
    {rp2350_flash_init, NULL, NULL, NULL, NULL, 0u},
    rp2350_flash_geometry,
    rp2350_flash_read,
    rp2350_flash_program,
    rp2350_flash_erase,
};
#else
/* ISO C: a translation unit declares something */
typedef int emb_rp2350_flash_off_t;
#endif
