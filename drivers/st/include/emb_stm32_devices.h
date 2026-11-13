/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_stm32_devices.h
 * @brief   The STM32F4 drivers as devices (SPEC-016).
 *
 * @details
 * The operations and generated configurations the bindings name (drivers/bindings.yaml). Each
 * adapter is thin: the free functions of emb_stm32_drivers.h stay the low layer, used by the board
 * support before the kernel and by applications. The classes' requests come with SPEC-017.
 *
 * @author  Junior Deogracias
 * @date    2026-11-09 (created)
 */
#ifndef EMB_STM32_DEVICES_H
#define EMB_STM32_DEVICES_H

#include <emb/config.h>
#include <emb/device.h>
#include <emb/drv_counter.h>
#include <emb/drv_dma.h>
#include <emb/drv_flash.h>
#include <emb/drv_gpio.h>
#include <emb/drv_rtc.h>
#include <emb/drv_wdt.h>

#include <stdbool.h>
#include <stdint.h>
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
    emb_drv_dma_chan_t dma_tx; /* writes by DMA, where the board gives it one (dma: [tx]) */
} emb_stm32_usart_config_t;

#if CONFIG_EMB_DRV_UART
/* A USART device's RAM. */
typedef struct emb_stm32_usart_data {
    emb_drv_uart_rx_t rx;
    emb_drv_uart_req_t *tx;  /* the write in progress */
    const emb_device_t *dev; /* for the interrupt handler */
    uint8_t irq_on;          /* the interrupts serve requests: set by the first one */
    uint8_t dma;             /* the write in progress runs by DMA */
} emb_stm32_usart_data_t;
#endif

/* An SPI controller: its clock gate and interrupt. */
typedef struct emb_stm32_spi_config {
    uintptr_t base;
    uint32_t clock_hz; /* its bus clock */
    uint32_t clken_offset;
    uint32_t clken_bit;
    uint32_t irq;
    emb_drv_dma_chan_t dma_tx; /* where the board gives it DMA (dma: [tx, rx]); NULL's otherwise */
    emb_drv_dma_chan_t dma_rx;
} emb_stm32_spi_config_t;

#if CONFIG_EMB_DRV_SPI
/* An SPI controller's RAM. */
typedef struct emb_stm32_spi_data {
    emb_drv_bus_t bus;       /* the class's, first */
    emb_drv_spi_req_t *xfer; /* the transfer in progress */
    const emb_device_t *dev; /* for the interrupt handler */
    bool stopping;           /* canceled: ends when the frame in flight is back */
    bool dma;                /* the transfer runs by DMA: the board gave the device channels */
    uint8_t fill;            /* the ones a transfer with no transmit buffer sends, DMA's source */
    uint8_t sink;            /* where a transfer with no receive buffer's frames go */
} emb_stm32_spi_data_t;
#endif

/* A general-purpose timer, TIM2 to TIM5 (RM0390 §17) or TIM9 to TIM14 (§18): a counter's or
 * PWM's (A129). */
typedef struct emb_stm32_tim_config {
    uintptr_t base;
    uint32_t clock_hz; /* its kernel clock: twice its APB bus's when that is divided */
    uint32_t rate_hz;  /* a counter's counts per second: 1 MHz unless the board says */
    uint32_t clken_offset;
    uint32_t clken_bit;
    uint32_t irq;
    uint32_t bits;     /* its counter's width: 32 (TIM2, TIM5) or 16 */
    uint32_t channels; /* 4 on TIM2 to TIM5, 2 on TIM9 and TIM12, 1 on TIM10, 11, 13 and 14 */
} emb_stm32_tim_config_t;

#if CONFIG_EMB_DRV_COUNTER
/* A counter's RAM: the class's, first; the counts of the overflows so far, which widen the
 * hardware's to 64 bits; and each channel's count, those not yet in the counter's epoch
 * pending until it is. */
typedef struct emb_stm32_counter_data {
    emb_drv_counter_data_t counter;
    const emb_device_t *dev; /* for the interrupt handler */
    uint64_t high;
    uint64_t target[EMB_DRV_COUNTER_CHANNELS_MAX];
    uint8_t pending;
    uint8_t reserved_[7];
} emb_stm32_counter_data_t;
#endif

/* The independent watchdog (RM0390 §19), clocked by the LSI: no clock gate. */
typedef struct emb_stm32_iwdg_config {
    uintptr_t base;
} emb_stm32_iwdg_config_t;

/* Its RAM: whether it was started, and whether it caused this boot's reset. */
typedef struct emb_stm32_iwdg_data {
    bool started;
    bool caused;
} emb_stm32_iwdg_data_t;

/* The RTC (RM0390 §26): its registers and its alarm's interrupt (EXTI line 17). */
typedef struct emb_stm32_rtc_config {
    uintptr_t base;
    uint32_t irq;
} emb_stm32_rtc_config_t;

/* Its RAM: the class's, first, and the device for the alarm's interrupt. */
typedef struct emb_stm32_rtc_data {
    emb_drv_rtc_data_t rtc;
    const emb_device_t *dev;
} emb_stm32_rtc_data_t;

/* The flash interface (RM0390 §3): its registers, and the flash's geometry from the
 * description. */
typedef struct emb_stm32_flash_config {
    uintptr_t base;
    emb_drv_flash_geometry_t geometry;
} emb_stm32_flash_config_t;

/* Its RAM: the class's lock, first. */
typedef struct emb_stm32_flash_data {
    emb_drv_bus_t lock;
} emb_stm32_flash_data_t;

/* A DMA controller (RM0390 §9): its clock gate, each stream's interrupt, and whether it copies
 * memory to memory (DMA2 alone). */
typedef struct emb_stm32_dma_config {
    uintptr_t base;
    uint32_t clken_offset;
    uint32_t clken_bit;
    uint32_t irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7;
    bool memory;
} emb_stm32_dma_config_t;

#if CONFIG_EMB_DRV_DMA
#define EMB_STM32_DMA_STREAMS 8u
struct emb_stm32_dma_data;
/* A stream's job: its end's callback, and the count it started with. */
typedef struct emb_stm32_dma_stream {
    struct emb_stm32_dma_data *dma;
    emb_drv_dma_done_t done;
    void *arg;
    uint32_t count;
    uint8_t index;
    uint8_t running;
} emb_stm32_dma_stream_t;

/* A DMA controller's RAM. */
typedef struct emb_stm32_dma_data {
    const emb_device_t *dev;
    emb_stm32_dma_stream_t streams[EMB_STM32_DMA_STREAMS];
} emb_stm32_dma_data_t;
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
#if CONFIG_EMB_DRV_WDT
/* The watchdog class's operations (SPEC-017 §8): the IWDG, whose start chooses the prescaler
 * the timeout needs, and the reset's cause from RCC_CSR (emb_stm32f4_reset_flags_take()). */
extern const emb_drv_wdt_api_t emb_stm32_iwdg_api;
#endif
#if CONFIG_EMB_DRV_COUNTER
/* The counter class's operations (SPEC-017 §11a): a general-purpose timer at its rate, widened
 * to 64 bits by the update interrupt, an alarm on each of its compare channels. */
extern const emb_drv_counter_api_t emb_stm32_counter_api;
#endif

#if CONFIG_EMB_DRV_RTC
/* The RTC class's operations (SPEC-017 §10): the calendar in the backup domain on the LSE,
 * alarm A, the 20 backup registers. */
extern const emb_drv_rtc_api_t emb_stm32_rtc_api;
#endif
#if CONFIG_EMB_DRV_FLASH
/* The flash class's operations (SPEC-017 §9): a sector erased, or bytes programmed, with
 * interrupts held off while the CPU stalls on its flash, the clock catching up after (A122). */
extern const emb_drv_flash_api_t emb_stm32_flash_api;
#endif
#if CONFIG_EMB_DRV_DMA
/* The DMA class's operations (SPEC-017 §7): a job per stream, direct mode for a peripheral,
 * the FIFO for memory to memory, its end by the stream's interrupt. */
extern const emb_drv_dma_api_t emb_stm32_dma_api;
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
