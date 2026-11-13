/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    hw_config.h
 * @brief   Hardware configuration of esp32c3_devkitm1 (ESP32-C3-MINI-1, DevKitM-1 V1.0).
 *
 * @details
 * Hand-written from Espressif's ESP32-C3 Technical Reference Manual v1.4 and the board's schematic;
 * emb-hwgen generates it from M3 (SPEC-014).
 *
 * @author  Junior Deogracias
 * @date    2026-10-22 (created)
 */
#ifndef HW_CONFIG_H
#define HW_CONFIG_H

#define EMB_HW_BOARD        "esp32c3_devkitm1"
#define EMB_HW_SOC          "ESP32-C3"
#define EMB_HW_UART0_BASE   0x60000000u /* U0TXD GPIO21, U0RXD GPIO20: the CP2102N */
#define EMB_HW_CONSOLE_BAUD 115200u     /* as the boot ROM set UART0 up */
#define EMB_HW_TEST_IRQ     0u          /* FROM_CPU_INTR0 (rv_esp32c3.c) */

#endif /* HW_CONFIG_H */
