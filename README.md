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

## Status: R1b - the first disc (not yet run on the console)

- World 1's 26 levels; small Tux walks, runs (B), skids and jumps (C or A) with SuperTux 0.1.3's
  own rules (rebuilt in fixed point, at its own 100 steps a second); coins, lives, time, the level
  end and the next level. START pauses; in the pause LEFT / RIGHT picks a level, A or C goes there.
- Drawn with Victor Luchits' YATSSD (unchanged) in the Hurrican port's frame buffer layout:
  320 x 176 of level, a 24-line panel with the HUD and a timing line (frame rate, ms per part).
- The music is SuperTux's, as CD audio; 12 of its sounds play on the Sega CD's PCM chip.
- Not yet: enemies, bonus blocks, big Tux, animated tiles, the background's half-speed scroll.
- The boot path is Hurrican CD32X's, unchanged, with every diagnostic line on (white and green
  text from power-on until the game starts).

`docs/R1B_NOTES.md` explains the screen, how Vic's tiler runs under this boot path and what to
look for on the console. `build/previews/` shows every level as the 32X draws it.

## Building

Needs WSL / Linux with `git`, `gcc`, Python 3 with numpy and Pillow, and the Sega toolchains
(`/opt/toolchains/sega`: sh-elf for the 32X, m68k-elf for the Sega CD side), as for the Hurrican
port.

```bash
./build.sh 2>&1 | tee build_log.txt
```

The first run fetches SuperTux 0.1.3 (`tools/stx_fetch.sh`: tag v0.1.3 of
github.com/SuperTux/supertux) into `SuperTux-0.1.3/`. Nothing of SuperTux is stored in this
repository. The disc: `cd32x/subcpu/SuperTuxR1b.cue` (+ `.bin`) - ares or real hardware
(Fusion cannot emulate the RAM cart). `DATA_ONLY=1 ./build.sh` stops after the data and the PC
check.

## Layout

| Path | Contents |
|---|---|
| `game/stx.c` | the game (SuperTux's rules in C, fixed point) |
| `game/yt.h`, `yt_layers.c`, `yt_mars.c` | Vic's tiler on Hurrican's screen: the layer driver and the two registers it needs |
| `game/host.c`, `yt_host.c`, `host/` | the PC build: the platform layer and Vic's drawing as C stand-ins |
| `mars/Makefile` | the SH2 program (Hurrican's, with `game/` and Vic's files) |
| `yatssd/` | Victor Luchits' YATSSD files, unchanged (`YATSSD_COMMIT.txt`: the version) |
| `cd32x/`, `port/` | the Hurrican CD32X port's boot path and platform layer, unchanged |
| `tools/stx_convert.py` | SuperTux 0.1.3 -> tiles, palette, sprites, backgrounds, levels, previews |
| `tools/stx_pack.py` | the packs for the disc (resident `IMAGE8.RAW`, `OJnnn.PAK` per level) |
| `tools/make_stx_sfx.py`, `make_stx_splash.py`, `splash_bar_colours.py` | sound effects, the splash |
| `tools/stx_music.sh` | the modules -> CD audio tracks (libxmp-lite, `tools/it2wav.c`) |
| `tools/check_boot.py` | stops the build if a boot path file differs from Hurrican's |
| `tools/stx_fetch.sh` | gets SuperTux 0.1.3 |
| `docs/` | developer notes: `STX_HANDOFF.md`, `R1B_NOTES.md`, `BOOT_PATH_NOTES.md`, `EXAMPLES_NOTES.md` |

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
