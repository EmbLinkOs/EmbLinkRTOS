/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_stm32_devices.h
 * @brief   The STM32F4 drivers as devices (SPEC-016).
 *
 * @details
 * The operations and generated configurations the bindings name (drivers/bindings.yaml). Each
 * adapter is thin: the free functions of emb_stm32_drivers.h stay the low layer, used by the board
 * support before the kernel and by applications. The classes' requests come with SPEC-017.
 *
 * @author  Junior Deogracias
 * @date    2026-11-09 (created)
 */
#ifndef EMB_STM32_DEVICES_H
#define EMB_STM32_DEVICES_H

#include <emb/config.h>

#include <stdint.h>

#if CONFIG_EMB_DEVICE
#include <emb/device.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The reset and clock controller: the clock tree the board support brought up. */
typedef struct emb_stm32_rcc_config {
    uintptr_t base;
} emb_stm32_rcc_config_t;

/* A GPIO port: its clock gate. */
typedef struct emb_stm32_gpio_config {
    uintptr_t base;
    uint32_t clken_offset; /* the bus's clock-enable register in the RCC */
    uint32_t clken_bit;
} emb_stm32_gpio_config_t;

/* A USART: its clock gate and rate. */
typedef struct emb_stm32_usart_config {
    uintptr_t base;
    uint32_t clock_hz; /* its bus clock */
    uint32_t baud;
    uint32_t clken_offset;
    uint32_t clken_bit;
} emb_stm32_usart_config_t;

#if CONFIG_EMB_DEVICE
/* READY when the clock switch the board support asked for has completed. */
extern const emb_device_api_t emb_stm32_rcc_api;
/* Enables the port's clock. */
extern const emb_device_api_t emb_stm32_gpio_api;
/* Checks the divider and enables the USART's clock. A USART already running at the rate, as
 * the board's console is, is left as it is; another is set up for transmission at the rate
 * once the character on its line has finished. */
extern const emb_device_api_t emb_stm32_usart_api;
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_STM32_DEVICES_H */
