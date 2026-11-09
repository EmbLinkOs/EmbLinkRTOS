/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The STM32F4's independent watchdog (RM0390 Rev 6 §19) as a watchdog class device (SPEC-017
 * §8): once started it cannot be stopped, and it resets the part when not reloaded within its
 * timeout. Its LSI clock lies between 17 and 47 kHz (DS10693 §6.3.8); the timeout is counted at
 * 47 kHz, so the part is never reset before it. The reset's cause is RCC_CSR's, read once. */
#include <emb_stm32_devices.h>
#include <emb_stm32_drivers.h>

#if CONFIG_EMB_DRV_WDT
#define REG32(a)   (*(volatile uint32_t *)(uintptr_t)(a))
#define IWDG_KR    0x00u
#define IWDG_PR    0x04u
#define IWDG_RLR   0x08u
#define IWDG_SR    0x0Cu
#define KEY_ACCESS 0x5555u
#define KEY_RELOAD 0xAAAAu
#define KEY_START  0xCCCCu
#define RLR_MAX    0xFFFu
#define PR_MAX     6u  /* the divider 4 << PR: 4 to 256 */
#define LSI_KHZ    47u /* its fastest */
#define SR_BUSY    3u  /* PVU and RVU: the prescaler and reload still updating */
#define SR_POLL    100000u

static const emb_stm32_iwdg_config_t *stm32_iwdg_config(const emb_device_t *dev)
{
    return (const emb_stm32_iwdg_config_t *)dev->config;
}

static emb_stm32_iwdg_data_t *stm32_iwdg_data(const emb_device_t *dev)
{
    return (emb_stm32_iwdg_data_t *)dev->data;
}

static emb_status_t stm32_iwdg_init(const emb_device_t *dev)
{
    emb_stm32_iwdg_data_t *d = stm32_iwdg_data(dev);
    d->started = false;
    d->caused = (emb_stm32f4_reset_flags_take() & EMB_STM32F4_RESET_IWDG) != 0u;
    return EMB_OK;
}

/* The smallest prescaler whose count reaches @timeout_ms at the LSI's fastest. */
static emb_status_t stm32_iwdg_start(const emb_device_t *dev, uint32_t timeout_ms)
{
    uintptr_t base = stm32_iwdg_config(dev)->base;
    emb_stm32_iwdg_data_t *d = stm32_iwdg_data(dev);
    uint32_t pr;
    uint32_t ticks = 0u;
    uint32_t n;
    if (d->started) {
        return EMB_ESTATE;
    }
    if (timeout_ms > (UINT32_MAX / LSI_KHZ)) {
        return EMB_EINVAL;
    }
    for (pr = 0u; pr <= PR_MAX; pr++) { /* bounded: seven dividers */
        uint32_t div = 4u << pr;
        ticks = ((timeout_ms * LSI_KHZ) + div - 1u) / div;
        if (ticks <= RLR_MAX) {
            break;
        }
    }
    if (pr > PR_MAX) {
        return EMB_EINVAL; /* past some 22 s */
    }
    REG32(base + IWDG_KR) = KEY_START; /* starts the LSI and the counter */
    REG32(base + IWDG_KR) = KEY_ACCESS;
    REG32(base + IWDG_PR) = pr;
    REG32(base + IWDG_RLR) = ticks;
    for (n = 0u; n < SR_POLL; n++) { /* bounded: a few LSI cycles */
        if ((REG32(base + IWDG_SR) & SR_BUSY) == 0u) {
            break;
        }
    }
    REG32(base + IWDG_KR) = KEY_RELOAD;
    d->started = true;
    return EMB_OK;
}

static void stm32_iwdg_feed(const emb_device_t *dev)
{
    REG32(stm32_iwdg_config(dev)->base + IWDG_KR) = KEY_RELOAD;
}

static bool stm32_iwdg_caused_reset(const emb_device_t *dev)
{
    return stm32_iwdg_data(dev)->caused;
}

const emb_drv_wdt_api_t emb_stm32_iwdg_api = {
    {stm32_iwdg_init, NULL, NULL, NULL, NULL, 0u},
    stm32_iwdg_start,
    stm32_iwdg_feed,
    stm32_iwdg_caused_reset,
};
#else
/* ISO C: a translation unit declares something */
typedef int emb_stm32_iwdg_off_t;
#endif
