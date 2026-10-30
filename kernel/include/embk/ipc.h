/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Inter-thread communication objects (SPEC-007): message queues. */
#ifndef EMBK_IPC_H
#define EMBK_IPC_H

#include <emb/msgq.h>

#include <embk/binding.h>
#include <embk/kernel.h>

#if CONFIG_EMB_MSGQ
#if CONFIG_EMB_IPC_SMALL
typedef uint8_t embk_ipc_count_t; /* 8-bit counts on small targets (KRN-IPC-021) */
#else
typedef uint16_t embk_ipc_count_t;
#endif

typedef struct embk_msgq {
    embk_obj_t obj;              /* obj.flags: EMB_OBJ_ABORT_WAITERS | EMB_OBJ_FIFO */
    embk_wait_queue_t receivers; /* wait while empty; wake_data holds the out_item pointer */
    embk_wait_queue_t senders;   /* wait while full; wake_data holds the item pointer */
    unsigned char *ring;
    embk_ipc_count_t item_size;
    embk_ipc_count_t capacity;
    embk_ipc_count_t head;  /* slot of the next item received */
    embk_ipc_count_t count; /* items stored */
#if CONFIG_EMB_NOTIFY
    embk_binding_t binding; /* receive side: empty to nonempty, never on a hand-off */
#endif
#if CONFIG_EMB_OBJ_LIST
    embk_list_node_t all_node; /* in embk_msgq_all from init to destroy */
#endif
} embk_msgq_t;

#if CONFIG_EMB_OBJ_LIST
extern embk_list_t embk_msgq_all; /* every active message queue; lock: scheduler domain */
#endif
#endif

#endif /* EMBK_IPC_H */
