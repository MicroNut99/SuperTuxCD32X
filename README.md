# SuperTux CD32X

Work towards **SuperTux 0.1.3** ("Milestone 1", 2004 - the classic Mario-style platformer with Tux)
on the **Sega Mega Drive / Genesis + Mega-CD / Sega CD + 32X** with a **4 MB RAM cart**, built on
the boot, cart and Sub-CPU layers of the Hurrican CD32X port, with **Victor Luchits' YATSSD
tile renderer** for drawing.

## Why this exists

**This is an exercise, not an improvement.** The point is to practise building a Sega 32X / CD32X
game from source code and data that are already available. SuperTux gains nothing from running
on the 32X: it will look and play no better than on a PC, and the hardware forces small losses
(half resolution, 256 colours, keyed edges instead of soft ones, no floating point).

## Status: R1a - the data in 32X form

Not playable yet. This round turns SuperTux's data into what the 32X will draw, and measures it:

| | |
|---|---|
| Tiles | 296 tile ids, 412 animation frames, halved to 16x16: 105 KB |
| Colours | one palette for all tiles and sprites (1-191), 64 per level for its background (192-255) |
| Backgrounds | 320x240, 64 colours each (4 used by levels: arctis2, cave2, ocean, arctis_dark; the others are title/ending pictures) |
| Levels | 77 (world 1, bonus 1, bonus 2, menu); 15 tiles high = 240 lines at half size, the screen shows 224; 188 different tiles used in all |
| Objects | snowball, bouncingsnowball, flyingsnowball, mriceblock, mrbomb, jumpy, spiky, stalactite, fish, flame, money |
| Music | 12 CD audio tracks rendered from SuperTux's modules (2 theme, 3-12 the five level songs and their "-fast" versions, 13 level done), about 15 minutes |

`build/previews/` shows every level as the 32X will draw it (`ALL_LEVELS_START.png`: the start
of all 77).

### Next (R1b)

The engine and the first disc: Hurrican's boot path (already here, unchanged: `cd32x/`, `port/`),
YATSSD's tile map renderer (`yatssd/`, unchanged) for the three tile layers, level 1 scrolling,
Tux walking and jumping, with a timing line for the frame rate on the console.

SuperTux's own code is C++ with floating point and the C++ standard library; the SH2 has no FPU.
As in the Hurrican port, the game's rules are rebuilt in C with fixed-point maths, in SuperTux's
own units (its 640x480 pixels), and the PC build checks them.

## Building (R1a)

Needs WSL / Linux with `git`, `gcc`, Python 3 with numpy and Pillow.

```bash
./build.sh 2>&1 | tee build_log.txt
```

The first run fetches SuperTux 0.1.3 (`tools/stx_fetch.sh`: tag v0.1.3 of
github.com/SuperTux/supertux) into `SuperTux-0.1.3/`. Nothing of SuperTux is stored in this
repository.

## Layout

| Path | Contents |
|---|---|
| `tools/stx_convert.py` | SuperTux 0.1.3 -> tiles, palette, backgrounds, levels, previews (formats in its header) |
| `tools/stx_music.sh` | the modules -> CD audio tracks (libxmp-lite, `tools/it2wav.c`) |
| `tools/stx_fetch.sh` | gets SuperTux 0.1.3 |
| `yatssd/` | Victor Luchits' YATSSD files, unchanged (`YATSSD_COMMIT.txt`: the version) |
| `cd32x/`, `port/` | the Hurrican CD32X port's boot path and platform layer, unchanged |
| `tools/libxmp-lite`, `it2wav.c`, `make_boot_image.py`, `make_oj_rom.py`, `mars_boot.bin` | from the Hurrican port, unchanged |
| `docs/STX_HANDOFF.md` | developer notes |

## Licences and credits

- **SuperTux** - Bill Kendrick (original author), Tobias Glaesser (project leader), Ricardo Cruz
  and the SuperTux team; GPL-2.0 (code and data). A disc built from it falls under GPL-2.0. The
  music modules are by their own authors (see SuperTux's AUTHORS/credits): check before
  publishing a disc.
- **YATSSD** - Victor Luchits (viciious), MIT (`yatssd/LICENSE_YATSSD.txt`).
- **Hurrican CD32X / OpenJazz CD32X port** - the boot path, cart and Sub-CPU layers; **CD32X
  boot** from the OpenLara CD32X port; **D32XR** 32X code by Victor Luchits; **Sega CD / 32X
  framework** by Chilly Willy.
- **libxmp-lite** (LGPL 2.1) renders the music at build time; nothing of it is on the console.
- **SuperTux CD32X** - Micronut99.
