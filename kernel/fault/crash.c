/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    crash.c
 * @brief   The crash record (SPEC-015 §4, OBS-016, A71 to A73).
 *
 * @details
 * The record is a little-endian byte string, never a C struct, so its layout is the same for every
 * compiler; a CRC-32 (IEEE, reflected, bitwise: no table in flash) over everything before it closes
 * it.
 *
 *   version 1:  magic "EMBC" | version u16 | length u16 | build_id[20] | fw_version[8]
 *               | uptime_ticks u64 | reset_reason u32 | fault_class u8 | argument u8
 *               | code u16 | api_id u16 | cpu_id u8 | partition_id u8 | thread_index u16
 *               | thread_generation u16 | address u32 | pc u32 | lr_or_ra u32 | sp u32
 *               | stack_base u32 | stack_size u32 | status_reg_count u8 | gpr_count u8
 *               | flight_event_count u16 | registers u32[status + gpr] | crc32
 *                                                       (92 bytes, 4 more per register)
 *   version 2:  magic | version | length | build_id[4] | uptime_ticks u32 | reset_reason u8
 *               | fault_class u8 | argument u8 | thread_index u8 | code u16 | api_id u16
 *               | pc u16 | sp u16 | address u16 | crc32                         (34 bytes)
 *
 * The registers are the port's (emb_arch_crash_regs, A74): a hardware fault's status
 * registers, then general registers. The flight recorder is not captured yet (count 0); the
 * fields the kernel does not know (stack base and size, generation, cpu, partition) are 0.
 *
 * @author  Junior Deogracias
 * @date    2026-11-04 (created)
 */
#include <emb/arch.h>
#include <emb/crash.h>
#include <emb/fault.h>

#include <embk/crash.h>
#include <embk/kernel.h>
#include <embk/time.h>

#define MAGIC_0 0x45u /* 'E' */
#define MAGIC_1 0x4Du /* 'M' */
#define MAGIC_2 0x42u /* 'B' */
#define MAGIC_3 0x43u /* 'C' */

static uint8_t record_valid; /* lock: written at boot and by clear, before or outside a fault */

static void put8(uint8_t *p, uint16_t *at, uint32_t v)
{
    p[*at] = (uint8_t)v;
    (*at)++;
}

static void put16(uint8_t *p, uint16_t *at, uint32_t v)
{
    put8(p, at, v & 0xFFu);
    put8(p, at, (v >> 8) & 0xFFu);
}

static void put32(uint8_t *p, uint16_t *at, uint32_t v)
{
    put16(p, at, v & 0xFFFFu);
    put16(p, at, v >> 16);
}

static uint32_t get16(const uint8_t *p, uint16_t at)
{
    return (uint32_t)p[at] | ((uint32_t)p[at + 1u] << 8);
}

static uint32_t get32(const uint8_t *p, uint16_t at)
{
    return get16(p, at) | (get16(p, (uint16_t)(at + 2u)) << 16);
}

static uint32_t crc32(const uint8_t *p, uint16_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    uint16_t i;
    uint8_t k;
    for (i = 0u; i < n; i++) { /* bounded by the record length */
        c ^= p[i];
        for (k = 0u; k < 8u; k++) { /* eight bits */
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
    }
    return ~c;
}

static uint8_t *area(uint16_t *size)
{
    uint8_t *p = emb_arch_retained(size);
    return (p != NULL && *size >= EMBK_CRASH_LENGTH) ? p : NULL;
}

/* Whether the bytes at p, of the retained memory's size, hold a complete record. */
static bool record_ok(const uint8_t *p, uint16_t size)
{
    uint16_t length = (uint16_t)get16(p, 6u);
    if (p[0] != MAGIC_0 || p[1] != MAGIC_1 || p[2] != MAGIC_2 || p[3] != MAGIC_3 ||
        get16(p, 4u) != EMBK_CRASH_VERSION || length < EMBK_CRASH_LENGTH || length > size) {
        return false;
    }
#if EMBK_CRASH_VERSION == EMB_CRASH_VERSION_SMALL
    if (length != EMBK_CRASH_LENGTH) {
        return false;
    }
#else
    if (length != EMBK_CRASH_LENGTH + 4u * ((uint16_t)p[84] + p[85]) || get16(p, 86u) != 0u) {
        return false; /* the registers the counts announce, and no flight recorder yet */
    }
#endif
    return get32(p, (uint16_t)(length - 4u)) == crc32(p, (uint16_t)(length - 4u));
}

void embk_crash_record_write(const emb_fault_info_t *info, uint32_t reset_reason)
{
    uint16_t size = 0u;
    uint8_t *p = area(&size);
    uint16_t at = 0u;
    uint16_t i;
    uint64_t up;
#if EMBK_CRASH_VERSION == EMB_CRASH_VERSION_FULL
    uint32_t regs[EMB_CRASH_REGS_MAX];
    uint8_t n = 0u;
    uint8_t status = 0u;
    uint32_t lr = 0u;
#endif
    if (p == NULL) {
        return;
    }
#if EMBK_CRASH_VERSION == EMB_CRASH_VERSION_FULL
    {
        uint16_t room = (uint16_t)((size - EMBK_CRASH_LENGTH) / 4u);
        n = emb_arch_crash_regs(info, regs,
                                (uint8_t)(room < EMB_CRASH_REGS_MAX ? room : EMB_CRASH_REGS_MAX),
                                &status, &lr);
        if (status > n) {
            status = n;
        }
    }
#endif
    up = (embk_cpu.kernel_state == EMBK_KERNEL_RUNNING) ? (uint64_t)embk_time_ticks_locked() : 0u;
    put8(p, &at, MAGIC_0);
    put8(p, &at, MAGIC_1);
    put8(p, &at, MAGIC_2);
    put8(p, &at, MAGIC_3);
    put16(p, &at, EMBK_CRASH_VERSION);
#if EMBK_CRASH_VERSION == EMB_CRASH_VERSION_FULL
    put16(p, &at, EMBK_CRASH_LENGTH + 4u * (uint32_t)n);
#else
    put16(p, &at, EMBK_CRASH_LENGTH);
#endif
#if EMBK_CRASH_VERSION == EMB_CRASH_VERSION_SMALL
    for (i = 0u; i < 4u; i++) { /* the build id's first four bytes */
        put8(p, &at, EMB_FLASH_READ_U8(&emb_build_id[i]));
    }
    put32(p, &at, (uint32_t)up);
    put8(p, &at, reset_reason);
    put8(p, &at, info->fault_class);
    put8(p, &at, info->argument);
    put8(p, &at, info->thread_index);
    put16(p, &at, info->code);
    put16(p, &at, info->api_id);
    put16(p, &at, (uint32_t)info->pc);
    put16(p, &at, (uint32_t)info->sp);
    put16(p, &at, (uint32_t)info->address);
#else
    {
        static const char fw[] = CONFIG_EMB_FW_VERSION;
        for (i = 0u; i < EMB_CRASH_BUILD_ID_SIZE; i++) { /* twenty bytes */
            put8(p, &at, EMB_FLASH_READ_U8(&emb_build_id[i]));
        }
        for (i = 0u; i < 8u; i++) { /* zero-padded */
            put8(p, &at, (i < sizeof(fw) - 1u) ? (uint8_t)fw[i] : 0u);
        }
    }
    put32(p, &at, (uint32_t)up);
    put32(p, &at, (uint32_t)(up >> 32));
    put32(p, &at, reset_reason);
    put8(p, &at, info->fault_class);
    put8(p, &at, info->argument);
    put16(p, &at, info->code);
    put16(p, &at, info->api_id);
    put8(p, &at, 0u); /* cpu_id: uniprocessor */
    put8(p, &at, 0u); /* partition_id: the kernel partition */
    put16(p, &at, (info->thread_index == 0xFFu) ? EMB_CRASH_THREAD_NONE : info->thread_index);
    put16(p, &at, 0u); /* thread_generation: no generations without partitions */
    put32(p, &at, (uint32_t)info->address);
    put32(p, &at, (uint32_t)info->pc);
    put32(p, &at, lr);
    put32(p, &at, (uint32_t)info->sp);
    put32(p, &at, 0u); /* stack_base */
    put32(p, &at, 0u); /* stack_size */
    put8(p, &at, status);
    put8(p, &at, (uint32_t)n - status);
    put16(p, &at, 0u);         /* flight_event_count */
    for (i = 0u; i < n; i++) { /* at most EMB_CRASH_REGS_MAX */
        put32(p, &at, regs[i]);
    }
#endif
    put32(p, &at, crc32(p, at));
    emb_arch_retained_sync(true);
}

void embk_crash_record_boot(void)
{
    uint16_t size = 0u;
    uint8_t *p;
    emb_arch_retained_sync(false);
    p = area(&size);
    record_valid = (uint8_t)(p != NULL && record_ok(p, size));
}

emb_status_t emb_crash_record_get(emb_crash_record_t *out)
{
    uint16_t size = 0u;
    const uint8_t *p = area(&size);
    uint16_t i;
    EMBK_API_REQUIRE(EMB_API_ID_CRASH_RECORD_GET, out != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    if (record_valid == 0u || p == NULL) {
        return EMB_ENOENT;
    }
    out->version = (uint16_t)get16(p, 4u);
    out->length = (uint16_t)get16(p, 6u);
    out->raw = p;
    for (i = 0u; i < EMB_CRASH_BUILD_ID_SIZE; i++) { /* twenty bytes */
        out->build_id[i] = 0u;
    }
    for (i = 0u; i < 8u; i++) { /* eight bytes */
        out->fw_version[i] = '\0';
    }
    for (i = 0u; i < EMB_CRASH_REGS_MAX; i++) { /* sixteen words */
        out->regs[i] = 0u;
    }
    out->lr = 0u;
    out->status_count = 0u;
    out->gpr_count = 0u;
#if EMBK_CRASH_VERSION == EMB_CRASH_VERSION_SMALL
    for (i = 0u; i < 4u; i++) { /* four bytes */
        out->build_id[i] = p[8u + i];
    }
    out->uptime_ticks = get32(p, 12u);
    out->reset_reason = p[16];
    out->fault_class = p[17];
    out->argument = p[18];
    out->thread_index = (p[19] == 0xFFu) ? EMB_CRASH_THREAD_NONE : p[19];
    out->code = (uint16_t)get16(p, 20u);
    out->api_id = (uint16_t)get16(p, 22u);
    out->pc = (uintptr_t)get16(p, 24u);
    out->sp = (uintptr_t)get16(p, 26u);
    out->address = (uintptr_t)get16(p, 28u);
#else
    for (i = 0u; i < EMB_CRASH_BUILD_ID_SIZE; i++) { /* twenty bytes */
        out->build_id[i] = p[8u + i];
    }
    for (i = 0u; i < 8u; i++) { /* eight bytes */
        out->fw_version[i] = (char)p[28u + i];
    }
    out->uptime_ticks = (uint64_t)get32(p, 36u) | ((uint64_t)get32(p, 40u) << 32);
    out->reset_reason = get32(p, 44u);
    out->fault_class = p[48];
    out->argument = p[49];
    out->code = (uint16_t)get16(p, 50u);
    out->api_id = (uint16_t)get16(p, 52u);
    out->thread_index = (uint16_t)get16(p, 56u);
    out->address = (uintptr_t)get32(p, 60u);
    out->pc = (uintptr_t)get32(p, 64u);
    out->sp = (uintptr_t)get32(p, 72u);
    out->lr = get32(p, 68u);
    out->status_count = p[84];
    out->gpr_count = p[85];
    for (i = 0u; i < (uint16_t)(p[84] + p[85]) && i < EMB_CRASH_REGS_MAX; i++) { /* the counts */
        out->regs[i] = get32(p, (uint16_t)(88u + 4u * i));
    }
#endif
    return EMB_OK;
}

emb_status_t emb_crash_record_clear(void)
{
    uint16_t size = 0u;
    uint8_t *p = area(&size);
    record_valid = 0u;
    if (p != NULL) {
        p[0] = 0u; /* the magic: nothing valid survives the next reset either */
        emb_arch_retained_sync(true);
    }
    return EMB_OK;
}
