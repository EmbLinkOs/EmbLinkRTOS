/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    sha256.h
 * @brief   SHA-256 (FIPS 180-4), inside the kernel.
 *
 * @details
 * The entropy class's conditioning function (SPEC-017 §11, A124; NIST SP 800-90B §3.1.5.1.1 lists
 * it as vetted). Not a public cryptography API: applications bring their own.
 *
 * @see     kernel/device/sha256.c
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#ifndef EMBK_SHA256_H
#define EMBK_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define EMBK_SHA256_SIZE 32u /* bytes of a digest */

typedef struct embk_sha256 {
    uint32_t h[8];
    uint8_t block[64];
    uint32_t used;  /* bytes of block filled */
    uint64_t bytes; /* bytes hashed */
} embk_sha256_t;

/* Starts a hash. */
void embk_sha256_start(embk_sha256_t *s);

/* Adds n bytes. */
void embk_sha256_add(embk_sha256_t *s, const uint8_t *p, size_t n);

/* Ends the hash: the digest into out (EMBK_SHA256_SIZE bytes); the state is cleared. */
void embk_sha256_end(embk_sha256_t *s, uint8_t *out);

#endif /* EMBK_SHA256_H */
