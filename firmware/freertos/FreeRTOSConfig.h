/*
 * FreeRTOSConfig.h — FreeRTOS kernel configuration for the IOGear GPSU21.
 *
 * Target:  MediaTek MT7688 SoC
 * CPU:     MIPS24KEc, MIPS32r2, little-endian, 580 MHz
 * RAM:     64 MB SDRAM at KSEG0 0x80000000
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ── Scheduler behaviour ───────────────────────────────────────────────── */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            0

/* ── Hardware clocks ───────────────────────────────────────────────────── *
 *
 * MT7688 CPU runs at 580 MHz.
 * MIPS24KEc increments CP0 Count every 2 CPU cycles → Count rate 290 MHz.
 * The MIPS32 port uses CP0 Count/Compare for the tick; portCOMPARE_DELTA
 * (defined in portmacro.h) is derived from configCPU_CLOCK_HZ.
 */
#define configCPU_CLOCK_HZ                      580000000UL
#define configTICK_RATE_HZ                      100UL   /* 10 ms tick */

/* ── Priorities ────────────────────────────────────────────────────────── *
 *
 * eCos uses 0–31 where 0 = highest.  rtos.h maps eCos priorities to
 * FreeRTOS priorities by inverting: frt_prio = MAX_PRI - 1 - ecos_prio.
 * configMAX_PRIORITIES must be large enough that all eCos priorities fit.
 */
#define configMAX_PRIORITIES                    32

/* ── Stack / heap ──────────────────────────────────────────────────────── */
#define configMINIMAL_STACK_SIZE                256     /* in StackType_t words */
#define configTOTAL_HEAP_SIZE                   (8UL * 1024UL * 1024UL)  /* 8 MB */

/* ── Task names ────────────────────────────────────────────────────────── */
#define configMAX_TASK_NAME_LEN                 16

/* ── Static allocation ─────────────────────────────────────────────────── *
 *
 * All firmware tasks use xTaskCreateStatic() (static stacks / TCBs) to
 * avoid heap fragmentation.  Dynamic allocation is left available for lwIP
 * and application-level malloc() calls.
 */
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1

/* ── Feature switches ──────────────────────────────────────────────────── */
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    0
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configQUEUE_REGISTRY_SIZE               0

/* ── Stack-overflow detection (disabled for production) ────────────────── */
#define configCHECK_FOR_STACK_OVERFLOW          0

/* ── Run-time statistics (disabled) ────────────────────────────────────── */
#define configGENERATE_RUN_TIME_STATS           0

/* ── Co-routines (not used) ─────────────────────────────────────────────── */
#define configUSE_CO_ROUTINES                   0

/* ── Software timers (not used by firmware, but lwIP may need them) ─────── */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                8
#define configTIMER_TASK_STACK_DEPTH            configMINIMAL_STACK_SIZE

/* ── Idle task static allocation ────────────────────────────────────────── *
 *
 * When configSUPPORT_STATIC_ALLOCATION=1 the application must provide
 * vApplicationGetIdleTaskMemory() and (for timers) vApplicationGetTimerTaskMemory().
 * These are defined in firmware/bsp/mt7688_init.c.
 */

/* ── API inclusions ─────────────────────────────────────────────────────── */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xResumeFromISR                  1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     0
#define INCLUDE_xTaskGetIdleTaskHandle          0
#define INCLUDE_eTaskGetState                   0
#define INCLUDE_xEventGroupSetBitFromISR        1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskAbortDelay                 0
#define INCLUDE_xTaskGetHandle                  0
#define INCLUDE_xTaskResumeFromISR              1

/* ── Assert ─────────────────────────────────────────────────────────────── */
#define configASSERT(x)  do { if (!(x)) { for (;;); } } while (0)

#endif /* FREERTOS_CONFIG_H */
