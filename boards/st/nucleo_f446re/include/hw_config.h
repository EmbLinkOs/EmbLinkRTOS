/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Hardware configuration of nucleo_f446re (STM32F446RET6, MB1136 C-03). Hand-written from
 * RM0390, the STM32F446xC/E datasheet and UM1724; emb-hwgen generates it from M3 (SPEC-014). */
#ifndef HW_CONFIG_H
#define HW_CONFIG_H

#define EMB_HW_BOARD        "nucleo_f446re"
#define EMB_HW_SOC          "STM32F446RE"
#define EMB_HW_HSE_HZ       8000000u   /* the ST-LINK's MCO, HSE bypass (UM1724 §6.7.1) */
#define EMB_HW_CPU_HZ       180000000u /* SYSCLK and HCLK from the PLL */
#define EMB_HW_APB1_HZ      45000000u  /* HCLK / 4: USART2 */
#define EMB_HW_CONSOLE_BAUD 115200u
#define EMB_HW_USART2_BASE  0x40004400u /* PA2 TX, PA3 RX, AF7: the virtual COM port */
#define EMB_HW_LED_PIN      5u          /* LD2 on PA5, high is on */
#define EMB_HW_TEST_IRQ \
    61u /* reserved on the F446 (no Ethernet): the test framework's software interrupt */

#define EMB_HW_TEST_IRQ_HIGH 62u /* second idle line for NVIC priority conformance */

#endif /* HW_CONFIG_H */
