/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Newlib's reentrancy block for Espressif's GCC (docs/ports/xtensa.md, A95): Espressif's newlib
 * is built with __DYNAMIC_REENT__ and asks the system for the running task's block through
 * __getreent(), which ESP-IDF supplies. Its weak default found none outside ESP-IDF, and the
 * first libc call that set errno stored to address 0 (test_smoke on the board, 2026-11-05).
 * As on RISC-V (A88), one block serves every thread: errno is shared. Built only with GCC
 * (arch/xtensa/CMakeLists.txt); EmbCC's C library keeps errno itself.
 */
#include <sys/reent.h> /* declares __getreent() */

static struct _reent embn_xt_reent = _REENT_INIT(embn_xt_reent);

struct _reent *__getreent(void)
{
    return &embn_xt_reent;
}
