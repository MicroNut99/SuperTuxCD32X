/*
 * SuperTux CD32X - the PC stand-in for Vic's drawing code (yatssd/dtiles.c draw_handle_layercmd
 * and draw_dirtyrect, dsprite.c draw_sprite, draw.c draw_setScissor / draw_clip). Vic's files
 * use SH2 assembly and the 32X's fixed addresses, so the PC build has these C copies of the
 * same rules (the dirty matrix rule is transcribed line by line), drawing into game/host.c's
 * frame buffers. They also CHECK the layout: every write must land inside the canvas
 * (lines 0-199, columns 0-383 of the 0x200 area) - the 32X would not complain.
 */
#include "yt.h"
#include "types.h"
#include "plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern tilemap_t tm;
extern int window_canvas_x, window_canvas_y;
extern uint32_t canvas_pitch, canvas_yaw;

static rect_t scissor;
static uint8_t *canvas(void) { return plat_scroll_ring(); }   /* frame buffer + 0x200 */

int16_t *yt_extrafb(void)
{
	/* the PC's frame buffer is bytes: Vic's int16 matrix lives at the same byte offset */
	return (int16_t *)(canvas() + YT_PITCH * YT_YAW);
}

void yt_tile_counter_reset(void) { }

static void put(int x, int y, int w, int h, const uint8_t *src, int sx, int sy, int stride, int keyed)
{
	if (x < 0 || y < 0 || x + w > YT_PITCH || y + h > YT_ROWS)
	{
		fprintf(stderr, "YT HOST: write outside the canvas: x %d y %d w %d h %d\n", x, y, w, h);
		abort();
	}
	uint8_t *c = canvas();
	for (int r = 0; r < h; r++)
	{
		const uint8_t *s = src + (sy + r) * stride + sx;
		uint8_t *d = c + (y + r) * YT_PITCH + x;
		for (int i = 0; i < w; i++)
			if (!keyed || s[i]) d[i] = s[i];
	}
}

void draw_setScissor(int16_t x, int16_t y, int16_t w, int16_t h)
{
	scissor.x1 = x; scissor.y1 = y; scissor.x2 = (int16_t)(x + w); scissor.y2 = (int16_t)(y + h);
}

/* dtiles.c draw_dirtyrect, as Vic wrote it (including start_tile_ver = y >> 3, then >> 4 again:
 * it marks from line y / 128 - more cells than needed, never fewer) */
void draw_dirtyrect(tilemap_t *t, int x, int y, int w, int h)
{
	int16_t *extrafb = yt_extrafb();
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x >= (int)canvas_pitch || y >= (int)canvas_yaw) return;
	unsigned sth = (unsigned)x / t->tw, eth = (unsigned)(x + w - 1) / t->tw;
	unsigned stv = ((unsigned)y >> 3) / t->th, etv = (unsigned)(y + h - 1) / t->th;
	int ntx = (int)(eth - sth + 1), nty = (int)(etv - stv + 1);
	if ((int)sth + ntx > (int)t->canvas_tiles_hor) ntx = (int)t->canvas_tiles_hor - (int)sth;
	if ((int)stv + nty > (int)t->canvas_tiles_ver) nty = (int)t->canvas_tiles_ver - (int)stv;
	int16_t *dirty = extrafb + 3 + stv * t->canvas_tiles_hor + sth;
	for (int yy = 0; yy < nty; yy++, dirty += t->canvas_tiles_hor)
		for (int xx = 0; xx < ntx; xx++) dirty[xx] = -1;
}

/* dtiles.c draw_handle_layercmd, the non-parallax branch, one CPU (every tile is "ours") */
int draw_handle_layercmd(drawtilelayerscmd_t *cmd)
{
	tilemap_t *t = cmd->tm;
	const int w = (int)t->tw, h = (int)t->th;
	int16_t *dirty = yt_extrafb() + 3;
	const int cth = (int)t->canvas_tiles_hor;
	const uint8_t **reslist = (const uint8_t **)t->reslist;
	const unsigned l = (unsigned)cmd->startlayer;
	const int16_t *layer = (const int16_t *)t->layers[l].tiles;
	const int16_t *last_layer = (const int16_t *)t->layers[t->numlayers - 1].tiles;
	int drawcnt = 0, stid = cmd->scroll_tile_id, y = cmd->y;
	for (int y_tile = cmd->start_tile; y_tile < cmd->end_tile; y_tile += (int)t->tiles_hor)
	{
		int id = stid, x = cmd->x;
		for (int tile = y_tile; tile < y_tile + cmd->num_tiles_x; tile++)
		{
			const int16_t idx = layer[tile];
			if (dirty[id] != idx && (idx != 0 || (l == 0 && last_layer[tile] != dirty[id])))
			{
				if (l == 0) dirty[id] = idx;
				else dirty[id] = dirty[id] == 0 ? idx : -1;
				put(x + window_canvas_x, y + window_canvas_y, w, h, reslist[idx >> 2], 0, 0, w, l != 0);
				drawcnt++;
			}
			id++;
			x += w;
		}
		y += h;
		stid += cth;
		if (y >= (int)canvas_pitch) break;
	}
	return drawcnt;
}

/* dsprite.c draw_sprite + draw.c draw_clip, for the flags the game uses (overwrite, no flip,
 * not PRECISE: x and w snapped to even as draw_clipsprite does) */
int draw_sprite(int x, int y, int w, int h, int stride, const uint8_t *data, int flags)
{
	if (x + w < scissor.x1 || y + h < scissor.y1 || x >= scissor.x2 || y >= scissor.y2) return 0;
	int cl = 0, cr = 0, ct = 0, cb = 0;
	if (w + x > scissor.x2) cr = w + x - scissor.x2;
	if (x < scissor.x1) cl = scissor.x1 - x;
	if (h + y > scissor.y2) cb = h + y - scissor.y2;
	if (y < scissor.y1) ct = scissor.y1 - y;
	if (w <= cr + cl || h <= cb + ct) return 0;
	const int sw = w;
	x += window_canvas_x + cl;
	y += window_canvas_y + ct;
	w -= cr + cl;
	h -= cb + ct;
	x &= ~1;
	w &= ~1;
	put(x, y, w, h, data, cl & ~1, ct, stride ? stride : sw, (flags & DRAWSPR_OVERWRITE) != 0);
	draw_dirtyrect(&tm, x, y, w, h);
	return 1;
}

void yt_picture(const uint8_t *pixels)
{
	for (int y = 0; y < YT_VIEW_H; y++) memcpy(canvas() + y * YT_PITCH, pixels + y * YT_VIEW_W, YT_VIEW_W);
}
