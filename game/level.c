/*
 * SuperTux CD32X - a level: its pack (OJnnn.PAK, tools/stx_pack.py), its layers for Vic's tiler,
 * the tile rules (level.cpp, collision.cpp) and the changes to the map while it is played
 * (coins taken, boxes emptied, bricks broken: level->change). See game/stx.h.
 *
 * Memory: the background layer and SuperTux's background / foreground layers are read straight
 * from the cart (big-endian = the SH2's order); the interactive layer and its rules (SuperTux
 * tile ids) are copied into SDRAM, because they change. The widest level (bonus 2) has 15,630
 * cells: 2 x 31 KB.
 * Animated tiles (coins, water...): each animated tile's YATSSD resource points at its current
 * frame (reslist); the cells in view that show one are marked for redrawing in each buffer when
 * the frame changes (level_mark_animated), since Vic's dirty matrix compares tile numbers only.
 */
#include "stx.h"

#define MAX_CELLS 15700
int lw, lh, ltrack, ltime, lgrav, lstart_x, lstart_y, lparticles, lback_scroll, lautoscroll, lbkgd_speed;
int level_n = -1;
static int level_in_cart = -1;
fx grav_fx;
const uint8_t *lobjs;
int lnobjs;
const uint8_t *lresets;
int lnresets;

static uint16_t ia_ids[MAX_CELLS];     /* SuperTux tile ids of interactive-tm (the rules) */
static uint16_t ia_layer[MAX_CELLS];   /* YATSSD layer 2 */
#ifndef OJ32X_MARS
static uint16_t host_layers[3][MAX_CELLS];   /* PC: the other layers in the PC's byte order */
#endif
static const uint8_t *level_bin;
static uint8_t *reslist[1024];
static const uint16_t *layer_ptr[4];
static uint16_t res_of_tile[512];      /* SuperTux tile id -> YATSSD tile number (resource << 2) */
static uint8_t res_anim[1024];         /* resources of animated tiles */
static uint8_t anim_gen[2];            /* per frame buffer: the animation step it shows */
static uint32_t anim_step;

static const uint16_t *native_layer(const uint8_t *p, int k)
{
#ifdef OJ32X_MARS
	(void)k;
	return (const uint16_t *)p;
#else
	for (int i = 0; i < lw * lh; i++) host_layers[k][i] = be16(p + 2 * i);
	return host_layers[k];
#endif
}

void level_palette(void)
{
	const uint8_t *bt = need("BACKT.BIN");
	for (int i = 0; i < 64; i++) palette[192 + i] = be16(bt + 2 * i);
}

void level_reset_map(void)
{
	const int n = lw * lh;
	const uint8_t *L = level_bin + 32;
	for (int i = 0; i < n; i++)
	{
		ia_layer[i] = be16(L + 2 * n * 2 + 2 * i);
		ia_ids[i] = be16(L + 2 * n * 4 + 2 * i);
	}
	yt_invalidate();
}

int level_cached(int li) { return li == level_in_cart; }

void load_level(int li)
{
	if (li != level_in_cart)
	{
		level_in_cart = -1;
		music(0);                      /* no CD data reads while CD audio plays (OpenLara lesson 4) */
		const int32_t len = plat_load_pack(PACK_LEVEL_ID0 + li + 1, PACK_LEVEL_CART, PACK_LEVEL_MAX);
		if (len <= 0 || !pack_mount_at(1, plat_cart_ptr(PACK_LEVEL_CART)))
		{
			static char msg[40];
			put_str(put_dec(put_str(msg, "LEVEL PACK OJ"), (uint32_t)(li + 1), 3), ".PAK: NOT LOADED");
			plat_fatal(msg);
		}
		level_in_cart = li;
	}
	level_n = li;
	level_bin = need("LEVEL.BIN");
	if (memcmp(level_bin, "STXR", 4)) plat_fatal("LEVEL.BIN: NOT STXR");
	lw = be16(level_bin + 4); lh = be16(level_bin + 6);
	ltrack = be16(level_bin + 8); ltime = be16(level_bin + 10); lgrav = be16(level_bin + 12);
	lstart_x = (int16_t)be16(level_bin + 14); lstart_y = (int16_t)be16(level_bin + 16);
	lparticles = level_bin[20]; lback_scroll = level_bin[21];
	lnobjs = be16(level_bin + 22);
	lautoscroll = be16(level_bin + 24);
	lbkgd_speed = be16(level_bin + 26);
	if (lw * lh > MAX_CELLS) plat_fatal("LEVEL TOO BIG");
	grav_fx = (fx)(((uint32_t)lgrav << 16) / 100u);
	const int cells = lw * lh;
	const uint8_t *L = level_bin + 32;
	lobjs = L + 2 * 5 * cells;
	lnresets = be16(lobjs + 6 * lnobjs);
	lresets = lobjs + 6 * lnobjs + 2;
	level_palette();
	/* the resources (yt.h): 1..nframes tile frames, then the background tiles */
	reslist[0] = 0;
	for (int i = 0; i < nframes; i++) reslist[1 + i] = (uint8_t *)(tile_px + YT_TILE * YT_TILE * i);
	uint32_t btsize;
	const uint8_t *bt;
	pack_file("BACKT.BIN", &bt, &btsize);
	const int nb = (int)((btsize - 128) / (YT_TILE * YT_TILE));
	if (1 + nframes + nb > 1024) plat_fatal("TOO MANY TILE RESOURCES");
	for (int i = 0; i < nb; i++) reslist[1 + nframes + i] = (uint8_t *)(bt + 128 + YT_TILE * YT_TILE * i);
	/* SuperTux tile -> YATSSD number: as tools/stx_pack.py (0 and see-through tiles = nothing) */
	for (int t = 0; t < ntiles; t++)
	{
		res_of_tile[t] = 0;
		if (t && tdesc[t].frames)
		{
			const uint8_t *f = tile_px + YT_TILE * YT_TILE * tdesc[t].first;
			int any = 0;
			for (int i = 0; i < YT_TILE * YT_TILE && !any; i++) any = f[i] != 0;
			if (any) res_of_tile[t] = (uint16_t)((tdesc[t].first + 1) << 2);
		}
	}
	memset(res_anim, 0, sizeof res_anim);
	for (int t = 1; t < ntiles; t++)
		if (tdesc[t].frames > 1 && tdesc[t].speed && tdesc[t].first + 1 < 1024) res_anim[tdesc[t].first + 1] = 1;
	layer_ptr[0] = native_layer(L, 0);
	layer_ptr[1] = native_layer(L + 2 * cells, 1);
	layer_ptr[2] = ia_layer;
	layer_ptr[3] = native_layer(L + 2 * cells * 3, 2);
	level_reset_map();
	yt_level(layer_ptr, lw, lh, reslist);
	anim_gen[0] = anim_gen[1] = 0xFF;
}

/* animated tiles (Tile::draw: frame = time / anim speed): their first frame's resource shows the
 * current frame */
void level_animate(uint32_t ms)
{
	uint32_t step = ms / 25u;          /* SuperTux's global_frame_counter (25 ms) */
	if (step == anim_step) return;
	anim_step = step;
	int changed = 0;
	for (int t = 1; t < ntiles; t++)
	{
		const TileDesc *d = &tdesc[t];
		if (d->frames > 1 && d->speed)
		{
			uint8_t *now = (uint8_t *)(tile_px + YT_TILE * YT_TILE * (d->first + (ms / d->speed) % d->frames));
			if (reslist[d->first + 1] != now) { reslist[d->first + 1] = now; changed = 1; }
		}
	}
	if (changed) anim_gen[0]++, anim_gen[1]++;
}

static uint8_t anim_drawn[2] = { 0xFE, 0xFE };
void level_mark_animated(int cx, int cy)
{
	const int b = plat_back_index() & 1;
	if (anim_drawn[b] == anim_gen[b]) return;
	anim_drawn[b] = anim_gen[b];
	const int tx0 = cx / YT_TILE, ty0 = cy / YT_TILE;
	for (int ty = ty0; ty <= ty0 + YT_VIEW_H / YT_TILE + 1 && ty < lh; ty++)
		for (int tx = tx0; tx <= tx0 + YT_VIEW_W / YT_TILE + 1 && tx < lw; tx++)
		{
			const int c = ty * lw + tx;
			for (int l = 1; l <= 3; l++)
			{
				const uint16_t v = layer_ptr[l][c];
				if (v && (v >> 2) < 1024 && res_anim[v >> 2]) { yt_mark_tile(tx, ty); break; }
			}
		}
}

int tile_at(int x, int y)
{
	if (x < 0 || y < 0) return 0;
	const int tx = x >> 5, ty = y >> 5;
	if (tx >= lw || ty >= lh) return 0;
	return ia_ids[ty * lw + tx];
}
int flags_at(int x, int y) { const int t = tile_at(x, y); return t < ntiles ? tdesc[t].flags : 0; }
int level_goal_at(int x, int y) { return flags_at(x, y) & TF_GOAL; }

/* collision.cpp collision_object_map: a box at whole units, "1 pixel smaller" */
int box_hits(int bx, int by, int w, int h)
{
	for (int tx = (bx + 1) >> 5; tx * 32 < bx + w; tx++)
		for (int ty = (by + 1) >> 5; ty * 32 < by + h; ty++)
		{
			if (tx < 0 || ty < 0 || tx >= lw || ty >= lh) continue;
			const int t = ia_ids[ty * lw + tx];
			if (t < ntiles && (tdesc[t].flags & TF_SOLID)) return 1;
		}
	return 0;
}

void level_change(int x, int y, int tile)
{
	if (x < 0 || y < 0 || (x >> 5) >= lw || (y >> 5) >= lh) return;
	const int c = (y >> 5) * lw + (x >> 5);
	if (tile >= ntiles) tile = 0;
	ia_ids[c] = (uint16_t)tile;
	ia_layer[c] = res_of_tile[tile];   /* the tiler sees the number change and redraws the cell */
}

void level_hide(int tx, int ty, int hide)
{
	if (tx < 0 || ty < 0 || tx >= lw || ty >= lh) return;
	const int c = ty * lw + tx;
	ia_layer[c] = hide ? 0 : res_of_tile[ia_ids[c] < ntiles ? ia_ids[c] : 0];
}

/* ------------------------------------------------------------------ the sweep */
/* collision.cpp collision_swept_object_map, axis by axis (x, then y): the box moves unit by unit
 * and stops at the last free one ("current = old - d; while (collision) current -= d") */
static int move_axis(fx *pos, fx target, int horizontal, int ox, int oy, int w, int h)
{
	const int from = FX_INT(*pos), to = FX_INT(target);
	if (from == to) { *pos = target; return 0; }
	const int d = to > from ? 1 : -1;
	for (int p = from + d; ; p += d)
	{
		const int hit = horizontal ? box_hits(p, oy, w, h) : box_hits(ox, p, w, h);
		if (hit) { *pos = (fx)(p - d) << 16; return 1; }
		if (p == to) break;
	}
	*pos = target;
	return 0;
}

int sweep(fx *x, fx *y, fx tx, fx ty, int w, int h)
{
	const int sx = move_axis(x, tx, 1, 0, FX_INT(*y), w, h);
	move_axis(y, ty, 0, FX_INT(*x), 0, w, h);
	return sx;
}
