/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Thread notifications (SPEC-006 §2): one word of level-sensitive bits per thread,
 * set from any context, waited on by the owner only. The canonical ISR-to-thread
 * completion path and the only wake mechanism the tiny profile needs besides timeouts.
 */
#ifndef EMB_NOTIFY_H
#define EMB_NOTIFY_H

#include <emb/status.h>
#include <emb/thread.h>
#include <emb/time.h>
#include <emb/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMB_NOTIFY_BITS CONFIG_EMB_NOTIFY_BITS

#if CONFIG_EMB_NOTIFY_BITS == 8
typedef uint8_t emb_notify_bits_t;
#define EMB_NOTIFY_KERNEL_BITS 4
#define EMB_NOTIFY_BIT(n)      ((emb_notify_bits_t)((uint8_t)1u << (n)))
#elif CONFIG_EMB_NOTIFY_BITS == 16
typedef uint16_t emb_notify_bits_t;
#define EMB_NOTIFY_KERNEL_BITS 4
#define EMB_NOTIFY_BIT(n)      ((emb_notify_bits_t)((uint16_t)1u << (n)))
#else
typedef uint32_t emb_notify_bits_t;
#define EMB_NOTIFY_KERNEL_BITS 8
#define EMB_NOTIFY_BIT(n)      ((emb_notify_bits_t)(UINT32_C(1) << (n)))
#endif

#define EMB_NOTIFY_APP_BITS (EMB_NOTIFY_BITS - EMB_NOTIFY_KERNEL_BITS)
#define EMB_NOTIFY_APP_MASK ((emb_notify_bits_t)(EMB_NOTIFY_BIT(EMB_NOTIFY_APP_BITS) - 1u))
#define EMB_NOTIFY_K_WORK \
    EMB_NOTIFY_BIT(EMB_NOTIFY_BITS - 1) /* work queue thread: items available */
#define EMB_NOTIFY_K_FAULT \
    EMB_NOTIFY_BIT(EMB_NOTIFY_BITS - 2) /* supervisor: a partition fault is pending */
#define EMB_NOTIFY_K_BUDGET \
    EMB_NOTIFY_BIT(EMB_NOTIFY_BITS - 3) /* NOTIFY overrun policy (ADR-029) */

/* Wait modes (SPEC-006 §2.1). */
#define EMB_NOTIFY_ANY   ((uint8_t)0x00u)
#define EMB_NOTIFY_ALL   ((uint8_t)0x01u)
#define EMB_NOTIFY_CLEAR ((uint8_t)0x02u)

/**
 * emb_notify_set() - OR @bits into @thread's notification word; wake it if satisfied.
 * @thread: Target thread.
 * @bits:   Application bits only; a kernel bit is EMB_EINVAL.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_NOTIFY
 * @req      KRN-NOTIF-001 KRN-NOTIF-002 KRN-NOTIF-003
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EINVAL for a bad handle, zero bits, or a kernel bit;
 *         EMB_ESTATE for a TERMINATED thread (the bits are discarded).
 */
emb_status_t emb_notify_set(emb_thread_t thread, emb_notify_bits_t bits);

/**
 * emb_notify_wait() - Wait until the caller's word satisfies @mask in @mode.
 * @mask:     Bits of interest; zero is EMB_EINVAL.
 * @mode:     EMB_NOTIFY_ANY or EMB_NOTIFY_ALL, optionally | EMB_NOTIFY_CLEAR.
 * @timeout:  How long to wait.
 * @out_bits: Receives word & mask on EMB_OK; may be NULL.
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_NOTIFY
 * @req      KRN-NOTIF-001 KRN-NOTIF-003
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_ETIMEDOUT; EMB_ECANCELED; EMB_EINVAL.
 */
emb_status_t emb_notify_wait(emb_notify_bits_t mask, uint8_t mode, emb_timeout_t timeout,
                             emb_notify_bits_t *out_bits);

/**
 * emb_notify_wait_until() - Absolute-deadline form of emb_notify_wait().
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_NOTIFY
 * @req      KRN-NOTIF-001 API-024
 * @since    0.2
 * @stable   yes
 */
emb_status_t emb_notify_wait_until(emb_notify_bits_t mask, uint8_t mode, emb_instant_t deadline,
                                   emb_notify_bits_t *out_bits);

/**
 * emb_notify_get() - Snapshot of the caller's notification word.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_NOTIFY
 * @req      KRN-NOTIF-001
 * @since    0.2
 * @stable   yes
 */
emb_notify_bits_t emb_notify_get(void);

/**
 * emb_notify_clear() - Clear @bits in the caller's word.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_NOTIFY
 * @req      KRN-NOTIF-001
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK.
 */
emb_status_t emb_notify_clear(emb_notify_bits_t bits);

#ifdef __cplusplus
}
#endif

#endif /* EMB_NOTIFY_H */
