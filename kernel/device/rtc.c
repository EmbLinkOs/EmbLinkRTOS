/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The RTC class (SPEC-017 §10, A123): its checks over a driver's operations, the one alarm's
 * callback, and the wall clock seeded from a set RTC when its device starts. */
#include <emb/drv_rtc.h>
#if CONFIG_EMB_WALLCLOCK
#include <emb/wallclock.h>
#endif

#include <embk/kernel.h>

static bool is_rtc(const emb_device_t *dev)
{
    return (dev != NULL) && (dev->api != NULL) && (dev->class_id == EMB_DRV_CLASS_RTC) &&
           (dev->data != NULL);
}

/* The class's operations: the device's first. MISRA Dev CS-12: rule 11.3. */
static const emb_drv_rtc_api_t *rtc_api(const emb_device_t *dev)
{
    return (const emb_drv_rtc_api_t *)(const void *)dev->api;
}

/* The class's part of the driver's RAM, its first member. MISRA Dev CS-12: rule 11.5. */
static emb_drv_rtc_data_t *rtc_part(const emb_device_t *dev)
{
    return (emb_drv_rtc_data_t *)dev->data;
}

emb_status_t emb_drv_rtc_get(const emb_device_t *dev, int64_t *ns)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_RTC_GET, is_rtc(dev) && (ns != NULL), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    return rtc_api(dev)->get(dev, ns);
}

emb_status_t emb_drv_rtc_set(const emb_device_t *dev, int64_t ns)
{
    emb_drv_rtc_data_t *d;
    emb_irq_key_t key;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_RTC_SET, is_rtc(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_RTC_SET);
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    if (ns < 0) {
        return EMB_EINVAL;
    }
    d = rtc_part(dev);
    key = embk_irq_lock(); /* the alarm counted in the old time goes */
    if (d->armed != 0u) {
        d->armed = 0u;
        rtc_api(dev)->alarm_cancel(dev);
    }
    embk_irq_unlock(key);
    return rtc_api(dev)->set(dev, ns);
}

emb_status_t emb_drv_rtc_alarm_set(const emb_device_t *dev, int64_t at_ns,
                                   emb_drv_rtc_alarm_fn_t fn, void *arg)
{
    emb_drv_rtc_data_t *d;
    emb_irq_key_t key;
    emb_status_t st;
    int64_t now = 0;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_RTC_ALARM_SET, is_rtc(dev) && (fn != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    d = rtc_part(dev);
    key = embk_irq_lock();
    if (d->armed != 0u) {
        embk_irq_unlock(key);
        return EMB_EBUSY;
    }
    st = rtc_api(dev)->get(dev, &now);
    if ((st == EMB_OK) && (at_ns <= now)) {
        st = EMB_EINVAL; /* not after now */
    }
    if (st == EMB_OK) {
        d->fn = fn;
        d->arg = arg;
        d->armed = 1u;
        st = rtc_api(dev)->alarm(dev, at_ns);
        if (st != EMB_OK) {
            d->armed = 0u;
        }
    }
    embk_irq_unlock(key);
    return st;
}

emb_status_t emb_drv_rtc_alarm_cancel(const emb_device_t *dev)
{
    emb_drv_rtc_data_t *d;
    emb_irq_key_t key;
    emb_status_t st = EMB_ESTATE;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_RTC_ALARM_CANCEL, is_rtc(dev), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    d = rtc_part(dev);
    key = embk_irq_lock();
    if (d->armed != 0u) {
        d->armed = 0u;
        rtc_api(dev)->alarm_cancel(dev);
        st = EMB_OK;
    }
    embk_irq_unlock(key);
    return st;
}

emb_status_t emb_drv_rtc_backup_read(const emb_device_t *dev, uint8_t index, uint32_t *value)
{
    const emb_drv_rtc_api_t *api;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_RTC_BACKUP_READ, is_rtc(dev) && (value != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    api = rtc_api(dev);
    if ((api->backup_count == 0u) || (api->backup_read == NULL)) {
        return EMB_ENOTSUP;
    }
    if (index >= api->backup_count) {
        return EMB_EINVAL;
    }
    return api->backup_read(dev, index, value);
}

emb_status_t emb_drv_rtc_backup_write(const emb_device_t *dev, uint8_t index, uint32_t value)
{
    const emb_drv_rtc_api_t *api;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_RTC_BACKUP_WRITE, is_rtc(dev), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    api = rtc_api(dev);
    if ((api->backup_count == 0u) || (api->backup_write == NULL)) {
        return EMB_ENOTSUP;
    }
    if (index >= api->backup_count) {
        return EMB_EINVAL;
    }
    return api->backup_write(dev, index, value);
}

emb_status_t emb_drv_rtc_started(const emb_device_t *dev)
{
    emb_drv_rtc_data_t *d;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_RTC_STARTED, is_rtc(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    d = rtc_part(dev);
    d->fn = NULL;
    d->arg = NULL;
    d->armed = 0u;
#if CONFIG_EMB_WALLCLOCK
    {
        int64_t ns = 0;
        if (rtc_api(dev)->get(dev, &ns) == EMB_OK) {
            (void)emb_wallclock_set(ns);
        }
    }
#endif
    return EMB_OK;
}

emb_status_t emb_drv_rtc_alarm_fired(const emb_device_t *dev)
{
    emb_drv_rtc_data_t *d;
    emb_drv_rtc_alarm_fn_t fn = NULL;
    void *arg = NULL;
    emb_irq_key_t key;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_RTC_ALARM_FIRED, is_rtc(dev), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    d = rtc_part(dev);
    key = embk_irq_lock();
    if (d->armed != 0u) {
        d->armed = 0u; /* first: the callback may arm it again */
        fn = d->fn;
        arg = d->arg;
    }
    embk_irq_unlock(key);
    if (fn == NULL) {
        return EMB_ESTATE;
    }
    fn(arg);
    return EMB_OK;
}
