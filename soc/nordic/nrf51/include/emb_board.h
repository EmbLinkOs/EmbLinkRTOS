/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The board interface of every nRF51 board described in YAML (A86): the console is the one the
 * board's generated hw_config.h names. */
#ifndef EMB_BOARD_H
#define EMB_BOARD_H

#include <emb/compiler.h>
#include <emb/fault.h>

#include <stdint.h>

#include <hw_config.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMB_HW_BOARD_NAME EMB_HW_BOARD

void emb_board_init(void);
void emb_board_led_toggle(void);
void emb_board_puts(const char *s);
void emb_board_put_u32(uint32_t v);
EMB_NORETURN void emb_board_exit(int code);
void emb_board_fault_report(const emb_fault_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* EMB_BOARD_H */
