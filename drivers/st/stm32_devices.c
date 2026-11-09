/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The STM32F4 drivers as devices (SPEC-016): thin adapters over emb_stm32_drivers.h. */
#include <emb_stm32_devices.h>
#include <emb_stm32_drivers.h>

#if CONFIG_EMB_DEVICE
#define REG32(a)       (*(volatile uint32_t *)(uintptr_t)(a))
#define RCC_CFGR       0x08u
#define CFGR_SW_MASK   3u
#define CFGR_SWS_SHIFT 2u

static emb_status_t rcc_init(const emb_device_t *dev)
{
    const emb_stm32_rcc_config_t *cfg = (const emb_stm32_rcc_config_t *)dev->config;
    uint32_t cfgr = REG32(cfg->base + RCC_CFGR);
    /* RM0390 §6.3.3: SWS follows SW once the switch is done */
    return (((cfgr >> CFGR_SWS_SHIFT) & CFGR_SW_MASK) == (cfgr & CFGR_SW_MASK)) ? EMB_OK : EMB_EIO;
}

static emb_status_t gpio_init(const emb_device_t *dev)
{
    const emb_stm32_gpio_config_t *cfg = (const emb_stm32_gpio_config_t *)dev->config;
    emb_stm32_clock_enable(cfg->clken_offset, cfg->clken_bit);
    return EMB_OK;
}

const emb_device_api_t emb_stm32_rcc_api = {rcc_init, NULL, NULL, NULL, NULL, 0u};
const emb_device_api_t emb_stm32_gpio_api = {gpio_init, NULL, NULL, NULL, NULL, 0u};
#else
/* ISO C: a translation unit declares something */
typedef int emb_stm32_devices_off_t;
#endif
