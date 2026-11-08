/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The debug descriptor (SPEC-015 §5, ADR-011, OBS-008, PORT-012, A107): a read-only table
 * every image exports at the symbol emb_debug_descriptor, so that a debugger or a memory
 * dump reader finds the kernel's threads, their states, priorities, stacks and owned
 * mutexes, and each switched-out thread's saved registers, without knowing the private
 * structures' layout.
 *
 * The format is the header below followed by `count` values, each `pointer_size` bytes in
 * the image's byte order: an address, a field offset, a size or a small constant, by the
 * EMB_DEBUG_* index. A value that does not apply to this build is EMB_DEBUG_ABSENT
 * (all ones). Indexes are append-only: a later kernel adds values at the end and raises
 * count, never moves or redefines one; `version` changes only if the header itself does.
 * tools/descriptor reads it (docs/engineering/debug-descriptor.md).
 */
#ifndef EMB_DEBUG_H
#define EMB_DEBUG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMB_DEBUG_MAGIC0  0x45u /* 'E' */
#define EMB_DEBUG_MAGIC1  0x4Du /* 'M' */
#define EMB_DEBUG_MAGIC2  0x42u /* 'B' */
#define EMB_DEBUG_MAGIC3  0x44u /* 'D' */
#define EMB_DEBUG_VERSION 1u
#define EMB_DEBUG_ABSENT  UINTPTR_MAX

/* header.flags: what this build keeps */
#define EMB_DEBUG_F_CHECKED      0x0001u /* CONFIG_EMB_CHECKED */
#define EMB_DEBUG_F_SCHED_TABLE  0x0002u /* the tiny scheduler: one thread per priority */
#define EMB_DEBUG_F_THREAD_LIST  0x0004u /* CONFIG_EMB_THREAD_LIST */
#define EMB_DEBUG_F_OBJ_NAMES    0x0008u /* CONFIG_EMB_OBJ_NAMES */
#define EMB_DEBUG_F_OBJ_LIST     0x0010u /* CONFIG_EMB_OBJ_LIST */
#define EMB_DEBUG_F_STACK_STATS  0x0020u /* CONFIG_EMB_STACK_STATS: stacks are filled */
#define EMB_DEBUG_F_STACK_CHECK  0x0040u /* CONFIG_EMB_STACK_CHECK: guard words */
#define EMB_DEBUG_F_INDEX_PACKED 0x0080u /* the thread index lives in obj.flags bits 5 to 7 */
#define EMB_DEBUG_F_IDLE_THREAD  0x0100u /* CONFIG_EMB_IDLE_THREAD */

/* header.endian */
#define EMB_DEBUG_LITTLE 0u
#define EMB_DEBUG_BIG    1u

/* EMB_DEBUG_FRAME_KIND: how a switched-out thread's registers lie from its saved sp */
#define EMB_DEBUG_KIND_NONE 0u /* not readable: native threads are host threads */
#define EMB_DEBUG_KIND_CORTEX_M                                                             \
    1u                          /* the table's words, then s16-s31 when EXC_RETURN bit 4 is \
                                   clear, then the exception's hardware frame */
#define EMB_DEBUG_KIND_TABLE 2u /* the table's words are the whole frame (RISC-V) */

/* EMB_DEBUG_FRAME_FLAGS for EMB_DEBUG_KIND_CORTEX_M */
#define EMB_DEBUG_FRAME_F_PSPLIM 0x1u /* the frame starts with the thread's PSPLIM (Armv8-M) */
#define EMB_DEBUG_FRAME_F_FPU    0x2u /* the core has an FPU: EXC_RETURN bit 4 may be clear */

/* A frame table entry: GDB's number of the register, and its word in the frame. Two
 * numbers name what is not a GDB register: EXC_RETURN, which the frame kind reads. */
#define EMB_DEBUG_REG_EXC_RETURN 0xFEu

/* The values, by index. Offsets are from the start of the structure they name. */
enum emb_debug_index {
    EMB_DEBUG_KERNEL_VERSION = 0, /* EMB_VERSION: major << 16 | minor << 8 | patch */
    EMB_DEBUG_BUILD_ID,           /* address of the 20-byte emb_build_id (A72) */
    EMB_DEBUG_PORT_NAME,          /* address of a string: "cortex-m", "riscv", ... */
    EMB_DEBUG_PORT_VARIANT,       /* address of a string: "armv7e-m", "rv32imac", ... */
    EMB_DEBUG_PRIORITY_COUNT,     /* CONFIG_EMB_PRIORITY_COUNT; 0 is the idle level */
    /* the per-CPU state (SPEC-002 §2) */
    EMB_DEBUG_CPU,             /* address */
    EMB_DEBUG_CPU_CURRENT,     /* offset of the running thread's pointer */
    EMB_DEBUG_CPU_IRQ_NESTING, /* offset, 1 byte */
    EMB_DEBUG_CPU_SCHED_LOCK,  /* offset, 1 byte */
    EMB_DEBUG_CPU_RESCHEDULE,  /* offset, 1 byte */
    EMB_DEBUG_CPU_STATE,       /* offset, 1 byte: 0 before init, 1 init, 2 running */
    /* where the threads are */
    EMB_DEBUG_THREAD_SIZE,        /* bytes of a thread control block */
    EMB_DEBUG_THREAD_LIST,        /* address of the all-threads list's sentinel node */
    EMB_DEBUG_THREAD_LIST_NODE,   /* offset of a thread's node in that list */
    EMB_DEBUG_THREAD_TABLE,       /* address of the table of thread pointers (tiny) */
    EMB_DEBUG_THREAD_TABLE_COUNT, /* its entries */
    EMB_DEBUG_IDLE_THREAD,        /* address of the idle context */
    /* a thread control block's fields; each 1 byte unless said */
    EMB_DEBUG_T_SP,          /* the saved stack pointer, pointer-sized */
    EMB_DEBUG_T_TYPE,        /* the object type tag: EMB_DEBUG_TYPE_BASE + n */
    EMB_DEBUG_T_STATE,       /* 0 uninitialized, 1 active, 2 being destroyed */
    EMB_DEBUG_T_FLAGS,       /* the object flags (the packed index's home) */
    EMB_DEBUG_T_NAME,        /* address of the name string, pointer-sized */
    EMB_DEBUG_T_BASE_PRIO,   /* the assigned priority */
    EMB_DEBUG_T_EFF_PRIO,    /* the effective priority, with inheritance or ceiling */
    EMB_DEBUG_T_WAIT_STATE,  /* 0 ready, 1 about to block, 2 blocked */
    EMB_DEBUG_T_WAIT_REASON, /* EMB_WAIT_REASON_* (<emb/thread.h>) */
    EMB_DEBUG_T_TFLAGS,      /* lifecycle bits: EMB_DEBUG_TF_* */
    EMB_DEBUG_T_INDEX,       /* the diagnostic index, ABSENT when packed in the flags */
    EMB_DEBUG_T_WAIT_QUEUE,  /* address of the queue it waits in, pointer-sized */
    EMB_DEBUG_T_DEADLINE,    /* the armed timeout's deadline, EMB_DEBUG_TICK_SIZE bytes */
    EMB_DEBUG_T_OWNED,       /* address of the first mutex it owns, pointer-sized */
    EMB_DEBUG_T_STACK_BASE,  /* the stack's lowest address, pointer-sized */
    EMB_DEBUG_T_STACK_SIZE,  /* its size, pointer-sized */
    EMB_DEBUG_T_NOTIFY_BITS, /* the pending notification bits, EMB_DEBUG_NOTIFY_SIZE bytes */
    EMB_DEBUG_TICK_SIZE,     /* bytes of emb_tick_t */
    EMB_DEBUG_NOTIFY_SIZE,   /* bytes of the notification word */
    /* a mutex's fields */
    EMB_DEBUG_M_NAME,       /* address of the name string, pointer-sized */
    EMB_DEBUG_M_OWNER,      /* address of the owning thread, pointer-sized */
    EMB_DEBUG_M_OWNED_NEXT, /* the next mutex its owner holds, pointer-sized */
    EMB_DEBUG_M_COUNT,      /* the recursion count, 1 byte */
    EMB_DEBUG_MUTEX_LIST,   /* address of the all-mutexes list's sentinel (CONFIG_EMB_OBJ_LIST) */
    EMB_DEBUG_MUTEX_LIST_NODE, /* offset of a mutex's node in it */
    /* object types */
    EMB_DEBUG_TYPE_BASE,  /* the first object type tag */
    EMB_DEBUG_TYPE_NAMES, /* address of an array of string addresses, one per tag */
    EMB_DEBUG_TYPE_COUNT, /* its entries */
    /* stacks */
    EMB_DEBUG_STACK_FILL,  /* the byte a filled stack holds where it was never used */
    EMB_DEBUG_STACK_GUARD, /* bytes of guard words at a thread stack's base */
    EMB_DEBUG_ISR_STACK,   /* address of the interrupt stack's lowest byte */
    EMB_DEBUG_ISR_STACK_SIZE,
    /* the port's switch frame (SPEC-011 §14) */
    EMB_DEBUG_FRAME_KIND,        /* EMB_DEBUG_KIND_* */
    EMB_DEBUG_FRAME_FLAGS,       /* EMB_DEBUG_FRAME_F_* */
    EMB_DEBUG_FRAME_SIZE,        /* bytes the table covers from the saved sp */
    EMB_DEBUG_FRAME_TABLE,       /* address of uint8_t pairs: GDB register number, word */
    EMB_DEBUG_FRAME_TABLE_COUNT, /* pairs */
    EMB_DEBUG_COUNT
};

/* EMB_DEBUG_T_TFLAGS */
#define EMB_DEBUG_TF_STARTED    0x01u
#define EMB_DEBUG_TF_TERMINATED 0x02u
#define EMB_DEBUG_TF_SUSPENDED  0x10u
#define EMB_DEBUG_TF_KERNEL     0x40u /* the idle or a work-queue thread */

typedef struct {
    uint8_t magic[4];     /* "EMBD" */
    uint16_t version;     /* EMB_DEBUG_VERSION */
    uint16_t count;       /* values that follow */
    uint8_t pointer_size; /* bytes of each value and of every address */
    uint8_t endian;       /* EMB_DEBUG_LITTLE or EMB_DEBUG_BIG */
    uint16_t reserved;
    uint32_t flags;                   /* EMB_DEBUG_F_* */
    uintptr_t value[EMB_DEBUG_COUNT]; /* at byte 16 */
} emb_debug_descriptor_t;

extern const emb_debug_descriptor_t emb_debug_descriptor;

#ifdef __cplusplus
}
#endif

#endif /* EMB_DEBUG_H */
