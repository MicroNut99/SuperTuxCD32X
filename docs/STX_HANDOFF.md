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
| Genesis display at the end of the 68000's set-up | `cd32x/subcpu/hw_md.s` `md_init_hw` | `0x8134` off (OL-13) | **`0x8174` on** | Chilly's white boot lines ("Activated 32X", "Cleared 32X", "32X Waiting", "CD BOOT TABLE" ...) from the very start, before the SH2 loader runs |

**The green lines - checked one by one** (Micronut99: "super important"). With the switches
above, every green line in the boot path is compiled in and drawn in a visible colour:

- the colour: `hw_md.s` sets palette line 1 to `0x00A0` (green; Kobo had it black for release);
- `main.c` `print_diag()` (all rows 0-27: build line, CD drive, Sub-CPU, 68K listener, 32X
  init and its errors, SH2 handshake, cart check, `STATE:` row 10, the Sub-CPU heartbeat row
  19, file and chunk lines): gated only by `KOBO_DIAG_TEXT` (now 1) and `quiet`, which is set
  only by command 44 when `SLIDESHOW_DEBUG` is 0 - it is 1, so never;
- the `#if KOBO_DIAG_TEXT` (OL-5) blocks - the heartbeat row 19 and the `STATE:` row 10 - are
  compiled in;
- `scd.c` `scd_diag_print`, `scd_diag_print_row2`, `scd_timeout_print`, `scd_print_row` (CKPT,
  INITCD, CD time-outs, file not found + the root directory list): gated only by
  `SCD_DIAG_TEXT` (now 1);
- the loader shows its codes (4D01-4D07, 4C00-4C3F, errors 4DE1-4DE4) through COMM12 in the
  `STATE:` row, and puts the Genesis text in front of the splash (`OJ_DIAG_TEXT` 1: DISPMODE
  0x81).

When they stop, by design of the boot path (not changed):

- `port/plat_mars.c` switches the Genesis text off (CMD 52) just before the game's `main()`,
  after the resident pack and the sound effects are loaded - the game's own screens follow;
- it then writes `STATE 4B10` (the game runs): the Sub-CPU stops reprinting rows 10, 18 and 19
  (OL-2b / HW5: reprinting every pass dropped Kobo to 1-2 fps, each line goes through the 68000).

In the game itself, problems show on the 32X side: the error screen (`plat_fatal`, with the last
log line) and the game's timing line.

**Why the white text is on (Micronut99):** for the first builds, so that a disc that stops
early is not a black screen - the white lines show how far the boot got. Later builds turn it
off again when the splash screen takes over that job: the splash is what tells the player the
system is working while a slow boot loads. Not every game boots equally fast - timing and
what the player sees while waiting matter (a black screen with no sign of life causes anxiety).
For a release: `0x8134` here (OL-13) and the other switches back to Hurrican's values, with the
splash on.

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

## R1b - the first disc (docs/R1B_NOTES.md)

- `game/stx.c`: world 1 (26 levels, `OJ001.PAK`-`OJ026.PAK`), Tux walking / running / skidding /
  jumping with SuperTux 0.1.3's rules in fixed point at its own 100 steps a second, coins, the
  level end, lives and time, the HUD and the timing line in a 24-line panel, pause with level
  choice.
- Drawing: Vic's tiler (`yatssd/` unchanged: `draw_handle_layercmd` + dirty matrix, `draw_sprite`,
  the pixel loops, the font) in Hurrican's frame buffer layout; `game/yt_layers.c` is YATSSD's
  `draw_tile_layer` without its three hardware calls (line table / shift register, the COMM4
  slave hand-shake, the Genesis planes), which cannot run under this boot path - R1B_NOTES.md
  says why, call by call.
- Build: `build.sh` = Hurrican's disc steps with SuperTux's data; `mars/Makefile` = Hurrican's
  with `game/` + Vic's files; `EXTRA_DEFS="-DOJ32X_MUSIC=1 -DOJ32X_DIAG_TEXT=1"`.
- Checked on the PC (`host/`, frames looked at, address/UB checkers with random input) and
  compiled + linked for the SH2 here; not yet run on the console.

## Next

1. R1b on the console (ares / real hardware): boot, first picture, timing line, music.
2. Then, by what the timing line shows: the background at half speed (SuperTux's bkgd_speed 50)
   - on the Genesis planes at the very end (68000 side, as Hurrican and Tyrian plan), or as a
   separate layer on the 32X; animated tiles; enemies (snowball, mriceblock, ... - the objects
   are already in Lnnn.BIN); bonus blocks and bricks; big Tux.
