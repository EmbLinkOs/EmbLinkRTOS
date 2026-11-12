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
#include <emb/context.h>
#if CONFIG_EMB_DEVICE
#include <emb/device.h>
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

#endif /* EMB_EMB_H */
