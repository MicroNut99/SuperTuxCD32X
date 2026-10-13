# SuperTux for Sega CD + 32X (SuperTux CD32X)

SuperTux 0.1.3 ("Milestone 1", 2004 – the classic platformer with Tux) on the **Sega CD32X**,
drawn with Victor Luchits' **YATSSD** tile renderer on the boot, cart and Sub-CPU layers of the
Hurrican CD32X port. **Version R3.** The whole game runs from one CD: the title with its demo
level, Icyisland and both Bonus Islands with their world maps, all 76 levels, every bad guy and
power-up, CD music, PCM sound effects, saving and high scores. The levels are kept in a 4 MB RAM
cart once they have been read from the disc.

> **SuperTux is free software (GPL-2.0).** The build downloads SuperTux 0.1.3 itself and
> converts its data; nothing of SuperTux is stored in this repository. A disc built from it
> falls under GPL-2.0 too.

**This is an exercise, not an improvement.** The point is to practise building a Sega 32X / CD32X
game from source code and data that are already available. SuperTux gains nothing from running
on the 32X: it looks and plays no better than on a PC, and the hardware forces small losses
(half resolution, 256 colours, keyed edges instead of soft ones, no floating point).

## Hardware
- Sega Genesis / Mega Drive
- Sega CD / Mega-CD
- Sega 32X
- **A 4MB RAM cart** (battery-backed), in the 32X cartridge slot – required
- a 3- or 6-button pad

Emulators: Kega Fusion cannot emulate the 4MB RAM cart, so the game does not run there. ares can.

## What you need
| What | Where it goes |
|---|---|
| SuperTux 0.1.3 | fetched by the build (`tools/stx_fetch.sh`, tag v0.1.3 of github.com/SuperTux/supertux) into `SuperTux-0.1.3/` |
| Chilly Willy's Sega toolchain (`/opt/toolchains/sega`: sh-elf for the 32X, m68k-elf for the Sega CD) | your build machine (Linux / WSL) |
| `git`, `gcc`, Python 3 with numpy and Pillow | your build machine |

The music is SuperTux's own (its modules rendered to CD audio tracks at build time with
libxmp-lite), and so are the sound effects, the pictures and the splash.

## Building
```bash
./build.sh 2>&1 | tee build_log.txt
```
1. The first run fetches SuperTux 0.1.3.
2. The build converts the data, makes the splash, the sound effects and the CD tracks, checks
   the game on the PC (`host/`), builds the SH2 program, the loader and the Sub-CPU side.
3. **Burn `cd32x/subcpu/SuperTuxR3.cue`** (with its `.bin`). It has the data track and the 12
   music tracks.

`DATA_ONLY=1 ./build.sh` stops after the data and the PC check (no Sega toolchain needed).
The build stops at once if a boot path file differs from Hurrican's (`tools/check_boot.py`).

## Starting the game
Put the RAM cart in the 32X, the disc in the Sega CD, and switch on. The SuperTux picture appears
with a loading bar while the program and the game's data are copied into the cart; the title
follows with Tux running through its demo level behind the menu.

## Fast start and the cart
The RAM cart keeps its contents without power, and the game uses it as a store of levels.

- **The data at power-on:** if the cart already holds the same data from the last time, the boot
  only checks it – the bar jumps to full and nothing is copied. After a new build, the first
  start copies it again.
- **The title never waits:** its demo level is part of that data.
- **A level is read from the disc once.** Each level has its own place in the cart. Icyisland's
  levels 1–4 stay there until the console is switched off; the others stay until newer levels
  need their room (about the last 5–8 played). Dying, restarting, aborting, coming back to a
  level, the world map and the title: no disc reads.
- **A level read** shows its name, the lives, its author and a yellow bar. Only the level itself
  is read (R2 also read the splash picture twice before every level).

Cart layout: program 0–1 MB, shared data and the title's level 1–1.6 MB, world 1's first levels
1.6–2.5 MB, the other levels 2.5–3.94 MB; the last 64 KB belong to the platform layer. The game's
saves and high scores are in the Sega CD's backup RAM. The layout is
explained in `tools/stx_pack.py`.

## Controls
| Button | Action |
|---|---|
| Left / Right | walk |
| **B** (held) | run, carry Mr. Iceblock; fire Tux shoots |
| **C** or **A** | jump (hold it to jump higher) |
| Down | duck (big Tux) |
| **Start** | pause menu |

**World map:** the pad walks Tux along the paths, **A** or **C** enters a level or a teleporter,
**Start** opens its menu (Continue, Options, Quit Game).

**Menus:** up/down chooses, **A**, **C** or **Start** selects, **B** goes back; in Options
left/right changes a setting. *Options → Jump* swaps A and B (C/B jump, A runs).

## What is in this version
- **The title:** SuperTux's demo level with Tux walking and jumping by himself behind the logo;
  Start Game, Bonus Levels, Options, Credits, High Scores.
- **Five save slots** (a new game shows the story first), plus one save for each Bonus Island.
- **Icyisland** (26 levels) and **Bonus Island I and II** (50 levels) with their world maps:
  paths, one-way paths, teleporters, messages, red and green level dots, Tux walking on by
  himself after a level, the ending text and the credits after the last level.
- **Level Select** (under Bonus Levels): any of the 76 levels on its own.
- **Tux** small, big and fire, running, skidding, ducking, ice, bonus boxes (coins, eggs, ice
  flowers, herrings, 1-ups), bricks, coins, invincibility, reset points, the time limit with
  hurry-up music, the end-of-level walk.
- **Every bad guy:** Mr. Iceblock (kick him, carry him), Mr. Bomb and his bomb, Jumpy, the
  stalactite, the flame, the fish, Spiky, the snowball, the bouncing and flying snowballs.
- Animated tiles, snow and clouds, floating scores, broken bricks.
- **Pause menu:** Continue, Restart Level, Options, Abort Level, Main Menu.
- **Options:** music, lives for a new game, the timing line, the jump button, controls, the
  story, high scores, credits (with a music player: left/right plays SuperTux's songs).
- CD music (SuperTux's 12 tracks), 20 sound effects on the Sega CD's PCM chip, high scores and
  saves in the Sega CD's backup RAM.

## Known limitations
- **Half size:** SuperTux's 640 × 480 is drawn at 320 × 240 and sharpened; the 32X shows the
  whole width and 352 of the 480 lines (176 pixels), with the camera following Tux up and down.
  The status line is in a panel under the level.
- The background picture scrolls with the level (SuperTux scrolls some at half speed).
- 24 snowflakes instead of SuperTux's 128.
- No level editor and no Quit in the main menu.
- Levels cannot be read while music plays (the CD drive can't do both), so the music stops
  during a level read.

## Frame rate
**Status: 38–56 frames per second** in Icyisland's first level, measured on real hardware with
the timing line (*Options → Timing*). R2 ran at 17–22.

The 32X shows a new picture only at a screen refresh (60 a second), so 60 is the ceiling: F on
the timing line counts real buffer swaps against the refresh counter. A frame now takes about
10 ms of work, inside one 16.7 ms refresh – most frames wait for the refresh (W).

| Part of a frame | R2 | R3 |
|---|---|---|
| F – frames a second | 17–22 | **38–56** |
| L – the game's rules | 6.5–9.5 ms | 1.9–2.8 ms |
| T – drawing the tiles (both SH2s) | 22–29 ms | 3–9 ms |
| N – tiles drawn a frame | 140–245 | 9–100 (mostly 13–35) |
| S – drawing the sprites | 1–2 ms | 1–1.5 ms |
| H – the status panel | 7–9 ms | 1–4 ms |
| W – waiting for the screen refresh | 0.5–6 ms | 6–9 ms |

### What was done (R3)
- **Scrolling costs almost nothing.** Vic's renderer draws into a canvas a little larger than
  the screen; scrolling only changes where the video chip starts reading lines, plus the
  32X's 1-pixel shift. Only the tiles coming into view are drawn.
- **One picture a cell.** A SuperTux level has four layers (the background picture, the
  background tiles, the tiles Tux stands on, the foreground). Vic's renderer remembers one tile
  number for each cell of its canvas and skips cells that have not changed – but with four
  layers almost every cell was redrawn every frame. The build now merges each cell's layers
  into one 16 × 16 picture, so a cell is drawn only when it scrolls in, changes (a coin is
  taken, a box emptied, an animation frame) or had a sprite over it. Tux, the bad guys and the
  clouds get the right tiles drawn over them again where they should be behind.
- **Both SH2s draw the tiles.** Vic's renderer hands out the cells through a shared counter;
  the second SH2 runs the same drawing call at the same time.
- **Exact sprite marks.** Vic's code marked whole columns of cells as changed under every
  sprite; ours marks only the cells the sprite covers.
- **The status panel** is drawn only when its text changes.
- **The busy code runs from the 32X's fast RAM**, not from the cart (the rules, the level,
  Tux, the bad guys, the drawing calls).
- **What's left:** every 64 pixels sideways the canvas is moved and the screen drawn whole once
  (the 38–42 fps moments, N 76–103).

## Versions
| Version | What changed |
|---|---|
| R1b | The first disc: world 1, Tux walking, running and jumping, coins, the HUD. |
| R1c–R1f | The SuperTux splash with a loading bar, louder music, the menus and extras of the other CD32X ports, options in the pause menu. |
| R2 | The whole game: title with demo, slots, world maps, all levels, every bad guy and power-up, texts, high scores. Half size, sharpened. 17–22 fps. |
| R3 | 38–56 fps: one merged picture a cell, both SH2s drawing, exact sprite marks, a cheap panel, the busy code in fast RAM. The cart as a store of levels: the title's level from power-on, levels read once. |

## Files
| Path | Contents |
|---|---|
| `game/stx.h` | the game's map: what each file does, the shared data and calls |
| `game/res.c`, `level.c` | the shared data (sprites, letters, pictures, texts); a level, the cart store, the tile rules |
| `game/tux.c`, `objects.c` | Tux (player.cpp); bad guys, upgrades, bullets, bricks, scores (badguy.cpp, special.cpp, gameobjs.cpp) |
| `game/game.c` | a level being played (gameloop.cpp, world.cpp): camera, particles, music, the end, drawing |
| `game/wmap.c`, `menus.c` | the world maps (worldmap.cpp); title, menus, texts, high scores, saves, `main()` |
| `game/yt.h`, `yt_layers.c`, `yt_mars.c` | Vic's renderer on Hurrican's screen; the level reader |
| `game/host.c`, `yt_host.c`, `host/` | the PC build that checks the game: the platform layer and Vic's drawing as C stand-ins |
| `mars/Makefile` | the SH2 program |
| `yatssd/` | Victor Luchits' YATSSD files, unchanged (`YATSSD_COMMIT.txt`: the version) |
| `cd32x/`, `port/` | the Hurrican CD32X port's boot path and platform layer, unchanged |
| `tools/stx_convert.py`, `stx_pack.py` | SuperTux 0.1.3 → the 32X's data; the disc's files and the cart layout |
| `tools/make_stx_sfx.py`, `make_stx_splash.py`, `stx_music.sh` | sound effects, the splash, the CD tracks |
| `tools/check_boot.py`, `check_sdram_code.py`, `where.sh` | build checks; naming the addresses on a red exception screen |
| `docs/` | developer notes: `STX_HANDOFF.md`, `R3_NOTES.md`, `R2_NOTES.md`, `R1B_NOTES.md`, `BOOT_PATH_NOTES.md` |

## Credits
- **SuperTux** – Bill Kendrick (original author), Tobias Glaesser (project leader), Ricardo Cruz
  and the SuperTux team: https://github.com/SuperTux/supertux (GPL-2.0, code and data). The
  music modules are by their own authors (see SuperTux's credits).
- **YATSSD** – Victor Luchits (viciious): the tile renderer (MIT, `yatssd/LICENSE_YATSSD.txt`)
- **Hurrican CD32X / OpenJazz CD32X ports** – the boot path, cart and Sub-CPU layers; the CD32X
  loader from the **OpenLara CD32X** port
- **Sega CD / 32X boot and command framework** – from the Kobo Deluxe CD32X project, built on
  Chilly Willy's Sega CD and 32X examples and toolchain
- **4MB 32X RAM cart** – Victor Luchits, Joseph Fenton, Leo Oliveira, Tiido Priimägi and Eric Witt
- **libxmp-lite** (LGPL 2.1) renders the music at build time; nothing of it is on the console
- SuperTux port – MicroNut99

Licences: GPL-2.0 for SuperTux and a disc built from it; see `yatssd/` and each upstream
project's own licence.
