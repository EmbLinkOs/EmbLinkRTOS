/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Thread lifecycle (SPEC-008), sleep and yield (SPEC-003 §6), priorities
 * (SPEC-005 §2). Threads live in caller-provided storage, are joinable by default,
 * end only by their own action, and keep their object until joined, detached, or
 * destroyed (KRN-THR-006).
 */
#ifndef EMB_THREAD_H
#define EMB_THREAD_H

#include <emb/kernel.h>
#include <emb/status.h>
#include <emb/time.h>
#include <emb/types.h>

#ifdef __cplusplus
extern "C" {
#endif

EMB_DECLARE_HANDLE(emb_thread_t);

/* Defined by the generated <emb/storage.h>; an incomplete type is enough for the
 * prototypes, and EMB_*_DEFINE users include <emb/emb.h>. */
typedef union emb_thread_storage emb_thread_storage_t;

typedef void (*emb_thread_entry_t)(void *arg); /* returning terminates the thread (KRN-THR-005) */

/* Attribute flags (SPEC-008 §2). */
#define EMB_THREAD_DETACHED        ((uint8_t)0x01u) /* no exit code kept; destroyable at exit */
#define EMB_THREAD_CRITICAL        ((uint8_t)0x02u) /* ADR-029; reserved for temporal protection */
#define EMB_THREAD_CANCEL_DISABLED ((uint8_t)0x04u) /* start with the cancel-disable count at 1 */
#define EMB_THREAD_NO_AUTOSTART    ((uint8_t)0x80u) /* EMB_THREAD_DEFINE only: no autostart */

typedef struct emb_thread_attr {
    const char *name;     /* kept by pointer; NULL allowed */
    void *stack;          /* required; aligned to EMB_STACK_ALIGN */
    size_t stack_size;    /* >= EMB_THREAD_STACK_MIN */
    uint8_t priority;     /* 1 .. EMB_PRIORITY_MAX */
    uint8_t flags;        /* EMB_THREAD_* */
    uint8_t cpu_affinity; /* SMP (FUTURE): bit mask; 0 = any */
    uint8_t reserved_;    /* explicit padding */
} emb_thread_attr_t;

/* Thread states and wait reasons reported by emb_thread_state() (03 §1.1). */
#define EMB_THREAD_INACTIVE   ((uint8_t)0u)
#define EMB_THREAD_READY      ((uint8_t)1u)
#define EMB_THREAD_RUNNING    ((uint8_t)2u)
#define EMB_THREAD_BLOCKED    ((uint8_t)3u)
#define EMB_THREAD_TERMINATED ((uint8_t)4u)

#define EMB_WAIT_REASON_NONE    ((uint8_t)0u)
#define EMB_WAIT_REASON_START   ((uint8_t)1u)
#define EMB_WAIT_REASON_SUSPEND ((uint8_t)2u)
#define EMB_WAIT_REASON_SLEEP   ((uint8_t)3u)
#define EMB_WAIT_REASON_OBJECT  ((uint8_t)4u)
#define EMB_WAIT_REASON_JOIN    ((uint8_t)5u)
#define EMB_WAIT_REASON_NOTIFY  ((uint8_t)6u)

#define EMB_STACK_ALIGN      CONFIG_EMB_ARCH_STACK_ALIGN
#define EMB_THREAD_STACK_MIN ((size_t)CONFIG_EMB_ARCH_STACK_MIN)
#define EMB_STACK_FILL       ((uint8_t)0xEBu)

/* Declares an aligned stack array of at least `size` bytes (SPEC-008 §10). MISRA Dev
 * CS-12: rule 20.7, `name` is a token. */
#define EMB_THREAD_STACK(name, size)    \
    static EMB_ALIGNED(EMB_STACK_ALIGN) \
        uint8_t name[((size) + (EMB_STACK_ALIGN) - 1u) & ~(size_t)((EMB_STACK_ALIGN) - 1u)]

/**
 * emb_thread_attr_default() - Fill @out_attr with the defaults.
 * @out_attr: name NULL, stack NULL, priority 1, flags 0.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      API-020
 * @since    0.2
 * @stable   yes
 */
void emb_thread_attr_default(emb_thread_attr_t *out_attr);

/**
 * emb_thread_init() - Construct a thread in caller-provided storage; it does not run yet.
 * @storage:    Storage for the control block, EMB_THREAD_STORAGE_ALIGN aligned.
 * @attr:       Attributes; stack and stack_size are required.
 * @entry:      The thread function.
 * @arg:        Passed to @entry.
 * @out_thread: The handle; EMB_HANDLE_NULL on failure.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     bounded scan of at most 254 IDs + stack fill when configured
 * @owns     borrows:storage borrows:attr->stack
 * @config
 * @req      API-021 KRN-THR-004 KRN-THR-007 KRN-THR-015 KRN-THR-023
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EINVAL for a NULL storage, entry, stack, a priority of 0 or
 *         above EMB_PRIORITY_MAX, a misaligned or undersized stack (misuse, SPEC-008
 *         §12); EMB_EBUSY when @storage still holds a thread that was not destroyed
 *         (misuse: storage is reused only through emb_thread_destroy()) or, on tiny,
 *         when the priority slot is taken; EMB_ENOMEM when
 *         all 254 list-scheduler diagnostic IDs are reserved. Admission failure leaves
 *         construction storage untouched.
 */
emb_status_t emb_thread_init(emb_thread_storage_t *storage, const emb_thread_attr_t *attr,
                             emb_thread_entry_t entry, void *arg, emb_thread_t *out_thread);

/**
 * emb_thread_start() - Make an INACTIVE thread READY.
 * @thread: The thread.
 *
 * @ctx      prekernel thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-THR-016 KRN-THR-009
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_ESTATE when the thread is not INACTIVE; EMB_EINVAL for a bad handle.
 */
emb_status_t emb_thread_start(emb_thread_t thread);

/**
 * emb_thread_self() - The calling thread.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-THR-008
 * @since    0.2
 * @stable   yes
 */
emb_thread_t emb_thread_self(void);

/**
 * emb_thread_exit() - End the calling thread with @code (SPEC-008 §5).
 * @code: Delivered to the joiner; ignored for a detached thread.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(owned mutexes)
 * @owns     none
 * @config
 * @req      KRN-THR-005 KRN-THR-006 KRN-THR-017
 * @since    0.2
 * @stable   yes
 */
EMB_NORETURN void emb_thread_exit(int code);

/**
 * emb_thread_join() - Wait for @thread to terminate and collect its exit code.
 * @thread:   A joinable thread that is not the caller.
 * @timeout: How long to wait.
 * @out_code: Receives the exit code on EMB_OK; may be NULL.
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(1) state check; O(timeouts) finite timeout arm
 * @owns     none
 * @config
 * @req      KRN-THR-006 KRN-THR-012 KRN-THR-018
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_ETIMEDOUT; EMB_ECANCELED; EMB_EBUSY for a second joiner;
 *         EMB_EINVAL for a detached/invalid thread; EMB_EDEADLK for self.
 */
emb_status_t emb_thread_join(emb_thread_t thread, emb_timeout_t timeout, int *out_code);

/**
 * emb_thread_detach() - Make @thread detached; it can be destroyed as soon as it exits.
 * @thread: Joinable initialized thread with no blocked joiner.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-THR-019
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EBUSY when a joiner is blocked on it.
 */
emb_status_t emb_thread_detach(emb_thread_t thread);

/**
 * emb_thread_suspend() - Keep @thread from running until emb_thread_resume().
 * @thread: Started application thread; suspending self delays return until resumed.
 *
 * An overlay (SPEC-004 §6.5): a blocked thread keeps its queue position and its
 * deadline. Idempotent.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-THR-010 KRN-THR-020
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EPERM for a kernel thread; EMB_ESTATE for an INACTIVE or TERMINATED thread.
 */
emb_status_t emb_thread_suspend(emb_thread_t thread);

/**
 * emb_thread_resume() - Undo emb_thread_suspend(). Idempotent.
 * @thread: Started thread whose suspend overlay is to be removed.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-THR-010 KRN-THR-020
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_ESTATE for an INACTIVE or TERMINATED thread.
 */
emb_status_t emb_thread_resume(emb_thread_t thread);

/**
 * emb_thread_cancel() - Request cancellation of @thread (SPEC-008 §7).
 * @thread: Started application thread to request cancellation of; storage is not destroyed.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-THR-011 KRN-THR-021
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EPERM for a kernel thread; EMB_ESTATE for INACTIVE or TERMINATED.
 */
emb_status_t emb_thread_cancel(emb_thread_t thread);

/**
 * emb_thread_cancel_point() - Deliver a pending cancel request to the caller.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-THR-011 KRN-THR-021
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_ECANCELED when a request was pending and enabled, else EMB_OK.
 */
emb_status_t emb_thread_cancel_point(void);

/**
 * emb_thread_cancel_disable() - Increment the caller's cancel-disable count.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-THR-021
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EOVERFLOW at 255.
 */
emb_status_t emb_thread_cancel_disable(void);

/**
 * emb_thread_cancel_enable() - Decrement the caller's cancel-disable count.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-THR-021
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_ESTATE when the count is already zero.
 */
emb_status_t emb_thread_cancel_enable(void);

/**
 * emb_thread_set_priority() - Change the base priority of @thread (SPEC-005 §10).
 * @thread: Initialized application thread whose base priority is to change.
 * @priority: 1 .. EMB_PRIORITY_MAX.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(PI work), plus waiter requeue and profile-specific scheduling
 * @owns     none
 * @config
 * @req      KRN-THR-022 KRN-SYNC-032
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EINVAL; EMB_EPERM for a kernel thread; EMB_EBUSY on the tiny
 *         profile when the new priority's slot is taken.
 */
emb_status_t emb_thread_set_priority(emb_thread_t thread, uint8_t priority);

/**
 * emb_thread_get_priority() - Base priority of @thread; 0 for a bad handle.
 * @thread: Initialized thread to inspect; returns 0 for an invalid handle.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-THR-022
 * @since    0.2
 * @stable   yes
 */
uint8_t emb_thread_get_priority(emb_thread_t thread);

/**
 * emb_thread_get_effective_priority() - Effective priority of @thread (SPEC-005 §2.1).
 * @thread: Initialized thread to inspect; returns 0 for an invalid handle.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-THR-022 KRN-SYNC-018
 * @since    0.2
 * @stable   yes
 */
uint8_t emb_thread_get_effective_priority(emb_thread_t thread);

/**
 * emb_thread_yield() - Move the caller behind equal-priority READY threads.
 *
 * Never yields to a lower-priority thread (KRN-TIM-024). A no-op that keeps the
 * reschedule pending while the scheduler is locked.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-TIM-024 KRN-IRQ-022
 * @since    0.2
 * @stable   yes
 */
void emb_thread_yield(void);

/**
 * emb_thread_sleep() - Block the caller for @d (SPEC-003 §6).
 * @d: Zero behaves as emb_thread_yield().
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(timeouts) arm
 * @owns     none
 * @config
 * @req      KRN-TIM-024
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK on expiry; EMB_ECANCELED.
 */
emb_status_t emb_thread_sleep(emb_duration_t d);

/**
 * emb_thread_sleep_until() - Block the caller until the instant @t.
 * @t: A past instant behaves as emb_thread_yield().
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(timeouts) arm
 * @owns     none
 * @config
 * @req      KRN-TIM-011 KRN-TIM-022
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_ECANCELED. In the 32-bit profile a deadline more than
 *         EMB_TIMEOUT_MAX_TICKS ahead is indistinguishable from a past one and yields.
 */
emb_status_t emb_thread_sleep_until(emb_instant_t t);

/**
 * emb_thread_state() - Lifecycle state and, when BLOCKED, the wait reason.
 * @thread: Initialized thread to inspect.
 * @out_state:  EMB_THREAD_INACTIVE .. EMB_THREAD_TERMINATED.
 * @out_reason: EMB_WAIT_REASON_*; may be NULL.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-THR-025
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EINVAL.
 */
emb_status_t emb_thread_state(emb_thread_t thread, uint8_t *out_state, uint8_t *out_reason);

/**
 * emb_thread_name() - The name given at init, or "" when none or names are compiled out.
 * @thread: Initialized thread whose borrowed name is returned; invalid handles return an empty
 * string.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-THR-025
 * @since    0.2
 * @stable   yes
 */
const char *emb_thread_name(emb_thread_t thread);

/**
 * emb_thread_index() - Stable diagnostic index; 0xFF for a bad handle.
 * @thread: Initialized thread to inspect. Stable until successful destroy, including
 *          after exit and priority changes. Unique among live thread objects;
 *          reusable after destroy, so not a generation-tagged handle. Idle uses 0xFE.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-THR-025 KRN-THR-029
 * @since    0.2
 * @stable   yes
 */
uint8_t emb_thread_index(emb_thread_t thread);

/**
 * emb_thread_stack_info() - Configured size and high-water mark of @thread's stack.
 * @thread: Initialized thread to inspect.
 * @out_size:       Configured size in bytes; may be NULL.
 * @out_high_water: Fill-pattern estimate of bytes used; may be NULL, 0 when stats are off.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(stack) scan with statistics on
 * @owns     none
 * @config
 * @req      KRN-THR-023 KRN-THR-025
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EINVAL.
 */
emb_status_t emb_thread_stack_info(emb_thread_t thread, size_t *out_size, size_t *out_high_water);

#if CONFIG_EMB_TLS_SLOTS > 0
/**
 * emb_tls_set() - Store @p in the caller's TLS slot @slot (SPEC-008 §9).
 * @slot: Slot index, 0 through CONFIG_EMB_TLS_SLOTS - 1.
 * @p: Pointer to retain in that slot; NULL clears it. Pointee lifetime stays with the caller.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_TLS_SLOTS
 * @req      KRN-THR-013
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EINVAL for a slot out of range.
 */
emb_status_t emb_tls_set(uint8_t slot, void *p);
/**
 * emb_tls_get() - Read a pointer from the calling thread TLS slot.
 * @slot: Slot index, 0 through CONFIG_EMB_TLS_SLOTS - 1.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_TLS_SLOTS
 * @req      KRN-THR-013
 * @since    0.2
 * @stable   yes
 *
 * Return: the stored pointer, or NULL for an unset/out-of-range slot or a
 * caller outside thread context. No allocation, copying or cleanup occurs.
 */
void *emb_tls_get(uint8_t slot);
#endif

#if CONFIG_EMB_THREAD_LIST
typedef void (*emb_thread_visit_t)(emb_thread_t thread, void *arg);

/**
 * emb_thread_foreach() - Call @cb for every thread object, under the scheduler lock.
 * @cb: Called under the scheduler lock; must not block or modify the thread registry. NULL is a
 * no-op.
 * @arg: Passed unchanged to each callback; needed only for the duration of traversal.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(threads) plus callback costs and any reschedule at final unlock
 * @owns     none
 * @config   CONFIG_EMB_THREAD_LIST
 * @req      KRN-THR-025
 * @since    0.2
 * @stable   yes
 */
void emb_thread_foreach(emb_thread_visit_t cb, void *arg);
#endif

/**
 * emb_thread_destroy() - Return the storage of an INACTIVE or joined/detached TERMINATED thread.
 * @thread: INACTIVE or joined/detached TERMINATED application thread; successful destruction
 * permits storage reuse.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     gives:storage
 * @config
 * @req      KRN-OBJ-001 KRN-THR-024
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EBUSY (misuse) for any other state; EMB_EINVAL.
 */
emb_status_t emb_thread_destroy(emb_thread_t thread);

/* EMB_THREAD_DEFINE(sym, entry, arg, priority, stack_size, flags): storage, stack,
 * the handle variable `sym`, and an init-table entry that constructs the thread in
 * emb_kernel_init() and starts it unless EMB_THREAD_NO_AUTOSTART is in flags
 * (SPEC-008 §4). MISRA Dev CS-12: rule 20.7, `sym` is a token. */
#define EMB_THREAD_DEFINE(sym, entry, arg, priority_, stack_size_, flags_)                     \
    static emb_thread_storage_t sym##_storage_;                                                \
    EMB_THREAD_STACK(sym##_stack_, (stack_size_));                                             \
    emb_thread_t sym;                                                                          \
    static void sym##_init_(void)                                                              \
    {                                                                                          \
        emb_thread_attr_t attr_;                                                               \
        emb_thread_attr_default(&attr_);                                                       \
        attr_.name = #sym;                                                                     \
        attr_.stack = sym##_stack_;                                                            \
        attr_.stack_size = sizeof(sym##_stack_);                                               \
        attr_.priority = (uint8_t)(priority_);                                                 \
        attr_.flags = (uint8_t)((flags_) & (uint8_t)~EMB_THREAD_NO_AUTOSTART);                 \
        (void)emb_check_status(emb_thread_init(&sym##_storage_, &attr_, (entry), (arg), &sym), \
                               "EMB_THREAD_DEFINE(" #sym ")");                                 \
        if (((flags_) & EMB_THREAD_NO_AUTOSTART) == 0u) {                                      \
            (void)emb_check_status(emb_thread_start(sym), "EMB_THREAD_DEFINE(" #sym ")");      \
        }                                                                                      \
    }                                                                                          \
    EMB_INIT_TABLE_ENTRY(sym##_init_)

#ifdef __cplusplus
}
#endif

#endif /* EMB_THREAD_H */
