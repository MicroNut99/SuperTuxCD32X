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
int draw_sprite(int x, int y, int w, int h, int stride, const uint8_t *data, int flags);

static dtilelayer_t layer_desc[4];
/* YATSSD's global_tilemap_id. Starts at an unlikely value: the first frame compares it with
 * whatever the loader's splash left at 0x14C00 (a match would skip the first full redraw). */
static uint16_t map_id = 0x5301;
static int cam_x, cam_y;               /* this frame's camera, clamped */
static int scroll_x, scroll_y;         /* inside the canvas: 0..63, 0..15 */

void yt_level(const uint16_t *const layers[4], int w, int h, uint8_t **reslist)
{
	/* as init_tilemap() (dtiles.c), without its Genesis plane and line table calls */
	memset(&tm, 0, sizeof tm);
	memset(layer_desc, 0, sizeof layer_desc);
	for (int i = 0; i < 4; i++)
	{
		layer_desc[i].tiles = (uint16_t *)layers[i];
		layer_desc[i].parallax[0] = layer_desc[i].parallax[1] = 1 << 16;   /* moves with the camera */
	}
	tm.id = map_id++;
	if (map_id == 0 || map_id == 0xFFFF) map_id = 0x5301;
	tm.tw = tm.th = YT_TILE;
	tm.layers = layer_desc;
	tm.numlayers = 4;
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
	yt_tile_counter_reset();
	return draw_handle_layercmd(&cmd);
}

int yt_layers(int first, int last)
{
	int n = 0;
	for (int l = first; l <= last && l < (int)tm.numlayers; l++)
		n += yt_layer(l);
	return n;
}

/* a sprite at view coordinates. Vic's pixel loops count down an unsigned "units / 4" with
 * do-while: fewer than 4 units after clipping (8 pixels in the 16-bit loops) and the count
 * wraps - about 4 billion writes (found by Sonic32 T7, VIC_YATSSD.md section 6). So a sprite
 * with less than 8 x 2 pixels on the screen is not drawn. */
int yt_sprite(int vx, int vy, int w, int h, const uint8_t *pixels)
{
	const int x0 = vx < 0 ? 0 : vx, x1 = vx + w > YT_VIEW_W ? YT_VIEW_W : vx + w;
	const int y0 = vy < 0 ? 0 : vy, y1 = vy + h > YT_VIEW_H ? YT_VIEW_H : vy + h;
	if (x1 - x0 < 8 || y1 - y0 < 2) return 0;
	/* overwrite mode: the 32X skips 0 bytes (see-through); 16-bit writes (even x) */
	return draw_sprite(vx, vy, w, h, 0, pixels, DRAWSPR_OVERWRITE);
}
