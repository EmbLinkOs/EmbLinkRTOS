/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_cmsdk_uart.h
 * @brief   Arm CMSDK APB UART (Arm DDI 0479C §4.6), as the MPS2 boards and QEMU's models have it.
 *
 * @details
 * A polled transmit for the board's console, and the device the generator binds to "arm,cmsdk-uart"
 * (SPEC-016 §8). The UART class's requests come with SPEC-017 §4.
 *
 * @author  Junior Deogracias
 * @date    2026-11-09 (created)
 */
#ifndef EMB_CMSDK_UART_H
#define EMB_CMSDK_UART_H

#include <emb/config.h>

#include <stdint.h>

#if CONFIG_EMB_DEVICE
#include <emb/device.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The generated configuration of a CMSDK UART device. */
typedef struct emb_cmsdk_uart_config {
    uintptr_t base;    /* the register block */
    uint32_t clock_hz; /* PCLK */
    uint32_t baud;
} emb_cmsdk_uart_config_t;

/* Sets the divider and enables the transmitter. BAUDDIV is at least 16. */
void emb_cmsdk_uart_setup(uintptr_t base, uint32_t clock_hz, uint32_t baud);
/* Waits for room in the transmit buffer, bounded, then writes one character. */
void emb_cmsdk_uart_putc(uintptr_t base, char c);

#if CONFIG_EMB_DEVICE
/* The device's operations: initialization checks the configuration and sets the UART up. */
extern const emb_device_api_t emb_cmsdk_uart_api;
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_CMSDK_UART_H */
