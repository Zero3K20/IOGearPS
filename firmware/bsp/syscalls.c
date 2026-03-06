/*
 * syscalls.c — Newlib syscall stubs for bare-metal MIPS32 (MT7688).
 *
 * Newlib's printf / malloc need several syscalls.  We implement the minimum
 * required set:
 *   _write   — sends bytes to UART0 (used by printf)
 *   _sbrk    — provides a heap to malloc/free (used by lwIP and app code)
 *   _read    — stub (returns EOF)
 *   _close   — stub
 *   _fstat   — stub (marks fd as character device)
 *   _isatty  — stub (returns 1 for TTY)
 *   _lseek   — stub
 *   _exit    — panic loop
 *
 * Heap layout (from linker.ld):
 *   _heap_start .. _heap_end  — available to _sbrk
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <stddef.h>
#include <errno.h>
#include <stdint.h>
#include "mt7688_uart.h"

/* Linker-defined heap boundaries. */
extern char _heap_start[];
extern char _heap_end[];

static char *_brk = NULL;

void *_sbrk(ptrdiff_t incr)
{
    char *prev;

    if (_brk == NULL) {
        _brk = _heap_start;
    }

    prev = _brk;
    /* Guard against integer overflow and heap exhaustion. */
    if (incr > 0 && (_brk > _heap_end - incr)) {
        errno = ENOMEM;
        return (void *)-1;
    }
    if (incr < 0 && (_brk + incr < _heap_start)) {
        errno = EINVAL;
        return (void *)-1;
    }
    _brk += incr;
    return prev;
}

int _write(int fd, const char *buf, int len)
{
    int i;
    (void)fd;
    for (i = 0; i < len; i++) {
        uart_putc(buf[i]);
    }
    return len;
}

int _read(int fd, char *buf, int len)
{
    (void)fd; (void)buf; (void)len;
    return 0; /* EOF */
}

int _close(int fd)
{
    (void)fd;
    return -1;
}

int _fstat(int fd, struct stat *st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int fd)
{
    (void)fd;
    return 1;
}

off_t _lseek(int fd, off_t offset, int whence)
{
    (void)fd; (void)offset; (void)whence;
    return 0;
}

void _exit(int status)
{
    (void)status;
    for (;;)
        ;
}

/* kill / getpid stubs required by some newlib configurations. */
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
int _getpid(void) { return 1; }
