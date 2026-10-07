/**
 * @file credits32x.cpp
 *
 * PORT32X (J11): the credits screen with a CD player, in the main menu's place of "Exit game"
 * (a console has nothing to exit to). The plasma background of the main menu, the credits in
 * the game's own font, and below them the CD player: the game's 35 songs (CD audio tracks 2-36,
 * alphabetical, tools/music_tracks.txt) with the names of their planets.
 *   A, C or Start: play / stop     Left / Right: previous / next song     B: back
 */

#ifdef OJ32X

#include "credits32x.h"
#include "plasma.h"

#include "io/controls.h"
#include "io/gfx/font.h"
#include "io/gfx/video.h"
#include "io/sound.h"
#include "oj32x.h"
#include "loop.h"

#include <stdio.h>
#include <stdlib.h>

namespace {

	struct Song { const char* file; const char* name; };

	// Disc tracks 2-36 in this order (alphabetical, as build.sh puts them on the disc). The
	// names: which planet's levels play each song, read from the game's own level files.
	const Song songs[] = {
		{ "BONUS",    "BONUS STAGE" },
		{ "BOSS",     "BOSS - GUARDIAN" },
		{ "ENDLEVEL", "END OF LEVEL" },
		{ "ENDSONG",  "ENDING" },
		{ "MENUSNG",  "MAIN MENU" },
		{ "SECRET",   "SECRET" },
		{ "SONG0",    "DIAMONDUS" },
		{ "SONG1",    "SLUGGION" },
		{ "SONG10",   "CRYSILIS" },
		{ "SONG11",   "ORBITUS" },
		{ "SONG12",   "PEZROCK" },
		{ "SONG13",   "DREEMPIPES" },
		{ "SONG14",   "SCRAPARAP" },
		{ "SONG15",   "BATTLESHIPS" },
		{ "SONG16",   "MEGAIRBASE" },
		{ "SONG17",   "MEDIVO" },
		{ "SONG2",    "TECHNOIR" },
		{ "SONG3",    "TUBELECTRIC" },
		{ "SONG4",    "TURTEMPLE" },
		{ "SONG5",    "NIPPIUS" },
		{ "SONG6",    "JUNGROCK" },
		{ "SONG7",    "LETNI" },
		{ "SONG8",    "FANOLINT" },
		{ "SONG9",    "MARBELARA" },
		{ "SONGCD0",  "EXOTICUS" },
		{ "SONGCD1",  "INDUSTRIUS" },
		{ "SONGCD2",  "MUCKAMOK" },
		{ "SONGCD3",  "RANEFORUS" },
		{ "SONGCD4",  "STONAR" },
		{ "SONGCD5",  "DECKSTAR" },
		{ "SONGCD6",  "CERAMICUS" },
		{ "SONGCD7",  "DESERTO" },
		{ "SONGCD8",  "LAGUNICUS" },
		{ "XM2",      "HOLIDAIUS" },
		{ "XM3",      "HOLIDAIUS 2" }
	};
	const int nSongs = sizeof(songs) / sizeof(songs[0]);
	const int firstSong = 19;   // SONG5 (Micronut99)

	// Two pages, each shown for 8 seconds
	const char* const page1[] = {
		"JAZZ JACKRABBIT",
		"EPIC MEGAGAMES 1994",
		"",
		"A GAME BY",
		"ARJAN BRUSSEE",
		"CLIFF BLESZINSKI",
		"",
		"OPENJAZZ",
		"ALISTER THOMSON",
		"AND CONTRIBUTORS"
	};
	const char* const page2[] = {
		"D32XR 32X CODE",
		"VICTOR LUCHITS",
		"",
		"SEGA CD AND 32X FRAMEWORK",
		"CHILLY WILLY",
		"",
		"SEGA CD32X PORT",
		"MICRONUT99",
		"",
		"THANK YOU FOR PLAYING!"
	};

}

int runCredits32X () {

	Plasma plasma;
	// J11 test: T32X_CREDITS_FONT picks the font on the PC
	Font* fonts[] = { fontmn2, fontbig, font2, fontmn1, fontiny };
	const char* fe = getenv("T32X_CREDITS_FONT");
	Font* font = fonts[fe ? atoi(fe) % 5 : 1];
	int song = firstSong;
	bool playing = true;
	char line[64];

	oj32x_music_track(song + 2);
	unsigned int start = globalTicks;

	while (true) {

		if (loop(NORMAL_LOOP) == E_QUIT) return E_QUIT;

		if (controls.release(C_ESCAPE)) break;
		if (controls.release(C_ENTER)) {
			playing = !playing;
			oj32x_music_track(playing ? song + 2 : 0);
		}
		if (controls.release(C_LEFT)) {
			song = (song + nSongs - 1) % nSongs;
			playing = true;
			oj32x_music_track(song + 2);
		}
		if (controls.release(C_RIGHT)) {
			song = (song + 1) % nSongs;
			playing = true;
			oj32x_music_track(song + 2);
		}

		plasma.draw();

		const bool second = ((globalTicks - start) / 8000) & 1;
		const char* const* page = second ? page2 : page1;
		const int lines = 10;
		const int h = font->getHeight() + 4;
		int y = 8;
		for (int i = 0; i < lines; i++, y += h)
			if (page[i][0]) font->showString(page[i], canvasW >> 1, y, alignX::Center);

		snprintf(line, sizeof line, "TRACK %d  %s", song + 2, songs[song].name);
		font->showString(line, canvasW >> 1, canvasH - 2 * h - 4, alignX::Center);
		font->showString(playing ? "< PLAYING >" : "< STOPPED >", canvasW >> 1, canvasH - h - 2, alignX::Center);

	}

	// Back to the menu's own music
	oj32x_music_track(0);
	playMusic("MENUSNG.PSM", true);

	return E_NONE;

}

#endif
