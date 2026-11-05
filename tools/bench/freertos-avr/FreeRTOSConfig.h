/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * FreeRTOS for tests/benchmarks/bench_compare.c on the Arduino Uno (ATmega328P at 16 MHz),
 * configured to match the EmbLinkRTOS build it is compared with (tiny profile, release): static
 * allocation only, no heap; preemption without time slicing; a 1 kHz tick; 32-bit ticks;
 * no stack-overflow checks, assertions, trace, hooks or software timers.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configCPU_CLOCK_HZ                      16000000UL
#define configTICK_RATE_HZ                      1000 /* Timer0, see build.sh: about 1042 Hz */
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

#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelay                      0
#define INCLUDE_vTaskDelete                     0
#define INCLUDE_vTaskPrioritySet                0
#define INCLUDE_uxTaskPriorityGet               0
#define INCLUDE_xTaskGetSchedulerState          0

#endif /* FREERTOS_CONFIG_H */
