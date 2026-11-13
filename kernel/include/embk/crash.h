/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    crash.h
 * @brief   The crash record's kernel side (SPEC-015 §4, A71 to A73).
 *
 * @details
 * Written on the fault path, validated at boot.
 *
 * @see     kernel/fault/crash.c
 * @author  Junior Deogracias
 * @date    2026-11-04 (created)
 */
#ifndef EMBK_CRASH_H
#define EMBK_CRASH_H

#include <emb/compiler.h>
#include <emb/crash.h>
#include <emb/fault.h>

#include <stdint.h>

/* The build identity (A72), generated at configure time; tools/crashdec reads it from the
 * ELF to refuse a record from another build. */
extern EMB_FLASH_CONST uint8_t emb_build_id[EMB_CRASH_BUILD_ID_SIZE];

#if UINTPTR_MAX == 0xFFFFu
#define EMBK_CRASH_VERSION EMB_CRASH_VERSION_SMALL
#define EMBK_CRASH_LENGTH  34u
#else
#define EMBK_CRASH_VERSION EMB_CRASH_VERSION_FULL
#define EMBK_CRASH_LENGTH  92u /* without registers; 4 bytes more for each */
#endif

/* Serializes the fault into the port's retained memory, before the hook and the action;
 * called with interrupts masked, from the fault path only. */
void embk_crash_record_write(const emb_fault_info_t *info, uint32_t reset_reason);

/* Validates what the retained memory holds (magic, version, length, CRC-32); from then on
 * emb_crash_record_get() reports it. emb_kernel_init() calls it once. */
void embk_crash_record_boot(void);

#endif /* EMBK_CRASH_H */
