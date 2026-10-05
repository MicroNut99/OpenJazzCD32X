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

#include <stdlib.h>
#include <stdio.h>

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

/* What a surface carries besides the SDL fields: its own palette storage, and
 * the remap table (logical palette -> index) with a flag for "identity". */
struct Surf {
	SDL_Surface s;
	SDL_PixelFormat fmt;
	SDL_Palette pal;
	SDL_Color colors[256];
	Uint8 remap[256];
	bool identity;
	bool owns_pixels;
};

static Surf *surf_of(SDL_Surface *s) { return reinterpret_cast<Surf *>(s); }

static void update_remap(Surf *u)
{
	u->identity = true;
	for (int i = 0; i < 256; ++i) {
		u->remap[i] = u->colors[i].r;  /* the logical palette is a grey ramp: r is the index */
		if (u->remap[i] != i) u->identity = false;
	}
}

static Surf *new_surface(int w, int h, void *pixels)
{
	Surf *u = new Surf();
	u->s.format = &u->fmt;
	u->fmt.palette = &u->pal;
	u->fmt.BitsPerPixel = 8;
	u->fmt.BytesPerPixel = 1;
	u->pal.ncolors = 256;
	u->pal.colors = u->colors;
	for (int i = 0; i < 256; ++i) u->colors[i].r = u->colors[i].g = u->colors[i].b = (Uint8)i;
	update_remap(u);
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
	Surf *u = new_surface(w, h, nullptr);
	if (!u->s.pixels) { delete u; last_error = "out of memory"; return nullptr; }
	return &u->s;
}

void SDL_FreeSurface(SDL_Surface *s)
{
	if (!s) return;
	Surf *u = surf_of(s);
	if (u->owns_pixels) free(u->s.pixels);
	delete u;
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
		for (int i = 0; i < n; ++i) u->colors[first + i] = colors[i];
		update_remap(u);
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

	const Surf *su = surf_of(src);
	const Surf *du = surf_of(dst);
	/* the destination's logical palette is always the grey ramp in OpenJazz; if a
	 * game path ever changes that, the colours here would be wrong - say so once */
	if (!du->identity) {
		static bool warned;
		if (!warned) { warned = true; plat_log("SDL_BlitSurface: destination palette is not the grey ramp"); }
	}
	const Uint8 *sp = static_cast<const Uint8 *>(src->pixels) + sy * src->pitch + sx;
	Uint8 *dp = static_cast<Uint8 *>(dst->pixels) + dy * dst->pitch + dx;
	const bool keyed = (src->flags & SDL_SRCCOLORKEY) != 0;
	const Uint8 key = (Uint8)src->format->colorkey;
	const Uint8 *map = su->remap;

	for (int y = 0; y < h; ++y, sp += src->pitch, dp += dst->pitch) {
		if (!keyed) {
			if (su->identity) memcpy(dp, sp, (size_t)w);
			else for (int x = 0; x < w; ++x) dp[x] = map[sp[x]];
		} else if (su->identity) {
			for (int x = 0; x < w; ++x) { Uint8 v = sp[x]; if (v != key) dp[x] = v; }
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
	if (!screen_surface) screen_surface = &new_surface(w, h, nullptr)->s;
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
	plat_present(static_cast<const uint8_t *>(screen->pixels), screen->pitch, phys555);
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

/* Pad layouts (PORT32X). Play: C jump, B fire, A change weapon, Start = the
 * level menu (OpenJazz's Escape). Menu: C or Start = Enter, B = back (Escape). */
struct PadKey { uint16_t bit; SDLKey play, menu; };
static const PadKey pad_keys[] = {
	{PAD_UP,    SDLK_UP,     SDLK_UP},
	{PAD_DOWN,  SDLK_DOWN,   SDLK_DOWN},
	{PAD_LEFT,  SDLK_LEFT,   SDLK_LEFT},
	{PAD_RIGHT, SDLK_RIGHT,  SDLK_RIGHT},
	{PAD_C,     SDLK_SPACE,  SDLK_RETURN},
	{PAD_B,     SDLK_LALT,   SDLK_ESCAPE},
	{PAD_A,     SDLK_RCTRL,  SDLK_UNKNOWN},
	{PAD_START, SDLK_ESCAPE, SDLK_RETURN},
	{PAD_X,     SDLK_UNKNOWN, SDLK_UNKNOWN},
	{PAD_Y,     SDLK_UNKNOWN, SDLK_UNKNOWN},
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
