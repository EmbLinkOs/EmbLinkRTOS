/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The STM32F4 drivers as devices (SPEC-016): the operations and generated configurations the
 * bindings name (drivers/bindings.yaml). Each adapter is thin: the free functions of
 * emb_stm32_drivers.h stay the low layer, used by the board support before the kernel and by
 * applications. The classes' requests come with SPEC-017.
 */
#ifndef EMB_STM32_DEVICES_H
#define EMB_STM32_DEVICES_H

#include <emb/config.h>

#include <stdbool.h>
#include <stdint.h>

#if CONFIG_EMB_DEVICE
#include <emb/device.h>
#include <emb/drv_gpio.h>
#endif
#if CONFIG_EMB_DRV_UART
#include <emb/drv_uart.h>
#endif
#if CONFIG_EMB_DRV_SPI
#include <emb/drv_spi.h>
#endif
#if CONFIG_EMB_DRV_I2C
#include <emb/drv_i2c.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The reset and clock controller: the clock tree the board support brought up. */
typedef struct emb_stm32_rcc_config {
    uintptr_t base;
} emb_stm32_rcc_config_t;

/* A GPIO port: its clock gate. Its 16 lines are the GPIO class's (SPEC-017 §5a). */
typedef struct emb_stm32_gpio_config {
    uintptr_t base;
    uint32_t clken_offset; /* the bus's clock-enable register in the RCC */
    uint32_t clken_bit;
} emb_stm32_gpio_config_t;

/* A USART: its clock gate, rate, interrupt and receive ring. */
typedef struct emb_stm32_usart_config {
    uintptr_t base;
    uint32_t clock_hz; /* its bus clock */
    uint32_t baud;
    uint32_t clken_offset;
    uint32_t clken_bit;
    uint32_t irq;
    uint8_t *rx; /* the receive ring (rx_buffer) */
    uint32_t rx_size;
} emb_stm32_usart_config_t;

#if CONFIG_EMB_DRV_UART
/* A USART device's RAM. */
typedef struct emb_stm32_usart_data {
    emb_drv_uart_rx_t rx;
    emb_drv_uart_req_t *tx;  /* the write in progress */
    const emb_device_t *dev; /* for the interrupt handler */
    uint8_t irq_on;          /* the interrupts serve requests: set by the first one */
} emb_stm32_usart_data_t;
#endif

/* An SPI controller: its clock gate and interrupt. */
typedef struct emb_stm32_spi_config {
    uintptr_t base;
    uint32_t clock_hz; /* its bus clock */
    uint32_t clken_offset;
    uint32_t clken_bit;
    uint32_t irq;
} emb_stm32_spi_config_t;

#if CONFIG_EMB_DRV_SPI
/* An SPI controller's RAM. */
typedef struct emb_stm32_spi_data {
    emb_drv_bus_t bus;       /* the class's, first */
    emb_drv_spi_req_t *xfer; /* the transfer in progress */
    const emb_device_t *dev; /* for the interrupt handler */
    bool stopping;           /* canceled: ends when the frame in flight is back */
} emb_stm32_spi_data_t;
#endif

/* An I2C controller: its lines first (the class's recovery drives them), its clock gate and
 * its event and error interrupts. */
typedef struct emb_stm32_i2c_config {
    emb_drv_gpio_line_t scl;
    emb_drv_gpio_line_t sda;
    uintptr_t base;
    uint32_t clock_hz; /* its bus clock: CR2.FREQ in MHz, CCR from it */
    uint32_t clken_offset;
    uint32_t clken_bit;
    uint32_t irq_ev;
    uint32_t irq_er;
} emb_stm32_i2c_config_t;

#if CONFIG_EMB_DRV_I2C
/* An I2C controller's RAM. It waits as a target while no transaction runs, and a
 * transaction's start condition makes it the bus's controller until its stop. */
typedef struct emb_stm32_i2c_data {
    emb_drv_bus_t bus;         /* the class's, first */
    emb_drv_i2c_req_t *xfer;   /* the transaction in progress */
    emb_drv_i2c_req_t *listen; /* the listen waiting or answering */
    const emb_device_t *dev;   /* for the interrupt handlers */
    size_t seg;                /* the segment in progress */
    size_t pos;                /* bytes of it moved */
    uint32_t moved;            /* bytes of the transaction moved */
    uint32_t hz;               /* the speed the controller is set up for */
    bool stopping;             /* the transaction was canceled */
} emb_stm32_i2c_data_t;
#endif

#if CONFIG_EMB_DEVICE
/* READY when the clock switch the board support asked for has completed. */
extern const emb_device_api_t emb_stm32_rcc_api;
/* Enables the port's clock. */
extern const emb_drv_gpio_api_t emb_stm32_gpio_api;
#endif
#if CONFIG_EMB_DRV_UART
/* The UART class's operations (SPEC-017 §4). Initialization checks the divider, enables the
 * clock and the receiver, leaves a USART already running at the rate as it is (the board's
 * console), and connects the interrupt; the first request turns it on, so an application
 * that connects its own handler to the line keeps it. */
extern const emb_drv_uart_api_t emb_stm32_usart_api;
#endif
#if CONFIG_EMB_DRV_SPI
/* The SPI class's operations (SPEC-017 §5): a controller with software chip-select
 * management, one frame in flight, carried by the receive interrupt. */
extern const emb_device_api_t emb_stm32_spi_api;
#endif
#if CONFIG_EMB_DRV_I2C
/* The I2C class's operations (SPEC-017 §6): standard and fast mode, and target mode, by
 * interrupt; one byte at a time, the receiver's acknowledge managed as RM0390 §24.3.3 asks. */
extern const emb_drv_i2c_api_t emb_stm32_i2c_api;
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_STM32_DEVICES_H */
