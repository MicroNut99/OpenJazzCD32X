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

## J3a (SH2 compile: all files compile after one fix; link fix J3b sent)
Toolchain (Micronut99's check): sh-elf-g++ 12.1 with libstdc++/libsupc++ for SH2 (`!m2`);
a test with exceptions, unique_ptr and std::list compiles and links (~100 KB with the
libraries). `mars/Makefile` compiles the game for the SH2 (-m2 -mb -Os) and trial-links it.
Dry run with the PC compiler: game 174 KB, ext 30 KB, xmp 98 KB, port 5 KB; trial link
282 KB text; missing = only the 6 plat_* functions. The SH2 numbers come from his log.
J3a log: only `jj1save.cpp` failed (`<time.h>` missing; glibc includes it through other
headers, newlib does not) - fixed. Trial link then failed: libc.a's own SH simulator
syscalls (lib_a-syscalls.o) clash with reent.o over `errno`. J3b: our own system calls
(`port/mars_syscalls.c`, from Tyrian's port/mars/syscalls.c + _stat, _rename, mkdir).
**J3b result (Micronut99's build): the whole game compiles and links for the SH2.**
| Part (-Os, before unused code is removed) | text | data | bss |
|---|---|---|---|
| game | 165,941 | 520 | 7,739 |
| ext (argparse, miniz, stb) | 23,108 | 0 | 0 |
| xmp (music player) | 84,896 | 4 | 8 |
| port | 5,199 | 24 | 3,465 |
| **trial link** (unused code removed, + libstdc++/newlib) | **395,412** | **3,112** | **10,400** |
Code runs from the cart (4 MB; 0.4 MB engine). SDRAM: data + bss 13.5 KB + stacks + heap.
Still missing: 7 plat_* functions, __heap_start/__heap_end (linker script), _access and
_getcwd (added to mars_syscalls.c in my tree, ships with J3).
To check later: GCC 12's emergency exception pool (malloc'd at start-up), and that the
32X start-up runs the C++ initialisers (the unwinder registers its tables there).

## J3 step 1 (PC): read-only graphics in the RAM cart
`port/cart.c` (allocator, block table 1,024 entries in SDRAM) + `plat_cart_data`/`plat_cart_write`
(PC: a read-only mapping, opened only for the copy - any other write crashes = a test).
Surfaces created from pixels go to the cart (`Video::createSurface`), finished font atlases
move there (`oj32x_freeze`). Exception: the HUD panel is drawn into (ammo) - stays in SDRAM.
Results: regression 0 different; all 79 levels play 300 frames with no write into the cart.
Heap after a level has loaded: **321-335 KB** (was 1.09-1.32 MB). Cart: ~0.9 MB in level 1.
Left in SDRAM after loading: level object 159 KB (grid), screen 64 KB (-> frame buffer on
the 32X), HUD panel 10 KB, ~65 KB small blocks. Loading still peaks at up to 777 KB
(the tile set is held twice while it is assembled) -> next: decode straight into the cart.
Cart layout (proposed): 0x000000 engine (≤ 1 MB), 0x100000-0x3EFFFF graphics,
0x3F0000 settings and saves.

`cd32x/ref/APP_openlara.BIN`: a second APP.BIN from Micronut99 - an early OpenLara loader
build (marker SH2-OPENLARA-CD-OL1-LOADER; files D32XR.32X, IMAGE.RAW, Kobo's names), binary only.

## OpenLara sources (received; key files kept in `cd32x/ref/openlara/`, unchanged)
`subcpu/` (Sub-CPU main.c + 68000 listener hw_md.s + disc Makefile), `sh2/` (loader:
cdloader_sh2.c, cdl_crt0.s, cdloader.ld, Makefile.cdloader), `tools/prep_openlara_cd.py`,
`build.sh`, the latest OpenLara handoff. What OpenJazz reuses:
- **Boot, unchanged:** disc → APP.BIN → CDLOADER.BIN copies the engine ROM (a normal 32X
  cartridge image, file 20) into the cart and starts both SH2s. Engine ROM rules
  (prep_openlara_cd.py): 'SEGA' at 0x100, 32X header at 0x3C0, SDRAM image ≤ 0x30000 bytes
  to ≤ SDRAM+0x38000, SH2 entries in SDRAM, marker 'CDB2' at 0x3BC.
- **Commands as they are:** 38 open file, 46 file chunk → cart, 48 pad, 45/54 CD audio,
  51 file → frame buffer, 52 Genesis display, 58/61 saves.
- **Decision (Micronut99): only proven paths, nothing invented.** The SH2 never writes the
  cart. Graphics are decoded on the PC at build time into disc files and copied into the
  cart with the proven CMD 38 + CMD 46, exactly like OpenLara's levels. (A frame buffer →
  68000 → cart path was considered and dropped: no proof it works on the console.)
- **Testing:** an emulator shows whether a disc has a chance to boot; only the console
  (Sega CD + 32X + RAM cart) is the real test, reported with phone photos. Every stage of
  start-up therefore shows something readable on screen.
- Engine start-up: OpenLara's engine uses its own 32x crt0 (not in this zip); OpenJazz will
  use Tyrian's cartridge crt0 (`port/mars/crt0_tyrian.s` + linker script), which already
  has the stack guard, slave start and error screens.

## J4a - first disc: boot test (sent)
- `build.sh`: splash → `make -C mars` (engine) → OpenLara's `prep_openlara_cd.py` (checks the
  header, writes `cd32x/subcpu/ROMS/OPENLARA.32X`) → loader → Sub-CPU → ISO → the disc checks
  of OpenLara's build.sh → a .cue with one 2 s silent track for emulators.
- `cd32x/sh2`, `cd32x/subcpu`: OpenLara's loader and Sub-CPU program, **byte for byte**
  (`cd32x/ref/openlara` is the untouched copy). The engine is on the disc as OPENLARA.32X,
  the name they open (file 20). Renaming comes later, as a separate change.
- Engine: `port/mars/crt0_oj.s` = Tyrian's crt0 (comments only changed), `port/mars/oj.ld`
  = tyrian.ld + .eh_frame/.gcc_except_table kept + .init_array; `port/plat_mars.c` from
  Tyrian's (no 0x0AAA handshake - the loader did it; C++ constructors + __register_frame_info
  before main; the SH2 never writes the cart); `tools/make_oj_rom.py` (Tyrian's cart tool
  without 68000 program/romfs; 'CDB2' at 0x3BC, "OpenJazz 32X" at 0x3C0; checked with a fake
  ELF against prep_openlara_cd.py in the sandbox).
- Cart: engine 0x000000-0x0FFFFF (1 MB), graphics from 0x100000, saves 0x3F0000.
- On the 32X, OpenJazz's log goes to the error screens' "last log" line (io/log.cpp, OJ32X_MARS).
- Boot test screen (before the game): cart read, C++ constructors, new/delete, exception
  throw/catch, heap, live pad + clock. Start runs the game; with no game files yet it must
  end on the blue screen "THE GAME STOPPED WITH AN ERROR", last log about the data files.
- Untested: everything SH2-side (no SH2 compiler in the sandbox); expect a fix round.

## J4a: the first disc (boot test) - sent
**One boot path (Micronut99): the CD32X boot is OpenLara's, unchanged; the game is attached
at its end** as the payload the loader copies into the cart and starts (file 20).
- `cd32x/sh2/`, `cd32x/subcpu/`: OpenLara's loader and Sub-CPU program, byte-identical copies
  (cdl_crt0.s differs only in line endings). The engine goes on the disc as `OPENLARA.32X`,
  the name the Sub-CPU opens for file 20 - nothing in the boot path is edited.
- Engine image: `mars/Makefile` links the game with `port/mars/crt0_oj.s` (Tyrian's crt0,
  unchanged) and `port/mars/oj.ld` (Tyrian's + .eh_frame/.init_array kept for C++);
  `tools/make_oj_rom.py` (Tyrian's make_tyrian_cart.py without 68000 program/romfs) writes
  the header the loader reads; build.sh runs OpenLara's own `prep_openlara_cd.py` on it as
  the check. Tested here with a made-up ELF: header accepted by prep_openlara_cd.py.
- `port/plat_mars.c`: Tyrian's plat_mars.c (display, pacing, clock, pad 48, Genesis text 52,
  error screens) minus the 0x0AAA handshake (the loader did it), minus sound/CD/mouse/net.
  The SH2 never writes the cart: plat_cart_write = red screen.
- Boot test screen before the game: cart readable, C++ constructors, new/delete, exception
  throw/catch, heap, live pad + clock. Start runs the game; expected end for J4a: blue screen
  "THE GAME STOPPED WITH AN ERROR", last log "Unable to find game data files" (no files yet).
- Cart: engine area 1 MB (0x000000-0x0FFFFF), graphics from 0x100000.
- Untested on SH2 here (no compiler in the sandbox): plat_mars.c, crt0/oj.ld link, C++
  start-up. Expect one fix round from the build log.

## J4a console result (Micronut99's video, real Sega CD + 32X + RAM cart)
SEGA logo -> splash (no bar) -> boot test: cart read OK, C++ constructors OK, new/delete OK,
exceptions OK (0123), heap 12,176 used of 219,024 before the game -> Start -> blue screen
"Unable to find game data files", exactly as expected. Seen: a strip of the splash's bottom
line under the text screens (to look at).

## J4b: splash with progress bar, diagnostics on (Micronut99's request)
Boot path edits, each marked `OJ-J4B`, only switching back what the docs/ comments show:
- `cd32x/sh2/cdloader_sh2.c`: the progress bar again (OL-7 had removed it): empty bar drawn
  with the splash, `splash_progress` after every chunk; bar moved to x 56-236, y 186-198
  (between the logo and the copyright line, measured on Splash.png); Genesis text ON
  (COMM2 = 1 at CMD 52, OL-3 had 0); 256-colour mode with 0x80 = text in front (Kobo K18).
- `cd32x/subcpu/main.c`: KOBO_DIAG_TEXT 1 (green), KOBO_BOOT_TEXT 1 (white) - the
  "diagnostics" column of the CD32X boot handoff's switch table.
- Engine: leaves the text as the loader set it until C++ start-up has worked, then switches
  it off for its own screens; writes COMM12 = 0x4B10 before main() (HW9: the Sub-CPU stops
  reprinting its rows). Exit code printed signed.
- Next: the engine's own loading screen (same picture + bar) when game files load (J4c).

**Diagnostics switches (one per program, all at the top of the file, all documented there):**
| Switch | File | Diagnostics | Release |
|---|---|---|---|
| `OJ_DIAG_TEXT` | `cd32x/sh2/cdloader_sh2.c` | 1 (text on, in front of the splash) | 0 |
| `KOBO_DIAG_TEXT` | `cd32x/subcpu/main.c` | 1 (green STATE text) | 0 |
| `KOBO_BOOT_TEXT` | `cd32x/subcpu/main.c` | 1 (white start-up messages) | 0 |
| `OJ32X_DIAG_TEXT` | `port/plat_mars.c` | 1 (text kept until C++ start-up) | 0 |
The progress bar is not a diagnostic: it is always on.

## Next round (J3)
Graphics into the cart, tested on the PC: a cart image in the host build (4 MB, written only
by a "68000" copy routine, read-only for the game), the decoded tile set, sprites, fonts and
menu pictures placed there, the level grid split. Goal: SDRAM heap under ~150 KB in every
level, regression still 0 different.
