/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    drv_adc.h
 * @brief   The ADC class (SPEC-017 §11c, A129).
 *
 * @details
 * One conversion of an input, waiting for the device or polled from any context; a count's
 * millivolts against the board's reference; the internal temperature sensor, calibrated where
 * the part stores calibration; and a sequence of inputs sampled at a rate into the caller's
 * buffer by DMA, a callback at each half of it.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#ifndef EMB_DRV_ADC_H
#define EMB_DRV_ADC_H

#include <emb/device.h>
#include <emb/drv_bus.h>
#include <emb/time.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Inputs: an ADC's own numbers (adc1_in4 is 4), and its internal ones. */
#define EMB_DRV_ADC_TEMPERATURE 0xFEu /* the internal temperature sensor (CAP_TEMPERATURE) */
#define EMB_DRV_ADC_VREFINT     0xFDu /* the internal reference (CAP_VREFINT) */

/* The most inputs a sampled sequence has. */
#define EMB_DRV_ADC_SEQUENCE_MAX 8u

/* Capabilities (emb_device_caps()). */
#define EMB_DRV_ADC_CAP_TEMPERATURE 0x01u /* an internal temperature sensor */
#define EMB_DRV_ADC_CAP_CALIBRATED  0x02u /* its temperature from the part's factory calibration */
#define EMB_DRV_ADC_CAP_VREFINT     0x04u /* an internal reference to read */
#define EMB_DRV_ADC_CAP_SAMPLE      0x08u /* sequences sampled into a buffer by DMA */
#define EMB_DRV_ADC_CAP_ASCENDING   0x10u /* a sequence's inputs taken in ascending order only */

/* A half of a sampling buffer filled, in the DMA interrupt: its samples, the sequence's inputs
 * in turn, a whole number of sequences; or the sampling's end, EMB_EOVERRUN when the hardware
 * lost samples, with no samples. */
typedef void (*emb_drv_adc_fn_t)(void *arg, const uint16_t *samples, uint32_t count,
                                 emb_status_t status);

/* A sampling: a sequence of inputs, converted in turn rate_hz times a second, into buffer, its
 * length samples a whole number of sequences in each half; fn is called at each half. The
 * buffer belongs to the hardware until the sampling stops; a half is the callback's to read
 * until the other half is full. */
typedef struct emb_drv_adc_sampling {
    const uint8_t *inputs;
    uint16_t *buffer;
    uint32_t length;
    uint32_t rate_hz;
    emb_drv_adc_fn_t fn;
    void *arg;
    uint8_t count; /* inputs in the sequence: 1 to EMB_DRV_ADC_SEQUENCE_MAX */
    uint8_t reserved_[3];
} emb_drv_adc_sampling_t;

/* The class's part of a driver's RAM, its first members: the device's lock, which queues
 * readers; whether a conversion or a sampling holds the hardware; the sampling's buffer and
 * callback. */
typedef struct emb_drv_adc_data {
    emb_drv_bus_t lock;
    emb_drv_adc_fn_t fn;
    void *arg;
    uint16_t *buffer;
    uint32_t length;
    uint8_t busy;
    uint8_t sampling;
    uint8_t reserved_[2];
} emb_drv_adc_data_t;

/* An ADC driver's operations: the device's, then one conversion, polled and bounded; a sampling
 * started and stopped; the temperature sensor's count in thousandths of a degree Celsius; and
 * the board's reference, in millivolts. read() refuses an input it does not have with
 * EMB_EINVAL; sample_start() a sampling it cannot make (an input, the rate, the buffer) with
 * EMB_EINVAL, and calls emb_drv_adc_sampled() at each half. Each runs with the hardware the
 * class's: read() and sample_start() never at once. */
typedef struct emb_drv_adc_api {
    emb_device_api_t dev;
    emb_status_t (*read)(const emb_device_t *dev, uint8_t input, uint16_t *count);
    emb_status_t (*sample_start)(const emb_device_t *dev, const emb_drv_adc_sampling_t *s);
    void (*sample_stop)(const emb_device_t *dev);
    emb_status_t (*to_mdegc)(const emb_device_t *dev, uint16_t count, int32_t *mdegc);
    uint32_t (*vref_mv)(const emb_device_t *dev);
    uint8_t bits; /* a count's width */
    uint8_t reserved_[3];
} emb_drv_adc_api_t;

/**
 * emb_drv_adc_read() - Convert an input once.
 * @dev:     A device of the ADC class.
 * @input:   Its number, or EMB_DRV_ADC_TEMPERATURE, EMB_DRV_ADC_VREFINT.
 * @count:   Receives the conversion, 0 to 2^bits - 1.
 * @timeout: How long to wait for the device.
 *
 * @ctx      thread
 * @blocks   yes
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_ADC
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * It waits for the device, up to @timeout, then converts in under 30 us, polled.
 *
 * Return: EMB_OK; EMB_ETIMEDOUT (the device stayed taken); EMB_EBUSY (it samples); EMB_EINVAL
 * (no such input); EMB_EIO (the conversion failed); EMB_ESTATE (not READY).
 */
emb_status_t emb_drv_adc_read(const emb_device_t *dev, uint8_t input, uint16_t *count,
                              emb_timeout_t timeout);

/**
 * emb_drv_adc_read_polled() - Convert an input once, from any context.
 * @dev:   A device of the ADC class.
 * @input: Its number, or EMB_DRV_ADC_TEMPERATURE, EMB_DRV_ADC_VREFINT.
 * @count: Receives the conversion.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_ADC
 * @req      DRV-001 DRV-007
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EBUSY (a conversion or a sampling holds the device); EMB_EINVAL (no such
 * input); EMB_EIO; EMB_ESTATE (not READY).
 */
emb_status_t emb_drv_adc_read_polled(const emb_device_t *dev, uint8_t input, uint16_t *count);

/**
 * emb_drv_adc_to_mv() - A count's millivolts, against the board's reference.
 * @dev:   A device of the ADC class.
 * @count: A conversion.
 * @mv:    Receives count x reference / (2^bits - 1), rounded.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_ADC
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * The reference is the board's (the device's vref_mv: 3300 unless it says).
 *
 * Return: EMB_OK; EMB_EINVAL (a count beyond the ADC's width).
 */
emb_status_t emb_drv_adc_to_mv(const emb_device_t *dev, uint16_t count, uint32_t *mv);

/**
 * emb_drv_adc_temperature() - The internal temperature sensor's reading.
 * @dev:     A device of the ADC class with CAP_TEMPERATURE.
 * @mdegc:   Receives the temperature, in thousandths of a degree Celsius.
 * @timeout: How long to wait for the device.
 *
 * @ctx      thread
 * @blocks   yes
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_ADC
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * With CAP_CALIBRATED, from the part's two calibration points; otherwise from the datasheet's
 * typical slope, a few degrees off.
 *
 * Return: EMB_OK; EMB_ENOTSUP (no sensor); EMB_EIO (no calibration, or the conversion failed);
 * and emb_drv_adc_read()'s.
 */
emb_status_t emb_drv_adc_temperature(const emb_device_t *dev, int32_t *mdegc,
                                     emb_timeout_t timeout);

/**
 * emb_drv_adc_sample_start() - Sample a sequence of inputs into a buffer, by DMA.
 * @dev: A device of the ADC class with CAP_SAMPLE.
 * @s:   The sampling; copied: it may be on the caller's stack.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_ADC
 * @req      DRV-001 DRV-004
 * @since    0.3
 * @stable   no
 *
 * It runs until emb_drv_adc_sample_stop(), or until the hardware loses samples, which ends it
 * with a last call of @s's fn with EMB_EOVERRUN. Meanwhile the device is the sampling's: a read
 * is refused with EMB_EBUSY. With CAP_ASCENDING, a sequence's inputs are in ascending order,
 * each once. The RP2350's buffer is a power of two bytes, at most 32 KiB, aligned to its size
 * (its DMA's ring, A129).
 *
 * Return: EMB_OK; EMB_EBUSY (it samples, or converts); EMB_ENOTSUP (no sampling); EMB_EINVAL (a
 * sequence of 0 or more than EMB_DRV_ADC_SEQUENCE_MAX inputs, an input it does not have, a rate
 * it cannot make, or a buffer of another shape); EMB_ESTATE (not READY).
 */
emb_status_t emb_drv_adc_sample_start(const emb_device_t *dev, const emb_drv_adc_sampling_t *s);

/**
 * emb_drv_adc_sample_stop() - Stop a sampling.
 * @dev: A device of the ADC class.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_ADC
 * @req      DRV-001 DRV-006
 * @since    0.3
 * @stable   no
 *
 * No callback runs after it returns; the buffer is the caller's again.
 *
 * Return: EMB_OK; EMB_ESTATE (it was not sampling).
 */
emb_status_t emb_drv_adc_sample_stop(const emb_device_t *dev);

/**
 * emb_drv_adc_started() - For drivers, at the end of init(): the class's part of the RAM made
 * ready, its lock made.
 * @dev: A device of the ADC class.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_ADC
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; what making the lock returns otherwise.
 */
emb_status_t emb_drv_adc_started(const emb_device_t *dev);

/**
 * emb_drv_adc_sampled() - For drivers, in the DMA interrupt: a half of the buffer filled, or the
 * sampling ended by the hardware.
 * @dev:    A device of the ADC class.
 * @half:   0 for the buffer's first half, 1 for its second.
 * @status: EMB_OK; or EMB_EOVERRUN, the driver having stopped the hardware.
 *
 * @ctx      isr
 * @blocks   no
 * @time     O(1) plus the callback
 * @owns     none
 * @config   CONFIG_EMB_DRV_ADC
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Nothing runs after a stop.
 *
 * Return: EMB_OK; EMB_ESTATE (not sampling).
 */
emb_status_t emb_drv_adc_sampled(const emb_device_t *dev, uint8_t half, emb_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* EMB_DRV_ADC_H */
