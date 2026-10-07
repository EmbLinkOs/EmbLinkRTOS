/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Execution contexts (SPEC-001 §5.1, SPEC-002 §2.1). */
#ifndef EMB_CONTEXT_H
#define EMB_CONTEXT_H

#include <emb/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t emb_context_t;

enum emb_context_kind {
    EMB_CONTEXT_THREAD = 0,   /* on a thread stack after the scheduler started */
    EMB_CONTEXT_ISR = 1,      /* kernel-aware interrupt or exception handler */
    EMB_CONTEXT_PREKERNEL = 2 /* before emb_kernel_start() */
};

/**
 * emb_context() - The current execution context.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      API-012 KRN-IRQ-012
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_CONTEXT_THREAD, EMB_CONTEXT_ISR, or EMB_CONTEXT_PREKERNEL. Not
 *         meaningful inside a kernel-independent interrupt (SPEC-002 §3.2).
 */
emb_context_t emb_context(void);

/**
 * emb_in_isr() - Is the caller in interrupt context?
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      API-012
 * @since    0.2
 * @stable   yes
 */
bool emb_in_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* EMB_CONTEXT_H */
