/*
 * SuperTux CD32X - the layer driver for Vic's tiler (see game/yt.h for what is Vic's and what
 * is ours).
 *
 * yt_layer() below is YATSSD's draw_tile_layer() (yatssd/dtiles.c) for one layer without
 * parallax, line by line the same arithmetic, minus its hardware calls:
 *   - MARS_VDP_SHIFTREG = old_camera_x and Hw32xUpdateLineTable(): replaced by the values
 *     yt_scroll_x()/yt_scroll_y() hand to plat_present_scroll (the platform layer writes the
 *     line table, and the shift bit at the buffer swap - OJ32X_FLIP_ASYNC)
 *   - the slave hand-shake (while COMM4 != 0; COMM6 = 0; COMM4 = 3; ... COMM4 == 3): only
 *     "COMM6 = 0" stays (yt_tile_counter_reset) - draw_handle_layercmd() hands out the tiles
 *     through COMM6 (get_next_tile); with the master alone it draws every tile. COMM6 has no
 *     other user in this boot path (cd32x/subcpu/hw_md.s line 1307 is a nop - OpenLara's fix)
 *   - HwMd*() (Genesis planes): not used
 * The tile drawing itself and the dirty matrix are Vic's draw_handle_layercmd(), unchanged.
 *
 * Built for the 32X (with yatssd/dtiles.c, dsprite.c, draw.c) and for the PC (with
 * game/yt_host.c, which stands in for those three).
 */
#include "yt.h"
#include "types.h"          /* yatssd/types.h: tilemap_t, dtilelayer_t, drawtilelayerscmd_t */

#include <stdint.h>
#include <string.h>

/* ---- what YATSSD's own main.c / hw_32x.c define and its files use (yatssd/draw.h) ---- */
tilemap_t tm;                          /* the level (dsprite.c marks sprites in its matrix) */
int window_canvas_x, window_canvas_y;  /* the canvas offset of the view (draw_tile_layer sets it) */
int debug = 0;
int nodraw = 0;                        /* 0: draw (draw_inc.h) */
int32_t canvas_width = YT_VIEW_W;      /* hw_32x.c's values for Hw32xInit - here the view's */
int32_t canvas_height = YT_VIEW_H;
uint32_t canvas_pitch = YT_PITCH;
uint32_t canvas_yaw = YT_YAW;          /* places Vic's extrafb at 0x14C00 (yt.h) */

int draw_handle_layercmd(drawtilelayerscmd_t *cmd);
void draw_setScissor(int16_t x, int16_t y, int16_t w, int16_t h);
int draw_clip(int x, int y, int w, int h, rect_t *cliprect);
void draw_handle_drawspritecmd(drawsprcmd_t *cmd);
void plat_parallel(void (*fn)(void *, int), void *arg);   /* port/plat.h */

static dtilelayer_t layer_desc[1];
/* YATSSD's global_tilemap_id. Starts at an unlikely value: the first frame compares it with
 * whatever the loader's splash left at 0x14C00 (a match would skip the first full redraw). */
static uint16_t map_id = 0x5301;
static int cam_x, cam_y;               /* this frame's camera, clamped */
static int scroll_x, scroll_y;         /* inside the canvas: 0..63, 0..15 */

void yt_level(const uint16_t *layer, int w, int h, uint8_t **reslist)
{
	/* as init_tilemap() (dtiles.c), without its Genesis plane and line table calls. R3: ONE
	 * layer - tools/stx_pack.py merges a level's four into one picture a cell, so the dirty
	 * matrix (one tile number a cell) skips every cell that has not changed */
	memset(&tm, 0, sizeof tm);
	memset(layer_desc, 0, sizeof layer_desc);
	layer_desc[0].tiles = (uint16_t *)layer;
	layer_desc[0].parallax[0] = layer_desc[0].parallax[1] = 1 << 16;   /* moves with the camera */
	tm.id = map_id++;
	if (map_id == 0 || map_id == 0xFFFF) map_id = 0x5301;
	tm.tw = tm.th = YT_TILE;
	tm.layers = layer_desc;
	tm.numlayers = 1;
	tm.reslist = reslist;
	tm.tiles_hor = (unsigned)w;
	tm.tiles_ver = (unsigned)h;
	tm.scroll_tiles_hor = YT_SCROLL_X / YT_TILE;       /* (canvas_pitch - canvas_width) / tw */
	tm.scroll_interval_hor = YT_SCROLL_X;
	tm.scroll_tiles_ver = YT_SCROLL_Y / YT_TILE;       /* the 200 lines leave 16 below the view */
	tm.scroll_interval_ver = YT_SCROLL_Y;
	tm.canvas_tiles_hor = YT_PITCH / YT_TILE;          /* 24 */
	tm.canvas_tiles_ver = YT_ROWS / YT_TILE;           /* 12: never a tile row past line 191 */
	tm.numtiles = w * h;
	draw_setScissor(0, 0, YT_VIEW_W, YT_VIEW_H);
}

void yt_invalidate(void)
{
	tm.id = map_id++;
	if (map_id == 0 || map_id == 0xFFFF) map_id = 0x5301;
}

void yt_begin(int cx, int cy)
{
	const int maxx = (int)(tm.tiles_hor * tm.tw) - YT_VIEW_W;
	const int maxy = (int)(tm.tiles_ver * tm.th) - YT_VIEW_H;
	if (cx > maxx) cx = maxx;
	if (cy > maxy) cy = maxy;
	if (cx < 0) cx = 0;
	if (cy < 0) cy = 0;
	cam_x = cx;
	cam_y = cy;
	scroll_x = cam_x % YT_SCROLL_X;
	scroll_y = cam_y % YT_SCROLL_Y;
	window_canvas_x = scroll_x;     /* draw_tile_layer, layer 0 */
	window_canvas_y = scroll_y;

	/* draw_tile_layer, layer 0: a new tile map in this buffer -> every cell dirty */
	int16_t *extrafb = yt_extrafb();
	if (extrafb[0] != (int16_t)tm.id)
	{
		int16_t *p = extrafb + 3;
		for (unsigned i = 0; i < tm.canvas_tiles_hor * tm.canvas_tiles_ver; i++) p[i] = -1;
		extrafb[0] = (int16_t)tm.id;
	}
}

int yt_scroll_x(void) { return scroll_x; }
int yt_scroll_y(void) { return scroll_y; }

typedef struct { drawtilelayerscmd_t *cmd; int n[2]; } TilesJob;
static void tiles_job(void *arg, int which)
{
	TilesJob *j = (TilesJob *)arg;
	const int n = draw_handle_layercmd(j->cmd);
#ifdef OJ32X_MARS
	((volatile int *)((uintptr_t)j | 0x20000000u))[1 + (which & 1)] = n;   /* past the caches */
#else
	j->n[which & 1] = n;
#endif
}
static int job_count(TilesJob *j)
{
#ifdef OJ32X_MARS
	const volatile int *p = (const volatile int *)((uintptr_t)j | 0x20000000u);
	return p[1] + p[2];
#else
	return j->n[0] + j->n[1];
#endif
}

static int yt_layer(int layer)
{
	const unsigned w = tm.tw, h = tm.th;
	const int scroll_count_hor = cam_x / (int)tm.scroll_interval_hor;
	const int scroll_count_ver = cam_y / (int)tm.scroll_interval_ver;
	const int top_scroll_tile_hor = (int)tm.scroll_tiles_hor * scroll_count_hor;
	const int top_scroll_tile_ver = (int)tm.scroll_tiles_ver * scroll_count_ver;

	unsigned start_tile_hor = (unsigned)cam_x / w;
	unsigned end_tile_hor = ((unsigned)cam_x + w - 1 + (unsigned)canvas_width) / w;
	unsigned start_tile_ver = (unsigned)cam_y / h;
	unsigned end_tile_ver = ((unsigned)cam_y + h - 1 + (unsigned)canvas_height) / h;
	if (start_tile_hor >= tm.tiles_hor || start_tile_ver >= tm.tiles_ver) return 0;
	if (end_tile_hor > tm.tiles_hor) end_tile_hor = tm.tiles_hor;
	if (end_tile_ver < 1) end_tile_ver = 1;
	if (end_tile_ver > tm.tiles_ver) end_tile_ver = tm.tiles_ver;

	const int start_tile = (int)(start_tile_ver * tm.tiles_hor + start_tile_hor);
	if (start_tile >= tm.numtiles) return 0;
	int end_tile = (int)((end_tile_ver - 1) * tm.tiles_hor + end_tile_hor);
	if (end_tile > tm.numtiles) end_tile = tm.numtiles;

	drawtilelayerscmd_t cmd;
	cmd.tm = &tm;
	cmd.x = (int16_t)(((int)start_tile_hor - top_scroll_tile_hor) * (int)w - scroll_x);
	cmd.y = (int16_t)(-(scroll_y & (int)(h - 1)));
	cmd.start_tile = (uint16_t)start_tile;
	cmd.end_tile = (uint16_t)end_tile;
	cmd.scroll_tile_id = (uint16_t)(((int)start_tile_ver - top_scroll_tile_ver) * (int)tm.canvas_tiles_hor
	                                + ((int)start_tile_hor - top_scroll_tile_hor));
	cmd.num_tiles_x = (uint16_t)(end_tile_hor - start_tile_hor);
	cmd.startlayer = (int16_t)layer;
	cmd.camera_x = cam_x;
	cmd.camera_y = cam_y;
	cmd.drawmode = layer == 0 ? 0 : (DRAWSPR_PRECISE | DRAWSPR_OVERWRITE);   /* as draw_tile_layer */
	cmd.parallax = 0;
	/* R3: both SH2s, as YATSSD's own draw_tile_layer does it - draw_handle_layercmd hands out
	 * the cells through Vic's counter in COMM6 (get_next_tile, under his TAS lock), so each
	 * CPU draws the cells it takes. The second CPU runs the same call through the platform
	 * layer's plat_parallel (port/plat_mars.c J10: the job in SDRAM, the slave purges its
	 * cache first), not YATSSD's COMM4 loop. */
	TilesJob job;
	job.cmd = &cmd;
	job.n[0] = job.n[1] = 0;
	yt_tile_counter_reset();
	plat_parallel(tiles_job, &job);
	return job_count(&job);
}

int yt_layers(int first, int last)
{
	int n = 0;
	for (int l = first; l <= last && l < (int)tm.numlayers; l++)
		n += yt_layer(l);
	return n;
}

/* a sprite at view coordinates: Vic's draw_clip and pixel loops (draw_handle_drawspritecmd,
 * overwrite mode: colour 0 see-through), as his draw_sprite / draw_clipsprite do it - but the
 * cells marked for redrawing are exactly the ones under the sprite. (R3: Vic's draw_dirtyrect
 * shifts y right by 3 and then by 4 again, so it marks every cell from line y / 128 down: a
 * sprite in the lower half of the view made its whole columns redraw in the next frame.)
 * Vic's pixel loops count down an unsigned "units / 4" with do-while: fewer than 4 units after
 * clipping (8 pixels in the 16-bit loops) and the count wraps - about 4 billion writes (found by
 * Sonic32 T7, VIC_YATSSD.md section 6). So a sprite with less than 8 x 2 pixels on the screen
 * is not drawn. */
static int rect_log_on, rect_n;
static int16_t rect_xy[YT_RECTS][4];

void yt_rect_log(int on) { rect_log_on = on; if (on) rect_n = 0; }
int yt_rect_count(void) { return rect_n; }
void yt_rect(int i, int *x, int *y, int *w, int *h)
{
	*x = rect_xy[i][0]; *y = rect_xy[i][1]; *w = rect_xy[i][2]; *h = rect_xy[i][3];
}

int yt_sprite(int vx, int vy, int w, int h, const uint8_t *pixels)
{
	const int x0 = vx < 0 ? 0 : vx, x1 = vx + w > YT_VIEW_W ? YT_VIEW_W : vx + w;
	const int y0 = vy < 0 ? 0 : vy, y1 = vy + h > YT_VIEW_H ? YT_VIEW_H : vy + h;
	if (x1 - x0 < 8 || y1 - y0 < 2) return 0;
	rect_t clip;
	if (draw_clip(vx, vy, w, h, &clip) == 2) return 0;
	/* draw_clipsprite, for DRAWSPR_OVERWRITE without flips or scaling */
	drawsprcmd_t cmd;
	int x = vx + window_canvas_x + clip.x1, y = vy + window_canvas_y + clip.y1;
	int cw = w - (clip.x2 + clip.x1), ch = h - (clip.y2 + clip.y1);
	x &= ~1;
	cw &= ~1;
	if (cw < 8 || ch < 2) return 0;
	cmd.flags = DRAWSPR_OVERWRITE;
	cmd.x = (uint16_t)x; cmd.y = (uint16_t)y;
	cmd.w = (uint16_t)cw; cmd.h = (uint16_t)ch;
	cmd.sx = (int16_t)clip.x1; cmd.sy = (int16_t)clip.y1;
	cmd.sw = (uint16_t)w; cmd.sh = (uint16_t)h;
	cmd.sdata = (void *)pixels;
	cmd.scale = 0;
	cmd.stride = 0;
	draw_handle_drawspritecmd(&cmd);
	/* the cells under it, in this buffer's dirty matrix */
	if (tm.tw)
	{
		int16_t *dirty = yt_extrafb() + 3;
		const int ctx = (int)tm.canvas_tiles_hor, cty = (int)tm.canvas_tiles_ver;
		int c0 = x / YT_TILE, c1 = (x + cw - 1) / YT_TILE, r0 = y / YT_TILE, r1 = (y + ch - 1) / YT_TILE;
		if (c1 >= ctx) c1 = ctx - 1;
		if (r1 >= cty) r1 = cty - 1;
		for (int r = r0; r <= r1; r++)
			for (int c = c0; c <= c1; c++) dirty[r * ctx + c] = -1;
	}
	if (rect_log_on && rect_n < YT_RECTS)
	{
		rect_xy[rect_n][0] = (int16_t)x0; rect_xy[rect_n][1] = (int16_t)y0;
		rect_xy[rect_n][2] = (int16_t)(x1 - x0); rect_xy[rect_n][3] = (int16_t)(y1 - y0);
		rect_n++;
	}
	return 1;
}

void yt_screen(void)
{
	if (tm.tw == 0)
	{
		/* no level yet: the sizes Vic's draw_dirtyrect divides by (dsprite.c marks letters too) */
		tm.tw = tm.th = YT_TILE;
		tm.canvas_tiles_hor = YT_PITCH / YT_TILE;
		tm.canvas_tiles_ver = YT_ROWS / YT_TILE;
		draw_setScissor(0, 0, YT_VIEW_W, YT_VIEW_H);
	}
	cam_x = cam_y = scroll_x = scroll_y = 0;
	window_canvas_x = window_canvas_y = 0;
	yt_invalidate();
}

/* the cell of world tile (tx, ty) redrawn in this buffer (R2: animated tiles - Vic's dirty matrix
 * compares tile numbers, and an animated tile keeps its number while its picture changes) */
void yt_mark_tile(int tx, int ty)
{
	/* the canvas cell that holds world tile (tx, ty) now: as draw_tile_layer numbers them */
	const int top_hor = (int)tm.scroll_tiles_hor * (cam_x / (int)tm.scroll_interval_hor);
	const int top_ver = (int)tm.scroll_tiles_ver * (cam_y / (int)tm.scroll_interval_ver);
	const int cx = tx - top_hor, cy = ty - top_ver;
	if (cx < 0 || cy < 0 || cx >= (int)tm.canvas_tiles_hor || cy >= (int)tm.canvas_tiles_ver) return;
	yt_extrafb()[3 + cy * (int)tm.canvas_tiles_hor + cx] = -1;
}
