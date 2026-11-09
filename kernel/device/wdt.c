/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The watchdog class (SPEC-017 §8): its checks over a driver's operations, and whether it was
 * started, which the class keeps so every driver behaves alike. */
#include <emb/drv_wdt.h>

#include <embk/kernel.h>

static const emb_drv_wdt_api_t *wdt_api(const emb_device_t *dev)
{
    if ((dev == NULL) || (dev->api == NULL) || (dev->class_id != EMB_DRV_CLASS_WATCHDOG)) {
        return NULL;
    }
    /* MISRA Dev CS-12: rule 11.3, the class's operations begin with the device's */
    return (const emb_drv_wdt_api_t *)(const void *)dev->api;
}

emb_status_t emb_drv_wdt_start(const emb_device_t *dev, uint32_t timeout_ms)
{
    const emb_drv_wdt_api_t *api = wdt_api(dev);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_WDT_START, (api != NULL) && (api->start != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    if (timeout_ms == 0u) {
        return EMB_EINVAL;
    }
    return api->start(dev, timeout_ms);
}

emb_status_t emb_drv_wdt_feed(const emb_device_t *dev)
{
    const emb_drv_wdt_api_t *api = wdt_api(dev);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_WDT_FEED, (api != NULL) && (api->feed != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    api->feed(dev);
    return EMB_OK;
}

emb_status_t emb_drv_wdt_caused_reset(const emb_device_t *dev, bool *yes)
{
    const emb_drv_wdt_api_t *api = wdt_api(dev);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_WDT_CAUSED_RESET,
                     (api != NULL) && (api->caused_reset != NULL) && (yes != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    *yes = api->caused_reset(dev);
    return EMB_OK;
}
