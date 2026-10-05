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

## Tested (PC sandbox, no game data yet)
- Host build: compiles, links, no warnings.
- `make -C host test`: SDL shim checks against SDL 1.2 behaviour (plain/keyed/recoloured
  blits, the key tested before recolouring, clipping and the returned rectangle, fill, MapRGB) – all pass.
- Without data: starts, reports "Unable to find game data files", exits cleanly (heap 64 KB).
- `IMAGE.RAW` from `Splash.png`: 135 colours, exact (no quantising), 12 black lines top and bottom.

## Not tested / not done
- Nothing with the game data (title, menus, levels, demos): data not uploaded yet.
- No sound (the shim's audio does not open; OpenJazz carries on silently).
- No 32X build: waits for the toolchain answer and the OpenLara Sub-CPU sources.

## Waiting for (Micronut99)
1. The Jazz 1 data folder (the GOG collection folder), zipped.
2. Output of `bash tools/check_toolchain.sh`.
3. From `OpenLaraCD32X`: `subcpu/main.c`, `subcpu/hw_md.s`, `subcpu/Makefile`,
   `subcpu/make_mixed_cd2.py`, the loader's `cdloader.ld` + `Makefile.cdloader`, and
   `tools/prep_openlara_cd.py` (the uploaded `APP.BIN` is Kobo's).
4. The target decision (RAM cart, proposed).

## Next round (J2, with the data)
Run title → menus → episode 1 level 1 on the PC; the JJ1 demo files as regression (every
15th frame against a reference); heap peak per level; the real sprite and level sizes for
the memory plan.
