/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Umbrella header (SPEC-001 §8): every public header of this milestone. */
#ifndef EMB_EMB_H
#define EMB_EMB_H

#include <emb/compiler.h>
#include <emb/config.h>
#if CONFIG_EMB_COND
#include <emb/cond.h>
#endif
#include <emb/context.h>
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
#include <emb/trace.h>
#include <emb/types.h>
#include <emb/version.h>

#endif /* EMB_EMB_H */
