/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    gpio.c
 * @brief   The GPIO class (SPEC-017 §5a, A116): its checks over a driver's line operations.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/drv_gpio.h>

#include <embk/kernel.h>

#define LINE_MAX 31u /* a controller's lines fit one 32-bit register */

static const emb_drv_gpio_api_t *gpio_api(const emb_device_t *dev)
{
    if ((dev == NULL) || (dev->api == NULL) || (dev->class_id != EMB_DRV_CLASS_GPIO)) {
        return NULL;
    }
    /* MISRA Dev CS-12: rule 11.3, the class's operations begin with the device's */
    return (const emb_drv_gpio_api_t *)(const void *)dev->api;
}

static bool flags_ok(uint32_t flags)
{
    bool output = (flags & EMB_DRV_GPIO_OUTPUT) != 0u;
    if ((flags & ~EMB_DRV_GPIO_FLAGS) != 0u) {
        return false;
    }
    if ((flags & EMB_DRV_GPIO_FUNCTION) != 0u) {
        return flags == EMB_DRV_GPIO_FUNCTION; /* alone */
    }
    if ((flags & (EMB_DRV_GPIO_PULL_UP | EMB_DRV_GPIO_PULL_DOWN)) ==
        (EMB_DRV_GPIO_PULL_UP | EMB_DRV_GPIO_PULL_DOWN)) {
        return false;
    }
    return output || ((flags & (EMB_DRV_GPIO_OPEN_DRAIN | EMB_DRV_GPIO_INIT_HIGH)) == 0u);
}

emb_status_t emb_drv_gpio_configure(const emb_device_t *dev, uint32_t line, uint32_t flags)
{
    const emb_drv_gpio_api_t *api = gpio_api(dev);
    emb_irq_key_t key;
    emb_status_t st;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_GPIO_CONFIGURE, (api != NULL) && (api->configure != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_GPIO_CONFIGURE, (line <= LINE_MAX) && flags_ok(flags),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    key = emb_irq_lock(); /* the controller's shared mode registers */
    st = api->configure(dev, line, flags);
    emb_irq_unlock(key);
    return st;
}

emb_status_t emb_drv_gpio_write(const emb_device_t *dev, uint32_t line, bool high)
{
    const emb_drv_gpio_api_t *api = gpio_api(dev);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_GPIO_WRITE, (api != NULL) && (api->write != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_GPIO_WRITE, line <= LINE_MAX, EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 2u);
    api->write(dev, line, high);
    return EMB_OK;
}

emb_status_t emb_drv_gpio_read(const emb_device_t *dev, uint32_t line, bool *high)
{
    const emb_drv_gpio_api_t *api = gpio_api(dev);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_GPIO_READ, (api != NULL) && (api->read != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_GPIO_READ, (line <= LINE_MAX) && (high != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    *high = api->read(dev, line);
    return EMB_OK;
}

emb_status_t emb_drv_gpio_assert(const emb_drv_gpio_line_t *l, bool on)
{
    if ((l == NULL) || (l->gpio == NULL)) {
        return EMB_OK;
    }
    return emb_drv_gpio_write(l->gpio, l->line, (l->active_low != 0u) ? !on : on);
}
