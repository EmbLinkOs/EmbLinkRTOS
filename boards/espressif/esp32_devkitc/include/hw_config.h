/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    hw_config.h
 * @brief   Hardware configuration of esp32_devkitc (ESP-WROOM-32).
 *
 * @details
 * Hand-written from Espressif's ESP32 Technical Reference Manual v5.x and ESP-IDF v5.4's
 * components/soc/esp32 register headers.
 *
 * @author  Junior Deogracias
 * @date    2026-11-05 (created)
 */
#ifndef HW_CONFIG_H
#define HW_CONFIG_H

#define EMB_HW_BOARD        "esp32_devkitc"
#define EMB_HW_SOC          "ESP32"
#define EMB_HW_UART0_BASE   0x3FF40000u /* U0TXD GPIO1, U0RXD GPIO3: the USB-serial bridge */
#define EMB_HW_CONSOLE_BAUD 115200u     /* as the boot ROM set UART0 up */
#define EMB_HW_TEST_IRQ     24u         /* FROM_CPU_INTR0 (xt_port.c) */

#endif /* HW_CONFIG_H */
