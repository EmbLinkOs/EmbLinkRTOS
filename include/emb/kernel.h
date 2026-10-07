/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Kernel initialization and start (SPEC-001 §13, SPEC-008 §4, SPEC-011 §4), the
 * generated initialization table of EMB_*_DEFINE objects (SPEC-001 §6.4), and
 * EMB_CHECK(). This header is an addition to the SPEC-001 §8 list.
 */
#ifndef EMB_KERNEL_H
#define EMB_KERNEL_H

#include <emb/fault.h>
#include <emb/status.h>
#include <emb/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * emb_kernel_init() - Initialize the kernel and run the initialization table.
 *
 * Must be the first kernel call; calls emb_arch_init(). Objects defined with
 * EMB_*_DEFINE are constructed here, in link order.
 *
 * @ctx      prekernel
 * @blocks   no
 * @time     O(init table)
 * @owns     none
 * @config
 * @req      KRN-THR-015
 * @since    0.2
 * @stable   yes
 */
void emb_kernel_init(void);

/**
 * emb_kernel_start() - Launch the first thread; never returns.
 *
 * Preemption point P3 (SPEC-002 §6.1). Threads started before this call run now,
 * highest priority first. The pre-kernel stack becomes the interrupt stack or the
 * idle context's stack per port (SPEC-008 §10).
 *
 * @ctx      prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-SCH-001
 * @since    0.2
 * @stable   yes
 */
EMB_NORETURN void emb_kernel_start(void);

/* ---- initialization table (SPEC-001 §6.4, SPEC-009) ---------------------------- */

typedef void (*emb_init_fn_t)(void);

typedef struct EMB_ALIGNED(sizeof(void *)) emb_init_entry {
    emb_init_fn_t fn; /* one pointer: the table walks by sizeof, which equals the alignment */
} emb_init_entry_t;

/* Registers @fn to run from emb_kernel_init(). The table is the bracketed section
 * emb_init_table that embld and GNU ld both provide (09 §7). MISRA Dev CS-12: rule
 * 20.7, `fn` is a token. */
#define EMB_INIT_TABLE_ENTRY(fn) \
    static const emb_init_entry_t fn##_init_entry_ EMB_USED EMB_SECTION("emb_init_table") = {fn}

/* ---- EMB_CHECK --------------------------------------------------------------- */

/**
 * EMB_CHECK() - Fault on a status other than EMB_OK in checked builds.
 *
 * In a release build the macro is the expression's value. SPEC-001 §13.
 */
#if CONFIG_EMB_CHECKED
#define EMB_CHECK(expr) emb_check_status((expr), #expr)
#else
#define EMB_CHECK(expr) (expr)
#endif

/**
 * emb_check_status() - The function behind EMB_CHECK().
 * @status: The value to check.
 * @what:   The checked expression, for the crash record.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_CHECKED
 * @req      API-016
 * @since    0.2
 * @stable   yes
 *
 * Return: @status when it is EMB_OK; otherwise raises EMB_FAULT_CHECK and does not return.
 */
emb_status_t emb_check_status(emb_status_t status, const char *what);

#ifdef __cplusplus
}
#endif

#endif /* EMB_KERNEL_H */
