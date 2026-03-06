/*
 * sys_arch.h — lwIP OS abstraction layer using FreeRTOS.
 *
 * Maps lwIP's sys_sem_t, sys_mutex_t, sys_mbox_t, sys_thread_t to their
 * FreeRTOS equivalents.  Included by lwip/sys.h.
 */

#ifndef GPSU21_LWIP_SYS_ARCH_H
#define GPSU21_LWIP_SYS_ARCH_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* ── Semaphore ───────────────────────────────────────────────────────────── */
typedef SemaphoreHandle_t  sys_sem_t;
#define SYS_SEM_NULL        NULL

/* ── Mutex ───────────────────────────────────────────────────────────────── */
typedef SemaphoreHandle_t  sys_mutex_t;
#define SYS_MUTEX_NULL      NULL

/* ── Mailbox (message queue) ─────────────────────────────────────────────── */
typedef QueueHandle_t      sys_mbox_t;
#define SYS_MBOX_NULL       NULL

/* ── Thread ──────────────────────────────────────────────────────────────── */
typedef TaskHandle_t       sys_thread_t;

/* ── Timeout value (returned by sys_arch_mbox_fetch on timeout) ─────────── */
#define SYS_ARCH_TIMEOUT   0xFFFFFFFFUL

/* ── Critical-section protection ────────────────────────────────────────── */
#define SYS_ARCH_DECL_PROTECT(lev)    UBaseType_t lev
#define SYS_ARCH_PROTECT(lev)         (lev) = taskENTER_CRITICAL_FROM_ISR()
#define SYS_ARCH_UNPROTECT(lev)       taskEXIT_CRITICAL_FROM_ISR(lev)

#endif /* LWIP_ARCH_SYS_ARCH_H */
