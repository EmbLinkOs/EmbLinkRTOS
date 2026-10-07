/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Kernel-private core: the thread control block, per-CPU state, lock domains,
 * misuse and invariant macros, and trace macros. Every kernel module includes this.
 *
 * Lock domains (SPEC-004 §8): on a uniprocessor every domain (object lock, scheduler
 * domain, timeout lock) is the one critical section. The names are kept distinct so
 * the SMP work can split them without touching the callers.
 */
#ifndef EMBK_KERNEL_H
#define EMBK_KERNEL_H

#include <emb/arch.h>
#include <emb/config.h>
#include <emb/context.h>
#include <emb/fault.h>
#include <emb/irq.h>
#include <emb/mutex.h>
#include <emb/notify.h>
#include <emb/sched.h>
#include <emb/status.h>
#include <emb/thread.h>
#include <emb/time.h>
#include <emb/trace.h>
#include <emb/types.h>

#include <embk/list.h>
#include <embk/word.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- object header (SPEC-009) ------------------------------------------------- */

#define EMBK_OBJ_THREAD ((uint8_t)0x51u)
#define EMBK_OBJ_SEM    ((uint8_t)0x52u)
#define EMBK_OBJ_MUTEX  ((uint8_t)0x53u)

#define EMBK_OBJ_UNINIT     ((uint8_t)0u)
#define EMBK_OBJ_ACTIVE     ((uint8_t)1u)
#define EMBK_OBJ_DESTROYING ((uint8_t)2u)

typedef struct embk_obj {
    uint8_t type;  /* EMBK_OBJ_*; a type tag the handle check reads */
    uint8_t state; /* EMBK_OBJ_UNINIT | ACTIVE | DESTROYING */
    uint8_t flags; /* EMB_OBJ_ABORT_WAITERS | EMB_OBJ_FIFO | type-specific low bits */
    uint8_t reserved_;
#if CONFIG_EMB_OBJ_NAMES
    const char *name;
#endif
} embk_obj_t;

/* ---- wait queue (SPEC-004 §2.2, §11) ------------------------------------------- */

#define EMBK_WAIT_FIFO    ((uint8_t)0x01u) /* arrival order instead of priority order */
#define EMBK_WAIT_INHERIT ((uint8_t)0x02u) /* the queue of an INHERIT mutex (SPEC-005 §3.3) */
#define EMBK_WAIT_SELF    ((uint8_t)0x04u) /* a thread's pseudo-queue: sleep and notify */
#define EMBK_WAIT_JOIN    ((uint8_t)0x08u) /* a thread's join slot */

typedef struct embk_wait_queue {
#if CONFIG_EMB_SCHED_TABLE
    embk_word_t
        set; /* bit i: the thread in table slot i waits here; the order is the priority order */
#else
    embk_list_t waiters; /* ordered by policy */
#endif
    uint8_t flags;
} embk_wait_queue_t;

/* ---- timeout node (SPEC-003 §5.1) ---------------------------------------------- */

#if CONFIG_EMB_SCHED_TABLE
typedef uint8_t embk_wait_gen_t;
#else
typedef uint16_t embk_wait_gen_t;
#endif

typedef struct embk_timeout {
    embk_list_node_t node; /* in the timeout list when armed; next == NULL otherwise */
    emb_tick_t deadline;
    embk_wait_gen_t gen; /* the wait generation this deadline belongs to */
} embk_timeout_t;

/* ---- thread control block (SPEC-004 §2.1, SPEC-005 §2.3, SPEC-008) --------------- */

/* wait_state */
#define EMBK_WAIT_READY           ((uint8_t)0u)
#define EMBK_WAIT_INTEND_TO_BLOCK ((uint8_t)1u)
#define EMBK_WAIT_BLOCKED         ((uint8_t)2u)

/* wake results (SPEC-004 §2.3) */
typedef uint8_t embk_wait_result_t;
#define EMBK_WAKE_SATISFIED   ((embk_wait_result_t)0u)
#define EMBK_WAKE_TIMEOUT     ((embk_wait_result_t)1u)
#define EMBK_WAKE_CANCELED    ((embk_wait_result_t)2u)
#define EMBK_WAKE_DESTROYED   ((embk_wait_result_t)3u)
#define EMBK_WAKE_STALE       ((embk_wait_result_t)4u)
#define EMBK_WAKE_INTERRUPTED ((embk_wait_result_t)5u)
#define EMBK_WAKE_BUDGET      ((embk_wait_result_t)6u)
#define EMBK_WAKE_NONE        ((embk_wait_result_t)0xFFu)

/* tflags: lifecycle and overlays */
#define EMBK_THREAD_STARTED        ((uint8_t)0x01u)
#define EMBK_THREAD_TERMINATED     ((uint8_t)0x02u)
#define EMBK_THREAD_DETACHED       ((uint8_t)0x04u)
#define EMBK_THREAD_JOINED         ((uint8_t)0x08u) /* exit code collected: storage reusable */
#define EMBK_THREAD_SUSPENDED      ((uint8_t)0x10u)
#define EMBK_THREAD_CANCEL_PENDING ((uint8_t)0x20u)
#define EMBK_THREAD_KERNEL         ((uint8_t)0x40u) /* idle or work-queue thread */
#define EMBK_THREAD_CRITICAL       ((uint8_t)0x80u)

struct embk_mutex;

struct embk_thread {
    emb_arch_tcb_t arch; /* first: the debug descriptor and the ports read ->arch.sp */
    embk_obj_t obj;
    uint8_t base_prio;
    uint8_t eff_prio; /* the one every queue and the scheduler read */
    uint8_t wait_state;
    uint8_t wait_reason; /* EMB_WAIT_REASON_* */
    uint8_t tflags;      /* EMBK_THREAD_* */
    uint8_t cancel_disable;
    embk_wait_result_t wake_result;
#if CONFIG_EMB_CHECKED
    uint8_t lock_depth; /* the critical-section depth of this thread's context while switched out */
#endif
    embk_wait_gen_t wait_gen;
    uintptr_t wake_data;
    embk_wait_queue_t *wait_queue; /* the queue the thread waits in, or NULL */
#if !CONFIG_EMB_SCHED_TABLE
    embk_list_node_t wait_node;
    embk_list_node_t ready_node;
    uint8_t index; /* stable small integer (SPEC-008 §3); the table slot on tiny */
#endif
    embk_timeout_t timeout;
    embk_wait_queue_t self_q; /* pseudo-queue for SLEEP and NOTIFY (SPEC-004 §5.3) */
    embk_wait_queue_t join_q; /* the one join slot (SPEC-008 §6) */
#if CONFIG_EMB_NOTIFY
    emb_notify_bits_t notify_bits;
    emb_notify_bits_t notify_mask;
    uint8_t notify_mode;
#endif
#if CONFIG_EMB_MUTEX
    struct embk_mutex *owned; /* singly linked in acquisition order (SPEC-005 §2.3) */
#endif
    int exit_code;
    uint8_t *stack_base; /* lowest address; the guard words live here */
    size_t stack_size;
#if CONFIG_EMB_THREAD_LIST
    embk_list_node_t all_node;
#endif
#if CONFIG_EMB_TLS_SLOTS > 0
    void *tls[CONFIG_EMB_TLS_SLOTS];
#endif
};

#define EMBK_STACK_GUARD_BYTES 8u

static EMB_INLINE uint8_t embk_thread_index(const embk_thread_t *t)
{
#if CONFIG_EMB_SCHED_TABLE
    return t->base_prio;
#else
    return t->index;
#endif
}

/* ---- per-CPU state (SPEC-002 §2) ---------------------------------------------- */

#define EMBK_KERNEL_UNINIT  ((uint8_t)0u)
#define EMBK_KERNEL_INIT    ((uint8_t)1u)
#define EMBK_KERNEL_RUNNING ((uint8_t)2u)

typedef struct embk_cpu {
    embk_thread_t *current;    /* the running thread; the idle context when nothing is ready */
    uint8_t irq_nesting_depth; /* 0 in thread context */
    uint8_t sched_lock_depth;
    uint8_t reschedule_pending;
    uint8_t irq_lock_depth; /* checked builds: critical-section nesting */
    uint8_t kernel_state;   /* EMBK_KERNEL_* */
    uint8_t reserved_[3];
} embk_cpu_t;

extern embk_cpu_t embk_cpu; /* lock domain: the critical section; reads of single bytes are safe */

/* ---- lock domains -------------------------------------------------------------- */

static EMB_ALWAYS_INLINE emb_irq_key_t embk_irq_lock(void)
{
    emb_irq_key_t key = emb_arch_irq_lock();
#if CONFIG_EMB_CHECKED
    embk_cpu.irq_lock_depth++;
#endif
    return key;
}

static EMB_ALWAYS_INLINE void embk_irq_unlock(emb_irq_key_t key)
{
#if CONFIG_EMB_CHECKED
    embk_cpu.irq_lock_depth--;
#endif
    emb_arch_irq_unlock(key);
}

/* The three domains of SPEC-004 §8, one critical section each on a uniprocessor. */
#define embk_lock_object(obj)        embk_irq_lock()
#define embk_unlock_object(obj, key) embk_irq_unlock(key)
#define embk_lock_sched()            embk_irq_lock()
#define embk_unlock_sched(key)       embk_irq_unlock(key)
#define embk_lock_timeout()          embk_irq_lock()
#define embk_unlock_timeout(key)     embk_irq_unlock(key)

/* ---- faults, misuse, invariants (SPEC-001 §5.3) ------------------------------- */

EMB_NORETURN void embk_fault_raise(uint8_t fault_class, uint16_t code, uint8_t argument,
                                   const char *where);

/* A misuse: a fault in checked builds, the status in release builds. Used only in a
 * function returning emb_status_t. */
#if CONFIG_EMB_CHECKED
#define EMBK_MISUSE(fault_class, status, argument)                                    \
    do {                                                                              \
        embk_fault_raise((fault_class), (uint16_t)(-(status)), (argument), __func__); \
    } while (0)
#else
#define EMBK_MISUSE(fault_class, status, argument) \
    do {                                           \
        return (status);                           \
    } while (0)
#endif

#define EMBK_REQUIRE(cond, fault_class, status, argument)     \
    do {                                                      \
        if (EMB_UNLIKELY(!(cond))) {                          \
            EMBK_MISUSE((fault_class), (status), (argument)); \
        }                                                     \
    } while (0)

/* Kernel invariants: checked builds only, never with side effects in the condition. */
#if CONFIG_EMB_CHECKED
#define EMBK_ASSERT(cond)                                                                   \
    do {                                                                                    \
        if (EMB_UNLIKELY(!(cond))) {                                                        \
            embk_fault_raise(EMB_FAULT_KERNEL_INVARIANT, (uint16_t)__LINE__, 0u, __func__); \
        }                                                                                   \
    } while (0)
#else
#define EMBK_ASSERT(cond) \
    do {                  \
    } while (0)
#endif

/* Context predicates (SPEC-002 §2.1). */
static EMB_INLINE bool embk_in_isr(void)
{
    return embk_cpu.irq_nesting_depth != 0u;
}

static EMB_INLINE bool embk_in_thread(void)
{
    return embk_cpu.kernel_state == EMBK_KERNEL_RUNNING && embk_cpu.irq_nesting_depth == 0u;
}

/* Thread-only function entry. */
#define EMBK_REQUIRE_THREAD() EMBK_REQUIRE(embk_in_thread(), EMB_FAULT_API_CONTEXT, EMB_EPERM, 0u)
/* prekernel or thread. */
#define EMBK_REQUIRE_NOT_ISR() EMBK_REQUIRE(!embk_in_isr(), EMB_FAULT_API_CONTEXT, EMB_EPERM, 0u)
/* A blocking call with a nonzero timeout: thread context, scheduler not locked, no
 * critical section held by the caller (SPEC-001 §5.1, SPEC-002 §12). */
#define EMBK_REQUIRE_CAN_BLOCK(timeout_ticks)                                   \
    do {                                                                        \
        if ((timeout_ticks) != 0u) {                                            \
            EMBK_REQUIRE(embk_in_thread() && embk_cpu.sched_lock_depth == 0u && \
                             !emb_arch_irq_locked(),                            \
                         EMB_FAULT_API_CONTEXT, EMB_EPERM, 0u);                 \
        }                                                                       \
    } while (0)

/* ---- trace (SPEC-015 M1 subset) ------------------------------------------------ */

#if CONFIG_EMB_TRACE
void embk_trace_emit(uint16_t id, uintptr_t a0, uintptr_t a1, uintptr_t a2);
#define EMBK_TRACE(id, a0, a1, a2) \
    embk_trace_emit((uint16_t)(id), (uintptr_t)(a0), (uintptr_t)(a1), (uintptr_t)(a2))
#else
#define EMBK_TRACE(id, a0, a1, a2) \
    do {                           \
    } while (0)
#endif

/* ---- handles (POINTER model, SPEC-009) ---------------------------------------- */

/* Returns the object behind a handle after checking its type tag and state, or NULL. */
static EMB_INLINE void *embk_obj_from_raw(uintptr_t raw, uint8_t type, size_t obj_offset)
{
    const embk_obj_t *o;
    if (raw == 0u) {
        return NULL;
    }
    o = (const embk_obj_t *)(const void *)((const unsigned char *)(const void *)raw +
                                           obj_offset); /* MISRA Dev CS-12: 11.4 */
    if (o->type != type || o->state != EMBK_OBJ_ACTIVE) {
        return NULL;
    }
    return (void *)raw; /* MISRA Dev CS-12: 11.4, the handle boundary */
}

static EMB_INLINE uintptr_t embk_obj_to_raw(const void *obj)
{
    return (uintptr_t)obj; /* MISRA Dev CS-12: 11.4, the handle boundary */
}

static EMB_INLINE embk_thread_t *embk_thread_from_handle(emb_thread_t h)
{
    return (embk_thread_t *)embk_obj_from_raw(h.raw, EMBK_OBJ_THREAD, offsetof(embk_thread_t, obj));
}

static EMB_INLINE emb_thread_t embk_thread_handle(const embk_thread_t *t)
{
    emb_thread_t h;
    h.raw = embk_obj_to_raw(t);
    return h;
}

/* ---- utilities ------------------------------------------------------------------ */

#define EMBK_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static EMB_INLINE void embk_obj_init(embk_obj_t *o, uint8_t type, uint8_t flags, const char *name)
{
    o->type = type;
    o->state = EMBK_OBJ_ACTIVE;
    o->flags = flags;
    o->reserved_ = 0u;
#if CONFIG_EMB_OBJ_NAMES
    o->name = name;
#else
    (void)name;
#endif
}

#endif /* EMBK_KERNEL_H */
