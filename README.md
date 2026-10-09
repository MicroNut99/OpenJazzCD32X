# Jazz Jackrabbit - Sega CD32X port

A port of **Jazz Jackrabbit** (Epic MegaGames, 1994), through the open-source **OpenJazz**
engine, to the **Sega Mega Drive / Genesis with Mega-CD / Sega CD and 32X** and a **4 MB RAM
cart**.

This is a straight port, not an enhancement. It does not improve on the original game or on
OpenJazz: it runs the same game on 1994 Sega hardware, with the compromises that hardware
requires. The PC version plays better. The port exists to show that the game - a large C++
engine and 17.5 MB of data - can run on a Sega CD + 32X at all.

Build: J19b. Source release: `OpenJazzCD32X-src.zip` (no game data).

---

## What is included

| Area | State |
|---|---|
| Episodes 1-6, A-C and Holiday Hare '94 (79 levels) | playable |
| Holiday Hare '95 (5 levels) | playable, as the last entry of the episode menu |
| Bonus levels (30, 3D floor) | playable; low frame rate |
| Planet screens | shown; the planet stays still (no zoom) |
| Music | the game's own music as 40 CD audio tracks, one complete pass each, repeated |
| Sound effects | all 22, on the Sega CD's PCM chip |
| Saves | 4 slots per game in the RAM cart; survive switching off |
| Settings | saved in the RAM cart; frame counter and A/B swap remembered |
| Credits screen | with a CD player for the soundtrack |
| Quick start | the game stays in the RAM cart; later starts reach the menu in seconds |

## What is missing or reduced

- **Frame rate:** about 10-13 frames per second in levels. Scrolling is not smooth.
- **Intro and cutscenes:** not included.
- **Demo mode, multiplayer, keyboard and joystick set-up:** not available.
- **Volume:** music and sound effects can only be switched on or off.
- **Sound effects** are 5 kHz (lower quality than the original) to fit the PCM chip.
- **Loading:** each planet is loaded once, 30-45 seconds.
- **Emulators** without a 4 MB RAM cart cannot run it.

---

## Requirements

- Sega Mega Drive / Genesis + Mega-CD / Sega CD + 32X
- 4 MB RAM cart in the 32X cartridge slot
- The disc, burned from the `.cue` file (disc-at-once)
- To build: a copy of Jazz Jackrabbit (CD edition); optionally Holiday Hare '95

## Controls

| In a level | |
|---|---|
| D-pad | move, look up / down |
| C or A | jump |
| B | fire |
| X or Y | change weapon (6-button pad) |
| Z | frame counter on / off (6-button pad) |
| Start | pause menu |

Menus: D-pad to choose; A, C or Start to select; B to go back.
Power-on with **A + B + C** held: reload everything from the disc.

---

## How the port works

### Hardware roles
- **32X master SH2 (23 MHz):** runs the whole game - OpenJazz in C++ with newlib and
  libstdc++, about 420 KB of code executing from the RAM cart.
- **Sega CD Sub-CPU (68000, 12.5 MHz):** reads the CD, plays CD audio and the PCM effects,
  writes the RAM cart, and answers the SH2's requests through the 32X communication registers.
- **Genesis 68000:** a command listener for the Sub-CPU (32X registers, cart writes, pads).
- **32X slave SH2:** idle. Drawing on both SH2s was tried and measured: no gain, because both
  wait on the same bus to the frame buffer and the cart.

### Boot
The boot path of the OpenLara (Tomb Raider) CD32X port is used unchanged: the Sega CD BIOS loads
the Sub-CPU program, the Genesis becomes a listener, a small SH2 loader is booted from the 32X
frame buffer and copies the game program from the disc into the RAM cart, then jumps to it.

### The RAM cart as main memory
The 32X has 256 KB of its own memory; the game needs far more. Everything read-only lives in the
cart, which keeps its content while the console is off:

| Cart area | Size | Holds |
|---|---|---|
| `0x000000`-`0x06E000` | 0.43 MB | the game program |
| `0x06E000`-`0x100000` | 0.57 MB | free |
| `0x100000`-`0x280000` | 1.5 MB | common data: menus, fonts, status panel, file catalogue |
| `0x280000`-`0x3F0000` | 1.4 MB | the current planet (both levels) or bonus level |
| `0x3F0000`-`0x400000` | 64 KB | saves and settings |

The SH2 can read the cart but not write it; all cart writes go through the 68000.

### Data packs, prepared on the PC
The PC build of the same code plays the game and records every file it opens and every picture
it builds. Those become packs: pictures already decoded, tile sets, level maps, the status panel
and fonts ready to use, so the 32X draws straight from the cart and never decodes into its small
memory. One pack per planet (both of its levels), one per bonus level, one common pack.

### Blast loading
On the first start the program and common data are copied into the cart (about a minute and a
half). Later starts compare a few pieces of the disc with the cart and skip the copy when they
match, so the main menu appears within seconds.

### Drawing
- The game draws directly into the 32X frame buffer (8-bit, 256 colours).
- **Line-table scrolling:** each frame buffer keeps the level's background in a 384 x 200 ring;
  the 32X line table moves it, the shift register handles odd pixels. Only new tile strips,
  changed tiles and the background under the previous frame's sprites are drawn again.
- The sky's colour gradient is kept by rotating its palette entries instead of redrawing it.
- Pixel routines run from fast SDRAM; the status panel is copied only where it changed; the
  buffer swap overlaps with the game logic.

### Audio
- The game's music modules are rendered on the PC (with OpenJazz's own libxmp) into one
  complete pass each and burned as CD audio tracks.
- The 22 sound effects are converted to the PCM chip's format, filtered and resampled to 5 kHz.

### Saves
In the RAM cart's save area, one 4 KB piece per file (settings, four slots per game, the
port's own settings). The Sega CD's internal backup RAM is too small for the game's saves.

---

## Fixes along the way (selection)

- Bonus levels ran out of memory: their 64 KB map blocks are now prepared in the packs.
- Memory fragmentation after several level starts: the status panel comes from the cart.
- A hang after "Setup options" from the pause menu (buffer swap timing).
- See-through black (the 32X shows the Genesis layer for colour value 0): black is drawn as
  `0x0400`.
- A white flash when menus change palettes.
- Settings never saved (OpenJazz saves on program exit, which a console never reaches).

---

## Building from source

See `README_SOURCE.md` in the source release. In short: copy the game into
`JazzJackRabbitGame/` (and optionally Holiday Hare '95 into `JAZZXMAS1995/`), add a 320 x 224
`Splash.png`, render the music once with `tools/make_music_wavs.sh`, then run `./build.sh`.
The disc appears as `cd32x/subcpu/OpenJazzCD32X.bin` / `.cue`.

The technical write-up for developers is `docs/CD32X_ADDENDUM_OPENJAZZ.md` (an addendum to the
CD32X boot documents).

---

## Credits

**Jazz Jackrabbit** - Epic MegaGames, 1994. A game by Arjan Brussee and Cliff Bleszinski.

**OpenJazz** - Alister Thomson and contributors.

**D32XR 32X code** - Victor Luchits.

**Sega CD and 32X framework** - Chilly Willy.

**libxmp** (music rendering on the PC) - the libxmp authors.

**Sega CD32X port** - Micronut99.

Jazz Jackrabbit, Holiday Hare and their music are the property of their rights holders. This
release contains no game data: the disc is built from a copy of the game you own.

## Licence

OpenJazz is GPL-2.0; its changes in this port (marked `PORT32X`) are released under the same
licence. The licence of the port's own code (`port/`, `tools/`) and of the boot programs
(`cd32x/`, derived from the OpenLara port and Chilly Willy's framework) is to be stated by the
publisher before release.
