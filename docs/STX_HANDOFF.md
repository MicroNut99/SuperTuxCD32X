# SuperTux CD32X - handoff

Project folder: `S:\KoboPort\sega-toolchain-12.1\sega\SuperTuxCD32X`
(WSL `/mnt/s/KoboPort/sega-toolchain-12.1/sega/SuperTuxCD32X`), repository
github.com/MicroNut99/SuperTuxCD32X.

## Decisions (Micronut99, Oct 2026)

- **Target: SuperTux 0.1.3 ("Milestone 1", 2004).** The current SuperTux (0.6.x) is 136,000
  lines of C++ with OpenGL and Squirrel and 245 MB of data - out of reach. 0.1.3: 20,000 lines
  (1,900 of them the level editor), SDL 1.2, 640x480, 32x32 tiles, 16 MB of data.
- **A CD32X disc with the 4 MB RAM cart from the start** (not a cartridge first). The boot path,
  cart and Sub-CPU layers are Hurrican CD32X's (= the OpenJazz port's J19b, proven on the
  console), taken unchanged into `cd32x/` and `port/`.
- **Vic's tiler for drawing:** YATSSD (`yatssd/`, MIT): tile maps of any size, several layers
  with parallax, dirty rectangles, the hardware 1-pixel shift, both SH2s drawing, sprites.

## The boot path - never edited

Rule taken over from Hurrican (its first benchmark disc, with an edited boot path, gave a black
screen; with the boot path byte for byte as the last proven disc, it booted): `cd32x/`, `port/`,
`tools/make_oj_rom.py`, `tools/make_boot_image.py`, `tools/mars_boot.bin` are Hurrican CD32X's
(commit 37852a6), unchanged. New code runs only from `main()` and uses the platform layer's
existing calls. `tools/check_boot.py` (run first by build.sh) compares every one of those files
with `docs/BOOT_FILES.md5` (Hurrican's) and stops the build on any difference.

**Diagnostics ON** (Micronut99: the Sega CD may not boot; without the text nothing can be
diagnosed). Hurrican had them off (release). The documented switches, nothing else:

| Switch | File | Hurrican | Here | Shows |
|---|---|---|---|---|
| `OJ_DIAG_TEXT` | `cd32x/sh2/cdloader_sh2.c` | 0 | **1** | the loader switches the Genesis text on (CMD 52) and draws it in front of the splash |
| `KOBO_DIAG_TEXT` | `cd32x/subcpu/main.c` | 0 | **1** | the Sub-CPU's green text (STATE codes, file names, checkpoints) |
| `KOBO_BOOT_TEXT` | `cd32x/subcpu/main.c` | 1 | 1 | Chilly's white 32X start-up messages (already on) |
| `SCD_DIAG_TEXT` | `cd32x/subcpu/scd.c` | 0 | **1** | the green CKPT / INITCD lines and the CD error / root-directory lists |
| `OJ32X_DIAG_TEXT` | `port/plat_mars.c` (build flag `-DOJ32X_DIAG_TEXT=1` in mars/, R1b) | 0 | **1** | the text stays until the engine's C++ start-up has worked |

Not changed (part of the boot path): `cd32x/subcpu/hw_md.s` switches the Genesis display OFF at
the end of the 68000's set-up (OL-13), so the white messages written before the SH2 loader runs
are in video RAM but not visible until the loader's CMD 52 turns the display on.

## Facts that shape the design

- **No floating point, as in Hurrican:** SuperTux 0.1.3's physics are float (`physic.cpp`,
  `player.cpp`); the SH2 has no FPU. Hurrican's lesson: porting the C++ objects ran at 4-5 fps.
  So the rules are rebuilt in C, fixed point, in SuperTux's own units, checked on the PC.
- **C++ standard library:** `ls /opt/toolchains/sega/sh-elf/sh-elf/lib/ | grep stdc++` printed
  nothing on Micronut99's PC, but Hurrican's `mars/Makefile` links `-lstdc++` (the multilib
  folders hold it). Not needed if the game is C.
- **The RAM cart is read-only for the SH2** (`port/pack.h`: "The SH2 cannot write the cart",
  OpenLara lesson 1): it is a 4 MB store filled from the disc through the 68000 (commands 38 +
  46, `plat_load_pack`, `plat_cart_write`), not extra working RAM. Working memory is the 256 KB
  SDRAM. Everything is prepared on the PC in the form the 32X draws from.
- **Screen height:** levels are 15 tiles = 240 lines at half size, the screen 224: the camera
  scrolls the last 16 lines vertically (the previews show the bottom 224 lines).
- Levels hold tile ids up to 295 (and one 2560, a bad id in one level); 188 different ids are
  used in all - a level map can become u8 through a table.
- The "-fast" modules are real faster versions (not copies): separate CD tracks.

## R1a (this round) - measured on SuperTux 0.1.3 (tag v0.1.3)

`./build.sh` -> `build/data/REPORT.TXT`:

| | |
|---|---|
| tiles | 296 ids, 412 frames 16x16 = 105,472 bytes |
| global colours | 191 (1-191); 192-255 the level's background |
| sprites (shared + small status pictures) | 189 pictures, 53,470 visible pixels after halving |
| backgrounds | 7 pictures 320x240 in 64 colours = 76,800 bytes each |
| levels | 77; 3.0 MB as stored now (3 layers u16), 101 KB compressed (zlib) |
| objects | snowball 661, mrbomb 508, stalactite 504, mriceblock 492, spiky 440, bouncingsnowball 350, flame 347, flyingsnowball 312, money 245, jumpy 125, fish 83 |
| music | 12 tracks, 15 min in all (Mortimers_chipdisko 158 s, theme 96 s, SALCON 90 s, cave 77 s, fortress2 67 s, fortress 45 s, the fast versions shorter, leveldone 8 s) |

Previews checked: all 77 levels draw (tiles, backgrounds, gradients, objects marked).

## Next (R1b)

1. Level packs: maps through a tile table (u8) and block-compressed (as Hurrican's MAP.BIN),
   one pack per level set; the resident pack: tiles, sprites, backgrounds, palette.
2. `mars/`: Hurrican's engine Makefile with `game/` new: YATSSD `draw_tilemap` for the three tile
   layers, the background picture as a parallax layer (or on a Genesis plane - to measure),
   Tux's walk / run / jump in fixed point (SuperTux's `physic.cpp`, `player.cpp` rules).
3. `host/`: the same game code on the PC, frames as pictures (as Hurrican's host build).
4. The disc: Hurrican's build steps, the timing line on screen.
