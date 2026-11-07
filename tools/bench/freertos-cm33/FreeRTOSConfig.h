/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * FreeRTOS's configuration for bench_compare on the Pico 2 W (RP2350, Cortex-M33,
 * tools/bench/freertos-cm33/), as the NUCLEO's (tools/bench/freertos-cm4/FreeRTOSConfig.h):
 * matched to the EmbLinkRTOS side it is compared with. Static allocation only, preemption without
 * time slicing, 32-bit ticks at 1 kHz, and no assertions, stack-overflow checks, trace, hooks or
 * software timers. build.sh gives the CPU clock and the board's NVIC priority bits.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configCPU_CLOCK_HZ                      ((unsigned long)BENCH_CPU_HZ)
#define configTICK_RATE_HZ                      1000
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configMAX_PRIORITIES                    4
#define configMINIMAL_STACK_SIZE                128
#define configMAX_TASK_NAME_LEN                 4
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 0
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configUSE_MUTEXES                       0
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           0
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_QUEUE_SETS                    0
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        0
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_TRACE_FACILITY                0
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TIMERS                        0
#define configUSE_CO_ROUTINES                   0
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0

/* The Cortex-M port's interrupt priorities: the kernel's at the lowest level, and every
 * interrupt that calls a FromISR function at or below configMAX_SYSCALL_INTERRUPT_PRIORITY,
 * level 2 of the board's levels (bench_platform.c puts the test line below it). */
#define configPRIO_BITS                      BENCH_PRIO_BITS
#define configKERNEL_INTERRUPT_PRIORITY      (0xFFu)
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (2u << (8u - configPRIO_BITS))

/* The Armv8-M port without TrustZone (ARM_CM33_NTZ), running in the Secure state as the
 * EmbLinkRTOS port does: the FPU on, no MPU, no secure context. */
#define configENABLE_FPU               1
#define configENABLE_MPU               0
#define configENABLE_TRUSTZONE         0
#define configENABLE_MVE               0
#define configRUN_FREERTOS_SECURE_ONLY 1

#define INCLUDE_vTaskSuspend           1
#define INCLUDE_vTaskDelay             0
#define INCLUDE_vTaskDelete            0
#define INCLUDE_vTaskPrioritySet       0
#define INCLUDE_uxTaskPriorityGet      0
#define INCLUDE_xTaskGetSchedulerState 0

#endif /* FREERTOS_CONFIG_H */
