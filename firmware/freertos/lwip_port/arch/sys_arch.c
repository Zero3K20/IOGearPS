/*
 * sys_arch.c — lwIP OS abstraction layer backed by FreeRTOS.
 *
 * Implements all functions declared in lwip/sys.h that require OS support:
 *   sys_sem_new / free / signal / arch_sem_wait
 *   sys_mutex_new / free / lock / unlock
 *   sys_mbox_new / free / post / trypost / arch_mbox_fetch / tryfetch
 *   sys_thread_new
 *   sys_now
 *   sys_init
 */

#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/err.h"
#include "arch/sys_arch.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include <string.h>

/* ── Initialisation ─────────────────────────────────────────────────────── */
void sys_init(void)
{
    /* Nothing needed — FreeRTOS is already running when lwIP is initialised. */
}

/* ── Time (milliseconds since scheduler start) ──────────────────────────── */
u32_t sys_now(void)
{
    return (u32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* ── Semaphores ─────────────────────────────────────────────────────────── */
err_t sys_sem_new(sys_sem_t *sem, u8_t count)
{
    *sem = xSemaphoreCreateCounting(255, (UBaseType_t)count);
    if (*sem == NULL) {
        return ERR_MEM;
    }
    return ERR_OK;
}

void sys_sem_free(sys_sem_t *sem)
{
    if (*sem != NULL) {
        vSemaphoreDelete(*sem);
        *sem = NULL;
    }
}

void sys_sem_signal(sys_sem_t *sem)
{
    xSemaphoreGive(*sem);
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    TickType_t ticks = (timeout_ms == 0)
                       ? portMAX_DELAY
                       : (TickType_t)(timeout_ms / portTICK_PERIOD_MS + 1);

    if (xSemaphoreTake(*sem, ticks) == pdTRUE) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        return (u32_t)(elapsed * portTICK_PERIOD_MS);
    }
    return SYS_ARCH_TIMEOUT;
}

int sys_sem_valid(sys_sem_t *sem)
{
    return (*sem != NULL) ? 1 : 0;
}

void sys_sem_set_invalid(sys_sem_t *sem)
{
    *sem = NULL;
}

/* ── Mutexes ─────────────────────────────────────────────────────────────── */
err_t sys_mutex_new(sys_mutex_t *mutex)
{
    *mutex = xSemaphoreCreateMutex();
    if (*mutex == NULL) {
        return ERR_MEM;
    }
    return ERR_OK;
}

void sys_mutex_free(sys_mutex_t *mutex)
{
    if (*mutex != NULL) {
        vSemaphoreDelete(*mutex);
        *mutex = NULL;
    }
}

void sys_mutex_lock(sys_mutex_t *mutex)
{
    xSemaphoreTake(*mutex, portMAX_DELAY);
}

void sys_mutex_unlock(sys_mutex_t *mutex)
{
    xSemaphoreGive(*mutex);
}

int sys_mutex_valid(sys_mutex_t *mutex)
{
    return (*mutex != NULL) ? 1 : 0;
}

void sys_mutex_set_invalid(sys_mutex_t *mutex)
{
    *mutex = NULL;
}

/* ── Mailboxes ───────────────────────────────────────────────────────────── */
err_t sys_mbox_new(sys_mbox_t *mbox, int size)
{
    *mbox = xQueueCreate((UBaseType_t)size, sizeof(void *));
    if (*mbox == NULL) {
        return ERR_MEM;
    }
    return ERR_OK;
}

void sys_mbox_free(sys_mbox_t *mbox)
{
    if (*mbox != NULL) {
        vQueueDelete(*mbox);
        *mbox = NULL;
    }
}

void sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
    while (xQueueSend(*mbox, &msg, portMAX_DELAY) != pdTRUE)
        ;
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
    if (xQueueSend(*mbox, &msg, 0) == pdTRUE) {
        return ERR_OK;
    }
    return ERR_MEM;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xQueueSendFromISR(*mbox, &msg, &xHigherPriorityTaskWoken) == pdTRUE) {
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        return ERR_OK;
    }
    return ERR_MEM;
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout_ms)
{
    void        *dummy;
    TickType_t   start = xTaskGetTickCount();
    TickType_t   ticks = (timeout_ms == 0)
                         ? portMAX_DELAY
                         : (TickType_t)(timeout_ms / portTICK_PERIOD_MS + 1);
    void       **pmsg  = (msg != NULL) ? msg : &dummy;

    if (xQueueReceive(*mbox, pmsg, ticks) == pdTRUE) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        return (u32_t)(elapsed * portTICK_PERIOD_MS);
    }
    return SYS_ARCH_TIMEOUT;
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
    void *dummy;
    void **pmsg = (msg != NULL) ? msg : &dummy;

    if (xQueueReceive(*mbox, pmsg, 0) == pdTRUE) {
        return 0;
    }
    return SYS_ARCH_TIMEOUT;
}

int sys_mbox_valid(sys_mbox_t *mbox)
{
    return (*mbox != NULL) ? 1 : 0;
}

void sys_mbox_set_invalid(sys_mbox_t *mbox)
{
    *mbox = NULL;
}

/* ── Threads ─────────────────────────────────────────────────────────────── */
sys_thread_t sys_thread_new(const char *name,
                             lwip_thread_fn thread,
                             void          *arg,
                             int            stacksize,
                             int            prio)
{
    TaskHandle_t handle = NULL;
    xTaskCreate((TaskFunction_t)thread, name,
                (uint16_t)(stacksize / sizeof(StackType_t)),
                arg, (UBaseType_t)prio, &handle);
    return handle;
}
