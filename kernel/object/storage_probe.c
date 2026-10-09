/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Layout probe for the generated <emb/storage.h> (SPEC-001 §6.2, SPEC-009, ADR-006).
 * Compiled with the real configuration; tools/storage/gen_storage.py reads the symbol
 * sizes with nm. Never linked into an image.
 */
#include <embk/kernel.h>
#include <embk/sync.h>

#include <stdalign.h>

#define PROBE(name, type)                                                    \
    const unsigned char embk_probe_size_##name[sizeof(type)] EMB_USED = {0}; \
    const unsigned char embk_probe_align_##name[alignof(type)] EMB_USED = {0}

PROBE(thread, embk_thread_t);
#if CONFIG_EMB_SEM
PROBE(sem, embk_sem_t);
#endif
#if CONFIG_EMB_MUTEX
PROBE(mutex, embk_mutex_t);
#endif
