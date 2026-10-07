# Jazz Jackrabbit CD32X

Jazz Jackrabbit (1994, Epic MegaGames) for the **Sega CD + 32X**, built on the OpenJazz engine.
All episodes of the CD edition, Holiday Hare '94 and '95, the bonus levels, CD audio music and
sound effects.

## What you need

- A Sega Genesis / Mega Drive with **Sega CD** and **32X**
- A **4 MB RAM cart** in the 32X cartridge slot (the game keeps its data there)
- The game disc, burned from `OpenJazzCD32X.cue` (disc-at-once, 80-minute CD-R)

Emulators without a 4 MB RAM cart (for example Fusion) cannot run it: the loader stops on the
splash screen.

## Starting

Switch on with the disc inserted. The first start copies the game into the RAM cart (about a
minute and a half, with a loading bar). Later starts find it there and go straight to the main
menu.

If something looks wrong after starting, do a **full reload**: hold **A + B + C**, switch on, and
let go when the loading bar appears.

## Controls

| In a level | |
|---|---|
| D-pad | move, look up / down |
| C or A | jump |
| B | fire |
| X or Y | change weapon (6-button pad) |
| Z | frame counter on / off (6-button pad) |
| Start | pause menu: continue, save, load, setup, quit |

The set-up can swap A and B (A fires, B jumps; C always jumps).

| In menus | |
|---|---|
| D-pad | choose |
| A, C or Start | select |
| B | back |

## Main menu

- **New game:** choose an episode, then a difficulty.
  - Episodes 1-6 and A-C, X Holiday Hare ('94), Z Bonus levels.
  - **Holiday Hare 95** is the last entry of the episode list.
- **Load game:** four save slots.
- **Setup options:** character (name and colours), buttons (A/B swap), audio (music on/off,
  sound effects on/off), gameplay options.
- **Instructions**, **Order info:** the original screens.
- **Credits** (last entry): the credits over a moving background, with a **CD player** for the
  soundtrack:
  - Left / Right choose a track, A, C or Start play it (on the playing track: stop / play)
  - tracks play one after another, starting again after the last one
  - Y, X, Z, Mode, Up or Down hide / show the credits text

## Bonus levels

Finish a level carrying the big red gem to reach a 3D bonus stage, or start them from
"Z Bonus levels" in the episode menu.

## Saving

Games are saved in the RAM cart and survive switching off. Each game (Jazz Jackrabbit and
Holiday Hare '95) has its own four slots. Settings are saved when you leave the set-up menu;
the frame counter (Z) and the A/B swap are remembered too.

## Loading times

Each planet (two levels) is loaded once with a loading screen, about 30-45 seconds; its second
level starts without loading. A bonus level is loaded on entry, and the next planet again after it.

## Known limitations

- The game runs at about 10-13 frames per second in levels; scrolling is not smooth.
- The intro and the cutscenes between episodes are not included.
- No demo mode, no multiplayer.
- Music and sound effects can only be switched on or off, not set to a volume.
- Sound effects are lower quality than the original (5 kHz, to fit the Sega CD's sound chip).

## Credits

Jazz Jackrabbit: Epic MegaGames - a game by Arjan Brussee and Cliff Bleszinski.
OpenJazz: Alister Thomson and contributors.
D32XR 32X code: Victor Luchits. Sega CD and 32X framework: Chilly Willy.
Sega CD32X port: Micronut99.

Jazz Jackrabbit is the property of its rights holders. This package contains no game data; the
disc is built from a copy of the game you own.
