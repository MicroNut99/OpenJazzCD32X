/*
 * OpenJazz CD32X - platform layer for the real 32X (master SH2).
 *
 * Built from Tyrian 32X T3j's port/plat_mars.c (proven on hardware). What changed and why:
 *   - Commands go to OpenLara's Sub-CPU program (through its 68000 listener), not to Kobo's
 *     68000 cartridge program. The two use the same COMM protocol and command numbers for
 *     what is used here: 48 pad (COMM8 = pad, COMM10 = V-blank count), 52 Genesis display.
 *     (OpenLara handoff, "SH2 -> Sub-CPU commands"; cd32x/ref/openlara/subcpu/main.c.)
 *   - NO 0x0AAA/0x0BBB start-up handshake: OpenLara's loader (cdloader_sh2.c main) already
 *     did it with the Sub-CPU before it started us; nothing in the docs says it may be
 *     repeated.
 *   - The SH2 never writes the cart (OpenLara lesson 1): plat_cart_write stops with a red
 *     screen. Graphics will come into the cart from the disc (commands 38 + 46), J4b.
 *   - C++: the global constructors are run and .eh_frame is registered with the unwinder
 *     before main() (Tyrian is C and needed neither).
 *   - J4a boot test: before the game, one screen shows each start-up check, the live pad
 *     and the clock; Start begins the game. Everything visible for a phone photo.
 *   - Dropped for now (not needed to reach the title): sound on the slave SH2, CD files,
 *     mouse, network, music, DMA, the profiler.
 */
#include "plat.h"
#include "cart.h"
#include "pack.h"

#include <malloc.h>
#include <stdint.h>
#include <string.h>

#include "mars/font3x5.h"

/* ------------------------------------------------------------ registers */
#define MARS_ADAPTER_B   (*(volatile uint8_t  *)0x20004000)
#define MARS_INTMSK      (*(volatile uint16_t *)0x20004000)
#define MARS_CMD_CLR     (*(volatile uint16_t *)0x2000401A)
#define MARS_COMM0       (*(volatile uint16_t *)0x20004020)
#define MARS_COMM2       (*(volatile uint16_t *)0x20004022)
#define MARS_COMM8       (*(volatile uint16_t *)0x20004028)
#define MARS_COMM10      (*(volatile uint16_t *)0x2000402A)
#define MARS_COMM12      (*(volatile uint16_t *)0x2000402C)
#define VDP_DISPMODE     (*(volatile uint16_t *)0x20004100)
#define VDP_FBCTL        (*(volatile uint16_t *)0x2000410A)
#define VDP_CRAM         ((volatile uint16_t *)0x20004200)
#define FB16             ((volatile uint16_t *)0x24000000)
#define FB32             ((volatile uint32_t *)0x24000000)
#define CART_BYTES       ((const volatile uint8_t *)0x22000000)   /* cart, cache-through */

#define FBCTL_VBLK       0x8000
#define FBCTL_PEN        0x2000
#define FBCTL_FS         0x0001
#define DISP_MODE_256    0x0001
#define DISP_PRI_TEXT    0x0080

#define SH2_WTCSR_TCNT   (*(volatile uint16_t *)0xFFFFFE80)
#define SH2_WTCNT_R      (*(volatile uint8_t  *)0xFFFFFE81)
#define SH2_IPRA         (*(volatile uint16_t *)0xFFFFFEE2)
#define SH2_VCRWDT       (*(volatile uint16_t *)0xFFFFFEE4)

/* DIAGNOSTICS SWITCH (with OJ_DIAG_TEXT in cd32x/sh2/cdloader_sh2.c and KOBO_DIAG_TEXT /
 * KOBO_BOOT_TEXT in cd32x/subcpu/main.c): 1 = the Genesis text the loader switched on stays
 * until C++ start-up has worked (a crash before that still shows the Sub-CPU's last STATE);
 * 0 = text off at once. Either way it goes off for the engine's own screens (CMD 52). */
#ifndef OJ32X_DIAG_TEXT
#define OJ32X_DIAG_TEXT 0
#endif

/* SPLASH SWITCH (like Kobo's KOBO_SPLASH / SPLASH_HOLD, sh2_main.c K15/K16): 1 = the
 * loader's splash (picture + full bar, in both frame buffers) stays on screen when the engine
 * starts, through C++ start-up, for at least OJ32X_SPLASH_HOLD frames; 0 = the engine clears
 * the screen at once (Tyrian's start-up). */
#ifndef OJ32X_SPLASH
#define OJ32X_SPLASH 1
#endif
#define OJ32X_SPLASH_HOLD 120   /* frames at 60 Hz = 2 s */

#define CMD_GET_PAD      48
#define CMD_OPEN_FILE    38      /* main.c: COMM2 = n opens IMAGE<n>.RAW for cart slot n; length -> COMM8 (32 bit) */
#define CMD_CHUNK_TO_CART 46     /* main.c: COMM2 = chunk (bit 15: no read-back, OL-13); reply COMM12 = bad */
#define PACK_SLOT        8       /* IMAGE8.RAW -> cart 8 * 128 KB = 0x100000 = PLAT_CART_DATA_OFFSET */
#define MARS_COMM8_32    (*(volatile uint32_t *)0x20004028)
#define CMD_TEXT_ONOFF   52

#define PPHI_HZ          23011364u

/* Frame buffer layout (byte offsets) in each 128 KB buffer, as Tyrian:
 *   0x00000 line table, 0x00200 200 picture lines, 0x0FE00 blank line, 0x10000 spare */
#define FB_PIX_WORD      0x100
#define FB_LINE_WORDS    (PLAT_SCREEN_W / 2)
#define FB_BLANK_LINE    PLAT_GAME_H

/* ---------------------------------------------- symbols for crt0.s */
volatile unsigned mars_pwdt_ovf_count = 0;
volatile unsigned mars_swdt_ovf_count = 0;
static volatile uint32_t vblank_count = 0;

void pri_vbi_handler(void) { vblank_count++; }
void pri_cmd_handler(void) { MARS_CMD_CLR = 0; }
void sec_cmd_handler(void) { MARS_CMD_CLR = 0; }
void sec_dma1_handler(void) { }

/* The slave SH2: idle for now (Tyrian's sound mixer comes back with the sound round). */
void secondary(void)
{
	for (;;) { }
}

/* ------------------------------------------------------------ helpers */
static void set_sr(uint32_t sr) { __asm__ __volatile__("ldc %0,sr" : : "r"(sr) : "memory"); }

static void comm_pause(void)
{
	volatile int k;
	for (k = 0; k < 64; k++) { }
}

static int send_cmd(uint16_t cmd, uint32_t tries)
{
	MARS_COMM0 = cmd;
	for (uint32_t n = 0; n < tries && MARS_COMM0 != 0; n++)
		comm_pause();
	return MARS_COMM0 == 0;
}

static void take_fb(void)
{
	MARS_ADAPTER_B = 0x80;
	while ((MARS_INTMSK & 0x8000) == 0) { }
}

static void flip_wait(void)
{
	uint16_t cur = VDP_FBCTL & FBCTL_FS;
	VDP_FBCTL = cur ^ FBCTL_FS;
	while ((VDP_FBCTL & FBCTL_FS) == cur) { }
}

static void set_genesis_text(int on)
{
	MARS_COMM2 = on ? 1 : 0;
	send_cmd(CMD_TEXT_ONOFF, 800000UL);
	uint16_t m = VDP_DISPMODE;
	VDP_DISPMODE = on ? (uint16_t)(m | DISP_PRI_TEXT) : (uint16_t)(m & ~DISP_PRI_TEXT);
}

static void write_game_line_table(void)
{
	for (int y = 0; y < 256; ++y)
	{
		int gy = y - PLAT_TOP_BORDER;
		int line = (gy >= 0 && gy < PLAT_GAME_H) ? gy : FB_BLANK_LINE;
		FB16[y] = (uint16_t)(FB_PIX_WORD + line * FB_LINE_WORDS);
	}
}

static void clear_back_buffer(void)
{
	uint32_t longs = (0x200 + (PLAT_GAME_H + 1) * PLAT_SCREEN_W) / 4;
	for (uint32_t i = 0; i < longs; ++i)
		FB32[i] = 0;
}

/* -------------------------------------------------------------- start-up */
static void wdt_start(void)
{
	SH2_VCRWDT = (uint16_t)((65 << 8) | (SH2_VCRWDT & 0x00FF));
	SH2_IPRA = (uint16_t)((SH2_IPRA & 0xFF0F) | 0x0020);
	SH2_WTCSR_TCNT = 0x5A00;
	SH2_WTCSR_TCNT = 0xA53E;
}

#define STACK_GUARD_ADDR  ((volatile uint32_t *)0x0603B800)
#define STACK_GUARD_WORDS 16
#define STACK_GUARD_VALUE 0x5AA5C33Cu

static void stack_guard_set(void)
{
	for (int i = 0; i < STACK_GUARD_WORDS; ++i)
		STACK_GUARD_ADDR[i] = STACK_GUARD_VALUE ^ (uint32_t)i;
}

static void stack_guard_check(void)
{
	for (int i = 0; i < STACK_GUARD_WORDS; ++i)
		if (STACK_GUARD_ADDR[i] != (STACK_GUARD_VALUE ^ (uint32_t)i))
			plat_fatal("STACK OVERFLOW: A FUNCTION NEEDS MORE THAN THE 16 KB MASTER STACK");
}

/* C++ start-up (oj.ld collects these; no crtbegin/crtend with -nostartfiles) */
typedef void (*init_fn)(void);
extern init_fn init_array_start[] __asm__("__init_array_start");
extern init_fn init_array_end[] __asm__("__init_array_end");
extern init_fn ctors_start[] __asm__("__ctors_start");
extern init_fn ctors_end[] __asm__("__ctors_end");
extern const char eh_frame_start[] __asm__("__eh_frame_start");
void __register_frame_info(const void *begin, void *object);
static uint32_t eh_object[16];   /* libgcc's "struct object" (6 words on 32-bit; room to spare) */

static void cxx_start(void)
{
	__register_frame_info(eh_frame_start, eh_object);
	for (init_fn *f = init_array_start; f < init_array_end; ++f)
		(*f)();
	for (init_fn *f = ctors_end; f > ctors_start; )
	{
		--f;
		if (*f != 0 && *f != (init_fn)-1)
			(*f)();
	}
}

int main(int argc, char *argv[]);
void plat_exit(int code) __attribute__((noreturn));
static void boot_test(void);

/* ---- J4c: the data pack, with the loader's progress bar on the splash -------------------
 * The bar is the loader's (cdloader_sh2.c splash_bar: same place, same colours 253-255 from
 * IMAGE.RAW's palette), drawn in both frame buffers like the loader's splash_progress. */
#define BAR_X0 56
#define BAR_X1 236
#define BAR_Y0 186
#define BAR_Y1 198
static uint8_t bar_c(int x, int y, int fill)
{
	if (y < BAR_Y0 + 2 || y >= BAR_Y1 - 2 || x < BAR_X0 + 2 || x >= BAR_X1 - 2) return 255;
	return (x - (BAR_X0 + 2) < fill) ? 254 : 253;
}
static void splash_bar(uint32_t done, uint32_t total)
{
	volatile uint16_t *fb = FB16 + 0x100;
	uint32_t inner = (uint32_t)((BAR_X1 - 2) - (BAR_X0 + 2));
	int fill = total ? (int)(inner * done / total) : 0;
	for (int y = BAR_Y0; y < BAR_Y1; y++)
		for (int x = BAR_X0; x < BAR_X1; x += 2)
			fb[y * (PLAT_SCREEN_W / 2) + (x >> 1)] = (uint16_t)((bar_c(x, y, fill) << 8) | bar_c(x + 1, y, fill));
}
static void splash_progress(uint32_t done, uint32_t total)
{
	if (!OJ32X_SPLASH) return;
	for (int i = 0; i < 2; i++) { splash_bar(done, total); flip_wait(); }
}

static uint32_t pack_bytes;
static int pack_bad_chunks;

static void pack_load(void)
{
	MARS_COMM2 = PACK_SLOT;
	MARS_COMM8_32 = 0;
	if (!send_cmd(CMD_OPEN_FILE, 2000000UL)) { plat_log("PACK: OPEN TIMED OUT"); return; }
	uint32_t len = MARS_COMM8_32;
	if ((int32_t)len <= 0) { plat_log("PACK: IMAGE8.RAW NOT ON THE DISC"); return; }
	if (len > PLAT_CART_DATA_END - PLAT_CART_DATA_OFFSET) { plat_log("PACK: TOO BIG FOR THE CART"); return; }
	MARS_COMM8_32 = len;                   /* main.c CMD 46 reads the length from COMM8 */
	const uint32_t chunks = (len + 32767) >> 15;
	splash_progress(0, chunks);
	for (uint32_t c = 0; c < chunks; c++)
	{
		MARS_COMM2 = (uint16_t)(c | 0x8000);   /* OL-13: no read-back (level loads) */
		MARS_COMM12 = 0;
		if (!send_cmd(CMD_CHUNK_TO_CART, 2000000UL) || MARS_COMM12 != 0)
			pack_bad_chunks++;
		splash_progress(c + 1, chunks);
	}
	/* the 68000 wrote the cart behind the SH2's cache: purge it (as the loader's hand-over) */
	*(volatile uint8_t *)0xFFFFFE92 = 0x11;
	pack_bytes = len;
	if (!pack_mount((const uint8_t *)(0x02000000u + PLAT_CART_DATA_OFFSET)))
		plat_log("PACK: COPIED BUT NOT A PACK (OJPK)");
}

void plat_mars_entry(void)
{
	/* Tyrian's start-up minus the 68000 handshake (see the top of this file). */
	MARS_COMM0 = 0;
	MARS_INTMSK |= 0x0002;
	set_sr(0);

	take_fb();
	wdt_start();
	stack_guard_set();

	/* J4b: the Genesis text (the Sub-CPU's green/white diagnostics) stays as the loader left
	 * it until C++ start-up has worked; our own screens take over from there */
	VDP_DISPMODE = (uint16_t)((VDP_DISPMODE & (0x8000 | DISP_PRI_TEXT)) | DISP_MODE_256);
	if (!OJ32X_DIAG_TEXT)
		set_genesis_text(0);

	const uint32_t splash_from = vblank_count;
	if (!OJ32X_SPLASH)
	{
		for (int i = 0; i < 2; ++i)
		{
			clear_back_buffer();
			write_game_line_table();
			flip_wait();
		}
		for (int i = 0; i < 256; ++i)
			VDP_CRAM[i] = 0;
	}
	/* OJ32X_SPLASH 1: the frame buffers, line tables and palette are the loader's splash -
	 * nothing is drawn or flipped until the boot test */

	plat_log("C++ START-UP");
	cxx_start();
	plat_log("PACK LOADING");
	pack_load();
	if (OJ32X_SPLASH)
		while (vblank_count - splash_from < OJ32X_SPLASH_HOLD) { }
	set_genesis_text(0);
	boot_test();

	static char arg0[] = "openjazz";
	static char *argv[] = { arg0, NULL };
	plat_log("MAIN()");
	/* HW9: state 4B10 = the game runs - the Sub-CPU stops reprinting its diagnostic rows
	 * (each print goes through the 68000 and would slow the pad replies) */
	MARS_COMM12 = 0x4B10;
	plat_exit(main(1, argv));
}

void plat_init(void)
{
	/* Hardware is set up in plat_mars_entry(), before main(). */
}

/* ---------------------------------------------------------------- clock */
uint32_t plat_ticks_ms(void)
{
	static uint32_t last_ms;
	uint32_t ovf, cnt;

	do
	{
		ovf = mars_pwdt_ovf_count;
		cnt = SH2_WTCNT_R;
	} while (ovf != mars_pwdt_ovf_count);

	uint32_t ms;
	if (ovf == 0 && vblank_count > 120)
		ms = (uint32_t)((uint64_t)vblank_count * 1000u / 60u);
	else
	{
		uint64_t counts = (uint64_t)ovf * 256u + cnt;
		ms = (uint32_t)(counts * 4096u * 1000u / PPHI_HZ);
	}
	if ((int32_t)(ms - last_ms) < 0)
		ms = last_ms;
	last_ms = ms;
	return ms;
}

uint32_t plat_random_seed(void)
{
	uint32_t s = ((uint32_t)mars_pwdt_ovf_count << 8) ^ SH2_WTCNT_R ^ (vblank_count * 2654435761u);
	return s ^ MARS_COMM10;
}

/* ------------------------------------------------------------------ pad */
uint16_t plat_read_pad(void)
{
	static uint16_t pad;
	static uint32_t last_ms = 0xFFFFFFFFu;

	uint32_t now = plat_ticks_ms();
	if (last_ms != 0xFFFFFFFFu && now - last_ms < 4)
		return pad;
	last_ms = now;

	if (!send_cmd(CMD_GET_PAD, 40000UL))
		return pad;

	uint16_t bits = MARS_COMM8;
	pad = (bits & 0xF000) == 0xF000 ? 0 : (uint16_t)(bits & 0x0FFF);
	return pad;
}

/* ---------------------------------------------------------------- frame pacing */
#define PACE_HISTORY 16
#define PACE_MAX     6
static uint32_t last_flip_vblank;
static uint8_t pace_history[PACE_HISTORY];
static unsigned int pace_pos;
static int pace_n = 1;

static void pace_before_flip(void)
{
	const uint32_t natural = vblank_count - last_flip_vblank + 1;
	if (natural <= PACE_MAX)
	{
		pace_history[pace_pos++ % PACE_HISTORY] = (uint8_t)natural;
		uint8_t sorted[PACE_HISTORY];
		for (int i = 0; i < PACE_HISTORY; ++i)
		{
			int j = i;
			while (j > 0 && sorted[j - 1] > pace_history[i])
			{
				sorted[j] = sorted[j - 1];
				--j;
			}
			sorted[j] = pace_history[i];
		}
		const int n = sorted[PACE_HISTORY - 3];
		pace_n = n > 0 ? n : 1;
		while (vblank_count - last_flip_vblank < (uint32_t)(pace_n - 1)) { }
	}
	while (VDP_FBCTL & FBCTL_VBLK) { }
}

/* -------------------------------------------------------------- display */
static void present_finish(volatile uint32_t *blank_line, const uint16_t *palette555)
{
	for (int x = 0; x < PLAT_SCREEN_W / 4; ++x)
		blank_line[x] = 0;
	pace_before_flip();
	flip_wait();
	last_flip_vblank = vblank_count;
	while ((VDP_FBCTL & FBCTL_PEN) == 0) { }
	for (int i = 0; i < 256; ++i)
		VDP_CRAM[i] = palette555[i];
}

void plat_present(const uint8_t *pixels, int pitch, const uint16_t *palette555)
{
	stack_guard_check();
	write_game_line_table();

	volatile uint32_t *dst = FB32 + (FB_PIX_WORD * 2) / 4;
	for (int y = 0; y < PLAT_GAME_H; ++y)
	{
		const uint32_t *src = (const uint32_t *)(pixels + y * pitch);
		for (int x = 0; x < PLAT_SCREEN_W / 4; x += 4)
		{
			dst[0] = src[x + 0];
			dst[1] = src[x + 1];
			dst[2] = src[x + 2];
			dst[3] = src[x + 3];
			dst += 4;
		}
	}
	present_finish(dst, palette555);
}

uint8_t *plat_fb_area(int area)
{
	return (uint8_t *)FB16 + (area == PLAT_FB_DISPLAY ? FB_PIX_WORD * 2 : 0x10000);
}

/* ---------------------------------------------------------------- cart */
uint8_t *plat_cart_data(uint32_t *size)
{
	*size = PLAT_CART_DATA_END - PLAT_CART_DATA_OFFSET;
	return (uint8_t *)(0x02000000u + PLAT_CART_DATA_OFFSET);
}

void plat_cart_write(uint32_t offset, const void *src, size_t bytes)
{
	(void)offset; (void)src; (void)bytes;
	plat_fatal("CART WRITE: THE SH2 CANNOT WRITE THE CART (J4B: GRAPHICS COME FROM THE DISC)");
}

/* ---------------------------------------------------------------- memory */
size_t oj32x_heap_used(void);
size_t oj32x_heap_size(void);
extern char *oj32x_heap_peak;
extern char heap_start_sym[] __asm__("__heap_start");

size_t plat_heap_free(void)
{
	return oj32x_heap_size() - oj32x_heap_used() + (size_t)mallinfo().fordblks;
}

void plat_mem_mark(const char *label)
{
	(void)label;
}

/* ------------------------------------------------------------- messages */
static char last_log[80];
static char prev_log[2][80];   /* J4c2: the two lines before the last one */

void plat_log(const char *msg)
{
	/* Kept for the error screens: the last three lines */
	strcpy(prev_log[0], prev_log[1]);
	strcpy(prev_log[1], last_log);
	strncpy(last_log, msg, sizeof last_log - 1);
	last_log[sizeof last_log - 1] = '\0';
	for (char *p = last_log; *p; ++p)
		if (*p == '\n')
			*p = '\0';
}

/* Text screen in the back buffer: 224 lines, 3x5 glyphs drawn 2x (8x12 cells). */
static void text_screen(uint16_t bg, uint16_t fg, const char *const *lines, int count)
{
	for (int y = 0; y < 256; ++y)
		FB16[y] = (uint16_t)(FB_PIX_WORD + (y < PLAT_SCREEN_H ? y : 0) * FB_LINE_WORDS);

	/* J4b7: clear with 32-bit writes. Console video: byte writes of 0 did not clear the
	 * frame buffer (the boot test was drawn over the splash), while clear_back_buffer's
	 * 32-bit writes did (J4a: clean screens). main.c K12 saw zero data not landing too. */
	volatile uint8_t *pix = (volatile uint8_t *)FB16 + FB_PIX_WORD * 2;
	volatile uint32_t *pix32 = FB32 + (FB_PIX_WORD * 2) / 4;
	for (int i = 0; i < PLAT_SCREEN_W * PLAT_SCREEN_H / 4; ++i)
		pix32[i] = 0;

	int row = 1;
	for (int l = 0; l < count && row < PLAT_SCREEN_H / 12; ++l)
	{
		const char *s = lines[l] ? lines[l] : "";
		int col = 1;
		for (; *s && row < PLAT_SCREEN_H / 12; ++s)
		{
			if (*s == '\n' || col >= PLAT_SCREEN_W / 8 - 1)
			{
				row++;
				col = 1;
				if (*s == '\n')
					continue;
			}
			int c = (unsigned char)*s;
			if (c >= 'a' && c <= 'z')
				c -= 32;
			if (c < 32 || c > 95)
				c = '?';
			uint16_t g = font3x5[c - 32];
			for (int gy = 0; gy < 5; ++gy)
				for (int gx = 0; gx < 3; ++gx)
					if (g & (1u << (14 - (gy * 3 + gx))))
					{
						int px = col * 8 + gx * 2, py = row * 12 + gy * 2;
						volatile uint8_t *p = pix + py * PLAT_SCREEN_W + px;
						p[0] = p[1] = p[PLAT_SCREEN_W] = p[PLAT_SCREEN_W + 1] = 1;
					}
			col++;
		}
		row++;
	}

	flip_wait();
	while ((VDP_FBCTL & FBCTL_PEN) == 0) { }
	VDP_CRAM[0] = bg;
	VDP_CRAM[1] = fg;
}

static void dec(char *out, uint32_t v)
{
	char tmp[11];
	int n = 0;
	do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
	while (n)
		*out++ = tmp[--n];
	*out = '\0';
}

static void hex(char *out, uint32_t v, int digits)
{
	for (int i = digits - 1; i >= 0; --i, v >>= 4)
		out[i] = "0123456789ABCDEF"[v & 15];
	out[digits] = '\0';
}

/* "HEAP 123456 USED, PEAK 130000 OF 172000" */
static const char *heap_line(void)
{
	static char line[64];
	char *p = line;
	strcpy(p, "HEAP "); p += 5;
	dec(p, (uint32_t)oj32x_heap_used()); p += strlen(p);
	strcpy(p, " USED, PEAK "); p += strlen(p);
	dec(p, oj32x_heap_peak ? (uint32_t)(oj32x_heap_peak - heap_start_sym) : 0); p += strlen(p);
	strcpy(p, " OF "); p += strlen(p);
	dec(p, (uint32_t)oj32x_heap_size());
	return line;
}

void plat_fatal(const char *msg)
{
	set_sr(0xF0);
	const char *lines[] = { "OPENJAZZ 32X - FATAL ERROR", "", msg, "", "LAST LOG:", prev_log[0], prev_log[1], last_log, "", heap_line() };
	text_screen(0x0010 /* dark red */, 0x7FFF, lines, 10);
	for (;;) { }
}

/* J4c2: crt0 passes the old stack pointer and PR as well. The call chain: PR, then the
 * words on the old stack that point into the engine's code (return addresses saved by the
 * callers; some may be stale - look them up with tools/where.sh). */
extern char text_start_sym[] __asm__("__text_start");
extern char text_end_sym[] __asm__("__text_end");

void plat_exception(uint32_t kind, uint32_t pc, uint32_t sp, uint32_t pr)
{
	static const char *const kinds[] = { "?", "ILLEGAL INSTRUCTION", "INVALID SLOT INSTRUCTION",
	                                     "ADDRESS ERROR (MISALIGNED ACCESS?)", "DMA ADDRESS ERROR",
	                                     "OTHER EXCEPTION" };
	static char pc_line[40] = "PC ";
	static char chain[3][48];
	set_sr(0xF0);
	hex(pc_line + 3, pc, 8);
	strcat(pc_line, "  PR ");
	hex(pc_line + strlen(pc_line), pr, 8);
	int found = 0;
	if (sp >= 0x06000000u && sp < 0x06040000u && (sp & 3) == 0)
	{
		const volatile uint32_t *st = (const volatile uint32_t *)(uintptr_t)(sp | 0x20000000u);
		for (int i = 2; i < 256 && found < 9 && sp + 4u * i < 0x06040000u; ++i)
		{
			uint32_t v = st[i];
			if (v >= (uint32_t)(uintptr_t)text_start_sym && v < (uint32_t)(uintptr_t)text_end_sym && (v & 1) == 0)
			{
				char *l = chain[found / 3];
				if (found % 3 == 0) strcpy(l, "CALLS ");
				hex(l + strlen(l), v, 8);
				strcat(l, " ");
				found++;
			}
		}
	}
	const char *lines[] = { "OPENJAZZ 32X - SH2 EXCEPTION", kinds[kind < 6 ? kind : 0], pc_line,
	                        chain[0], chain[1], chain[2], "(TOOLS/WHERE.SH <ADDRESS>)",
	                        "LAST LOG:", prev_log[0], prev_log[1], last_log, heap_line() };
	text_screen(0x0010, 0x7FFF, lines, 12);
	for (;;) { }
}

void plat_exit(int code)
{
	set_sr(0xF0);
	static char code_line[24] = "EXIT CODE ";
	if (code < 0) { code_line[10] = '-'; dec(code_line + 11, (uint32_t)-code); }
	else dec(code_line + 10, (uint32_t)code);
	const char *lines[] = { "OPENJAZZ 32X", "", code == 0 ? "THE GAME HAS ENDED." : "THE GAME STOPPED WITH AN ERROR.",
	                        code_line, "", "LAST LOG:", last_log, "", heap_line() };
	text_screen(0x2800 /* dark blue */, 0x7FFF, lines, 9);
	for (;;) { }
}

/* ------------------------------------------------------------ J4a boot test */
/* port/mars_boottest.cpp: C++ checks (constructor ran, throw/catch, new/delete) */
int oj32x_test_ctor(void);
int oj32x_test_throw(void);
int oj32x_test_new(void);

static void line_add(char *line, const char *label, int ok, const char *detail)
{
	strcpy(line, label);
	strcat(line, ok ? "OK" : "FAILED");
	if (detail && *detail) { strcat(line, "  "); strcat(line, detail); }
}

static void boot_test(void)
{
	static char l_build[64], l_cart[64], l_ctor[64], l_new[64], l_throw[64], l_heap[64];
	static char l_pad[64], l_time[64], l_pack[64];
	char tmp[24];

	strcpy(l_build, "BUILD J4C2 " __DATE__ " " __TIME__);

	/* the engine's own header in the cart (the loader checked it too) */
	int cart_ok = CART_BYTES[0x100] == 'S' && CART_BYTES[0x101] == 'E' &&
	              CART_BYTES[0x102] == 'G' && CART_BYTES[0x103] == 'A' &&
	              CART_BYTES[0x3C0] == 'O' && CART_BYTES[0x3C1] == 'p';
	line_add(l_cart, "CART READ: ", cart_ok, "");

	line_add(l_ctor, "C++ CONSTRUCTORS: ", oj32x_test_ctor(), "");
	line_add(l_new, "C++ NEW/DELETE: ", oj32x_test_new(), "");

	/* if the throw is not caught, the C++ runtime calls abort(): the blue end screen then
	   shows this as its last log line */
	plat_log("EXCEPTION TEST: THROWING");
	int t = oj32x_test_throw();
	plat_log("EXCEPTION TEST: DONE");
	hex(tmp, (uint32_t)t, 4);
	line_add(l_throw, "C++ EXCEPTIONS: ", t == 0x0123, tmp);

	strcpy(l_heap, heap_line());

	/* J4c: the data pack */
	if (pack_count() > 0)
	{
		strcpy(l_pack, "PACK: OK  ");
		dec(l_pack + strlen(l_pack), pack_bytes);
		strcat(l_pack, " BYTES");
		if (pack_bad_chunks) { strcat(l_pack, "  BAD CHUNKS "); dec(l_pack + strlen(l_pack), (uint32_t)pack_bad_chunks); }
	}
	else
	{
		strcpy(l_pack, "PACK: FAILED  ");
		strncat(l_pack, last_log, 40);
	}

	/* live screen: pad + clock, until Start */
	uint16_t pad = 0;
	uint32_t frames = 0;
	do
	{
		pad = plat_read_pad();
		strcpy(l_pad, "PAD ");
		hex(l_pad + 4, pad, 4);
		strcat(l_pad, "  START = RUN THE GAME");
		strcpy(l_time, "CLOCK ");
		dec(l_time + 6, plat_ticks_ms());
		strcat(l_time, " MS  VBL ");
		dec(l_time + strlen(l_time), vblank_count);
		strcat(l_time, "  FR ");
		dec(l_time + strlen(l_time), frames++);
		const char *lines[] = { "OPENJAZZ 32X - BOOT TEST", l_build, "", l_cart, l_ctor, l_new, l_throw,
		                        l_pack, "", l_heap, "", l_pad, l_time };
		text_screen(0x0000, 0x7FFF, lines, 13);
	} while (!(pad & PAD_START));

	/* wait for Start to be let go, so the game does not see it */
	while (plat_read_pad() & PAD_START) { }

	/* back to the game's display set-up */
	for (int i = 0; i < 2; ++i)
	{
		clear_back_buffer();
		write_game_line_table();
		flip_wait();
	}
	for (int i = 0; i < 256; ++i)
		VDP_CRAM[i] = 0;
}

/* What crtbegin.o normally provides (we link with -nostartfiles): the handle C++ uses to
   register static destructors (__cxa_atexit). Same fix as the J3a trial link. */
void *__dso_handle = 0;
