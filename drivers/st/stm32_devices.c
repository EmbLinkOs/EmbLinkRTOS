/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The STM32F4 drivers as devices (SPEC-016): thin adapters over emb_stm32_drivers.h. */
#include <emb_stm32_devices.h>
#include <emb_stm32_drivers.h>
#include <emb_stm32_pins.h>

#if CONFIG_EMB_DEVICE
#define REG32(a)       (*(volatile uint32_t *)(uintptr_t)(a))
#define RCC_CFGR       0x08u
#define CFGR_SW_MASK   3u
#define CFGR_SWS_SHIFT 2u
/* a GPIO port (RM0390 §7.4) */
#define GPIO_MODER  0x00u
#define GPIO_OTYPER 0x04u
#define GPIO_PUPDR  0x0Cu
#define GPIO_IDR    0x10u
#define GPIO_BSRR   0x18u
#define GPIO_LINES  16u
#define MODE_INPUT  0u
#define MODE_OUTPUT 1u
#define PUPD_UP     1u
#define PUPD_DOWN   2u

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

static uintptr_t stm32_gpio_base(const emb_device_t *dev)
{
    return ((const emb_stm32_gpio_config_t *)dev->config)->base;
}

static void stm32_gpio_write(const emb_device_t *dev, uint32_t line, bool high)
{
    REG32(stm32_gpio_base(dev) + GPIO_BSRR) = high ? (1u << line) : (1u << (line + 16u));
}

static bool stm32_gpio_read(const emb_device_t *dev, uint32_t line)
{
    return ((REG32(stm32_gpio_base(dev) + GPIO_IDR) >> line) & 1u) != 0u;
}

/* The board's pin table's entry for the line, applied again; EMB_EINVAL when it has none. */
static emb_status_t stm32_gpio_function(uintptr_t base, uint32_t line)
{
    unsigned i;
    for (i = 0u; i < emb_board_pin_count; i++) { /* bounded by the board's pin table */
        if ((emb_board_pins[i].port_base == base) && (emb_board_pins[i].pin == line)) {
            emb_stm32_pins_apply(&emb_board_pins[i], 1u);
            return EMB_OK;
        }
    }
    return EMB_EINVAL;
}

/* In the class's critical section: MODER, OTYPER and PUPDR hold all 16 lines. */
static emb_status_t stm32_gpio_configure(const emb_device_t *dev, uint32_t line, uint32_t flags)
{
    uintptr_t base = stm32_gpio_base(dev);
    uint32_t shift = line * 2u;
    uint32_t pupd = 0u;
    bool output = (flags & EMB_DRV_GPIO_OUTPUT) != 0u;
    if (line >= GPIO_LINES) {
        return EMB_EINVAL;
    }
    if ((flags & EMB_DRV_GPIO_FUNCTION) != 0u) {
        return stm32_gpio_function(base, line);
    }
    if ((flags & EMB_DRV_GPIO_PULL_UP) != 0u) {
        pupd = PUPD_UP;
    } else if ((flags & EMB_DRV_GPIO_PULL_DOWN) != 0u) {
        pupd = PUPD_DOWN;
    } else {
        pupd = 0u;
    }
    REG32(base + GPIO_PUPDR) = (REG32(base + GPIO_PUPDR) & ~(3u << shift)) | (pupd << shift);
    if (output) { /* the level and the output type first, so the line never shows another */
        stm32_gpio_write(dev, line, (flags & EMB_DRV_GPIO_INIT_HIGH) != 0u);
        REG32(base + GPIO_OTYPER) = (REG32(base + GPIO_OTYPER) & ~(1u << line)) |
                                    (((flags & EMB_DRV_GPIO_OPEN_DRAIN) != 0u) ? (1u << line) : 0u);
    }
    REG32(base + GPIO_MODER) = (REG32(base + GPIO_MODER) & ~(3u << shift)) |
                               ((output ? MODE_OUTPUT : MODE_INPUT) << shift);
    return EMB_OK;
}

const emb_device_api_t emb_stm32_rcc_api = {rcc_init, NULL, NULL, NULL, NULL, 0u};
const emb_drv_gpio_api_t emb_stm32_gpio_api = {
    {gpio_init, NULL, NULL, NULL, NULL, EMB_DRV_GPIO_CAP_OPEN_DRAIN | EMB_DRV_GPIO_CAP_PULL_DOWN},
    stm32_gpio_configure,
    stm32_gpio_write,
    stm32_gpio_read,
};
#else
/* ISO C: a translation unit declares something */
typedef int emb_stm32_devices_off_t;
#endif
