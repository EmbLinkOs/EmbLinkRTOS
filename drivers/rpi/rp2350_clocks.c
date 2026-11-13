/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    rp2350_clocks.c
 * @brief   RP2350 clocks (datasheet §8).
 *
 * @details
 * XOSC, PLL_SYS and the clock generators, and the QSPI flash interface's divider, which must follow
 * clk_sys.
 *
 * @author  Junior Deogracias
 * @date    2026-11-09 (created)
 */
#include <emb_rp2350_drivers.h>
#include <hw_config.h>

#define REG(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
/* every peripheral has atomic aliases: write ones to set (+0x2000) or clear (+0x3000) bits */
#define SET_ALIAS 0x2000u
#define CLR_ALIAS 0x3000u

#define RESETS_RESET      (EMB_HW_RESETS_BASE + 0x00u)
#define RESETS_RESET_DONE (EMB_HW_RESETS_BASE + 0x08u)

#define XOSC_CTRL       (EMB_HW_XOSC_BASE + 0x00u)
#define XOSC_STATUS     (EMB_HW_XOSC_BASE + 0x04u)
#define XOSC_STARTUP    (EMB_HW_XOSC_BASE + 0x0Cu)
#define XOSC_RANGE_1_15 0xAA0u
#define XOSC_ENABLE     (0xFABu << 12)
#define XOSC_STABLE     (1u << 31)
/* pico-sdk's delay, 1 ms of crystal cycles in units of 256, times a margin of 64 */
#define XOSC_DELAY ((((EMB_HW_CPU_HZ / 1000u) + 128u) / 256u) * 64u)

#define CLK_REF_CTRL     (EMB_HW_CLOCKS_BASE + 0x30u)
#define CLK_REF_DIV      (EMB_HW_CLOCKS_BASE + 0x34u)
#define CLK_REF_SELECTED (EMB_HW_CLOCKS_BASE + 0x38u)
#define CLK_SYS_CTRL     (EMB_HW_CLOCKS_BASE + 0x3Cu)
#define CLK_SYS_DIV      (EMB_HW_CLOCKS_BASE + 0x40u)
#define CLK_SYS_SELECTED (EMB_HW_CLOCKS_BASE + 0x44u)
#define CLK_PERI_CTRL    (EMB_HW_CLOCKS_BASE + 0x48u)
#define CLK_PERI_DIV     (EMB_HW_CLOCKS_BASE + 0x4Cu)
#define CLK_REF_SRC_XOSC 2u
#define CLK_SYS_SRC_REF  0u
#define CLK_SYS_SRC_AUX  1u         /* AUXSRC 0: clksrc_pll_sys */
#define CLK_PERI_ENABLE  (1u << 11) /* AUXSRC 0: clk_sys */
#define CLK_DIV_ONE      (1u << 16) /* INT 1, FRAC 0 */

#define POLL_LIMIT 1000000u

/* ---- PLL_SYS (RP2350 datasheet §8.6) ----------------------------------------------------- */
#define PLL_CS            (EMB_HW_PLL_SYS_BASE + 0x00u)
#define PLL_PWR           (EMB_HW_PLL_SYS_BASE + 0x04u)
#define PLL_FBDIV_INT     (EMB_HW_PLL_SYS_BASE + 0x08u)
#define PLL_PRIM          (EMB_HW_PLL_SYS_BASE + 0x0Cu)
#define PLL_CS_LOCK       (1u << 31)
#define PLL_PWR_PD        (1u << 0)
#define PLL_PWR_POSTDIVPD (1u << 3)
#define PLL_PWR_VCOPD     (1u << 5)
#define PLL_PRIM_POSTDIV1 16u
#define PLL_PRIM_POSTDIV2 12u

/* ---- the QSPI flash interface (QMI, datasheet §12.14) ------------------------------------ */
#define QMI_M0_TIMING     (EMB_HW_QMI_BASE + 0x0Cu)
#define QMI_TIMING_CLKDIV 0xFFu
/* The boot ROM leaves window 0 on the flash's EBh quad read, CLKDIV 3 and RXDELAY 2 (read back
 * on a Pico 2 W, 2026-11-07): 50 MHz of SCK at a 150 MHz clk_sys, the speed it prepares for. */
#define QMI_SCK_MAX_HZ 50000000u

static int poll_set(uint32_t addr, uint32_t mask, uint32_t want)
{
    for (uint32_t i = 0u; i < POLL_LIMIT; i++) { /* bounded */
        if ((REG(addr) & mask) == want) {
            return 0;
        }
    }
    return -1;
}

#ifdef EMB_HW_PLL_N
/* SCK is clk_sys / CLKDIV: raise CLKDIV, never lower it, so that SCK stays at or under
 * QMI_SCK_MAX_HZ once clk_sys runs at EMB_HW_CPU_HZ. With the boot ROM's 3 at 150 MHz it is
 * already there. Changed only while clk_sys is still slow, so a slower SCK is always safe. */
static void flash_for_clk_sys(void)
{
    uint32_t need = (EMB_HW_CPU_HZ + QMI_SCK_MAX_HZ - 1u) / QMI_SCK_MAX_HZ;
    uint32_t timing = REG(QMI_M0_TIMING);
    if ((timing & QMI_TIMING_CLKDIV) < need) {
        REG(QMI_M0_TIMING) = (timing & ~QMI_TIMING_CLKDIV) | need;
    }
}
#endif

int emb_rp2350_clock_init(void)
{
    REG(XOSC_STARTUP) = XOSC_DELAY;
    REG(XOSC_CTRL) = XOSC_ENABLE | XOSC_RANGE_1_15;
    if (poll_set(XOSC_STATUS, XOSC_STABLE, XOSC_STABLE) != 0) {
        return -1;
    }
    REG(CLK_REF_DIV) = CLK_DIV_ONE;
    REG(CLK_REF_CTRL) = CLK_REF_SRC_XOSC; /* glitchless: SELECTED says when it took */
    if (poll_set(CLK_REF_SELECTED, 0xFu, 1u << CLK_REF_SRC_XOSC) != 0) {
        return -1;
    }
    REG(CLK_SYS_DIV) = CLK_DIV_ONE;
    REG(CLK_SYS_CTRL) = CLK_SYS_SRC_REF;
    if (poll_set(CLK_SYS_SELECTED, 0x3u, 1u << CLK_SYS_SRC_REF) != 0) {
        return -1;
    }
#ifdef EMB_HW_PLL_N
    /* clk_sys from PLL_SYS, as pico-sdk's pll_init and clock_configure do it: clk_sys is on
     * clk_ref (above), so the PLL can be reset; lock the VCO, then the post-dividers, then the
     * glitchless switch. The flash interface slows first (flash_for_clk_sys). */
    flash_for_clk_sys();
    REG(RESETS_RESET + SET_ALIAS) = EMB_RP2350_RESET_PLL_SYS;
    REG(RESETS_RESET + CLR_ALIAS) = EMB_RP2350_RESET_PLL_SYS;
    if (poll_set(RESETS_RESET_DONE, EMB_RP2350_RESET_PLL_SYS, EMB_RP2350_RESET_PLL_SYS) != 0) {
        return -1;
    }
    REG(PLL_CS) = EMB_HW_PLL_M;
    REG(PLL_FBDIV_INT) = EMB_HW_PLL_N;
    REG(PLL_PWR + CLR_ALIAS) = PLL_PWR_PD | PLL_PWR_VCOPD;
    if (poll_set(PLL_CS, PLL_CS_LOCK, PLL_CS_LOCK) != 0) {
        return -1;
    }
    REG(PLL_PRIM) = (EMB_HW_PLL_POSTDIV1 << PLL_PRIM_POSTDIV1) |
                    (EMB_HW_PLL_POSTDIV2 << PLL_PRIM_POSTDIV2);
    REG(PLL_PWR + CLR_ALIAS) = PLL_PWR_POSTDIVPD;
    REG(CLK_SYS_CTRL) = CLK_SYS_SRC_AUX; /* AUXSRC pll_sys, SRC the aux mux */
    if (poll_set(CLK_SYS_SELECTED, 0x3u, 1u << CLK_SYS_SRC_AUX) != 0) {
        return -1;
    }
#endif
    REG(CLK_PERI_DIV) = CLK_DIV_ONE;
    REG(CLK_PERI_CTRL) = CLK_PERI_ENABLE;
    return 0;
}
