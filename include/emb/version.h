/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Version constants (SPEC-001 §11). */
#ifndef EMB_VERSION_H
#define EMB_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#define EMB_VERSION_MAJOR 0
#define EMB_VERSION_MINOR 2
#define EMB_VERSION_PATCH 0
#define EMB_VERSION       ((EMB_VERSION_MAJOR << 16) | (EMB_VERSION_MINOR << 8) | EMB_VERSION_PATCH)
#define EMB_API_VERSION   1

/**
 * emb_version_string() - The kernel version as "major.minor.patch".
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      API-001
 * @since    0.2
 * @stable   yes
 *
 * Return: a string that lives for the whole program.
 */
const char *emb_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* EMB_VERSION_H */
