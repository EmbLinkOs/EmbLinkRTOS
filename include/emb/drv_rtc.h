/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The RTC class (SPEC-017 §10, A123): a clock kept across resets, its time as nanoseconds since
 * the Unix epoch (UTC), the wall clock's unit; one alarm, its callback in the alarm interrupt;
 * and backup registers where the part has them. A set RTC seeds the wall clock when its device
 * starts (<emb/wallclock.h>). The wake-up timer is M4.4's, with the power policy. */
#ifndef EMB_DRV_RTC_H
#define EMB_DRV_RTC_H

#include <emb/device.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Capabilities (emb_device_caps()). */
#define EMB_DRV_RTC_CAP_CALENDAR   0x01u /* a calendar in hardware, years 2000 to 2099 */
#define EMB_DRV_RTC_CAP_SUBSECONDS 0x02u /* finer than a second */
#define EMB_DRV_RTC_CAP_BACKUP     0x04u /* backup registers */

/* An alarm's callback, in the alarm interrupt. */
typedef void (*emb_drv_rtc_alarm_fn_t)(void *arg);

/* The class's part of a driver's RAM, its first member: the alarm armed. */
typedef struct emb_drv_rtc_data {
    emb_drv_rtc_alarm_fn_t fn;
    void *arg;
    uint8_t armed;
    uint8_t reserved_[3];
} emb_drv_rtc_data_t;

/* An RTC driver's operations: the device's, then the time read and set, an alarm armed and
 * disarmed, and its backup registers (backup_count of them; 0 and NULL where it has none).
 * get() gives EMB_ENOTSET while the time is lost; alarm() refuses a time it cannot reach with
 * EMB_EINVAL. A driver's alarm interrupt calls emb_drv_rtc_alarm_fired(). */
typedef struct emb_drv_rtc_api {
    emb_device_api_t dev;
    emb_status_t (*get)(const emb_device_t *dev, int64_t *ns);
    emb_status_t (*set)(const emb_device_t *dev, int64_t ns);
    emb_status_t (*alarm)(const emb_device_t *dev, int64_t at_ns);
    void (*alarm_cancel)(const emb_device_t *dev);
    emb_status_t (*backup_read)(const emb_device_t *dev, uint8_t index, uint32_t *value);
    emb_status_t (*backup_write)(const emb_device_t *dev, uint8_t index, uint32_t value);
    uint8_t backup_count;
    uint8_t reserved_[3];
} emb_drv_rtc_api_t;

/**
 * emb_drv_rtc_get() - The RTC's time.
 * @dev: A device of the RTC class.
 * @ns:  Receives nanoseconds since 1970-01-01T00:00:00Z.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_RTC
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_ENOTSET (lost with power and not set since); EMB_ESTATE (not READY).
 */
emb_status_t emb_drv_rtc_get(const emb_device_t *dev, int64_t *ns);

/**
 * emb_drv_rtc_set() - Set the RTC's time.
 * @dev: A device of the RTC class.
 * @ns:  Nanoseconds since 1970-01-01T00:00:00Z.
 *
 * @ctx      thread
 * @blocks   yes
 * @time     O(1); the first set after power-up waits for the oscillator, up to 3 s
 * @owns     none
 * @config   CONFIG_EMB_DRV_RTC
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * The wall clock is not set by this (emb_wallclock_set()). An alarm armed is disarmed.
 *
 * Return: EMB_OK; EMB_EINVAL (a time the RTC cannot hold: before 2000 or from 2100 with a
 * calendar); EMB_EOSCFAIL (its oscillator did not start); EMB_ESTATE (not READY).
 */
emb_status_t emb_drv_rtc_set(const emb_device_t *dev, int64_t ns);

/**
 * emb_drv_rtc_alarm_set() - Arm the RTC's alarm.
 * @dev:   A device of the RTC class.
 * @at_ns: When, nanoseconds since the epoch, after the RTC's time now.
 * @fn:    Called in the alarm interrupt when it fires.
 * @arg:   Passed to @fn.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_RTC
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * One alarm per device. It fires once, at the RTC's resolution (1/256 s on the STM32F4, 1 ms on
 * the RP2350); @fn may arm it again.
 *
 * Return: EMB_OK; EMB_EBUSY (armed already); EMB_EINVAL (not after now, or beyond what the RTC
 * reaches: 28 days with a calendar); EMB_ENOTSET (the time is not set); EMB_ESTATE (not READY).
 */
emb_status_t emb_drv_rtc_alarm_set(const emb_device_t *dev, int64_t at_ns,
                                   emb_drv_rtc_alarm_fn_t fn, void *arg);

/**
 * emb_drv_rtc_alarm_cancel() - Disarm the RTC's alarm.
 * @dev: A device of the RTC class.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_RTC
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * No callback runs after it returns.
 *
 * Return: EMB_OK; EMB_ESTATE (none armed: it fired, or was never armed).
 */
emb_status_t emb_drv_rtc_alarm_cancel(const emb_device_t *dev);

/**
 * emb_drv_rtc_backup_read() - Read a backup register, kept with the RTC across resets.
 * @dev:   A device of the RTC class.
 * @index: Which, from 0.
 * @value: Receives it.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_RTC
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_ENOTSUP (it has none: CAP_BACKUP absent); EMB_EINVAL (@index past them).
 */
emb_status_t emb_drv_rtc_backup_read(const emb_device_t *dev, uint8_t index, uint32_t *value);

/**
 * emb_drv_rtc_backup_write() - Write a backup register.
 * @dev:   A device of the RTC class.
 * @index: Which, from 0.
 * @value: The word.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_RTC
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_ENOTSUP (it has none); EMB_EINVAL (@index past them).
 */
emb_status_t emb_drv_rtc_backup_write(const emb_device_t *dev, uint8_t index, uint32_t value);

/**
 * emb_drv_rtc_started() - For drivers, at the end of init(): the class's part of the RAM made
 * ready, and the wall clock seeded from the RTC when its time is set.
 * @dev: A device of the RTC class.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_RTC
 * @req      KRN-TIM-031
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK, whether or not the time was set.
 */
emb_status_t emb_drv_rtc_started(const emb_device_t *dev);

/**
 * emb_drv_rtc_alarm_fired() - For drivers, in the alarm interrupt: the alarm's callback run.
 * @dev: A device of the RTC class.
 *
 * @ctx      isr
 * @blocks   no
 * @time     O(1) plus the callback
 * @owns     none
 * @config   CONFIG_EMB_DRV_RTC
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Disarms it first, so the callback may arm it again. Nothing runs if it was canceled.
 *
 * Return: EMB_OK; EMB_ESTATE (none armed).
 */
emb_status_t emb_drv_rtc_alarm_fired(const emb_device_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* EMB_DRV_RTC_H */
