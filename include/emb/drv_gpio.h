/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The GPIO class (SPEC-017 §5a, A116): a controller's lines set as inputs or outputs, driven
 * and read, at once and from any context. What a chip select, a reset line or an LED needs;
 * a board's fixed pins stay in its generated pin table. */
#ifndef EMB_DRV_GPIO_H
#define EMB_DRV_GPIO_H

#include <emb/device.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How a line is set up: input or output, then its options. */
#define EMB_DRV_GPIO_INPUT      0x00u
#define EMB_DRV_GPIO_OUTPUT     0x01u
#define EMB_DRV_GPIO_OPEN_DRAIN 0x02u /* an output that drives low and lets go high */
#define EMB_DRV_GPIO_PULL_UP    0x04u
#define EMB_DRV_GPIO_PULL_DOWN  0x08u
#define EMB_DRV_GPIO_INIT_HIGH  0x10u /* an output's level from the start: no glitch low */
#define EMB_DRV_GPIO_FUNCTION   0x20u /* back to the board's pin table: a peripheral's again */
#define EMB_DRV_GPIO_FLAGS      0x3Fu /* every flag */

/* Capabilities (emb_device_caps()). */
#define EMB_DRV_GPIO_CAP_OPEN_DRAIN 0x01u
#define EMB_DRV_GPIO_CAP_PULL_DOWN  0x02u

/* A GPIO driver's operations: the device's, then the line operations, each O(1) and safe in
 * an interrupt. configure() runs inside the class's critical section, so it may read, modify
 * and write a register the controller's lines share; write() and read() are single accesses
 * (a set/clear register, the input register) and need none. */
typedef struct emb_drv_gpio_api {
    emb_device_api_t dev;
    /* EMB_EINVAL for a line the controller does not have, EMB_ENOTSUP for an option */
    emb_status_t (*configure)(const emb_device_t *dev, uint32_t line, uint32_t flags);
    void (*write)(const emb_device_t *dev, uint32_t line, bool high);
    bool (*read)(const emb_device_t *dev, uint32_t line);
} emb_drv_gpio_api_t;

/* A line of a controller: what a chip select or a reset names. */
typedef struct emb_drv_gpio_line {
    const emb_device_t *gpio; /* a device of the GPIO class, or NULL for none */
    uint8_t line;             /* its number on the controller */
    uint8_t active_low;       /* the line is asserted low (a chip select's usual sense) */
    uint8_t reserved_[2];
} emb_drv_gpio_line_t;

/**
 * emb_drv_gpio_configure() - Set a line up as an input or an output.
 * @dev:   A device of the GPIO class.
 * @line:  The line's number on the controller.
 * @flags: EMB_DRV_GPIO_INPUT or _OUTPUT, with _OPEN_DRAIN, _PULL_UP or _PULL_DOWN, and
 *         _INIT_HIGH for an output; or EMB_DRV_GPIO_FUNCTION alone.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_GPIO
 * @req      DRV-004
 * @since    0.3
 * @stable   no
 *
 * The line becomes a plain input or output, whatever the board's pin table made it; an
 * output takes its level first, so it never shows another. EMB_DRV_GPIO_FUNCTION gives it
 * back what the pin table made it, a peripheral's function with its output type and pull: a
 * driver that borrowed its pins (an I2C bus's recovery) returns them so.
 *
 * Return: EMB_OK; EMB_EINVAL (not a GPIO device, a line it does not have, both pulls, an
 * input that starts high, EMB_DRV_GPIO_FUNCTION with another flag or for a line the pin table
 * does not name, or an unknown flag); EMB_ENOTSUP (open drain or a pull-down the controller
 * lacks).
 */
emb_status_t emb_drv_gpio_configure(const emb_device_t *dev, uint32_t line, uint32_t flags);

/**
 * emb_drv_gpio_write() - Drive an output line high or low.
 * @dev:  A device of the GPIO class.
 * @line: The line's number on the controller.
 * @high: true for high.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_GPIO
 * @req      DRV-004
 * @since    0.3
 * @stable   no
 *
 * One write to the controller's set or clear register: atomic against the other lines.
 *
 * Return: EMB_OK; EMB_EINVAL (not a GPIO device, or a line number above 31).
 */
emb_status_t emb_drv_gpio_write(const emb_device_t *dev, uint32_t line, bool high);

/**
 * emb_drv_gpio_read() - Read a line's level.
 * @dev:  A device of the GPIO class.
 * @line: The line's number on the controller.
 * @high: Receives true when the line is high.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_GPIO
 * @req      DRV-004
 * @since    0.3
 * @stable   no
 *
 * The level at the pin, an output's included: what the line actually shows.
 *
 * Return: EMB_OK; EMB_EINVAL (not a GPIO device, a line number above 31, or no @high).
 */
emb_status_t emb_drv_gpio_read(const emb_device_t *dev, uint32_t line, bool *high);

/**
 * emb_drv_gpio_assert() - Assert or release a named line, as its sense says.
 * @l:  The line; one whose gpio is NULL does nothing.
 * @on: true to assert it.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_GPIO
 * @req      DRV-004
 * @since    0.3
 * @stable   no
 *
 * An active-low line asserted goes low. Drivers drive chip selects through it.
 *
 * Return: EMB_OK; the statuses of emb_drv_gpio_write().
 */
emb_status_t emb_drv_gpio_assert(const emb_drv_gpio_line_t *l, bool on);

#ifdef __cplusplus
}
#endif

#endif /* EMB_DRV_GPIO_H */
