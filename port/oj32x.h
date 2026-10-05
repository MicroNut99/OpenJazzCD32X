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
/* J3: a finished surface (built in SDRAM) moved into the cart; returns the new one */
SDL_Surface *oj32x_move_to_cart(SDL_Surface *s);
extern "C" {
#endif

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
