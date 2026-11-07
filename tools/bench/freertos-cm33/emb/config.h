/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The two configuration values the RP2350 board support's headers read (<emb/types.h>,
 * <emb/fault.h>), for FreeRTOS's build of bench_compare, which has no EmbLinkRTOS
 * configuration: the board code only brings up the clocks, the pins and the console here.
 */
#ifndef EMB_CONFIG_H
#define EMB_CONFIG_H
#define CONFIG_EMB_TICK_32BIT         1
#define CONFIG_EMB_FAULT_HOOK_RUNTIME 0
#endif
