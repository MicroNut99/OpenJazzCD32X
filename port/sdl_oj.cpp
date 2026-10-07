/*
 * OpenJazz CD32X - SDL 1.2 subset on the plat layer (see port/include/SDL.h).
 *
 * Surfaces are 8-bit only. OpenJazz recolours sprites and fonts through SDL 1.2's
 * palette-mapped blits: every surface has a "logical" palette that is grey ramp
 * i -> (i,i,i), and a recolour sets some entries to another grey (k,k,k), so the
 * blit turns index i into k. Here that is a 256-byte table per surface (remap), and
 * blits copy straight when it is the identity - the common, fast case.
 *
 * The pad becomes key events (oj32x_pad_mode picks the play or the menu layout).
 */
#include "SDL.h"
extern "C" {
#include "plat.h"
}
#include "oj32x.h"
extern "C" {
#include "cart.h"
#include "pack.h"
}

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <strings.h>

/* J4c3: heap check points on the 32X (port/mars_syscalls.c oj32x_heap_check) */
/* J5: OJ32X_HEAP_CHECK 1 = check the heap at every picture create/free/blit (slow: a walk over
 * all blocks each time - a diagnostic); 0 = only at log lines (port/plat_mars.c) */
#ifndef OJ32X_HEAP_CHECK
#define OJ32X_HEAP_CHECK 0
#endif
#if defined(OJ32X_MARS) && OJ32X_HEAP_CHECK
extern "C" void oj32x_heap_check(const char *where);
#define HEAP_CHECK(w) oj32x_heap_check(w)
#else
#define HEAP_CHECK(w) ((void)0)
#endif


/* ------------------------------------------------------------------ init/time */

static const char *last_error = "";

int SDL_Init(Uint32 flags) { (void)flags; plat_init(); return 0; }
void SDL_Quit(void) { }
const char *SDL_GetError(void) { return last_error; }
const SDL_version *SDL_Linked_Version(void) { static SDL_version v = {1, 2, 0}; return &v; }

Uint32 SDL_GetTicks(void) { return plat_ticks_ms(); }

void SDL_Delay(Uint32 ms)
{
	Uint32 start = plat_ticks_ms();
	while (plat_ticks_ms() - start < ms) { }
}

/* ------------------------------------------------------------------ surfaces */

/* What a surface carries besides the SDL fields. Most surfaces never change their
 * palette, so they share one grey ramp and have no table (J2: a palette + table per
 * surface cost ~1.3 KB each, 450 KB over a level's 345 surfaces). A surface gets its own
 * colours and remap table (256 + 1024 bytes) only when it is recoloured. */
struct Surf {
	SDL_Surface s;
	SDL_PixelFormat fmt;
	SDL_Palette pal;
	SDL_Color *own_colors;  /* nullptr: the shared grey ramp */
	Uint8 *remap;           /* nullptr: identity */
	bool owns_pixels;
	bool in_cart;           /* pixels in the RAM cart (read-only) */
	bool in_pack;           /* J4c: pixels are a picture in a cart pack (read-only, shared) */
};

static SDL_Color grey_ramp[256];

static Surf *surf_of(SDL_Surface *s) { return reinterpret_cast<Surf *>(s); }

static void update_remap(Surf *u)
{
	bool identity = true;
	for (int i = 0; i < 256; ++i) {
		u->remap[i] = u->own_colors[i].r;  /* the logical palette is a grey ramp: r is the index */
		if (u->remap[i] != i) identity = false;
	}
	if (identity) {  /* back to normal (restorePalette): drop the copy */
		delete[] u->own_colors;
		delete[] u->remap;
		u->own_colors = nullptr;
		u->remap = nullptr;
		u->pal.colors = grey_ramp;
	}
}

static Surf *new_surface(int w, int h, void *pixels)
{
	if (grey_ramp[255].r != 255)
		for (int i = 0; i < 256; ++i) grey_ramp[i].r = grey_ramp[i].g = grey_ramp[i].b = (Uint8)i;
	Surf *u = new Surf();
	u->s.format = &u->fmt;
	u->fmt.palette = &u->pal;
	u->fmt.BitsPerPixel = 8;
	u->fmt.BytesPerPixel = 1;
	u->pal.ncolors = 256;
	u->pal.colors = grey_ramp;
	u->s.w = w;
	u->s.h = h;
	u->s.pitch = (Uint16)w;
	if (pixels) {
		u->s.pixels = pixels;
	} else {
		u->s.pixels = calloc((size_t)w * h, 1);
		u->owns_pixels = true;
	}
	u->s.clip_rect.x = 0;
	u->s.clip_rect.y = 0;
	u->s.clip_rect.w = (Uint16)w;
	u->s.clip_rect.h = (Uint16)h;
	u->s.refcount = 1;
	return u;
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int bpp, Uint32 r, Uint32 g, Uint32 b, Uint32 a)
{
	(void)flags; (void)r; (void)g; (void)b; (void)a;
	if (bpp != 8) { last_error = "only 8-bit surfaces"; return nullptr; }
	HEAP_CHECK("CREATESURFACE");
	Surf *u = new_surface(w, h, nullptr);
	if (!u->s.pixels) { delete u; last_error = "out of memory"; return nullptr; }
	return &u->s;
}

void SDL_FreeSurface(SDL_Surface *s)
{
	if (!s) return;
	HEAP_CHECK("FREESURFACE");
	Surf *u = surf_of(s);
	if (u->owns_pixels) free(u->s.pixels);
	if (u->in_cart) cart_free(u->s.pixels);
	delete[] u->own_colors;
	delete[] u->remap;
	delete u;
}

/* J3: a surface whose pixels never change after loading: they go to the RAM cart.
 * Falls back to an ordinary surface when the cart is full. */
SDL_Surface *oj32x_cart_surface(const unsigned char *pixels, int w, int h)
{
	/* J4c: the PC build records the picture for the pack (T32X_BAKE); a picture that is in a
	 * mounted pack is used in place */
	pack_bake_picture(pixels, w, h);
	const uint8_t *baked = pack_picture(pixels, w, h);
	if (baked) {
		Surf *u = new_surface(w, h, const_cast<uint8_t *>(baked));
		u->in_pack = true;
		return &u->s;
	}
#ifdef OJ32X_MARS
	/* the SH2 cannot write the cart: a picture the pack does not have stays in SDRAM */
	{
		char msg[64];
		snprintf(msg, sizeof msg, "PICTURE %dX%d NOT IN THE PACK", w, h);
		plat_log(msg);
	}
	SDL_Surface *s = SDL_CreateRGBSurface(0, w, h, 8, 0, 0, 0, 0);
	if (s) memcpy(s->pixels, pixels, (size_t)w * h);
	return s;
#else
	void *p = cart_alloc((size_t)w * h);
	if (p == nullptr) {
		SDL_Surface *s = SDL_CreateRGBSurface(0, w, h, 8, 0, 0, 0, 0);
		if (s) memcpy(s->pixels, pixels, (size_t)w * h);
		return s;
	}
	cart_write(p, pixels, (size_t)w * h);
	Surf *u = new_surface(w, h, p);
	u->in_cart = true;
	return &u->s;
#endif
}

/* J3: a surface that is finished (e.g. a font atlas, built by blitting) moves to the cart;
 * the SDRAM copy is freed. Colour key and recolouring are kept. */
SDL_Surface *oj32x_freeze(SDL_Surface *s)
{
	Surf *u = surf_of(s);
	if (u->in_cart || u->in_pack || s->pitch != s->w) return s;
	SDL_Surface *c = oj32x_cart_surface(static_cast<const unsigned char *>(s->pixels), s->w, s->h);
	if (!c || !(surf_of(c)->in_cart || surf_of(c)->in_pack)) { if (c) SDL_FreeSurface(c); return s; }
	c->flags = s->flags;
	c->format->colorkey = s->format->colorkey;
	if (u->own_colors) SDL_SetPalette(c, SDL_LOGPAL, u->own_colors, 0, 256);
	SDL_FreeSurface(s);
	return c;
}

int SDL_SetClipRect(SDL_Surface *s, const SDL_Rect *r)
{
	if (!r) {
		s->clip_rect.x = 0; s->clip_rect.y = 0;
		s->clip_rect.w = (Uint16)s->w; s->clip_rect.h = (Uint16)s->h;
		return 1;
	}
	int x0 = r->x < 0 ? 0 : r->x, y0 = r->y < 0 ? 0 : r->y;
	int x1 = r->x + r->w, y1 = r->y + r->h;
	if (x1 > s->w) x1 = s->w;
	if (y1 > s->h) y1 = s->h;
	if (x1 < x0) x1 = x0;
	if (y1 < y0) y1 = y0;
	s->clip_rect.x = (Sint16)x0; s->clip_rect.y = (Sint16)y0;
	s->clip_rect.w = (Uint16)(x1 - x0); s->clip_rect.h = (Uint16)(y1 - y0);
	return x1 > x0 && y1 > y0;
}

int SDL_SetColorKey(SDL_Surface *s, Uint32 flag, Uint32 key)
{
	if (flag & SDL_SRCCOLORKEY) { s->flags |= SDL_SRCCOLORKEY; s->format->colorkey = key; }
	else s->flags &= ~SDL_SRCCOLORKEY;
	return 0;
}

/* the screen's physical palette (what is shown); its logical one stays the grey ramp */
static SDL_Surface *screen_surface;
static uint16_t phys555[256];

int SDL_SetPalette(SDL_Surface *s, int flags, const SDL_Color *colors, int first, int n)
{
	if (first < 0 || first + n > 256) return 0;
	if (flags & SDL_LOGPAL) {
		Surf *u = surf_of(s);
		bool same = true;
		for (int i = 0; i < n && same; ++i)
			same = memcmp(&u->pal.colors[first + i], &colors[i], 3) == 0;
		if (!same) {
			if (!u->own_colors) {
				u->own_colors = new SDL_Color[256];
				u->remap = new Uint8[256];
				memcpy(u->own_colors, grey_ramp, sizeof grey_ramp);
				u->pal.colors = u->own_colors;
			}
			for (int i = 0; i < n; ++i) u->own_colors[first + i] = colors[i];
			update_remap(u);
		}
	}
	if ((flags & SDL_PHYSPAL) && s == screen_surface) {
		for (int i = 0; i < n; ++i) {
			const SDL_Color &c = colors[i];
			uint16_t v = (uint16_t)((c.r >> 3) | (c.g >> 3) << 5 | (c.b >> 3) << 10);
			phys555[first + i] = v;
		}
	}
	return 1;
}

Uint32 SDL_MapRGB(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b)
{
	/* nearest colour of the (logical) palette */
	const SDL_Palette *p = f->palette;
	int best = 0, bestd = 1 << 30;
	for (int i = 0; i < p->ncolors; ++i) {
		int dr = p->colors[i].r - r, dg = p->colors[i].g - g, db = p->colors[i].b - b;
		int d = dr * dr + dg * dg + db * db;
		if (d < bestd) { bestd = d; best = i; if (!d) break; }
	}
	return (Uint32)best;
}

int SDL_FillRect(SDL_Surface *s, SDL_Rect *r, Uint32 colour)
{
	int x0 = s->clip_rect.x, y0 = s->clip_rect.y;
	int x1 = x0 + s->clip_rect.w, y1 = y0 + s->clip_rect.h;
	if (r) {
		if (r->x > x0) x0 = r->x;
		if (r->y > y0) y0 = r->y;
		if (r->x + r->w < x1) x1 = r->x + r->w;
		if (r->y + r->h < y1) y1 = r->y + r->h;
	}
	if (x1 <= x0 || y1 <= y0) return 0;
	Uint8 *p = static_cast<Uint8 *>(s->pixels) + y0 * s->pitch + x0;
	for (int y = y0; y < y1; ++y, p += s->pitch) memset(p, (int)colour, (size_t)(x1 - x0));
	return 0;
}

/* J9: copy n bytes with as many aligned 32-bit writes as possible. On the 32X the game draws
 * straight into the frame buffer, where every write is a bus access: four pixels per write
 * instead of one. The source may be unaligned (tiles in the cart, scrolled positions): its
 * bytes are put together (memcpy into a word = right byte order on any machine). */
static void copy_run(Uint8 *d, const Uint8 *s, int n)
{
	while (n > 0 && ((uintptr_t)d & 3)) { *d++ = *s++; n--; }
	if (((uintptr_t)s & 3) == 0) {
		Uint32 *dw = reinterpret_cast<Uint32 *>(d);
		const Uint32 *sw = reinterpret_cast<const Uint32 *>(s);
		for (; n >= 16; n -= 16) { dw[0] = sw[0]; dw[1] = sw[1]; dw[2] = sw[2]; dw[3] = sw[3]; dw += 4; sw += 4; }
		for (; n >= 4; n -= 4) *dw++ = *sw++;
		d = reinterpret_cast<Uint8 *>(dw);
		s = reinterpret_cast<const Uint8 *>(sw);
	} else {
		Uint32 *dw = reinterpret_cast<Uint32 *>(d);
		for (; n >= 4; n -= 4) { Uint32 v; memcpy(&v, s, 4); *dw++ = v; s += 4; }
		d = reinterpret_cast<Uint8 *>(dw);
	}
	while (n-- > 0) *d++ = *s++;
}

int SDL_UpperBlit(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
	int sx = 0, sy = 0, w = src->w, h = src->h;
	if (srcrect) { sx = srcrect->x; sy = srcrect->y; w = srcrect->w; h = srcrect->h; }
	/* clip against the source */
	if (sx < 0) { w += sx; sx = 0; }
	if (sy < 0) { h += sy; sy = 0; }
	if (sx + w > src->w) w = src->w - sx;
	if (sy + h > src->h) h = src->h - sy;
	int dx = dstrect ? dstrect->x : 0, dy = dstrect ? dstrect->y : 0;
	/* clip against the destination's clip rectangle */
	const SDL_Rect &c = dst->clip_rect;
	if (dx < c.x) { int d = c.x - dx; sx += d; w -= d; dx = c.x; }
	if (dy < c.y) { int d = c.y - dy; sy += d; h -= d; dy = c.y; }
	if (dx + w > c.x + c.w) w = c.x + c.w - dx;
	if (dy + h > c.y + c.h) h = c.y + c.h - dy;
	if (dstrect) {
		dstrect->x = (Sint16)dx; dstrect->y = (Sint16)dy;
		dstrect->w = (Uint16)(w > 0 ? w : 0); dstrect->h = (Uint16)(h > 0 ? h : 0);
	}
	if (w <= 0 || h <= 0) return 0;
	HEAP_CHECK("BLIT");

	const Surf *su = surf_of(src);
	const Surf *du = surf_of(dst);
	/* the destination's logical palette is always the grey ramp in OpenJazz; if a
	 * game path ever changes that, the colours here would be wrong - say so once */
	if (du->remap) {
		static bool warned;
		if (!warned) { warned = true; plat_log("SDL_BlitSurface: destination palette is not the grey ramp"); }
	}
	const Uint8 *sp = static_cast<const Uint8 *>(src->pixels) + sy * src->pitch + sx;
	Uint8 *dp = static_cast<Uint8 *>(dst->pixels) + dy * dst->pitch + dx;
	const bool keyed = (src->flags & SDL_SRCCOLORKEY) != 0;
	const Uint8 key = (Uint8)src->format->colorkey;
	const Uint8 *map = su->remap;
	const bool identity = map == nullptr;

	for (int y = 0; y < h; ++y, sp += src->pitch, dp += dst->pitch) {
		if (!keyed) {
			if (identity) copy_run(dp, sp, w);
			else for (int x = 0; x < w; ++x) dp[x] = map[sp[x]];
		} else if (identity) {
			/* J9: runs of visible pixels, each copied with 32-bit writes where possible */
			int x = 0;
			while (x < w) {
				while (x < w && sp[x] == key) x++;
				const int start = x;
				while (x < w && sp[x] != key) x++;
				if (x > start) copy_run(dp + start, sp + start, x - start);
			}
		} else {
			for (int x = 0; x < w; ++x) { Uint8 v = sp[x]; if (v != key) dp[x] = map[v]; }
		}
	}
	return 0;
}

SDL_Surface *SDL_SetVideoMode(int w, int h, int bpp, Uint32 flags)
{
	(void)flags;
	if (bpp != 8 || w != PLAT_SCREEN_W || h != PLAT_GAME_H) {
		static char msg[64];
		snprintf(msg, sizeof msg, "video mode %dx%dx%d not supported", w, h, bpp);
		last_error = msg;
		return nullptr;
	}
	/* J5: on the 32X the screen is the frame buffer itself (plat_canvas) */
	if (!screen_surface) screen_surface = &new_surface(w, h, plat_canvas())->s;
	return screen_surface;
}

SDL_Rect **SDL_ListModes(SDL_PixelFormat *f, Uint32 flags)
{
	(void)f; (void)flags;
	static SDL_Rect mode = {0, 0, PLAT_SCREEN_W, PLAT_GAME_H};
	static SDL_Rect *list[] = {&mode, nullptr};
	return list;
}

int SDL_Flip(SDL_Surface *screen)
{
	if (screen->pixels == plat_canvas() && screen->pixels) {
		plat_present_canvas(phys555);
		return 0;
	}
	plat_present(static_cast<const uint8_t *>(screen->pixels), screen->pitch, phys555);
#ifndef OJ32X_MARS
	/* J5 test (PC): T32X_DOUBLE_BUFFER=1 swaps the screen between two buffers at every flip,
	 * the second one starting as junk - like the 32X frame buffer the game now draws into.
	 * Frames identical to a normal run = the game redraws everything it shows each frame. */
	{
		static int dbl = -1;
		static uint8_t *other;
		if (dbl < 0) { const char *e = getenv("T32X_DOUBLE_BUFFER"); dbl = e && *e == '1'; }
		if (dbl) {
			const size_t n = (size_t)screen->pitch * screen->h;
			if (!other) { other = static_cast<uint8_t *>(malloc(n)); for (size_t i = 0; i < n; i++) other[i] = (uint8_t)(i * 7); }
			uint8_t *cur = static_cast<uint8_t *>(screen->pixels);
			screen->pixels = other;
			other = cur;
		}
	}
#endif
	return 0;
}

/* ------------------------------------------------------------------ keys / pad */

char *SDL_GetKeyName(SDLKey key)
{
	static char buf[16];
	switch (key) {
		case SDLK_UP: return (char *)"up";
		case SDLK_DOWN: return (char *)"down";
		case SDLK_LEFT: return (char *)"left";
		case SDLK_RIGHT: return (char *)"right";
		case SDLK_SPACE: return (char *)"C";
		case SDLK_LALT: return (char *)"B";
		case SDLK_RCTRL: return (char *)"A";
		case SDLK_RETURN: return (char *)"start";
		case SDLK_ESCAPE: return (char *)"start";
		default: snprintf(buf, sizeof buf, "key %d", (int)key); return buf;
	}
}

/* Pad layouts (PORT32X). Play: C or A jump, B fire, X or Y change weapon, Start = the level
 * menu (OpenJazz's Escape), Z statistics. Menu: C, A or Start = Enter, B = back (Escape). */
struct PadKey { uint16_t bit; SDLKey play, menu; };
static const PadKey pad_keys[] = {
	{PAD_UP,    SDLK_UP,     SDLK_UP},
	{PAD_DOWN,  SDLK_DOWN,   SDLK_DOWN},
	{PAD_LEFT,  SDLK_LEFT,   SDLK_LEFT},
	{PAD_RIGHT, SDLK_RIGHT,  SDLK_RIGHT},
	{PAD_C,     SDLK_SPACE,  SDLK_RETURN},
	{PAD_B,     SDLK_LALT,   SDLK_ESCAPE},
	{PAD_A,     SDLK_SPACE,  SDLK_RETURN},   /* J9 (Micronut99): A = C */
	{PAD_START, SDLK_ESCAPE, SDLK_RETURN},
	{PAD_X,     SDLK_RCTRL,  SDLK_UNKNOWN},  /* J9: change weapon moved to X / Y (6-button pad) */
	{PAD_Y,     SDLK_RCTRL,  SDLK_UNKNOWN},
	{PAD_Z,     SDLK_F9,     SDLK_UNKNOWN},   /* Z: statistics screen */
	{PAD_MODE,  SDLK_UNKNOWN, SDLK_UNKNOWN},
};
#define NPAD (int)(sizeof pad_keys / sizeof pad_keys[0])

static int pad_mode = OJ32X_PAD_MENU;
static uint16_t pad_down;       /* buttons whose key is down */
static uint16_t pad_latched;    /* held across a layout change: ignored until released */
static SDLKey key_down[NPAD];   /* the key each held button produced */

/* small queue of generated events */
static SDL_Event queue[32];
static int q_head, q_tail;
static void push_key(Uint8 type, SDLKey sym)
{
	int next = (q_tail + 1) % 32;
	if (next == q_head) return;
	SDL_Event &e = queue[q_tail];
	memset(&e, 0, sizeof e);
	e.type = type;
	e.key.type = type;
	e.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
	e.key.keysym.sym = sym;
	q_tail = next;
}

void oj32x_pad_mode(int mode)
{
	if (mode == pad_mode) return;
	/* release what the old layout pressed; buttons still held stay quiet until let go */
	for (int i = 0; i < NPAD; ++i)
		if (pad_down & pad_keys[i].bit) {
			if (key_down[i] != SDLK_UNKNOWN) push_key(SDL_KEYUP, key_down[i]);
			key_down[i] = SDLK_UNKNOWN;
		}
	pad_latched |= pad_down;
	pad_down = 0;
	pad_mode = mode;
}

static void poll_pad(void)
{
	uint16_t now = plat_read_pad();
	pad_latched &= now;
	now &= (uint16_t)~pad_latched;
	uint16_t changed = now ^ pad_down;
	for (int i = 0; i < NPAD; ++i) {
		uint16_t bit = pad_keys[i].bit;
		if (!(changed & bit)) continue;
		if (now & bit) {
			SDLKey k = pad_mode == OJ32X_PAD_PLAY ? pad_keys[i].play : pad_keys[i].menu;
			key_down[i] = k;
			if (k != SDLK_UNKNOWN) push_key(SDL_KEYDOWN, k);
		} else {
			if (key_down[i] != SDLK_UNKNOWN) push_key(SDL_KEYUP, key_down[i]);
			key_down[i] = SDLK_UNKNOWN;
		}
	}
	pad_down = now;
}

int SDL_PollEvent(SDL_Event *e)
{
	if (q_head == q_tail) poll_pad();
	if (q_head == q_tail) return 0;
	*e = queue[q_head];
	q_head = (q_head + 1) % 32;
	return 1;
}

/* ------------------------------------------------------------------ audio */
/* J1: no sound yet - OpenJazz carries on silently when audio does not open.
 * The 32X version will mix on the slave SH2 (Tyrian's PWM mixer). */
int  SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
	(void)desired; (void)obtained;
	last_error = "no audio in this build yet";
	return -1;
}
void SDL_PauseAudio(int pause_on) { (void)pause_on; }
void SDL_CloseAudio(void) { }
void SDL_LockAudio(void) { }
void SDL_UnlockAudio(void) { }
void SDL_MixAudio(Uint8 *dst, const Uint8 *src, Uint32 len, int volume)
{
	(void)dst; (void)src; (void)len; (void)volume;
}

/* ------------------------------------------------------------------ misc */
void oj32x_log(const char *msg) { plat_log(msg); }

/* J10: a second surface on the same pixels with its own clip rectangle (band x,y,w,h inside
 * base's clip rectangle) - so the two SH2s can draw into two bands of one screen at once */
static Surf *views[2];

SDL_Surface *oj32x_view(SDL_Surface *base, int index, int x, int y, int w, int h)
{
	if (index < 0 || index > 1) return base;
	if (!views[index]) views[index] = new_surface(base->w, base->h, base->pixels);  /* master only: see oj32x_parallel */
	Surf *v = views[index];
	v->s.pixels = base->pixels;
	v->s.pitch = base->pitch;
	v->s.w = base->w;
	v->s.h = base->h;
	SDL_Rect r = { (Sint16)x, (Sint16)y, (Uint16)w, (Uint16)h };
	SDL_SetClipRect(&v->s, &r);
	const SDL_Rect &c = base->clip_rect;
	int x0 = v->s.clip_rect.x > c.x ? v->s.clip_rect.x : c.x;
	int y0 = v->s.clip_rect.y > c.y ? v->s.clip_rect.y : c.y;
	int x1 = v->s.clip_rect.x + v->s.clip_rect.w, y1 = v->s.clip_rect.y + v->s.clip_rect.h;
	if (c.x + c.w < x1) x1 = c.x + c.w;
	if (c.y + c.h < y1) y1 = c.y + c.h;
	if (x1 < x0) x1 = x0;
	if (y1 < y0) y1 = y0;
	v->s.clip_rect.x = (Sint16)x0; v->s.clip_rect.y = (Sint16)y0;
	v->s.clip_rect.w = (Uint16)(x1 - x0); v->s.clip_rect.h = (Uint16)(y1 - y0);
	return &v->s;
}

/* J10: fn(arg, 0) and fn(arg, 1) - on the 32X at the same time on the master and the slave
 * SH2 (plat_parallel); on the PC one after the other (same pixels, the bands do not overlap) */
void oj32x_parallel(void (*fn)(void *, int), void *arg)
{
	/* the views are made here, on the master: malloc must never run on both SH2s at once */
	for (int i = 0; i < 2; i++)
		if (!views[i] && screen_surface) views[i] = new_surface(screen_surface->w, screen_surface->h, screen_surface->pixels);
	plat_parallel(fn, arg);
}

/* J5: the pack for a level file (LEVELn.www -> k = www * 10 + n, disc file OJ<k>.PAK). */
void oj32x_level_pack(const char *levelFile)
{
	static int current = -1;
	const char *b = levelFile;
	for (const char *p = levelFile; *p; ++p)
		if (*p == '/' || *p == '\\') b = p + 1;
	int k;
	if (strncasecmp(b, "BONUSMAP.", 9) == 0) {
		k = PACK_BONUS_K;   /* J9: every bonus map uses the one bonus pack */
	} else {
		if (strncasecmp(b, "LEVEL", 5) != 0 || !isdigit((unsigned char)b[5]) || b[6] != '.' ||
		    !isdigit((unsigned char)b[7]) || !isdigit((unsigned char)b[8]) || !isdigit((unsigned char)b[9]))
			return;
		k = (b[7] - '0') * 1000 + (b[8] - '0') * 100 + (b[9] - '0') * 10 + (b[5] - '0');
	}
	if (k == current) return;
	{
		char msg[40];
		snprintf(msg, sizeof msg, "LEVEL FILE %s: PACK OJ%03d", b, k);
		plat_log(msg);
	}
	const int32_t len = plat_load_pack(PACK_LEVEL_ID0 + k, PACK_LEVEL_CART, PACK_LEVEL_MAX);
	if (len > 0 && pack_mount_at(1, plat_cart_ptr(PACK_LEVEL_CART))) {
		current = k;
		char msg[40];
		snprintf(msg, sizeof msg, "LEVEL PACK OJ%03d.PAK", k);
		plat_log(msg);
	} else {
		pack_mount_at(1, nullptr);
		current = -1;
	}
}

void oj32x_packs_ready(void) { plat_packs_ready(); }

void oj32x_sfx(int id) { plat_sfx(id); }

/* J11: a CD audio track directly (the credits' CD player); 0 = stop */
void oj32x_music_track(int track) { plat_music(track); }

/* J10: music as CD audio. The game's 35 music files in alphabetical order are disc tracks 2-36
 * (tools/music_tracks.txt - Micronut99 provides the WAVs); NULL = stop. */
void oj32x_music(const char *fileName, bool restart)
{
	static const char *const songs[] = {
		"BONUS",
		"BOSS",
		"ENDLEVEL",
		"ENDSONG",
		"MENUSNG",
		"SECRET",
		"SONG0",
		"SONG1",
		"SONG10",
		"SONG11",
		"SONG12",
		"SONG13",
		"SONG14",
		"SONG15",
		"SONG16",
		"SONG17",
		"SONG2",
		"SONG3",
		"SONG4",
		"SONG5",
		"SONG6",
		"SONG7",
		"SONG8",
		"SONG9",
		"SONGCD0",
		"SONGCD1",
		"SONGCD2",
		"SONGCD3",
		"SONGCD4",
		"SONGCD5",
		"SONGCD6",
		"SONGCD7",
		"SONGCD8",
		"XM2",
		"XM3"
	};
	static int current = -1;
	if (!fileName) { current = -1; plat_music(0); return; }
	const char *b = fileName;
	for (const char *p = fileName; *p; ++p) if (*p == '/' || *p == '\\') b = p + 1;
	for (int i = 0; i < (int)(sizeof songs / sizeof songs[0]); i++) {
		size_t n = strlen(songs[i]);
		if (strncasecmp(b, songs[i], n) == 0 && b[n] == '.') {
			if (i == current && !restart) return;
			current = i;
			plat_music(i + 2);
			return;
		}
	}
}

int oj32x_skip_intro(void)
{
#ifdef OJ32X_MARS
	return 1;
#else
	const char *e = getenv("T32X_SKIP_INTRO");
	return e && *e == '1';
#endif
}

void oj32x_mem_mark(const char *label)
{
	plat_mem_mark(label);
	char msg[96];
	snprintf(msg, sizeof msg, "cart at '%s': %lu bytes in %d blocks, peak %lu", label,
	         (unsigned long)cart_used(), cart_blocks(), (unsigned long)cart_peak());
	plat_log(msg);
}
