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
#include <stdlib.h>
#include <stdio.h>
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

/* J4c2: only the console streams 0-2 exist here. Streams from fmemopen (the data pack) have
 * no file (fd -1): newlib's __smakebuf asks _fstat about them, and the old version answered
 * "character device" for ANY fd and left st_blksize uninitialised - newlib then sized the
 * stream buffer from that garbage. Now: unknown fd = EBADF, so newlib uses BUFSIZ. */

/* ---- J11: saves and settings in the Sega CD's backup RAM --------------------------------
 * OpenJazz writes openjazz.cfg and SAVE.0-3 as files. Here they live in one 4 KB store that is
 * the backup-RAM file of main.c (CMD 56 load / 57 get word / 58 put word / 59 save - OpenLara's
 * OL-5 path; file name OPENJAZZ_JJ). Read at start-up; written back each time a file that was
 * opened for writing is closed. Layout: "OJSV", u16 count, then per file name[12] (NUL
 * padded), u16 size, data. A file that does not fit is not saved (the write fails). */
#include <fcntl.h>
#define STORE_BYTES 4096
#define STORE_FILES 8
#define FD_BASE 10
int plat_bram_read(uint8_t *buf, int bytes);    /* plat_mars.c */
int plat_bram_write(const uint8_t *buf, int bytes);

typedef struct { char name[12]; int off, size, used; } StoreFile;
static StoreFile store[STORE_FILES];
static uint8_t pool[STORE_BYTES];     /* the files' bytes, one after another */
static int pool_used;
static int store_loaded;
typedef struct { int file; int pos; int writing; int open; } StoreFd;
static StoreFd fds[4];
static uint8_t raw[STORE_BYTES];      /* the backup-RAM image (load / save) */

static void base_name(const char *path, char out[12])
{
	const char *b = path;
	for (const char *p = path; *p; ++p) if (*p == '/' || *p == '\\') b = p + 1;
	int i = 0;
	for (; b[i] && i < 11; ++i) out[i] = (char)((b[i] >= 'a' && b[i] <= 'z') ? b[i] - 32 : b[i]);
	for (; i < 12; ++i) out[i] = 0;
}

static void store_load(void)
{
	if (store_loaded) return;
	store_loaded = 1;
	int n = plat_bram_read(raw, STORE_BYTES);
	if (n < 6 || raw[0] != 'O' || raw[1] != 'J' || raw[2] != 'S' || raw[3] != 'V') return;
	int count = raw[4] << 8 | raw[5], at = 6;
	for (int i = 0; i < count && i < STORE_FILES && at + 14 <= n; i++)
	{
		StoreFile *f = &store[i];
		memcpy(f->name, raw + at, 12);
		f->size = raw[at + 12] << 8 | raw[at + 13];
		at += 14;
		if (at + f->size > n || pool_used + f->size > STORE_BYTES) break;
		f->off = pool_used;
		memcpy(pool + pool_used, raw + at, (size_t)f->size);
		pool_used += f->size;
		at += f->size;
		f->used = 1;
	}
}

static int store_find(const char *path)
{
	store_load();
	char n[12];
	base_name(path, n);
	for (int i = 0; i < STORE_FILES; i++)
		if (store[i].used && memcmp(store[i].name, n, 12) == 0) return i;
	return -1;
}

/* remove file i's bytes from the pool (the others move down) */
static void store_cut(int i)
{
	StoreFile *f = &store[i];
	memmove(pool + f->off, pool + f->off + f->size, (size_t)(pool_used - f->off - f->size));
	for (int k = 0; k < STORE_FILES; k++)
		if (store[k].used && store[k].off > f->off) store[k].off -= f->size;
	pool_used -= f->size;
	f->size = 0;
	f->off = pool_used;
}

static int store_commit(void)
{
	int count = 0, at = 6;
	for (int i = 0; i < STORE_FILES; i++)
	{
		if (!store[i].used) continue;
		if (at + 14 + store[i].size > STORE_BYTES) return 0;
		memcpy(raw + at, store[i].name, 12);
		raw[at + 12] = (uint8_t)(store[i].size >> 8);
		raw[at + 13] = (uint8_t)store[i].size;
		at += 14;
		memcpy(raw + at, pool + store[i].off, (size_t)store[i].size);
		at += store[i].size;
		count++;
	}
	raw[0] = 'O'; raw[1] = 'J'; raw[2] = 'S'; raw[3] = 'V';
	raw[4] = (uint8_t)(count >> 8); raw[5] = (uint8_t)count;
	return plat_bram_write(raw, at);
}

int _open(const char *name, int flags, int mode)
{
	(void)mode;
	int slot = -1;
	for (int i = 0; i < 4; i++) if (!fds[i].open) { slot = i; break; }
	if (slot < 0) { errno = EMFILE; return -1; }
	int f = store_find(name);
	const int writing = (flags & O_ACCMODE) != O_RDONLY;
	if (writing)
	{
		/* one file is written at a time, at the end of the pool (OpenJazz writes whole files) */
		for (int i = 0; i < 4; i++) if (fds[i].open && fds[i].writing) { errno = EBUSY; return -1; }
		if (f < 0)
		{
			for (int i = 0; i < STORE_FILES; i++) if (!store[i].used) { f = i; break; }
			if (f < 0) { errno = ENOSPC; return -1; }
			base_name(name, store[f].name);
			store[f].used = 1;
			store[f].size = 0;
			store[f].off = pool_used;
		}
		else store_cut(f);   /* rewritten from the start (OpenJazz never appends) */
	}
	else if (f < 0) { errno = ENOENT; return -1; }
	fds[slot].file = f;
	fds[slot].pos = 0;
	fds[slot].writing = writing;
	fds[slot].open = 1;
	return FD_BASE + slot;
}

static StoreFd *fd_of(int fd)
{
	if (fd < FD_BASE || fd >= FD_BASE + 4 || !fds[fd - FD_BASE].open) return NULL;
	return &fds[fd - FD_BASE];
}

int _read(int fd, char *buf, int len)
{
	StoreFd *d = fd_of(fd);
	if (!d) return 0;
	StoreFile *f = &store[d->file];
	int n = f->size - d->pos;
	if (n > len) n = len;
	if (n < 0) n = 0;
	memcpy(buf, pool + f->off + d->pos, (size_t)n);
	d->pos += n;
	return n;
}

int _write(int fd, const char *buf, int len)
{
	StoreFd *d = fd_of(fd);
	if (!d) return len;                          /* the console streams: nowhere to go */
	StoreFile *f = &store[d->file];
	if (d->pos != f->size) { errno = EINVAL; return -1; }   /* only appending (see _open) */
	if (pool_used + len > STORE_BYTES) { errno = ENOSPC; return -1; }
	memcpy(pool + pool_used, buf, (size_t)len);
	pool_used += len;
	f->size += len;
	d->pos += len;
	return len;
}

int _lseek(int fd, int off, int whence)
{
	StoreFd *d = fd_of(fd);
	if (!d) return 0;
	int base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? d->pos : store[d->file].size;
	if (base + off < 0 || (d->writing && base + off != store[d->file].size)) { if (!d->writing) { errno = EINVAL; return -1; } return d->pos; }
	d->pos = base + off;
	return d->pos;
}

int _close(int fd)
{
	StoreFd *d = fd_of(fd);
	if (!d) return -1;
	d->open = 0;
	if (d->writing && !store_commit())
	{
		errno = EIO;
		return -1;
	}
	return 0;
}

int _fstat(int fd, struct stat *st)
{
	if (fd_of(fd)) { memset(st, 0, sizeof *st); st->st_mode = S_IFREG; st->st_size = store[fds[fd - FD_BASE].file].size; st->st_blksize = 512; return 0; }
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
int access(const char *name, int mode) { (void)mode; if (store_find(name) >= 0) return 0; errno = ENOENT; return -1; }
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
