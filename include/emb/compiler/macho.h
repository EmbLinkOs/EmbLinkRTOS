/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Object-format layer for Mach-O (Apple ld64), selected by <emb/compiler.h> for the
 * native port on macOS. Two differences from ELF matter to the kernel:
 *
 * - A section name is "<segment>,<section>", the section part at most 16 characters.
 *   Registration tables are read-only data with pointers, so they go to __DATA_CONST,
 *   which the dynamic loader makes read-only after relocation.
 * - ld64 does not define __start_<section> and __stop_<section>. It provides the
 *   bounds of any section as section$start$<segment>$<section> and
 *   section$end$<segment>$<section>, creating an empty section when none exists.
 *   C cannot spell those names, so the declarations bind them with an asm label.
 */
#ifndef EMB_COMPILER_MACHO_H
#define EMB_COMPILER_MACHO_H

#define EMB_TABLE_SECTION(name) "__DATA_CONST," name

#define EMB_TABLE_BOUNDS(type, name)                                                   \
    extern type emb_table_start_##name[] __asm__("section$start$__DATA_CONST$" #name); \
    extern type emb_table_stop_##name[] __asm__("section$end$__DATA_CONST$" #name)
#define EMB_TABLE_START(name) emb_table_start_##name
#define EMB_TABLE_STOP(name)  emb_table_stop_##name

#endif /* EMB_COMPILER_MACHO_H */
