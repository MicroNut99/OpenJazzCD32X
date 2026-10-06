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
#include <string.h>
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

size_t oj32x_heap_used(void) { return heap_brk ? (size_t)(heap_brk - heap_start) : 0; }
size_t oj32x_heap_size(void) { return (size_t)(heap_end - heap_start); }

void _exit(int code) { plat_exit(code); }

int _write(int fd, const char *buf, int len) { (void)fd; (void)buf; return len; }
int _read(int fd, char *buf, int len) { (void)fd; (void)buf; (void)len; return 0; }
int _open(const char *name, int flags, int mode) { (void)name; (void)flags; (void)mode; errno = ENOENT; return -1; }
int _close(int fd) { (void)fd; return -1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
/* J4c2: only the console streams 0-2 exist here. Streams from fmemopen (the data pack) have
 * no file (fd -1): newlib's __smakebuf asks _fstat about them, and the old version answered
 * "character device" for ANY fd and left st_blksize uninitialised - newlib then sized the
 * stream buffer from that garbage. Now: unknown fd = EBADF, so newlib uses BUFSIZ. */
int _fstat(int fd, struct stat *st)
{
	if (fd < 0 || fd > 2) { errno = EBADF; return -1; }
	memset(st, 0, sizeof *st);
	st->st_mode = S_IFCHR;
	return 0;
}
int _stat(const char *name, struct stat *st) { (void)name; (void)st; errno = ENOENT; return -1; }
int _isatty(int fd) { return fd >= 0 && fd <= 2; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }
int _unlink(const char *name) { (void)name; errno = ENOENT; return -1; }
int _link(const char *a, const char *b) { (void)a; (void)b; errno = EMLINK; return -1; }
int _rename(const char *a, const char *b) { (void)a; (void)b; errno = ENOENT; return -1; }
/* access/getcwd: plain C names (newlib for sh-elf has none; the SH linker shows them as
 * _access/_getcwd because it puts an underscore before every C name) */
int access(const char *name, int mode) { (void)name; (void)mode; errno = ENOENT; return -1; }
char *getcwd(char *buf, size_t size) { if (buf && size > 1) { buf[0] = '/'; buf[1] = 0; return buf; } errno = ERANGE; return NULL; }
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

/* ---- J4c3: heap check (diagnostic) -------------------------------------------------------
 * newlib's malloc (dlmalloc) keeps its blocks one after another from the first _sbrk address:
 * [prev size][size | flags][data...], sizes multiples of 8, the last block ("top") ending at
 * the current break. Walking them shows when a block header has been overwritten. Called at a
 * few points (port/sdl_oj.cpp, plat_log); the first one that finds damage stops with a red
 * screen naming that point, so the damage happened between it and the check before. */
void oj32x_heap_check(const char *where)
{
	static int busy;
	if (busy || heap_brk == NULL) return;
	busy = 1;
	char *p = heap_start;
	int n = 0;
	while (p + 8 <= heap_brk)
	{
		uint32_t size = ((volatile uint32_t *)p)[1] & ~7u;
		if (size < 16 || (size & 7) || p + size > heap_brk)
		{
			static char msg[96];
			char *m = msg;
			const char *t = "HEAP DAMAGED, FOUND AT ";
			while (*t) *m++ = *t++;
			for (t = where; *t && m < msg + 60; ) *m++ = *t++;
			t = " BLOCK ";
			while (*t) *m++ = *t++;
			int v = n, d = 1; while (v / d >= 10) d *= 10;
			for (; d; d /= 10) *m++ = (char)('0' + (v / d) % 10);
			*m = 0;
			busy = 0;
			plat_fatal(msg);
		}
		p += size;
		n++;
	}
	busy = 0;
}
