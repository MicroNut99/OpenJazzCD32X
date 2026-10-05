/*
 * OpenJazz CD32X - minimal newlib system calls for the SH2 (from Tyrian 32X T3j,
 * port/mars/syscalls.c, + a few more that OpenJazz's file code can reach).
 * Without them, libc.a's own SH simulator syscalls (lib_a-syscalls.o) are linked in,
 * and they clash with newlib's reent.o over `errno' (J3a log).
 * malloc gets the SDRAM between the end of .bss and the stacks (__heap_start/__heap_end
 * come from the linker script; the J3a trial link has none, so they show as missing there).
 * No files or console here: the game's files will come through the plat layer.
 */
#include "plat.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>

#undef errno
extern int errno;

extern char heap_start[] __asm__("__heap_start");
extern char heap_end[] __asm__("__heap_end");

static char *heap_brk;

/* For the memory report: highest point the heap reached. */
char *oj32x_heap_peak;

void *_sbrk(ptrdiff_t incr)
{
	if (heap_brk == NULL)
		heap_brk = heap_start;
	if (heap_brk + incr > heap_end || heap_brk + incr < heap_start)
	{
		errno = ENOMEM;
		return (void *)-1;
	}
	char *prev = heap_brk;
	heap_brk += incr;
	if (heap_brk > oj32x_heap_peak)
		oj32x_heap_peak = heap_brk;
	return prev;
}

void _exit(int code) { plat_exit(code); }

int _write(int fd, const char *buf, int len) { (void)fd; (void)buf; return len; }
int _read(int fd, char *buf, int len) { (void)fd; (void)buf; (void)len; return 0; }
int _open(const char *name, int flags, int mode) { (void)name; (void)flags; (void)mode; errno = ENOENT; return -1; }
int _close(int fd) { (void)fd; return -1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _stat(const char *name, struct stat *st) { (void)name; (void)st; errno = ENOENT; return -1; }
int _isatty(int fd) { (void)fd; return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }
int _unlink(const char *name) { (void)name; errno = ENOENT; return -1; }
int _link(const char *a, const char *b) { (void)a; (void)b; errno = EMLINK; return -1; }
int _rename(const char *a, const char *b) { (void)a; (void)b; errno = ENOENT; return -1; }
int mkdir(const char *name, mode_t mode) { (void)name; (void)mode; errno = EROFS; return -1; }

int _gettimeofday(struct timeval *tv, void *tz)
{
	(void)tz;
	uint32_t ms = plat_ticks_ms();
	tv->tv_sec = ms / 1000;
	tv->tv_usec = (ms % 1000) * 1000;
	return 0;
}

clock_t _times(struct tms *buf)
{
	clock_t t = (clock_t)plat_ticks_ms();
	buf->tms_utime = t; buf->tms_stime = 0; buf->tms_cutime = 0; buf->tms_cstime = 0;
	return t;
}
