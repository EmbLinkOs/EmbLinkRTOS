/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    crash.h
 * @brief   The crash record (SPEC-015 §4, OBS-016, A71 to A73).
 *
 * @details
 * A kernel fault writes a record of itself into memory the port's reset leaves alone, before the
 * fault hook and the fault action; the next boot validates it (magic, version, length, CRC-32) and
 * these calls return or discard it. The record's bytes (raw) are the wire format tools/crashdec
 * decodes: version 1 on 32-bit and native ports, the small version 2 on AVR.
 *
 * @see     kernel/fault/crash.c
 * @author  Junior Deogracias
 * @date    2026-11-04 (created)
 */
#ifndef EMB_CRASH_H
#define EMB_CRASH_H

#include <emb/status.h>
#include <emb/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMB_CRASH_VERSION_FULL  1u /* 32-bit and native ports: 92 bytes and 4 per register */
#define EMB_CRASH_VERSION_SMALL 2u /* 16-bit pointers (AVR): 34 bytes */

/* What the kernel did after writing the record (emb_crash_record_t.reset_reason). */
#define EMB_CRASH_RESET_REBOOT   1u /* CONFIG_EMB_FAULT_ACTION_REBOOT: reset */
#define EMB_CRASH_RESET_HALT     2u /* CONFIG_EMB_FAULT_ACTION_HALT: stopped for a debugger */
#define EMB_CRASH_RESET_WATCHDOG 3u /* the software watchdog stopped feeding (FLT-004) */

#define EMB_CRASH_BUILD_ID_SIZE 20u /* version 2 keeps the first 4 bytes */
#define EMB_CRASH_REGS_MAX      16u /* status and general registers together, version 1 */
#define EMB_CRASH_THREAD_NONE   0xFFFFu

typedef struct emb_crash_record {
    uint16_t version;                          /* EMB_CRASH_VERSION_* */
    uint16_t length;                           /* of the raw record, CRC included */
    uint8_t build_id[EMB_CRASH_BUILD_ID_SIZE]; /* emb_build_id of the faulting image */
    char fw_version[8];    /* CONFIG_EMB_FW_VERSION, zero-padded, not terminated if 8 long */
    uint64_t uptime_ticks; /* ticks since start at the fault; 32 bits in version 2 */
    uint32_t reset_reason; /* EMB_CRASH_RESET_* */
    uint8_t fault_class;   /* enum emb_fault_class */
    uint8_t argument;      /* offending argument index for API faults */
    uint16_t code;         /* class-specific code */
    uint16_t api_id;       /* EMB_API_ID_* public entry, 0 for none */
    uint16_t thread_index; /* faulting thread, EMB_CRASH_THREAD_NONE outside one */
    uintptr_t address;     /* faulting address for hardware faults, else 0 */
    uintptr_t pc;          /* program counter where the port provides it, else 0 */
    uintptr_t sp;          /* stack pointer where the port provides it, else 0 */
    uint32_t lr;           /* link register or return address at a hardware fault, else 0 */
    uint8_t status_count;  /* regs[0 .. status_count): the architecture's fault status registers */
    uint8_t gpr_count;     /* then gpr_count general registers (docs/engineering/crash-record.md) */
    uint32_t regs[EMB_CRASH_REGS_MAX];
    const uint8_t *raw; /* the record as written, for upload */
} emb_crash_record_t;

/**
 * emb_crash_record_get() - The crash record the previous run left, if any.
 * @out: Receives the decoded record; its raw pointer stays valid until
 *       emb_crash_record_clear() or the next fault.
 * Return: EMB_OK with @out filled; EMB_ENOENT when no valid record survived the reset
 *         (none was written, it was cleared, or its magic, version, length or CRC-32
 *         does not check); EMB_EINVAL in release builds when @out is NULL.
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_CRASH_RECORD
 * @req      OBS-016 API-017
 * @since    0.2
 * @stable   no
 * The record is validated once, by emb_kernel_init(); a fault in this run overwrites it.
 */
emb_status_t emb_crash_record_get(emb_crash_record_t *out);

/**
 * emb_crash_record_clear() - Discard the crash record, typically once it is uploaded.
 * Return: EMB_OK, also when there was none.
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_CRASH_RECORD
 * @req      OBS-016
 * @since    0.2
 * @stable   no
 * The next emb_crash_record_get() returns EMB_ENOENT, after a reset as well.
 */
emb_status_t emb_crash_record_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* EMB_CRASH_H */
