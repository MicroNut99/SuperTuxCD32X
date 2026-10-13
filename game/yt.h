/*
 * SuperTux CD32X - Vic's tiler (YATSSD, yatssd/, MIT) on Hurrican CD32X's screen.
 *
 * WHAT IS VIC'S AND WHAT IS OURS
 *   Vic's code, unchanged (yatssd/), does the drawing:
 *     dtiles.c   draw_handle_layercmd() - draws one tile layer into the canvas and keeps the
 *                "dirty matrix" (which tile each canvas cell holds, so unchanged cells are
 *                skipped); draw_dirtyrect() - marks the cells a sprite covered
 *     dsprite.c  draw_sprite() - clips a sprite and calls the pixel loop; marks its cells dirty
 *     draw.c / draw_inc.h  the pixel loops (16-bit writes; the 32X "overwrite image" window
 *                makes colour 0 see-through), the scissor
 *     font.c     the 8x8 font (msx[]) - for the HUD
 *     sh2_fixed.s  FixedMul / IDiv (dsprite.c's scaled sprites)
 *   Ours (game/yt_layers.c, game/yt_mars.c), in place of Vic's draw_tile_layer() and hw_32x.c:
 *     the camera -> canvas arithmetic of draw_tile_layer(), WITHOUT its three hardware calls,
 *     which cannot run under Hurrican's boot path (docs/R1B_NOTES.md):
 *       - Hw32xUpdateLineTable() / MARS_VDP_SHIFTREG: the platform layer owns the line table
 *         and the shift register (plat_present_scroll writes them, the shift at the swap)
 *       - the COMM4 hand-shake with YATSSD's own slave-SH2 loop: our slave runs Hurrican's
 *         plat_parallel() jobs, not Vic's loop - the master would wait for ever
 *       - HwMd*(): the Genesis planes through YATSSD's own 68000 program - our 68000 runs the
 *         boot path's listener (cd32x/subcpu/hw_md.s), never changed
 *
 * THE SCREEN (Vic's canvas inside Hurrican's frame buffer layout, port/plat_mars.c J15)
 *   frame buffer byte 0x00200  the canvas: 384 bytes a line (canvas_pitch), 200 lines - the
 *                              same place and pitch as Hurrican's ring; used WITHOUT wrapping,
 *                              as YATSSD uses it
 *   0x12E00                    the panel: 24 lines of 320 (the HUD and the timing line)
 *   0x14C00                    Vic's "extrafb": the dirty matrix (canvas_yaw = 220 puts it here:
 *                              0x200 + 384 * 220), 3 + 24 * 12 words
 *   0x16000                    the platform layer's blank line (the border lines)
 *   The view is 320 x 176: plat_present_scroll(pal, scroll_y, scroll_x, 176) shows canvas rows
 *   scroll_y .. scroll_y + 175 from column scroll_x, then the 24 panel lines.
 *   As in YATSSD, the canvas holds the view plus 64 columns and 16 rows: when the camera passes
 *   a multiple of 64 sideways or 16 up / down, the canvas is "re-anchored" and the dirty matrix
 *   redraws the cells whose tile changed. scroll_x stays 0..63, scroll_y 0..15, so the line
 *   table entries never end in 0xFF - the hardware shift-register bug cannot happen (that is
 *   why Vic chose 384: "the sweet spot").
 */
#ifndef STX_YT_H
#define STX_YT_H

#include <stdint.h>

#define YT_VIEW_W   320
#define YT_VIEW_H   176          /* the level's part of the screen */
#define YT_PANEL_H  24           /* the panel below it (plat_present_scroll's panel) */
#define YT_PITCH    384
#define YT_ROWS     200          /* canvas lines available (Hurrican's ring area) */
#define YT_YAW      220          /* canvas_yaw: only places the dirty matrix (0x14C00) */
#define YT_TILE     16
#define YT_SCROLL_X 64           /* re-anchor every 64 columns (4 tiles) ... */
#define YT_SCROLL_Y 16           /* ... and every 16 lines (1 tile) */

/* a level for the tiler: four layers of YATSSD tile numbers (resource << 2, 0 = nothing),
 * w x h tiles each; reslist[r] = the 16x16 pixels of resource r */
void yt_level(const uint16_t *const layers[4], int w, int h, uint8_t **reslist);

/* one frame (into the back buffer, after plat_wait_flip):
 *   yt_begin(cx, cy)        the camera (half-size pixels, top-left of the view)
 *   yt_layers(first, last)  draws tile layers first..last (Vic's draw_handle_layercmd)
 *   yt_sprite(...)          a sprite at VIEW coordinates (Vic's draw_sprite, overwrite mode)
 *   yt_scroll_x/y()         what to give plat_present_scroll */
void yt_begin(int cx, int cy);
int  yt_layers(int first, int last);          /* returns the tiles drawn */
int  yt_sprite(int vx, int vy, int w, int h, const uint8_t *pixels);
int  yt_scroll_x(void);
int  yt_scroll_y(void);
/* a word of the dirty matrix's tile-map id (forces a full redraw of both buffers) */
void yt_invalidate(void);

/* platform parts (game/yt_mars.c on the 32X, game/yt_host.c on the PC) */
int16_t *yt_extrafb(void);                    /* Vic's extrafb in the back buffer */
void yt_tile_counter_reset(void);             /* COMM6 = 0 (Vic's tile counter, master only) */

#endif
