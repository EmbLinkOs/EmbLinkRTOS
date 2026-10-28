/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The slim idle context (ADR-050): the table profile without an idle thread allocates
 * only the prefix of a control block that the scheduler, the switch path and diagnostics
 * read for idle. Every other translation unit declares embk_idle_thread as a full
 * embk_thread_t (embk/sched.h) and touches only these leading fields of it, so this file
 * must not include that header.
 *
 * MISRA Dev CS-12: rules 8.3/8.4, the definition's type differs from the declaration on
 * purpose. The kernel is not built with LTO, since the layout probe needs real object
 * code; an LTO build would report this mismatch (-Wlto-type-mismatch) and fail.
 */
#include <embk/kernel.h>

#include <stdalign.h>

#define IDLE_BYTES offsetof(embk_thread_t, wait_gen)

/* Every field the kernel or a port reads or writes for the idle context lies in front. */
EMB_STATIC_ASSERT(offsetof(embk_thread_t, arch) == 0u, "idle prefix: arch");
EMB_STATIC_ASSERT(offsetof(embk_thread_t, obj) < IDLE_BYTES, "idle prefix: obj");
EMB_STATIC_ASSERT(offsetof(embk_thread_t, tflags) < IDLE_BYTES, "idle prefix: tflags");
EMB_STATIC_ASSERT(offsetof(embk_thread_t, cancel_disable) < IDLE_BYTES, "idle prefix");
#if !EMBK_THREAD_INDEX_PACKED
EMB_STATIC_ASSERT(offsetof(embk_thread_t, index) < IDLE_BYTES, "idle prefix: index");
#endif
#if CONFIG_EMB_CHECKED
EMB_STATIC_ASSERT(offsetof(embk_thread_t, lock_depth) < IDLE_BYTES, "idle prefix: lock_depth");
#endif

alignas(embk_thread_t) unsigned char embk_idle_thread[IDLE_BYTES];
