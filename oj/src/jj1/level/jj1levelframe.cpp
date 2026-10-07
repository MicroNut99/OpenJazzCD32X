
/**
 *
 * @file jj1levelframe.cpp
 *
 * Part of the OpenJazz project
 *
 * @par History:
 * - 23rd August 2005: Created level.c
 * - 3rd February 2009: Renamed level.c to level.cpp
 * - 19th July 2009: Created levelframe.cpp from parts of level.cpp
 * - 30th March 2010: Created baselevel.cpp from parts of level.cpp and
 *                  levelframe.cpp
 * - 29th June 2010: Created jj2levelframe.cpp from parts of levelframe.cpp
 * - 1st August 2012: Renamed levelframe.cpp to jj1levelframe.cpp
 *
 * @par Licence:
 * Copyright (c) 2005-2013 AJ Thomson
 *
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Provides the once-per-frame functions for levels.
 *
 */


#include "jj1bullet.h"
#include "event/jj1event.h"
#include "event/jj1guardians.h"
#include "jj1level.h"
#include "jj1levelplayer.h"

#include "game/game.h"
#include "game/gamemode.h"
#include "io/controls.h"
#include "io/gfx/font.h"
#include "io/gfx/video.h"
#include "util.h"
#ifdef OJ32X
#include "oj32x.h"
extern "C" {
#include "plat.h"
}
#endif


/**
 * Level iteration.
 *
 * @return Error code
 */
int JJ1Level::step () {

	JJ1Event *event;
	int viewH = canvasH;
	int x, y;

	if(setup.hudStyle == hudType::Classic) {
		// Can we see below the panel?
		if (canvasW > SW) viewH = canvasH;
		else viewH = canvasH - 33;
	} else if(setup.hudStyle == hudType::FPS) {
		// Leave space for panel in view height
		viewH = canvasH - 33;
	}

#if OJ_DEBUG
	// Apply cheats
	localPlayer->getJJ1LevelPlayer()->applyCheats();
#endif

	// Search for active events
	for (y = FTOT(viewY) - 5; y < ITOT(FTOI(viewY) + viewH) + 5; y++) {

		for (x = FTOT(viewX) - 5; x < ITOT(FTOI(viewX) + canvasW) + 5; x++) {

			if ((x >= 0) && (y >= 0) && (x < LW) && (y < LH) &&
				grid[y][x].event && (grid[y][x].event < 121) &&
				(+eventSet[grid[y][x].event].difficulty <= +getDifficulty())) {

				event = events;

				while (event) {

					// If the event has been found, stop searching
					if (event->isFrom(x, y)) break;

					event = event->getNext();

				}

				// If the event wasn't found, create it
				if (!event) {

					switch (getEvent(x, y)->movement) {

						case 28:

							events = new JJ1Bridge(x, y);

							break;

						case 41:

							events = new MedGuardian(x, y);

							break;

						case 60:

							events = new DeckGuardian(x, y);

							break;

						default:

							events = new JJ1StandardEvent(eventSet + grid[y][x].event, x, y, TTOF(x), TTOF(y + 1));

							break;

					}

				}

			}

		}

	}


	// Process bullets
	if (bullets) bullets = bullets->step(ticks);

	// Determine the players' trajectories
	for (x = 0; x < nPlayers; x++) players[x].getJJ1LevelPlayer()->control(ticks);

	// Process active events
	if (events) events = events->step(ticks);

	// Apply as much of those trajectories as possible, without going into the
	// scenery
	for (x = 0; x < nPlayers; x++) players[x].getJJ1LevelPlayer()->move(ticks);


	// Check if time has run out
	if (ticks > endTime) {

		if (multiplayer) {

			game->getMode()->outOfTime();

		} else {

			if ((getDifficulty() == difficultyType::Hard ||
				getDifficulty() == difficultyType::Turbo)
				&& (stage == LS_NORMAL))
				localPlayer->getJJ1LevelPlayer()->kill(nullptr, endTime);

		}

	}


	// Handle change in ammo selection

	x = localPlayer->getAmmoType() + 1;

	if (x != ammoType) {

		// Change the ammo type display on the panel
		ammoType = x;
		ammoOffset = ITOF(26);

	}

	if (ammoOffset > 0) {

		// Descending
		ammoOffset -= F1;

		// Avoid an offset of 0, which prevents changes
		if (ammoOffset == 0) ammoOffset = -1;

	}


	// Handle change in water level
	if (waterLevel < waterLevelTarget) waterLevelSpeed += 3200;
	else waterLevelSpeed -= 3200;
	if (waterLevelSpeed > 80000) waterLevelSpeed = 80000;
	if (waterLevelSpeed < -80000) waterLevelSpeed = -80000;

	waterLevel += waterLevelSpeed >> 6;


	// Handle player reactions
	for (x = 0; x < nPlayers; x++) {

		if (players[x].getJJ1LevelPlayer()->reacted(ticks) == PR_KILLED) {

			players[x].clearAmmo();

			if (!multiplayer) return LOST;

			game->resetPlayer(players + x);

		}

	}


	return E_NONE;

}



/**
 * Draw the level.
 */
#ifdef OJ32X_SCROLL
/*
 * PORT32X (J15): the line-table scrolling renderer.
 *
 * Each 32X frame buffer keeps the level's background (sky + background tiles) for the rows in
 * view in a ring of OJ32X_RING_W x OJ32X_RING_H pixels: world row wy at ring row wy % H, world
 * column wx at ring column wx - wx0. The line table shows it (port/plat_mars.c
 * plat_present_scroll). A frame only draws what changed since this buffer was last used (two
 * frames ago): the rows that came into view, the cells whose tile or event changed, and the
 * background under whatever was drawn on top last time (sprites, foreground tiles, text - their
 * rectangles are recorded by the screen mapping in port/sdl_oj.cpp). Moving more than the ring's
 * 64 spare columns sideways, or too much at once, redraws everything once.
 * The sky's colours are numbered by ring row; SkyPaletteEffect turns its palette to match.
 */
namespace {
	const int RW = OJ32X_RING_W, RH = OJ32X_RING_H, SHADOW_W = 13, SHADOW_H = 8;
	struct ShadowCell { short tx, ty; unsigned char tile, event, bg; };
	struct RingState {
		bool valid;
		int wx0, rowLo, rowHi;
		SDL_Rect dirty[256];
		int nDirty;
		bool overflow;
		ShadowCell shadow[SHADOW_W * SHADOW_H];
	};
	RingState ringState[2];
	RingState* cur;

	bool fgTile (const GridElement* ge, const JJ1EventType* eventSet) {
		return (ge->event == 123) || (ge->event == 124) || (ge->event == 125) ||
			(eventSet[ge->event].movement == 37) || (eventSet[ge->event].movement == 38);
	}
}

void oj32xScrollReset () {
	plat_wait_flip();   // J16: a swap still under way must finish before the screen is reused
	oj32x_panel_reset();
	ringState[0].valid = ringState[1].valid = false;
	cur = nullptr;
	oj32x_scroll_map(false, 0, 0, 0, 0);
	oj32x_scroll_record(nullptr, 0, nullptr, nullptr);
}

/** Sky and background tiles for ring columns rx..rx+w-1 and world rows wy..wy+h-1. */
void JJ1Level::scrollBg (int rx, int wy, int w, int h) {

	SDL_Surface* ring = oj32x_ring_surface();
	SDL_Rect src, dst, fill;
	if (rx < 0) { w += rx; rx = 0; }
	if (rx + w > RW) w = RW - rx;
	if (w <= 0 || h <= 0) return;
	src.w = TTOI(1); src.h = TTOI(1); src.x = 0;

	int row = wy;
	while (row < wy + h) {

		const int r0 = row % RH;
		int n = RH - r0;
		if (n > wy + h - row) n = wy + h - row;
		SDL_Rect clip = { (Sint16)rx, (Sint16)r0, (Uint16)w, (Uint16)n };
		SDL_SetClipRect(ring, &clip);

		// the sky (colours by ring row, two rows a colour) or the plain background
		if (sky) {
			for (int y = r0 & ~1; y < r0 + n; y += 2) {
				fill.x = rx; fill.y = y; fill.w = w; fill.h = 2;
				SDL_FillRect(ring, &fill, 156 + (y >> 1));
			}
		} else {
			SDL_FillRect(ring, &clip, 127);
		}

		// the background tiles
		const int tx0 = ITOT(cur->wx0 + rx), tx1 = ITOT(cur->wx0 + rx + w - 1);
		const int ty0 = ITOT(row), ty1 = ITOT(row + n - 1);
		for (int ty = ty0; ty <= ty1; ty++) {
			for (int tx = tx0; tx <= tx1; tx++) {
				dst.x = TTOI(tx) - cur->wx0;
				dst.y = TTOI(ty) - row + r0;
				if (tx >= LW || ty >= LH) {
					fill.x = dst.x; fill.y = dst.y; fill.w = 32; fill.h = 32;
					SDL_FillRect(ring, &fill, LEVEL_BLACK);
					continue;
				}
				GridElement* ge = grid[ty] + tx;
				if (ge->bg) {
					fill.x = dst.x; fill.y = dst.y; fill.w = 32; fill.h = 32;
					SDL_FillRect(ring, &fill, LEVEL_BLACK);
				}
				if (!fgTile(ge, eventSet)) {
					src.y = TTOI(ge->tile);
					SDL_BlitSurface(tileSet, &src, ring, &dst);
				}
				ShadowCell& sc = cur->shadow[(ty % SHADOW_H) * SHADOW_W + (tx - ITOT(cur->wx0)) % SHADOW_W];
				sc.tx = tx; sc.ty = ty; sc.tile = ge->tile; sc.event = ge->event; sc.bg = ge->bg;
			}
		}

		row += n;
	}
	SDL_SetClipRect(ring, nullptr);

}

/** Bring this frame buffer's ring up to date for the view at (vX, vY). */
void JJ1Level::scrollBegin (int vX, int vY, int viewH) {

	oj32x_scroll_map(false, 0, 0, 0, 0);       // the ring is drawn directly here
	plat_wait_flip();                          // J16: the logic ran while the last swap waited
	{	// J18c: another screen was shown in between (save / load / set-up from the pause
		// menu): it drew over the rings and the panel - everything again, in both buffers
		static unsigned seenPlain;
		const unsigned plain = oj32x_plain_presents();
		if (plain != seenPlain) {
			seenPlain = plain;
			ringState[0].valid = ringState[1].valid = false;
			oj32x_panel_reset();
		}
	}
	cur = &ringState[plat_back_index() & 1];
	RingState& S = *cur;

	const bool full = !S.valid || S.overflow || vX < S.wx0 || vX + canvasW > S.wx0 + RW ||
		vY + viewH <= S.rowLo || vY >= S.rowHi;

	if (full) {
		S.wx0 = vX - 32;
		if (S.wx0 < 0) S.wx0 = 0;
		S.wx0 &= ~31;
		for (int i = 0; i < SHADOW_W * SHADOW_H; i++) S.shadow[i].tx = -1;
		S.rowLo = vY; S.rowHi = vY + viewH;
		scrollBg(0, vY, RW, viewH);
		S.valid = true;
	} else {
		const int lo = vY, hi = vY + viewH;
		// the background under last time's sprites, text and foreground tiles
		for (int i = 0; i < S.nDirty; i++) {
			const SDL_Rect& d = S.dirty[i];
			int y0 = d.y, y1 = d.y + d.h;
			if (y0 < lo) y0 = lo;
			if (y0 < S.rowLo) y0 = S.rowLo;
			if (y1 > hi) y1 = hi;
			if (y1 > S.rowHi) y1 = S.rowHi;
			if (y1 > y0) scrollBg(d.x, y0, d.w, y1 - y0);
		}
		// the rows that came into view
		if (lo < S.rowLo) scrollBg(0, lo, RW, (S.rowLo < hi ? S.rowLo : hi) - lo);
		if (hi > S.rowHi) { const int from = S.rowHi > lo ? S.rowHi : lo; scrollBg(0, from, RW, hi - from); }
		S.rowLo = lo; S.rowHi = hi;
		// cells whose tile or event changed (destroyed blocks, collected items ...)
		for (int ty = ITOT(lo); ty <= ITOT(hi - 1) && ty < LH; ty++) {
			for (int tx = ITOT(vX); tx <= ITOT(vX + canvasW - 1) && tx < LW; tx++) {
				const ShadowCell& sc = S.shadow[(ty % SHADOW_H) * SHADOW_W + (tx - ITOT(S.wx0)) % SHADOW_W];
				const GridElement* ge = grid[ty] + tx;
				if (sc.tx == tx && sc.ty == ty &&
					(sc.tile != ge->tile || sc.event != ge->event || sc.bg != ge->bg)) {
					int y0 = TTOI(ty), y1 = TTOI(ty + 1);
					if (y0 < lo) y0 = lo;
					if (y1 > hi) y1 = hi;
					scrollBg(TTOI(tx) - S.wx0, y0, 32, y1 - y0);
				}
			}
		}
	}

	// from here on the game draws on top of the ring: map the screen, record what it covers
	S.nDirty = 0;
	S.overflow = false;
	oj32x_scroll_map(true, S.wx0, vX, vY, viewH);
	oj32x_scroll_record(S.dirty, 256, &S.nDirty, &S.overflow);

}

/** The sun / moon: fixed on the screen, behind the tiles - drawn on the sky's pixels only. */
void JJ1Level::scrollOrb (int vX, int vY) {

	if (!sky || !skyOrb) return;
	const int ox = ((canvasW * 4) / 5) - (vX & 3), oy = ((canvasH - 33) * 3) / 25;
	const unsigned char* sp = static_cast<const unsigned char*>(tileSet->pixels) + TTOI(skyOrb + (vX & 3)) * tileSet->pitch;
	const unsigned char key = video.getColorKey(tileSet);
	for (int y = 0; y < 32; y++, sp += tileSet->pitch) {
		unsigned char* row = oj32x_screen_row(oy + y);
		if (!row) continue;
		for (int x = 0; x < 32; x++) {
			const int sx = ox + x;
			if (sx < 0 || sx >= canvasW) continue;
			if (sp[x] != key && row[sx] >= 156) oj32x_put_pixel(row + sx, sp[x]);
		}
	}
	// the background there is restored next time like a sprite's
	(void)vY;
	if (cur && cur->nDirty < 256) {
		SDL_Rect& d = cur->dirty[cur->nDirty++];
		d.x = (Sint16)(ox + vX - cur->wx0); d.y = (Sint16)(oy + vY); d.w = 32; d.h = 32;
	}

}
#endif

#ifdef OJ32X
namespace {
	struct OJ32XBand { JJ1Level* level; int vX, vY, viewH; bool foreground; };
	void oj32xBandJob (void* arg, int part) {
		OJ32XBand* j = static_cast<OJ32XBand*>(arg);
		// part 0 = top half, part 1 = bottom half of the view
		const int mid = j->viewH / 2;
		const int y0 = part ? mid : 0, y1 = part ? j->viewH : mid;
		SDL_Surface* target = oj32x_view(canvas, part, 0, y0, canvasW, y1 - y0);
		j->level->drawBand(target, j->vX, j->vY, j->viewH, j->foreground);
	}
}

/**
 * PORT32X (J10): the original's sky + background tile pass (foreground = false) or its
 * foreground tile pass (true), drawn into one band (target's clip rectangle). Same pixels as
 * the original code: only the target surface and the row range differ.
 */
void JJ1Level::drawBand (SDL_Surface* target, int vX, int vY, int viewH, bool foreground) {

	SDL_Rect src, dst;
	GridElement* ge;
	const int bandTop = target->clip_rect.y, bandBottom = target->clip_rect.y + target->clip_rect.h;
	auto fill = [&](int x, int y, int w, int h, int colour) {
		SDL_Rect r = { (Sint16)x, (Sint16)y, (Uint16)w, (Uint16)h };
		SDL_FillRect(target, &r, colour);
	};

	src.w = TTOI(1);
	src.h = TTOI(1);
	src.x = 0;

	if (!foreground) {

		if (sky) {

			int bgScale;
			if (canvasW > SW) bgScale = ((canvasH - 1) / 100) + 1;
			else bgScale = ((canvasH - 34) / 100) + 1;

			for (int y = 0; y < viewH; y += bgScale)
				if (y + bgScale > bandTop && y < bandBottom)
					fill(0, y, canvasW, bgScale, 156 + (y / bgScale));

			if (skyOrb) {
				dst.x = ((canvasW * 4) / 5) - (vX & 3);
				dst.y = ((canvasH - 33) * 3) / 25;
				src.y = TTOI(skyOrb + (vX & 3));
				SDL_BlitSurface(tileSet, &src, target, &dst);
			}

		} else {

			fill(0, 0, canvasW, canvasH, 127);

		}

	}

	for (int y = 0; y <= ITOT(viewH - 1) + 1; y++) {

		const int sy = TTOI(y) - (vY & 31);
		if (sy + 32 <= bandTop || sy >= bandBottom) continue;

		for (int x = 0; x <= ITOT(canvasW - 1) + 1; x++) {

			const int sx = TTOI(x) - (vX & 31);

			if ((x + ITOT(vX) >= 256) || (y + ITOT(vY) >= 64)) {
				if (!foreground) fill(sx, sy, 32, 32, LEVEL_BLACK);
				continue;
			}

			ge = grid[y + ITOT(vY)] + x + ITOT(vX);

			if (!foreground) {

				if (ge->bg) fill(sx, sy, 32, 32, LEVEL_BLACK);

				if ((ge->event != 123) && (ge->event != 124) && (ge->event != 125) &&
					(eventSet[ge->event].movement != 37) &&
					(eventSet[ge->event].movement != 38)) {
					dst.x = sx;
					dst.y = sy;
					src.y = TTOI(ge->tile);
					SDL_BlitSurface(tileSet, &src, target, &dst);
				}

			} else {

				if (ge->event == 123) {
					dst.x = sx;
					dst.y = sy;
					if (ticks & 64) src.y = TTOI(eventSet[ge->event].multiB);
					else src.y = TTOI(eventSet[ge->event].multiA);
					SDL_BlitSurface(tileSet, &src, target, &dst);
				}

				if ((ge->event == 124) || (ge->event == 125) ||
					(eventSet[ge->event].movement == 37) ||
					(eventSet[ge->event].movement == 38)) {
					dst.x = sx;
					dst.y = sy;
					src.y = TTOI(ge->tile);
					SDL_BlitSurface(tileSet, &src, target, &dst);
				}

			}

		}

	}

}
#endif

void JJ1Level::draw () {

#ifndef OJ32X
	GridElement *ge;
#endif
	SDL_Rect src, dst;
	int viewH = canvasH;
	int vX, vY;
	int x, y;
	unsigned int change;


	// Calculate change since last step
	change = getTimeChange();


	// Calculate viewport
	if (game && (stage == LS_END)) game->view(paused? 0: ((ticks - prevTicks) * 160));
	else localPlayer->getJJ1LevelPlayer()->view(ticks, paused? 0: (ticks - prevTicks), change);

	if(setup.hudStyle == hudType::Classic) {
		// Can we see below the panel?
		if (canvasW > SW) viewH = canvasH;
		else viewH = canvasH - 33;
	} else if(setup.hudStyle == hudType::FPS) {
		// Leave space for panel in view height
		viewH = canvasH - 33;
	}

	// Ensure the new viewport is within the level
	if (FTOI(viewX) + canvasW >= TTOI(LW)) viewX = ITOF(TTOI(LW) - canvasW);
	if (viewX < 0) viewX = 0;
	if (FTOI(viewY) + viewH >= TTOI(LH)) viewY = ITOF(TTOI(LH) - viewH);
	if (viewY < 0) viewY = 0;

	// Use the viewport
	dst.x = 0;
	dst.y = 0;
	vX = FTOI(viewX);
	vY = FTOI(viewY);
	dst.w = canvasW;
	dst.h = viewH;
	video.setClipRect(canvas, &dst);


	// Set tile drawing dimensions
	src.w = TTOI(1);
	src.h = TTOI(1);
	src.x = 0;


#ifdef OJ32X
#ifdef OJ32X_SCROLL
	// PORT32X (J15): only what changed goes into this frame buffer's ring
	scrollBegin(vX, vY, viewH);
	scrollOrb(vX, vY);
#else
	// PORT32X (J10): sky and background tiles in two bands, one per SH2, at the same time
	{
		OJ32XBand job = { this, vX, vY, viewH, false };
		oj32x_parallel(oj32xBandJob, &job);
	}
#endif
	oj32x_prof(1);   // J14: background
#else
	// If there is a sky, draw it
	if (sky) {

		// Background scale
		int bgScale;
		if (canvasW > SW) bgScale = ((canvasH - 1) / 100) + 1;
		else bgScale = ((canvasH - 34) / 100) + 1;

		for (y = 0; y < viewH; y += bgScale)
			video.drawRect(0, y, canvasW, bgScale, 156 + (y / bgScale));


		// Show sun / moon / etc.
		if (skyOrb) {

			dst.x = ((canvasW * 4) / 5) - (vX & 3);
			dst.y = ((canvasH - 33) * 3) / 25;
			src.y = TTOI(skyOrb + (vX & 3));
			SDL_BlitSurface(tileSet, &src, canvas, &dst);

		}

	} else {

		// If there is no sky, draw a blank background
		// This is only very occasionally actually visible
		video.clearScreen(127);

	}



	// Show background tiles

	for (y = 0; y <= ITOT(viewH - 1) + 1; y++) {

		for (x = 0; x <= ITOT(canvasW - 1) + 1; x++) {

			if ((x + ITOT(vX) >= 256) || (y + ITOT(vY) >= 64)) {

				video.drawRect(TTOI(x) - (vX & 31), TTOI(y) - (vY & 31), 32, 32, LEVEL_BLACK);

				continue;

			}

			// Get the grid element from the given coordinates
			ge = grid[y + ITOT(vY)] + x + ITOT(vX);

			// If this tile uses a black background, draw it
			if (ge->bg)
				video.drawRect(TTOI(x) - (vX & 31), TTOI(y) - (vY & 31), 32, 32, LEVEL_BLACK);


			// If this is not a foreground tile, draw it
			if ((ge->event != 123) && (ge->event != 124) && (ge->event != 125) &&
				(eventSet[ge->event].movement != 37) &&
				(eventSet[ge->event].movement != 38)) {

				dst.x = TTOI(x) - (vX & 31);
				dst.y = TTOI(y) - (vY & 31);
				src.y = TTOI(ge->tile);
				SDL_BlitSurface(tileSet, &src, canvas, &dst);

#if OJ_DEBUG
				if(debug::showFlags & DS_TILEMASK)
					SDL_BlitSurface(maskedTileset, &src, canvas, &dst);
				if(debug::showFlags & DS_BGTILES)
					video.drawRect(dst.x, dst.y, TTOI(1), TTOI(1), 55, false);
#endif
			}

		}

	}


#endif

	// Show active events
	if (events) events->draw(ticks, change);


	// Show the players
	for (x = 0; x < nPlayers; x++) players[x].getJJ1LevelPlayer()->draw(ticks, change);


	// Show bullets
	if (bullets) bullets->draw(change);



#ifdef OJ32X
	oj32x_prof(2);   // J14: sprites (events, players, bullets)
#ifdef OJ32X_SCROLL
	// PORT32X (J15): the foreground tiles straight onto the (mapped) screen
	drawBand(canvas, vX, vY, viewH, true);
#else
	// PORT32X (J10): foreground tiles in two bands, one per SH2
	{
		OJ32XBand job = { this, vX, vY, viewH, true };
		oj32x_parallel(oj32xBandJob, &job);
	}
#endif
	oj32x_prof(3);   // J14: foreground
#else
	// Show foreground tiles

	for (y = 0; y <= ITOT(viewH - 1) + 1; y++) {

		for (x = 0; x <= ITOT(canvasW - 1) + 1; x++) {

			if ((x + ITOT(vX) >= 256) || (y + ITOT(vY) >= 64)) continue;

			// Get the grid element from the given coordinates
			ge = grid[y + ITOT(vY)] + x + ITOT(vX);

			// If this is an "animated" foreground tile, draw it
			if (ge->event == 123) {

				dst.x = TTOI(x) - (vX & 31);
				dst.y = TTOI(y) - (vY & 31);
				if (ticks & 64) src.y = TTOI(eventSet[ge->event].multiB);
				else src.y = TTOI(eventSet[ge->event].multiA);
				SDL_BlitSurface(tileSet, &src, canvas, &dst);

#if OJ_DEBUG
				if(debug::showFlags & DS_TILEMASK)
					SDL_BlitSurface(maskedTileset, &src, canvas, &dst);
				if(debug::showFlags & DS_ANIMTILES)
					video.drawRect(dst.x, dst.y, TTOI(1), TTOI(1), 44, false);
#endif
			}

			// If this is a foreground tile, draw it
			if ((ge->event == 124) || (ge->event == 125) ||
				(eventSet[ge->event].movement == 37) ||
				(eventSet[ge->event].movement == 38)) {

				dst.x = TTOI(x) - (vX & 31);
				dst.y = TTOI(y) - (vY & 31);
				src.y = TTOI(ge->tile);
				SDL_BlitSurface(tileSet, &src, canvas, &dst);

#if OJ_DEBUG
				if(debug::showFlags & DS_TILEMASK)
					SDL_BlitSurface(maskedTileset, &src, canvas, &dst);
				if(debug::showFlags & DS_FGTILES)
					video.drawRect(dst.x, dst.y, TTOI(1), TTOI(1), 33, false);
#endif
			}

		}

	}

#endif

	// FIXME: Temporary lines showing the water level
	video.drawRect(0, FTOI(waterLevel - viewY), canvasW, 2, 24);
	video.drawRect(0, FTOI(waterLevel - viewY) + 3, canvasW, 1, 24);
	video.drawRect(0, FTOI(waterLevel - viewY) + 6, canvasW, 1, 24);
	video.drawRect(0, FTOI(waterLevel - viewY) + 10, canvasW, 1, 24);

	// Show active guardian's energy bar
	if (events) events->drawEnergy(ticks);


	// If this is a competitive game, draw the score
	if (multiplayer) game->getMode()->drawScore(font);


	// Show panel

	int offsetX = 0;
	bool isWide = (canvasW != SW);

	if(setup.hudStyle == hudType::FPS && isWide) {
		// center panel
		offsetX = (canvasW >> 1) - (SW >> 1);
	}

	video.setClipRect(canvas, nullptr);

	if (ammoOffset != 0) {

		if (ammoOffset < 0) {

			// Finished descending
			ammoOffset = 0;

		}

		src.x = 0;
		src.y = FTOI(ammoOffset);
		src.w = 64;
		src.h = 26 - src.y;
		dst.x = 248;
		dst.y = 3;
		SDL_BlitSurface(panelAmmo[ammoType], &src, panel, &dst);

	}

	// always classic HUD if there is not enough space
	if(setup.hudStyle == hudType::Classic ||
		(setup.hudStyle == hudType::FPS && !isWide)) {

		dst.x = 0;
		dst.y = canvasH - 33;
		SDL_BlitSurface(panel, nullptr, canvas, &dst);

		// Fill the one missing pixel row at the bottom black
		video.drawRect(0, canvasH - 1, SW, 1, LEVEL_BLACK);
	} else if (setup.hudStyle == hudType::FPS) {
		// quake-style HUD

		dst.y = canvasH - 33;

		for (x = 0; x <= ITOT(offsetX); x++) {
			// draw left border
			dst.x = TTOI(x);
			SDL_BlitSurface(panelBG[0], nullptr, canvas, &dst);

			// draw right border
			dst.x = canvasW - TTOI(x + 1);
			SDL_BlitSurface(panelBG[1], nullptr, canvas, &dst);
		}

		// cut the panel borders and re-center
		src.x = 1; // left: 1 white pixel
		src.y = 0;
		src.w = SW - 8; // right: 3 black pixels + 5 pixels around screws
		src.h = TTOI(1);
		dst.x = offsetX + 5;
		SDL_BlitSurface(panel, &src, canvas, &dst);

		// Fill the one missing pixel row at the bottom black
		video.drawRect(0, canvasH - 1, canvasW, 1, LEVEL_BLACK);

		offsetX += 4; // move everything inside right
	}


	// Show panel data

	// Show score
	panelSmallFont->showNumber(localPlayer->getScore(), offsetX + 84, canvasH - 27);

	// Show time remaining
	if (endTime > ticks) x = endTime - ticks;
	else x = 0;
	y = x / (60 * 1000);
	panelSmallFont->showNumber(y, offsetX + 116, canvasH - 27);
	x -= (y * 60 * 1000);
	y = x / 1000;
	panelSmallFont->showNumber(y, offsetX + 136, canvasH - 27);
	x -= (y * 1000);
	y = x / 100;
	panelSmallFont->showNumber(y, offsetX + 148, canvasH - 27);

	// Show lives
	panelSmallFont->showNumber(localPlayer->getLives(), offsetX + 124, canvasH - 13);

	// Show planet number

	if (worldNum <= 41) // Main game levels
		panelSmallFont->showNumber((worldNum % 3) + 1, offsetX + 184, canvasH - 13);
	else if ((worldNum >= 50) && (worldNum <= 52)) // Christmas levels
		panelSmallFont->showNumber(worldNum - 49, offsetX + 184, canvasH - 13);
	else panelSmallFont->showNumber(worldNum, offsetX + 184, canvasH - 13);

	// Show level number
	panelSmallFont->showNumber(levelNum + 1, offsetX + 196, canvasH - 13);



	// Show ammo
	bool unlimitedAmmo = localPlayer->getAmmoType() == -1; // blaster
#if OJ_DEBUG
	// force unlimited ammo
	if(debug::cheatFlags & DC_AMMO)
		unlimitedAmmo = true;
#endif
	if (unlimitedAmmo) {

		// Draw "infinity" symbol
		panelSmallFont->showString(":", offsetX + 225, canvasH - 13);
		panelSmallFont->showString(";", offsetX + 233, canvasH - 13);

	} else {

		x = localPlayer->getAmmo();

		// Trailing 0s
		if (x < 100) {

			panelSmallFont->showNumber(0, offsetX + 229, canvasH - 13);
			if (x < 10) panelSmallFont->showNumber(0, offsetX + 237, canvasH - 13);

		}

		panelSmallFont->showNumber(x > 999? 999: x, offsetX + 245, canvasH - 13);

	}


	// Draw the health bar

	dst.x = 20;
	x = localPlayer->getJJ1LevelPlayer()->getEnergy();
	y = (ticks - prevTicks) * 40;

	if (FTOI(energyBar) < x) {
		// increase

		if (ITOF(x) - energyBar < y) energyBar = ITOF(x);
		else energyBar += y;

	} else if (FTOI(energyBar) > x) {
		// decrease

		if (energyBar - ITOF(x) < y) energyBar = ITOF(x);
		else energyBar -= y;

	}

	if (energyBar > F1) {

		dst.w = FTOI(energyBar) - 1;

		// Choose energy bar colour
		int color;
		if (x <= 20) color = 32 + (((ticks / 75) * 4) & 15); // flash
		else if (x > 51) color = 24; // blue only before first hit
		else if (x > 38) color = 17; // green
		else if (x > 25) color = 80; // pink
		else if (x > 20) color = 32; // orange is only seen in easy

		// Draw energy bar
		video.drawRect(offsetX + dst.x, canvasH - 13, dst.w, 7, color);

		dst.x += dst.w;
		dst.w = 64 - dst.w;

	} else dst.w = 64;


	// Fill in remaining energy bar space with black
	video.drawRect(offsetX + dst.x, canvasH - 13, dst.w, 7, LEVEL_BLACK);

#ifdef OJ32X
	oj32x_prof(4);   // J14: panel and the rest of the drawing
#ifdef OJ32X_PROFILE
	// J14: the profiler's line (ms per frame) in the top left corner of the level
	oj32x_prof_draw(canvas);   // J14c: own 3x5 font drawn 2x, bright on dark - readable everywhere
#endif
#endif
}
