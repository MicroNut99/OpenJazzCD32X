/*
 * OpenJazz CD32X - what the game code (under #ifdef OJ32X) calls in the port layer.
 */
#ifndef OJ32X_H
#define OJ32X_H

#ifdef __cplusplus
extern "C" {
#endif

/* Pad layout (port/sdl_oj.cpp): play = in a level, menu = everywhere else. */
#define OJ32X_PAD_MENU 0
#define OJ32X_PAD_PLAY 1
void oj32x_pad_mode(int mode);

#ifdef __cplusplus
}
struct SDL_Surface;
/* J3: surface with read-only pixels in the RAM cart (port/sdl_oj.cpp) */
SDL_Surface *oj32x_cart_surface(const unsigned char *pixels, int w, int h);
/* J3: a finished surface moves to the cart (returns the new one; the old one is freed) */
SDL_Surface *oj32x_freeze(SDL_Surface *s);
/* J10: a band view of a surface (same pixels, own clip rectangle; index 0 or 1) */
SDL_Surface *oj32x_view(SDL_Surface *base, int index, int x, int y, int w, int h);
extern "C" {
#endif

/* J5: the packs are mounted (PC build: T32X_PACK at the first file look-up) */
void oj32x_packs_ready(void);

/* J5: load the cart pack for a level file (JJ1Level::load) */
void oj32x_level_pack(const char *levelFile);

/* J10: a sound effect (32X: Sega CD PCM) */
void oj32x_sfx(int id);

/* J11: a CD audio track directly (credits screen); 0 = stop */
void oj32x_music_track(int track);

/* J10: music as CD audio (fileName NULL = stop) */
void oj32x_music(const char *fileName, bool restart);
#ifdef __cplusplus
}
/* J13: A/B swap (set-up menu) */
bool oj32x_ab_swapped(void);
void oj32x_ab_swap(bool on);
extern "C" {
#endif

/* J13: volume 0 stops the CD music, more starts it again; remember = the song to come back to */
void oj32x_music_volume(int volume);
void oj32x_music_remember(const char *fileName);

/* J10: run fn(arg, 0) and fn(arg, 1); on the 32X on both SH2s at the same time */
void oj32x_parallel(void (*fn)(void *, int), void *arg);

/* J5: skip the intro cutscene (32X: always; PC build: T32X_SKIP_INTRO=1) */
int oj32x_skip_intro(void);

/* a line for the platform's log (32X: the error screens' "last log") */
void oj32x_log(const char *msg);

/* memory report at a point of interest (PC build: heap now/peak; 32X: nothing) */
void oj32x_mem_mark(const char *label);

#ifdef __cplusplus
}
/* set by Level::loop just before ::loop (main.cpp), which applies and clears it */
extern bool oj32xPlayLayout;
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#endif
