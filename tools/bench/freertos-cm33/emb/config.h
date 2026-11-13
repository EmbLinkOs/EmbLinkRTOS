/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    config.h
 * @brief   The two configuration values the RP2350 board support's headers read (<emb/types.h>,
 *          <emb/fault.h>), for FreeRTOS's build of bench_compare, which has no EmbLinkRTOS
 *          configuration.
 *
 * @details
 * The board code only brings up the clocks, the pins and the console here.
 *
 * @author  Junior Deogracias
 * @date    2026-11-07 (created)
 */
#ifndef EMB_CONFIG_H
#define EMB_CONFIG_H
#define CONFIG_EMB_TICK_32BIT         1
#define CONFIG_EMB_FAULT_HOOK_RUNTIME 0
#endif
