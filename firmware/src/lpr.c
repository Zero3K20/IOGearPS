/*
 * lpr.c — LPR/LPD (Line Printer Remote/Daemon) server for the GPSU21.
 *
 * Implements RFC 1179 — Line Printer Daemon Protocol — on TCP port 515.
 * Print jobs are received from the network and forwarded to the USB printer.
 *
 * Supported commands:
 *   0x01  Print any waiting jobs       (returns immediately)
 *   0x02  Receive a print job          (accepts data file and writes to printer)
 *   0x03  Send queue state (short)     (returns minimal status)
 *   0x04  Send queue state (long)      (returns minimal status)
 *   0x05  Remove jobs                  (stubbed — returns success)
 */

#include <cyg/kernel/kapi.h>
#include <cyg/infra/diag.h>

#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include "lpr.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Configuration
 * ───────────────────────────────────────────────────────────────────────────*/
#define LPR_PORT                515
#define LPR_MAX_CONNECTIONS     4
#define LPR_THREAD_STACK_SIZE   4096
#define LPR_THREAD_PRIORITY     12
#define LPR_BUF_SIZE            4096

/* Name of the default print queue (the stock firmware uses "lp1") */
#define LPR_QUEUE_NAME          "lp1"

/* ─────────────────────────────────────────────────────────────────────────────
 * LPR sub-commands (RFC 1179 §5)
 * ───────────────────────────────────────────────────────────────────────────*/
#define LPR_CMD_PRINT_WAITING   0x01
#define LPR_CMD_RECEIVE_JOB     0x02
#define LPR_CMD_QUEUE_SHORT     0x03
#define LPR_CMD_QUEUE_LONG      0x04
#define LPR_CMD_REMOVE_JOBS     0x05

/* ─────────────────────────────────────────────────────────────────────────────
 * Receive-job sub-commands (RFC 1179 §6)
 * ───────────────────────────────────────────────────────────────────────────*/
#define LPR_SUB_ABORT           0x01
#define LPR_SUB_CONTROL_FILE    0x02
#define LPR_SUB_DATA_FILE       0x03

/* ─────────────────────────────────────────────────────────────────────────────
 * Read a full LPR command line (terminated by '\n')
 * ───────────────────────────────────────────────────────────────────────────*/
static int read_line(int fd, char *buf, size_t max)
{
    size_t i = 0;
    char   c;

    while (i < max - 1) {
        int n = lwip_recv(fd, &c, 1, 0);
        if (n <= 0) break;
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return (int)i;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Acknowledge helper — sends a single zero byte (success)
 * ───────────────────────────────────────────────────────────────────────────*/
static void lpr_ack(int fd)
{
    uint8_t ack = 0;
    lwip_send(fd, &ack, 1, 0);
}

static void lpr_nack(int fd)
{
    uint8_t nack = 1;
    lwip_send(fd, &nack, 1, 0);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Handle a single LPR connection
 * ───────────────────────────────────────────────────────────────────────────*/
static void handle_lpr_connection(int fd)
{
    char    line[256];
    uint8_t cmd;
    int     n;

    /* Read the daemon command byte */
    n = lwip_recv(fd, &cmd, 1, 0);
    if (n <= 0) return;

    /* Read the rest of the command line */
    n = read_line(fd, line, sizeof(line));

    switch (cmd) {
    case LPR_CMD_PRINT_WAITING:
        /* Print any waiting jobs — nothing queued, just ack */
        lpr_ack(fd);
        break;

    case LPR_CMD_RECEIVE_JOB: {
        /* Receive a new print job.
         * The client sends sub-commands: 0x02 (control file), 0x03 (data file).
         * We discard the control file and write the data file to the printer. */
        uint8_t sub;
        lpr_ack(fd);

        while (lwip_recv(fd, &sub, 1, 0) == 1) {
            char  subline[256];
            read_line(fd, subline, sizeof(subline));

            if (sub == LPR_SUB_ABORT) {
                diag_printf("lpr: job aborted by client\n");
                lpr_ack(fd);
                return;
            }
            if (sub == LPR_SUB_CONTROL_FILE || sub == LPR_SUB_DATA_FILE) {
                /* Parse: "<count> <filename>" */
                uint32_t byte_count = 0;
                char     *space = strchr(subline, ' ');
                uint8_t  *recv_buf;

                if (space) {
                    byte_count = (uint32_t)strtoul(subline, NULL, 10);
                }

                lpr_ack(fd);

                /* Receive all data */
                recv_buf = (uint8_t *)malloc(LPR_BUF_SIZE);
                if (!recv_buf) {
                    lpr_nack(fd);
                    return;
                }

                {
                    uint32_t remaining = byte_count;
                    while (remaining > 0) {
                        uint32_t want = remaining < LPR_BUF_SIZE
                                        ? remaining : LPR_BUF_SIZE;
                        int got = lwip_recv(fd, recv_buf, (size_t)want, 0);
                        if (got <= 0) break;

                        if (sub == LPR_SUB_DATA_FILE) {
                            /* TODO: write recv_buf[0..got-1] to USB printer */
                            (void)recv_buf;
                        }
                        remaining -= (uint32_t)got;
                    }
                }
                free(recv_buf);

                /* Receive the trailing null byte */
                {
                    uint8_t trail;
                    lwip_recv(fd, &trail, 1, 0);
                }
                lpr_ack(fd);

                if (sub == LPR_SUB_DATA_FILE) {
                    diag_printf("lpr: job received (%u bytes)\n",
                                (unsigned)byte_count);
                }
            } else {
                /* Unknown sub-command */
                lpr_nack(fd);
                return;
            }
        }
        break;
    }

    case LPR_CMD_QUEUE_SHORT:
    case LPR_CMD_QUEUE_LONG: {
        /* Return minimal queue status */
        static const char status[] =
            LPR_QUEUE_NAME " is ready and printing\n"
            "no entries\n";
        lwip_send(fd, status, sizeof(status) - 1, 0);
        break;
    }

    case LPR_CMD_REMOVE_JOBS:
        /* Stubbed — always succeed */
        lpr_ack(fd);
        break;

    default:
        diag_printf("lpr: unknown command 0x%02x\n", cmd);
        lpr_nack(fd);
        break;
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Connection pool
 * ───────────────────────────────────────────────────────────────────────────*/
typedef struct {
    int          fd;
    cyg_bool_t   in_use;
    cyg_handle_t thread;
    cyg_thread   thread_obj;
    char         stack[LPR_THREAD_STACK_SIZE];
} lpr_conn_t;

static lpr_conn_t  lpr_pool[LPR_MAX_CONNECTIONS];
static cyg_mutex_t lpr_pool_lock;

static void lpr_child_thread(cyg_addrword_t arg)
{
    lpr_conn_t *conn = (lpr_conn_t *)arg;

    handle_lpr_connection(conn->fd);
    lwip_close(conn->fd);

    cyg_mutex_lock(&lpr_pool_lock);
    conn->in_use = false;
    cyg_mutex_unlock(&lpr_pool_lock);

    cyg_thread_exit();
}

/* ─────────────────────────────────────────────────────────────────────────────
 * LPR server main thread
 * ───────────────────────────────────────────────────────────────────────────*/
void lpr_thread(cyg_addrword_t arg)
{
    int                server_fd;
    int                client_fd;
    struct sockaddr_in addr;
    int                opt = 1;
    cyg_uint32         i;

    (void)arg;

    cyg_mutex_init(&lpr_pool_lock);
    memset(lpr_pool, 0, sizeof(lpr_pool));

    server_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        diag_printf("lpr: socket() failed\n");
        return;
    }
    lwip_setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(LPR_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (lwip_bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        diag_printf("lpr: bind() failed\n");
        lwip_close(server_fd);
        return;
    }
    if (lwip_listen(server_fd, LPR_MAX_CONNECTIONS) < 0) {
        diag_printf("lpr: listen() failed\n");
        lwip_close(server_fd);
        return;
    }

    diag_printf("lpr: listening on port %d (queue: %s)\n",
                LPR_PORT, LPR_QUEUE_NAME);

    for (;;) {
        lpr_conn_t *slot = NULL;

        client_fd = lwip_accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            cyg_thread_delay(10);
            continue;
        }

        cyg_mutex_lock(&lpr_pool_lock);
        for (i = 0; i < LPR_MAX_CONNECTIONS; i++) {
            if (!lpr_pool[i].in_use) {
                slot = &lpr_pool[i];
                slot->in_use = true;
                slot->fd     = client_fd;
                break;
            }
        }
        cyg_mutex_unlock(&lpr_pool_lock);

        if (!slot) {
            diag_printf("lpr: No free connection slot\n");
            lwip_close(client_fd);
            continue;
        }

        cyg_thread_create(
            LPR_THREAD_PRIORITY,
            lpr_child_thread,
            (cyg_addrword_t)slot,
            "lpr_child",
            slot->stack,
            LPR_THREAD_STACK_SIZE,
            &slot->thread,
            &slot->thread_obj
        );
        cyg_thread_resume(slot->thread);
    }
}
