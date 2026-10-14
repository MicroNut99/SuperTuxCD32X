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
 * R5: the cells are listed here and drawn with Vic's pixel loop on both SH2s (yt_layer below);
 * draw_handle_layercmd is still there with YT_VIC_TILER.
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

/* ------------------------------------------------------------------ R4: re-anchoring
 * The canvas is the view plus 64 columns and 16 lines; when the camera crosses a multiple of 64
 * sideways (or 16 up / down), Vic's draw_tile_layer starts numbering the canvas cells from
 * another world tile - every cell then holds the "wrong" tile and the whole screen was drawn
 * again (R3 on the console: N 76-103, the dips to 38-42 fps). Now the canvas's pixels and the
 * dirty matrix are moved along by the same number of cells instead (32-bit copies, both SH2s):
 * only the cells that come in new are drawn. A move of more than two steps (a jump of the
 * camera) still redraws everything - and R6 redraws instead of moving whenever that measured
 * cheaper on the console (below). */
#define CANVAS_W (YT_PITCH)
#define CANVAS_H (YT_ROWS / YT_TILE * YT_TILE)   /* 192: the canvas cells' lines */
typedef struct { int dx, dy, y0, y1, by_cols; } MoveJob;

static void move_lines(void *arg, int which)
{
	const MoveJob *m = (const MoveJob *)arg;
	uint8_t *canvas = yt_canvas();
	/* destination line y takes source line y + dy; inside it, x takes x + dx */
	int x0 = m->dx < 0 ? -m->dx : 0, x1 = m->dx > 0 ? CANVAS_W - m->dx : CANVAS_W;
	int a = m->y0, b = m->y1;
	if (m->by_cols)
	{
		/* R6: an up / down move (dx 0) shared by columns - each CPU moves every line, its half
		 * of the columns, in the safe order; the lines cannot be shared (they overlap) */
		const int mid = CANVAS_W / 2;
		if (which) x0 = mid; else x1 = mid;
	}
	else
	{
		const int half = (m->y0 + m->y1) / 2;
		if (which) a = half; else b = half;
	}
	const int up = m->dy >= 0;
	for (int k = a; k < b; k++)
	{
		const int y = up ? k : (a + b - 1 - k);
		const int sy = y + m->dy;
		if (sy < 0 || sy >= CANVAS_H) continue;
		yt_move_row(canvas + y * CANVAS_W + x0, canvas + sy * CANVAS_W + x0 + m->dx, x1 - x0);
	}
}

/* R6: MOVE OR REDRAW, measured on the console. Moving the canvas reads the 32X frame buffer
 * (slow); drawing the view again reads the tile pictures from the cart and only writes the frame
 * buffer. Which one is cheaper was not known (R4 chose moving; R5 on the console: F 33-40 while
 * the view went up and down with Tux, T 11-15 ms - up / down moves were done by one SH2 and
 * happen every 16 lines, in each buffer). So the game times both as it plays: the microseconds
 * a cell costs the tiler (frames with 32 cells or more) and each kind of move (sideways; with
 * up / down), and re-anchors the cheaper way - trying the move again now and then. */
uint32_t plat_prof_us(void);
static uint32_t cell_us16;             /* microseconds a cell x 16, running average (0: unknown) */
static uint32_t move_us[2];            /* a move: [0] sideways only, [1] with up / down (0: unknown) */
static uint8_t redraws_since_move[2];
#define VIEW_CELLS ((YT_VIEW_W / YT_TILE + 1) * (YT_VIEW_H / YT_TILE + 1))

#ifndef OJ32X_MARS
#include <stdlib.h>
static int redraw_forced_bad(int dc, int dr, int ctx, int cty)   /* a move that must redraw */
{
	return dc < -2 * (int)tm.scroll_tiles_hor || dc > 2 * (int)tm.scroll_tiles_hor || dr < -2 || dr > 2
	       || dc >= ctx || -dc >= ctx || dr >= cty || -dr >= cty;
}
int yt_stat_reanchor[3];               /* PC only (ST_STATS): sideways, up/down, both moves */
#endif
static void reanchor(int dc, int dr)
{
#ifndef OJ32X_MARS
	yt_stat_reanchor[dc && dr ? 2 : dr ? 1 : 0]++;
#endif
	const int ctx = (int)tm.canvas_tiles_hor, cty = (int)tm.canvas_tiles_ver;
	int16_t *dirty = yt_extrafb() + 3;
	const int kind = dr != 0;
	int redraw = dc < -2 * (int)tm.scroll_tiles_hor || dc > 2 * (int)tm.scroll_tiles_hor || dr < -2 || dr > 2
	             || dc >= ctx || -dc >= ctx || dr >= cty || -dr >= cty;
	if (!redraw && move_us[kind] && cell_us16 && redraws_since_move[kind] < 16
	    && cell_us16 * VIEW_CELLS / 16 < move_us[kind])
	{
		redraw = 1;                    /* drawing the view is cheaper (measured) */
		redraws_since_move[kind]++;
	}
#ifndef OJ32X_MARS
	{   /* PC checks: ST_REANCHOR=move / draw forces one way (both must give the same picture) */
		const char *e = getenv("ST_REANCHOR");
		if (e && !redraw_forced_bad(dc, dr, ctx, cty)) redraw = e[0] == 'd';
	}
#endif
	if (redraw)
	{
		for (int i = 0; i < ctx * cty; i++) dirty[i] = -1;
		return;
	}
	redraws_since_move[kind] = 0;
	const uint32_t t0 = plat_prof_us();
	MoveJob m = { dc * YT_TILE, dr * YT_TILE, 0, CANVAS_H, 0 };
	if (dr == 0) plat_parallel(move_lines, &m);   /* the lines are independent: both SH2s */
	else if (dc == 0) { m.by_cols = 1; plat_parallel(move_lines, &m); }   /* R6: both SH2s */
	else { move_lines(&m, 0); move_lines(&m, 1); }
	const uint32_t dt = plat_prof_us() - t0;
	move_us[kind] = move_us[kind] ? (move_us[kind] * 3 + dt) / 4 : dt;
	/* the dirty matrix the same way: cell (c, r) takes (c + dc, r + dr); new cells: -1 */
	int16_t old[24 * 13];
	const int n = ctx * cty < (int)(sizeof old / sizeof old[0]) ? ctx * cty : (int)(sizeof old / sizeof old[0]);
	for (int i = 0; i < n; i++) old[i] = dirty[i];
	for (int r = 0; r < cty; r++)
		for (int c = 0; c < ctx; c++)
		{
			const int sc = c + dc, sr = r + dr;
			dirty[r * ctx + c] = (sc >= 0 && sc < ctx && sr >= 0 && sr < cty && sr * ctx + sc < n) ? old[sr * ctx + sc] : -1;
		}
}

/* ------------------------------------------------------------------ R4: the moving background
 * OPTIONS - BACKGROUND MOVES: the view filled from a picture (rows of `stride` bytes, pw x ph,
 * repeating) starting at picture pixel (sx, sy). One pixel of the start may move by one so the
 * copy can use 16-bit (or 32-bit) writes: the picture is far away, it does not show. */
void yt_back(const uint8_t *pic, int stride, int pw, int ph, int sx, int sy)
{
	uint8_t *canvas = yt_canvas();
	sx %= pw; if (sx < 0) sx += pw;
	sy %= ph; if (sy < 0) sy += ph;
	if ((sx ^ window_canvas_x) & 1) sx = (sx + 1) % pw;
	for (int y = 0; y < YT_VIEW_H; y++)
	{
		const uint8_t *row = pic + ((sy + y) % ph) * stride;
		uint8_t *d = canvas + (window_canvas_y + y) * CANVAS_W + window_canvas_x;
		int n1 = pw - sx;
		if (n1 > YT_VIEW_W) n1 = YT_VIEW_W;
		yt_copy_row(d, row + sx, n1);
		if (n1 < YT_VIEW_W) yt_copy_row(d + n1, row, YT_VIEW_W - n1);
	}
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
	const int top_h = (int)tm.scroll_tiles_hor * (cam_x / (int)tm.scroll_interval_hor);
	const int top_v = (int)tm.scroll_tiles_ver * (cam_y / (int)tm.scroll_interval_ver);
	if (extrafb[0] != (int16_t)tm.id)
	{
		int16_t *p = extrafb + 3;
		for (unsigned i = 0; i < tm.canvas_tiles_hor * tm.canvas_tiles_ver; i++) p[i] = -1;
		extrafb[0] = (int16_t)tm.id;
	}
	else if (extrafb[1] != top_h || extrafb[2] != top_v)
		reanchor(top_h - extrafb[1], top_v - extrafb[2]);
	/* this buffer's canvas now holds world tile (top_h + c, top_v + r) in cell (c, r) - kept in
	 * Vic's extrafb words 1 and 2, which nothing else uses here */
	extrafb[1] = (int16_t)top_h;
	extrafb[2] = (int16_t)top_v;
}

int yt_scroll_x(void) { return scroll_x; }
int yt_scroll_y(void) { return scroll_y; }

/* R5: the cells to draw this frame, listed by the master in one pass over the view (the rule of
 * Vic's draw_handle_layercmd, base layer: a cell is drawn when the dirty matrix does not hold the
 * tile it should show), then drawn by both SH2s - each takes every second cell of the list.
 * R3 ran Vic's draw_handle_layercmd on both CPUs: for EVERY cell of the view (about 260, also the
 * unchanged ones) each CPU took the next cell number from COMM6 under his TAS lock - two
 * locked, uncached accesses and a register round trip a cell, whether it was drawn or not. Now
 * the unchanged cells cost one read of the dirty matrix, and with only a few cells to draw the
 * second SH2 is not started at all. Each cell is drawn with Vic's pixel loop
 * (draw_handle_drawspritecmd, exactly what his draw_drawtile does for the base layer). Define
 * YT_VIC_TILER to go back to R3's way. */
#ifndef YT_VIC_TILER
typedef struct { int16_t x, y; int16_t idx; int16_t pad; } YtCell;
#define YT_MAX_CELLS (YT_PITCH / YT_TILE * (YT_ROWS / YT_TILE + 1))
static YtCell cell_list[YT_MAX_CELLS];
#define YT_SOLO_CELLS 4                /* up to this many: the master alone (no slave start) */

static void draw_cell(const YtCell *c)
{
	drawsprcmd_t cmd;
	cmd.flags = (uint16_t)(c->idx & 3);    /* base layer: drawmode 0 | the tile's flip bits */
	cmd.x = (uint16_t)c->x; cmd.y = (uint16_t)c->y;
	cmd.w = YT_TILE; cmd.h = YT_TILE;
	cmd.sx = 0; cmd.sy = 0;
	cmd.sw = YT_TILE; cmd.sh = YT_TILE;
	cmd.sdata = (void *)((uint8_t **)tm.reslist)[c->idx >> 2];
	cmd.scale = 0;
	cmd.stride = 0;
	draw_handle_drawspritecmd(&cmd);
}

typedef struct { int n; } CellsJob;
static void cells_job(void *arg, int which)
{
	const int n = ((const CellsJob *)arg)->n;
	for (int i = which & 1; i < n; i += 2) draw_cell(&cell_list[i]);
}
#else
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
#endif

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
#ifdef YT_VIC_TILER
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
#else
	/* R5: the same walk as draw_handle_layercmd's non-parallax branch (base layer), listing the
	 * cells instead of drawing them; the dirty matrix is updated here, by the master only */
	if (layer != 0) return 0;              /* R3 on: one merged layer */
	int16_t *dirty = yt_extrafb() + 3;
	const int16_t *tiles = (const int16_t *)tm.layers[0].tiles;
	const int16_t *last_layer = (const int16_t *)tm.layers[tm.numlayers - 1].tiles;
	const int cth = (int)tm.canvas_tiles_hor;
	int n = 0, stid = cmd.scroll_tile_id, y = cmd.y;
	for (int y_tile = start_tile; y_tile < end_tile; y_tile += (int)tm.tiles_hor)
	{
		int id = stid, x = cmd.x;
		for (int tile = y_tile; tile < y_tile + (int)cmd.num_tiles_x; tile++, id++, x += (int)w)
		{
			const int16_t idx = tiles[tile];
			const int16_t have = dirty[id];
			if (have != idx && (idx != 0 || last_layer[tile] != have) && n < YT_MAX_CELLS)
			{
				dirty[id] = idx;
				cell_list[n].x = (int16_t)(x + window_canvas_x);
				cell_list[n].y = (int16_t)(y + window_canvas_y);
				cell_list[n].idx = idx;
				n++;
			}
		}
		y += (int)h;
		stid += cth;
		if (y >= (int)canvas_pitch) break;
	}
	CellsJob job;
	job.n = n;
	const uint32_t t0 = plat_prof_us();
	if (n <= YT_SOLO_CELLS) { cells_job(&job, 0); cells_job(&job, 1); }
	else plat_parallel(cells_job, &job);
	if (n >= 32)                       /* R6: what a cell costs (for reanchor's choice) */
	{
		const uint32_t per16 = (plat_prof_us() - t0) * 16u / (uint32_t)n;
		cell_us16 = cell_us16 ? (cell_us16 * 3 + per16) / 4 : per16;
	}
	return n;
#endif
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
static void mark_cells(int x, int y, int w, int h)
{
	if (!tm.tw) return;
	int16_t *dirty = yt_extrafb() + 3;
	const int ctx = (int)tm.canvas_tiles_hor, cty = (int)tm.canvas_tiles_ver;
	int c0 = x / YT_TILE, c1 = (x + w - 1) / YT_TILE, r0 = y / YT_TILE, r1 = (y + h - 1) / YT_TILE;
	if (c1 >= ctx) c1 = ctx - 1;
	if (r1 >= cty) r1 = cty - 1;
	for (int r = r0; r <= r1; r++)
		for (int c = c0; c <= c1; c++) dirty[r * ctx + c] = -1;
}

static int rect_log_on, rect_n;
static int no_mark;                    /* R5: yt_keep(1) - draw without marking (kept clouds) */
/* R5: kept pictures are drawn into the whole canvas, not only the view: a cloud cut by the edge
 * of the view would otherwise have to be drawn again at every step of the view (the part that
 * scrolls in). The canvas cells around the view keep what is drawn in them until the tiler
 * draws them again (then the watch notices) or a re-anchor drops them. */
static int kb_x0, kb_y0, kb_x1, kb_y1;  /* the box kept pictures are cut to (view pixels) */
void yt_keep_box(int vx, int vy, int w, int h)
{
	/* inside the canvas */
	kb_x0 = vx < -window_canvas_x ? -window_canvas_x : vx;
	kb_y0 = vy < -window_canvas_y ? -window_canvas_y : vy;
	kb_x1 = vx + w > YT_PITCH - window_canvas_x ? YT_PITCH - window_canvas_x : vx + w;
	kb_y1 = vy + h > CANVAS_H - window_canvas_y ? CANVAS_H - window_canvas_y : vy + h;
	if (kb_x1 < kb_x0) kb_x1 = kb_x0;
	if (kb_y1 < kb_y0) kb_y1 = kb_y0;
	draw_setScissor((int16_t)kb_x0, (int16_t)kb_y0, (int16_t)(kb_x1 - kb_x0), (int16_t)(kb_y1 - kb_y0));
}
void yt_keep(int on)
{
	no_mark = on;
	if (on) yt_keep_box(-window_canvas_x, -window_canvas_y, YT_PITCH, CANVAS_H);
	else draw_setScissor(0, 0, YT_VIEW_W, YT_VIEW_H);
}

/* R5: the canvas cells under a view rectangle (any part of it inside the canvas, also outside
 * the view): their numbers into ids[] (at most max), returns how many */
static int cells_under(int vx, int vy, int w, int h, int16_t *ids, int max)
{
	if (!tm.tw || w <= 0 || h <= 0) return 0;
	const int ctx = (int)tm.canvas_tiles_hor, cty = (int)tm.canvas_tiles_ver;
	int x0 = window_canvas_x + vx, y0 = window_canvas_y + vy, x1 = x0 + w - 1, y1 = y0 + h - 1;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= ctx * YT_TILE) x1 = ctx * YT_TILE - 1;
	if (y1 >= cty * YT_TILE) y1 = cty * YT_TILE - 1;
	int n = 0;
	for (int r = y0 / YT_TILE; r <= y1 / YT_TILE && y0 <= y1; r++)
		for (int c = x0 / YT_TILE; c <= x1 / YT_TILE && x0 <= x1; c++)
			if (n < max) ids[n++] = (int16_t)(r * ctx + c);
	return n;
}

void yt_mark_view_rect(int vx, int vy, int w, int h)
{
	int16_t ids[64];
	int16_t *dirty = yt_extrafb() + 3;
	const int n = cells_under(vx, vy, w, h, ids, 64);
	for (int i = 0; i < n; i++) dirty[ids[i]] = -1;
}

int yt_watch(int vx, int vy, int w, int h, int16_t *ids, int16_t *vals, int max)
{
	const int16_t *dirty = yt_extrafb() + 3;
	const int n = cells_under(vx, vy, w, h, ids, max);
	for (int i = 0; i < n; i++) vals[i] = dirty[ids[i]];
	return n;
}

int yt_watch_changed(const int16_t *ids, const int16_t *vals, int n, uint8_t *cells)
{
	const int16_t *dirty = yt_extrafb() + 3;
	int k = 0;
	for (int i = 0; i < n; i++) if (dirty[ids[i]] != vals[i]) { cells[ids[i]] = 1; k++; }
	return k;
}

void yt_cell_box(int id, int *vx, int *vy)
{
	const int ctx = (int)tm.canvas_tiles_hor ? (int)tm.canvas_tiles_hor : YT_PITCH / YT_TILE;
	*vx = (id % ctx) * YT_TILE - window_canvas_x;
	*vy = (id / ctx) * YT_TILE - window_canvas_y;
}
static int16_t rect_xy[YT_RECTS][4];

void yt_rect_log(int on) { rect_log_on = on; if (on) rect_n = 0; }
int yt_rect_count(void) { return rect_n; }
void yt_rect(int i, int *x, int *y, int *w, int *h)
{
	*x = rect_xy[i][0]; *y = rect_xy[i][1]; *w = rect_xy[i][2]; *h = rect_xy[i][3];
}

#ifndef OJ32X_MARS
uint32_t yt_stat_spr, yt_stat_px;      /* PC only (ST_STATS): sprites and their pixels a frame */
#define STAT(n, px) (yt_stat_spr += (n), yt_stat_px += (uint32_t)(px))
#else
#define STAT(n, px) ((void)0)
#endif

int yt_sprite(int vx, int vy, int w, int h, const uint8_t *pixels)
{
	/* the area a sprite may cover: the view; kept pictures (yt_keep): the whole canvas */
	const int bx0 = no_mark ? kb_x0 : 0, bx1 = no_mark ? kb_x1 : YT_VIEW_W;
	const int by0 = no_mark ? kb_y0 : 0, by1 = no_mark ? kb_y1 : YT_VIEW_H;
	const int x0 = vx < bx0 ? bx0 : vx, x1 = vx + w > bx1 ? bx1 : vx + w;
	const int y0 = vy < by0 ? by0 : vy, y1 = vy + h > by1 ? by1 : vy + h;
	if (x1 <= x0 || y1 <= y0) return 0;
	if (x1 - x0 < 8 || y1 - y0 < 2)
	{
		/* R4: too small for Vic's loops - copied here a byte at a time (0 see-through), so a tile
		 * or a sprite at the edge of the view is not missing its last few pixels */
		uint8_t *canvas = yt_canvas();
		for (int y = y0; y < y1; y++)
		{
			const uint8_t *s = pixels + (y - vy) * w + (x0 - vx);
			uint8_t *d = canvas + (window_canvas_y + y) * CANVAS_W + window_canvas_x + x0;
			for (int x = 0; x < x1 - x0; x++) if (s[x]) d[x] = s[x];
		}
		if (!no_mark) mark_cells(window_canvas_x + x0, window_canvas_y + y0, x1 - x0, y1 - y0);
		STAT(1, (x1 - x0) * (y1 - y0));
		return 1;
	}
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
	STAT(1, cw * ch);
	if (no_mark) return 1;
	mark_cells(x, y, cw, ch);           /* the cells under it, in this buffer's dirty matrix */
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
