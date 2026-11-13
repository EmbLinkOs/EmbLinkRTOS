/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb.h
 * @brief   Umbrella header (SPEC-001 §8): every public header of this milestone.
 *
 * @author  Junior Deogracias
 * @date    2026-10-07 (created)
 */
#ifndef EMB_EMB_H
#define EMB_EMB_H

#include <emb/compiler.h>
#include <emb/config.h>
#if CONFIG_EMB_COND
#include <emb/cond.h>
#endif
#include <emb/console.h>
#include <emb/context.h>
#if CONFIG_EMB_DEVICE
#include <emb/device.h>
#endif
#if CONFIG_EMB_DRV_UART
#include <emb/drv_uart.h>
#endif
#if CONFIG_EMB_DRV_SPI
#include <emb/drv_counter.h>
#include <emb/drv_dma.h>
#include <emb/drv_entropy.h>
#include <emb/drv_flash.h>
#include <emb/drv_gpio.h>
#include <emb/drv_i2c.h>
#include <emb/drv_pwm.h>
#include <emb/drv_rtc.h>
#include <emb/drv_spi.h>
#include <emb/drv_wdt.h>
#endif
#if CONFIG_EMB_DRV_FLASH
#include <emb/flash_area.h>
#endif
#if CONFIG_EMB_WALLCLOCK
#include <emb/wallclock.h>
#endif
#if CONFIG_EMB_WDOG
#include <emb/wdog.h>
#endif
#if CONFIG_EMB_CRASH_RECORD
#include <emb/crash.h>
#endif
#if CONFIG_EMB_EVENT
#include <emb/event.h>
#endif
#include <emb/fault.h>
#include <emb/irq.h>
#include <emb/kernel.h>
#include <emb/log.h>
#if CONFIG_EMB_MSGQ
#include <emb/msgq.h>
#endif
#include <emb/mutex.h>
#include <emb/notify.h>
#include <emb/sched.h>
#include <emb/sem.h>
#include <emb/status.h>
#include <emb/storage.h>
#include <emb/thread.h>
#include <emb/time.h>
#if CONFIG_EMB_TIMER
#include <emb/timer.h>
#endif
#include <emb/trace.h>
#include <emb/types.h>
#include <emb/version.h>
#if CONFIG_EMB_WORKQ
#include <emb/work.h>
#endif
#if CONFIG_EMB_TASK
#include <emb/task.h>
#endif

#endif /* EMB_EMB_H */
