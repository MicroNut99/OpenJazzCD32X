# OpenJazz CD32X – handoff (Jazz Jackrabbit 1 on Sega CD + 32X + 4 MB RAM cart)

Project folder: `/mnt/s/KoboPort/sega-toolchain-12.1/sega/OpenJazzCD32X`
(Windows `S:\KoboPort\sega-toolchain-12.1\sega\OpenJazzCD32X`).
Working method: as in *Handoff: How We Work and the Kobo CD32X Framework* (PC host build
first, memory budget, then the 32X; one zip per round; this file is the memory).

## Target (proposed, waiting for Micronut99's decision)
The OpenLara model: the disc boots, `cdloader_sh2.c` copies the engine into the RAM cart,
each level is loaded from the disc into a cart slot, CD audio for music. A cartridge-only
build does not fit (memory table below).

## Folder layout
| Path | Role |
|---|---|
| `oj/` | OpenJazz (AlisterT/openjazz master as uploaded), git-tracked; port changes under `#ifdef OJ32X` with a `PORT32X:` comment |
| `port/include/SDL.h`, `port/sdl_oj.cpp` | the SDL 1.2 subset OpenJazz uses (built with `OJ_SDL1`): 8-bit surfaces, palette-mapped blits, pad → key events |
| `port/plat.h`, `plat_host.c`, `mixer.*` | Tyrian T3j's platform layer (PC side); `T32X_ROMFS` made optional |
| `port/oj32x.h` | what the game calls in the port (pad layout) |
| `host/Makefile`, `run_host.sh` | the PC test build: `make -C host`, `make -C host test`, `make -C host ASAN=1` |
| `tools/make_boot_image.py` | `Splash.png` → `IMAGE.RAW` for the loader (320×224 + palette, 72,192 bytes) |
| `tools/make_music_tracks.py` | from OpenLara (CD audio track numbering) – for later |
| `tools/check_toolchain.sh` | asks the Sega toolchain about C++ (sh-elf-g++, libstdc++, exceptions) |
| `tools/regress.sh` | regression: scripted runs, every 15th frame against `tests/*.ref` (J2) |
| `tools/heap_peak.sh`, `tools/heap_group.py` | who holds the heap at its peak (J2) |
| `tools/level_mem.sh` | heap per level, all 79 levels → `tests/level_mem.txt` (J2) |
| `port/host_new.cpp` | PC build: C++ `new`/`delete` through the counted heap (J2) |
| `mars/Makefile`, `mars/trial_stub.c` | J3a: first SH2 compile + trial link (sizes, list of missing platform functions); nothing runs yet |
| `JazzJackRabbitGame/` | Micronut99's Jazz 1 CD data (GOG collection), **not in git** |
| `cd32x/ref/` | uploaded references, **unchanged**: `cdloader_sh2.c` (OpenLara loader), `cdl_crt0.s` (its CD boot table start-up), `subcpu_crt0.s`, `APP.BIN` (**Kobo's** Sub-CPU program, not OpenLara's) |

## Measurements (J1)
OpenJazz: C++14, ~35,000 lines; Jazz 1 needs `jj1/ io/ menu/ level/ player/ game/`.
JJ2 is off by default upstream (not built). Network code is compiled but has no sockets.

C++ use: `unique_ptr`/`make_unique`, 5 `std::list`, 2 `std::vector`, no RTTI.
**Exceptions** carry the error paths (`throw E_FILE`, `E_DATA`, `E_QUIT` in loaders and menus).

Code size (PC, x86-64 `-Os`, as a guide): game 174 KB, libxmp 98 KB (music player – not
needed on the 32X if music is CD audio), miniz 30 KB. Data 10.5 KB, bss 8 KB.

| Memory item | As written | Plan |
|---|---|---|
| Tile set, 240 × 32×32 8-bit | ~240 KB | cart (read-only, written by the 68000 at level load) |
| Level grid 256×64 × 8-byte `GridElement` | 128 KB | split: static part in the cart, `hits`/`time` only for live events in SDRAM |
| Tile masks 240 × 64 | 15 KB | cart |
| Sprites per level | ? (needs data) | cart, used in place |
| Screen 320×200 8-bit | 64 KB | straight into the 32X frame buffer |
| Code | ~200–250 KB on the SH2 (estimate) | runs from the cart |

## Pad layout (port/sdl_oj.cpp)
| Button | Playing | Menus (and the in-level menu) |
|---|---|---|
| D-pad | move / look | move |
| C | jump (and swim) | select |
| B | fire | back |
| A | change weapon | – |
| Start | level menu | select |
| Z | statistics | – |

Switching layouts releases every key, and a button held across the switch stays quiet until
it is let go (otherwise Start, held while the menu opens, would select at once).

## Port changes in OpenJazz (`#ifdef OJ32X`)
| Step | File | Change |
|---|---|---|
| J1 | `io/sound.cpp` | `closeAudio` frees the xmp context only if audio opened (upstream frees a null context: crash when audio is unavailable) |
| J1 | `level/level.cpp`, `main.cpp` | pad layout per frame (play while playing, menu otherwise) |
| J2 | `jj1/level/jj1levelload.cpp` | memory marks (before tiles / tiles / sprites / loaded) |

## J2 results (PC build with the game data)
**Reproducible across machines:** Micronut99's `last.png` (his PC) and the sandbox's are
pixel-identical.

**What runs:** intro cutscene → title → main menu → demo 1 (all of episode 1 level 1,
~95 s) → demo 2 (Tubelectric). Scripted new game: New game → Episodes → Turtle Terror →
Medium → planet screen (Start skips) → level 1; walk, jump (C), fire (B), a kill, score;
Start opens the level menu (paused), B closes it, C picks Continue. All 79 `LEVELn.www`
files load and play 300 frames without an error (`-w`/`-l` start).

**Regression** (`tools/regress.sh <data>`, ~22 s): `attract` (no input, 9,200 frames,
614 compared) and `newgame` (1,380 frames, 92 compared): 0 different.

**Heap - two fixes in the measuring/port layer:**
1. C++ `new` was not counted (only our own objects' malloc calls are wrapped; libstdc++'s
   `operator new` calls the real malloc). `port/host_new.cpp` fixes it: the real peak of
   level 1 was **1.61 MB**, not 1.0 MB.
2. Every surface had its own 1.3 KB palette + remap table in the SDL stand-in (~345 surfaces
   in a level). Now shared until a surface is recoloured: peak **1.27 MB**, frames identical.

**At the peak (level 1, before fix 2; `tools/heap_peak.sh`):**
| What | Size | 32X plan |
|---|---|---|
| Level sprites (~320 surfaces) | ~440 KB | cart |
| Tile set | 247 KB | cart |
| Game menu's episode pictures | 178 KB | cart |
| Main + game menu backgrounds | 196 KB | cart |
| Level object (grid 128 KB, masks 15 KB) | 159 KB | split: static part cart, changing part SDRAM |
| Fonts | ~116 KB | cart |
| Screen | 65 KB | 32X frame buffer |
The menus stay alive while a level plays (level inside game menu inside main menu): their
pictures must go to the cart too.

**Per level (all 79, `tests/level_mem.txt`):** tile set 123–246 KB, sprites 200–358 KB,
heap after loading 1.09–1.32 MB, peak up to 1.34 MB (`LEVEL0.023`). Before any level file is
read: 761 KB (menus, fonts, level object).

**Game data on disc:** 17.5 MB of game files (cutscenes 5.6 MB, tile sets 4.1 MB, music
3.3 MB, levels 1.8 MB, sprites 1.3 MB …): a data track, not the 4 MB cart.

## Tested (PC sandbox)
- Host build from a fresh clone: 0 warnings; `make -C host test`: all pass.
- `tools/regress.sh`: 0 different (twice).

## Not tested / not done
- No sound (the shim's audio does not open; OpenJazz carries on silently).
- No 32X build: waits for the toolchain answer and the OpenLara Sub-CPU sources.

## Waiting for (Micronut99)
1. Output of `bash tools/check_toolchain.sh` (still open).
2. From `OpenLaraCD32X`: `subcpu/main.c`, `subcpu/hw_md.s`, `subcpu/Makefile`,
   `subcpu/make_mixed_cd2.py`, the loader's `cdloader.ld` + `Makefile.cdloader`, and
   `tools/prep_openlara_cd.py` (the uploaded `APP.BIN` is Kobo's).
3. The target decision (RAM cart, proposed).

## J3a (sent, waiting for Micronut99's log)
Toolchain (Micronut99's check): sh-elf-g++ 12.1 with libstdc++/libsupc++ for SH2 (`!m2`);
a test with exceptions, unique_ptr and std::list compiles and links (~100 KB with the
libraries). `mars/Makefile` compiles the game for the SH2 (-m2 -mb -Os) and trial-links it.
Dry run with the PC compiler: game 174 KB, ext 30 KB, xmp 98 KB, port 5 KB; trial link
282 KB text; missing = only the 6 plat_* functions. The SH2 numbers come from his log.
To check later: GCC 12's emergency exception pool (malloc'd at start-up), and that the
32X start-up runs the C++ initialisers (the unwinder registers its tables there).

## Next round (J3)
Graphics into the cart, tested on the PC: a cart image in the host build (4 MB, written only
by a "68000" copy routine, read-only for the game), the decoded tile set, sprites, fonts and
menu pictures placed there, the level grid split. Goal: SDRAM heap under ~150 KB in every
level, regression still 0 different.
