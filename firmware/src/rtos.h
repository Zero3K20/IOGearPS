/*
 * rtos.h — FreeRTOS compatibility shim for the IOGear GPSU21 firmware.
 *
 * This header replaces eCos kernel headers (<cyg/kernel/kapi.h>,
 * <cyg/infra/diag.h>, etc.) in every firmware source file.  It maps the
 * eCos type aliases and API functions that the firmware uses directly to
 * their FreeRTOS equivalents, so the core service logic (httpd, ipp, lpr,
 * mdns) needs no changes beyond swapping the includes at the top.
 *
 * eCos → FreeRTOS API mapping summary
 * ─────────────────────────────────────────────────────────────────────────
 *  eCos type/call             FreeRTOS equivalent
 *  ─────────────────────────  ───────────────────────────────────────────
 *  cyg_uint32                 uint32_t
 *  cyg_uint8                  uint8_t
 *  cyg_ucount32               uint32_t
 *  cyg_addrword_t             uintptr_t  (32-bit on MIPS32)
 *  cyg_bool_t                 int  (0 = false)
 *  cyg_handle_t               TaskHandle_t
 *  cyg_thread                 StaticTask_t
 *  cyg_thread_entry_t         TaskFunction_t  (void (*)(void *))
 *  cyg_mutex_t                struct { StaticSemaphore_t; SemaphoreHandle_t }
 *  diag_printf(...)           printf(...)
 *  cyg_thread_create(...)     xTaskCreateStatic(...)
 *  cyg_thread_resume(h)       (no-op — xTaskCreateStatic starts immediately)
 *  cyg_thread_delay(ticks)    vTaskDelay(ticks)
 *  cyg_thread_exit()          vTaskDelete(NULL)
 *  cyg_mutex_init(m)          xSemaphoreCreateMutexStatic(...)
 *  cyg_mutex_lock(m)          xSemaphoreTake(m, portMAX_DELAY)
 *  cyg_mutex_unlock(m)        xSemaphoreGive(m)
 */

#ifndef GPSU21_RTOS_H
#define GPSU21_RTOS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ── Type aliases (eCos → portable stdint / FreeRTOS) ─────────────────── */

typedef uint32_t         cyg_uint32;
typedef uint8_t          cyg_uint8;
typedef uint32_t         cyg_ucount32;
typedef uintptr_t        cyg_addrword_t;
typedef int              cyg_bool_t;
typedef TaskHandle_t     cyg_handle_t;
typedef StaticTask_t     cyg_thread;

/* Thread entry function: match FreeRTOS TaskFunction_t exactly (void (*)(void *))
 * so that thread functions can be passed to xTaskCreateStatic/xTaskCreate without
 * a cast and without -Wcast-function-type warnings. */
typedef TaskFunction_t   cyg_thread_entry_t;

/*
 * Mutex: wrap StaticSemaphore_t + handle so that existing code declaring
 * "cyg_mutex_t my_lock;" as a struct member still compiles and the struct
 * remains self-contained (no separate handle pointer needed).
 */
typedef struct {
    StaticSemaphore_t buf;
    SemaphoreHandle_t handle;
} cyg_mutex_t;

/* ── Diagnostics ───────────────────────────────────────────────────────── */

/* diag_printf maps directly to the standard C printf (backed by UART0). */
#define diag_printf  printf

/* ── Priority mapping ──────────────────────────────────────────────────── *
 *
 * eCos:     lower number = higher priority  (0 = highest)
 * FreeRTOS: higher number = higher priority (configMAX_PRIORITIES-1 = highest)
 *
 * Mapping: freertos_prio = configMAX_PRIORITIES - 1 - ecos_prio
 * Clamped so we never pass 0 (idle) for a real task.
 */
#define CYG_TO_FRT_PRIO(p) \
    ((UBaseType_t)( \
        ((p) < (configMAX_PRIORITIES - 1u)) \
        ? (configMAX_PRIORITIES - 1u - (p)) \
        : 1u \
    ))

/* ── Thread primitives ─────────────────────────────────────────────────── */

/*
 * cyg_thread_create() — create and enqueue a FreeRTOS task.
 *
 * The eCos API takes a pre-allocated stack (char array) and a separate
 * thread-object struct.  xTaskCreateStatic() accepts the same arguments
 * so the mapping is 1-to-1; the char stack is cast to StackType_t*.
 *
 * Stack depth is converted from bytes to words (StackType_t = uint32_t).
 */
static inline void
cyg_thread_create(cyg_ucount32       priority,
                  cyg_thread_entry_t fn,
                  void              *arg,
                  const char        *name,
                  void              *stack,
                  uint32_t           stack_size,
                  cyg_handle_t      *phandle,
                  cyg_thread        *pobj)
{
    *phandle = xTaskCreateStatic(
        fn,
        name,
        stack_size / sizeof(StackType_t),
        arg,
        CYG_TO_FRT_PRIO(priority),
        (StackType_t *)stack,
        pobj);
}

/*
 * cyg_thread_resume() — in FreeRTOS, xTaskCreateStatic() immediately makes
 * the task ready-to-run, so there is no separate resume step.
 */
#define cyg_thread_resume(h)   ((void)(h))

/*
 * cyg_thread_delay(ticks) — both eCos and FreeRTOS measure delay in
 * scheduler ticks, so the mapping is direct.
 */
#define cyg_thread_delay(t)    vTaskDelay((TickType_t)(t))

/* cyg_thread_exit() — delete the calling task. */
#define cyg_thread_exit()      vTaskDelete(NULL)

/* ── Mutex primitives ──────────────────────────────────────────────────── */

static inline void cyg_mutex_init(cyg_mutex_t *m)
{
    m->handle = xSemaphoreCreateMutexStatic(&m->buf);
}

static inline int cyg_mutex_lock(cyg_mutex_t *m)
{
    return xSemaphoreTake(m->handle, portMAX_DELAY) == pdTRUE;
}

static inline void cyg_mutex_unlock(cyg_mutex_t *m)
{
    xSemaphoreGive(m->handle);
}

#endif /* GPSU21_RTOS_H */
