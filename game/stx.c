/*
 * SuperTux CD32X - R1b: the levels of world 1, Tux walking, running and jumping.
 *
 * Runs from main(), after the boot path (Hurrican CD32X's = the OpenJazz port's J19b files,
 * byte for byte - tools/check_boot.py) has finished, and uses only the platform layer's
 * existing calls (port/plat.h): plat_load_pack / pack_mount_at for the level packs,
 * plat_wait_flip / plat_back_index / plat_scroll_panel / plat_present_scroll for the screen,
 * plat_read_pad, plat_sfx, plat_music, plat_prof_us, plat_ticks_ms, plat_fatal.
 *
 * Drawing: Vic's tiler (YATSSD) - game/yt.h explains what is Vic's and what is ours, and the
 * screen layout (view 320 x 176 + a 24-line panel for the HUD and the timing line).
 *
 * The game: SuperTux 0.1.3's rules (src/player.cpp, physic.cpp, collision.cpp, world.cpp),
 * rebuilt in C in SuperTux's own units - its 640 x 480 screen pixels, 32-pixel tiles - and its
 * own time step (frame_ratio 1 = 10 ms: 100 steps a second), in fixed point (16 bits of
 * fraction: the SH2 has no floating point unit). Drawn at half size.
 *
 * Buttons (SuperTux: arrows, space = jump, ctrl = run/fire):
 *   left / right walk, B held runs, C or A jumps (hold for the full height), START pauses.
 *   In the pause: LEFT / RIGHT choose a level, A or C goes there, START continues.
 *
 * Packs (tools/stx_pack.py): IMAGE8.RAW (resident, mounted by the platform layer before
 * main()): PALETTE.BIN, TILES.BIN, SPRITES.BIN. OJnnn.PAK (level nnn - 1): LEVEL.BIN, BACKT.BIN.
 */
#include "plat.h"
#include "pack.h"
#include "yt.h"

#include <stdint.h>
#include <string.h>
#ifndef OJ32X_MARS
#include <stdio.h>
#include <stdlib.h>
#endif

extern const unsigned char msx[];      /* yatssd/font.c: 8 x 8, bit 7 = left */

/* ------------------------------------------------------------------ data access */
static inline uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static char *put_str(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *put_dec(char *p, uint32_t v, int digits)
{
	char t[12];
	int n = 0;
	do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
	while (n < digits) t[n++] = '0';
	while (n) *p++ = t[--n];
	*p = 0;
	return p;
}

static const uint8_t *need(const char *name)
{
	const uint8_t *p;
	uint32_t n;
	if (!pack_file(name, &p, &n))
	{
		static char msg[48];
		put_str(put_str(msg, name), ": NOT IN THE PACKS");
		plat_fatal(msg);
	}
	return p;
}

/* ------------------------------------------------------------------ fixed point */
typedef int32_t fx;                    /* 16.16, SuperTux's units */
#define FX(v)    ((fx)((v) * 65536.0)) /* constants only: folded by the compiler */
#define FX_INT(v) ((v) >> 16)
#define ONE      (1 << 16)
static inline fx fxabs(fx v) { return v < 0 ? -v : v; }

/* SuperTux 0.1.3 src/defines.h (per 10 ms step) */
#define WALK_SPEED          FX(1.0)
#define MAX_WALK_XM         FX(2.3)
#define MAX_RUN_XM          FX(3.2)
#define WALK_ACCELERATION_X FX(0.03)
#define RUN_ACCELERATION_X  FX(0.04)
#define SKID_XM             FX(2.0)
#define SKID_STEPS          20         /* SKID_TIME 200 ms */
#define JUMP_YM             FX(5.2)    /* player.cpp handle_vertical_input */
#define JUMP_RUN_YM         FX(5.8)
#define X_SPACE             (400 - 16) /* world.cpp scrolling */
#define SCREEN_W_FULL       640
#define VIEW_H_FULL         (YT_VIEW_H * 2)   /* 352 of SuperTux's 480 lines are in view */
#define DISTROS_LIFEUP      100
#define START_LIVES         4          /* defines.h */
#define END_STEPS           700        /* the end sequence: 7 s (gameloop.cpp) */

/* tile flags (tools/stx_convert.py TILES.BIN) */
#define TF_SOLID 1
#define TF_ICE   2
#define TF_DISTRO 32
#define TF_GOAL  128

/* sound effects (tools/make_stx_sfx.py SOUNDS, plat_sfx(n) = SOUNDS[n - 1]) */
enum { SFX_JUMP = 1, SFX_SKID, SFX_DISTRO, SFX_LIFEUP, SFX_HURT, SFX_BIGJUMP, SFX_STOMP,
       SFX_SQUISH, SFX_KICK, SFX_BRICK, SFX_BUMP, SFX_FALL };
#define TRACK_LEVELDONE 13             /* tools/stx_convert.py MUSIC: leveldone */

/* ------------------------------------------------------------------ resident data */
static uint16_t palette[256];
static int nframes, ntiles;
static const uint8_t *tile_px;         /* nframes x 256 bytes */
static uint16_t tile_flags[1024];
static uint8_t col_white = 1, col_black = 1, col_panel = 1, col_yellow = 1;

/* sprites (SPRITES.BIN; the order of stx_convert.py SPRITES) */
enum { SPR_STAND_L, SPR_STAND_R, SPR_WALK_L, SPR_WALK_R, SPR_JUMP_L, SPR_JUMP_R, SPR_SKID_L, SPR_SKID_R, SPR_N };
typedef struct { int n, fps10, hx, hy, first; } Sprite;
static Sprite spr[SPR_N];
static const uint8_t *spr_bin, *spr_frames;

static int colour_near(int r, int g, int b)
{
	int best = 1, bd = 1 << 30;
	for (int i = 1; i < 192; i++)
	{
		const int c = palette[i];
		const int dr = (c & 31) * 8 - r, dg = (c >> 5 & 31) * 8 - g, db = (c >> 10 & 31) * 8 - b;
		const int d = dr * dr + dg * dg + db * db;
		if (d < bd) { bd = d; best = i; }
	}
	return best;
}

static void load_resident(void)
{
	const uint8_t *p = need("PALETTE.BIN");
	for (int i = 0; i < 192; i++) palette[i] = be16(p + 2 * i);
	palette[0] = 0;                    /* the platform layer shows 0 as 0x0400 (black, opaque) */
	p = need("TILES.BIN");
	if (memcmp(p, "STXT", 4)) plat_fatal("TILES.BIN: NOT STXT");
	nframes = be16(p + 4);
	ntiles = be16(p + 6);
	if (ntiles > 1024) plat_fatal("TILES.BIN: TOO MANY TILES");
	for (int t = 0; t < ntiles; t++) tile_flags[t] = be16(p + 8 + 8 * t + 6);
	tile_px = p + 8 + 8 * ntiles;
	spr_bin = need("SPRITES.BIN");
	if (memcmp(spr_bin, "STXS", 4) || be16(spr_bin + 4) < SPR_N) plat_fatal("SPRITES.BIN: NOT STXS");
	for (int i = 0; i < SPR_N; i++)
	{
		const uint8_t *e = spr_bin + 8 + 36 * i;
		spr[i].n = be16(e + 24);
		spr[i].fps10 = be16(e + 26);
		spr[i].hx = (int16_t)be16(e + 28);
		spr[i].hy = (int16_t)be16(e + 30);
		spr[i].first = be16(e + 32);
	}
	spr_frames = spr_bin + 8 + 36 * be16(spr_bin + 4);
	col_white = (uint8_t)colour_near(255, 255, 255);
	col_black = (uint8_t)colour_near(0, 0, 0);
	col_panel = (uint8_t)colour_near(16, 24, 64);
	col_yellow = (uint8_t)colour_near(255, 230, 64);
}

/* ------------------------------------------------------------------ the level */
#define MAX_CELLS 9000                 /* the widest level of world 1: 525 x 15 = 7875 */
static int lw, lh, ltrack, ltime, lgrav, lstart_x, lstart_y;
static int32_t grav_fx;               /* gravity per step: level gravity / 100 (Physic::apply) */
static int level_n = -1, level_in_cart = -1;
static uint16_t ia_ids[MAX_CELLS];     /* SuperTux tile ids of interactive-tm (rules; coins go) */
static uint16_t ia_layer[MAX_CELLS];   /* YATSSD layer 2, in SDRAM: taking a coin changes it */
#ifndef OJ32X_MARS
static uint16_t host_layers[3][MAX_CELLS];   /* PC: the other layers in the PC's byte order */
#endif
static const uint8_t *level_bin;
static uint8_t *reslist[1024];
static const uint16_t *layer_ptr[4];

static const uint16_t *native_layer(const uint8_t *p, int k)
{
#ifdef OJ32X_MARS
	(void)k;
	return (const uint16_t *)p;       /* big-endian in the cart = the SH2's order */
#else
	for (int i = 0; i < lw * lh; i++) host_layers[k][i] = be16(p + 2 * i);
	return host_layers[k];
#endif
}

static void level_reset_map(void)
{
	/* the interactive layer and its rules from the cart again (coins back) */
	const uint8_t *L = level_bin + 20;
	const int n = lw * lh;
	for (int i = 0; i < n; i++)
	{
		ia_layer[i] = be16(L + 2 * n * 2 + 2 * i);
		ia_ids[i] = be16(L + 2 * n * 4 + 2 * i);
	}
	yt_invalidate();                   /* both buffers redraw everything */
}

static void load_level(int n)
{
	if (n != level_in_cart)
	{
		level_in_cart = -1;
		plat_music(0);                 /* no CD data reads while CD audio plays (OpenLara lesson 4) */
		const int32_t len = plat_load_pack(PACK_LEVEL_ID0 + n + 1, PACK_LEVEL_CART, PACK_LEVEL_MAX);
		if (len <= 0 || !pack_mount_at(1, plat_cart_ptr(PACK_LEVEL_CART)))
		{
			static char msg[40];
			put_str(put_dec(put_str(msg, "LEVEL PACK OJ"), (uint32_t)(n + 1), 3), ".PAK: NOT LOADED");
			plat_fatal(msg);
		}
		level_in_cart = n;
	}
	level_n = n;
	level_bin = need("LEVEL.BIN");
	if (memcmp(level_bin, "STXR", 4)) plat_fatal("LEVEL.BIN: NOT STXR");
	lw = be16(level_bin + 4); lh = be16(level_bin + 6);
	ltrack = be16(level_bin + 8); ltime = be16(level_bin + 10); lgrav = be16(level_bin + 12);
	lstart_x = (int16_t)be16(level_bin + 14); lstart_y = (int16_t)be16(level_bin + 16);
	if (lw * lh > MAX_CELLS) plat_fatal("LEVEL TOO BIG");
	grav_fx = (int32_t)(((uint32_t)lgrav << 16) / 100u);
	const uint8_t *bt = need("BACKT.BIN");
	for (int i = 0; i < 64; i++) palette[192 + i] = be16(bt + 2 * i);
	/* the resources (yt.h): 1..nframes tile frames, then the background tiles */
	reslist[0] = 0;
	for (int i = 0; i < nframes; i++) reslist[1 + i] = (uint8_t *)(tile_px + 256 * i);
	uint32_t btsize;
	const uint8_t *btp;
	pack_file("BACKT.BIN", &btp, &btsize);
	const int nb = (int)((btsize - 128) / 256);
	if (1 + nframes + nb > 1024) plat_fatal("TOO MANY TILE RESOURCES");
	for (int i = 0; i < nb; i++) reslist[1 + nframes + i] = (uint8_t *)(bt + 128 + 256 * i);
	const int cells = lw * lh;
	const uint8_t *L = level_bin + 20;
	layer_ptr[0] = native_layer(L, 0);
	layer_ptr[1] = native_layer(L + 2 * cells, 1);
	layer_ptr[2] = ia_layer;
	layer_ptr[3] = native_layer(L + 2 * cells * 3, 2);
	level_reset_map();
	yt_level(layer_ptr, lw, lh, reslist);
	plat_music(ltrack);
}

static int tile_at(int x, int y)       /* SuperTux's gettile: tile id at a point (full units) */
{
	if (x < 0 || y < 0) return 0;
	const int tx = x >> 5, ty = y >> 5;
	if (tx >= lw || ty >= lh) return 0;
	return ia_ids[ty * lw + tx];
}
static int flags_at(int x, int y) { const int t = tile_at(x, y); return t < ntiles ? tile_flags[t] : 0; }
static int issolid(int x, int y) { return flags_at(x, y) & TF_SOLID; }

/* collision.cpp collision_object_map: a 32 x 32 box at whole pixels, "1 pixel smaller" */
static int box_hits(int bx, int by)
{
	for (int tx = (bx + 1) >> 5; tx * 32 < bx + 32; tx++)
		for (int ty = (by + 1) >> 5; ty * 32 < by + 32; ty++)
		{
			if (tx < 0 || ty < 0 || tx >= lw || ty >= lh) continue;
			const int t = ia_ids[ty * lw + tx];
			if (t < ntiles && (tile_flags[t] & TF_SOLID)) return 1;
		}
	return 0;
}

/* ------------------------------------------------------------------ Tux (player.cpp) */
typedef struct {
	fx x, y;                           /* base.x, base.y: the box's top-left (32 x 32, small Tux) */
	fx vx, vy, ax;                     /* vy: down positive (Physic keeps vy = -velocity_y) */
	int dir;                           /* 0 left, 1 right */
	int jumping, can_jump, old_up, gravity_on;
	int skid;                          /* steps left of the skid (skidding_timer) */
	int dead;
} Tux;
static Tux tux;
static int coins, lives = START_LIVES, time_left_steps, end_steps;
static fx scroll_x, cam_yf;            /* the camera in SuperTux's units */
static uint32_t game_ms;

static int on_ground(void)
{
	const int x = FX_INT(tux.x), y = FX_INT(tux.y);
	return issolid(x + 16, y + 32) || issolid(x + 1, y + 32) || issolid(x + 31, y + 32);
}
static int under_solid(void)
{
	const int x = FX_INT(tux.x), y = FX_INT(tux.y);
	return issolid(x + 16, y) || issolid(x + 1, y) || issolid(x + 31, y);
}

static void tux_start(void)
{
	memset(&tux, 0, sizeof tux);
	tux.x = lstart_x << 16;
	tux.y = lstart_y << 16;
	tux.dir = 1;
	tux.gravity_on = 1;
	scroll_x = 0;
	cam_yf = (lh * 32 - VIEW_H_FULL) << 16;
	time_left_steps = ltime * 100;
	end_steps = 0;
}

static void handle_horizontal(int left, int right, int run)
{
	fx vx = tux.vx, ax;
	int dirsign = 0;
	if (left && !right) { tux.dir = 0; dirsign = -1; }
	else if (right && !left) { tux.dir = 1; dirsign = 1; }
	if (!run)
	{
		ax = dirsign * WALK_ACCELERATION_X;
		if (vx >= MAX_WALK_XM && dirsign > 0) { vx = MAX_WALK_XM; ax = 0; }
		else if (vx <= -MAX_WALK_XM && dirsign < 0) { vx = -MAX_WALK_XM; ax = 0; }
	}
	else
	{
		ax = dirsign * RUN_ACCELERATION_X;
		if (vx >= MAX_RUN_XM && dirsign > 0) { vx = MAX_RUN_XM; ax = 0; }
		else if (vx <= -MAX_RUN_XM && dirsign < 0) { vx = -MAX_RUN_XM; ax = 0; }
	}
	/* "we can reach WALK_SPEED without any acceleration" */
	if (dirsign != 0 && fxabs(vx) < WALK_SPEED) vx = dirsign * WALK_SPEED;
	/* changing directions? */
	if (on_ground() && ((vx < 0 && dirsign > 0) || (vx > 0 && dirsign < 0)))
	{
		if (fxabs(vx) > SKID_XM && !tux.skid)
		{
			tux.skid = SKID_STEPS;
			plat_sfx(SFX_SKID);
			ax = ax * 5 / 2;
		}
		else
			ax *= 2;
	}
	/* "we get slower when not pressing any keys" */
	if (dirsign == 0)
	{
		if (fxabs(vx) < WALK_SPEED) { vx = 0; ax = 0; }
		else if (vx < 0) ax = WALK_ACCELERATION_X * 3 / 2;
		else ax = -WALK_ACCELERATION_X * 3 / 2;
	}
	/* on ice the acceleration is divided by the speed */
	if ((flags_at(FX_INT(tux.x), FX_INT(tux.y) + 32) & TF_ICE) && ax != 0 && vx != 0)
		ax = (fx)(((int64_t)ax << 16) / fxabs(vx));
	tux.vx = vx;
	tux.ax = ax;
}

static void handle_vertical(int up)
{
	if (up && tux.can_jump)
	{
		if (on_ground())
		{
			tux.vy = -(fxabs(tux.vx) > MAX_WALK_XM ? JUMP_RUN_YM : JUMP_YM);
			tux.y -= ONE;
			tux.jumping = 1;
			tux.can_jump = 0;
			plat_sfx(SFX_JUMP);
		}
	}
	else if (!up && tux.jumping)
	{
		tux.jumping = 0;
		if (tux.vy < 0) tux.vy = 0;    /* let go: the jump stops rising */
	}
	/* pressed again shortly before landing: the jump is allowed */
	const int x = FX_INT(tux.x), y = FX_INT(tux.y);
	if ((issolid(x + 16, y + 32 + 64) || issolid(x + 1, y + 32 + 64) || issolid(x + 31, y + 32 + 64))
	    && !tux.jumping && !tux.can_jump && up && !tux.old_up)
		tux.can_jump = 1;
	tux.old_up = up;
}

/* collision.cpp collision_swept_object_map, axis by axis: the box moves pixel by pixel and
 * stops at the last free pixel (SuperTux: "current = old - d; while (collision) current -= d") */
static void move_axis(fx *pos, fx target, int horizontal)
{
	const int from = FX_INT(*pos), to = FX_INT(target);
	if (from == to) { *pos = target; return; }
	const int d = to > from ? 1 : -1;
	for (int p = from + d; ; p += d)
	{
		const int hit = horizontal ? box_hits(p, FX_INT(tux.y)) : box_hits(FX_INT(tux.x), p);
		if (hit) { *pos = (fx)(p - d) << 16; return; }
		if (p == to) break;
	}
	*pos = target;
}

static void grab_distro(int x, int y)
{
	if (x < 0 || y < 0 || (x >> 5) >= lw || (y >> 5) >= lh) return;
	const int cell = (y >> 5) * lw + (x >> 5);
	const int t = ia_ids[cell];
	if (t < ntiles && (tile_flags[t] & TF_DISTRO))
	{
		ia_ids[cell] = 0;              /* level->change(..., tile->next_tile): 0 for coins */
		ia_layer[cell] = 0;            /* the tiler sees the cell change and redraws it */
		plat_sfx(SFX_DISTRO);
		if (++coins >= DISTROS_LIFEUP) { coins -= DISTROS_LIFEUP; lives++; plat_sfx(SFX_LIFEUP); }
	}
}

static void tux_step(uint16_t pad)
{
	int left = (pad & PAD_LEFT) != 0, right = (pad & PAD_RIGHT) != 0;
	const int run = (pad & PAD_B) != 0;
	int up = (pad & (PAD_C | PAD_A)) != 0;
	if (end_steps) { left = 0; right = 1; up = 0; }   /* the end sequence: Tux walks on */

	/* Player::handle_input */
	handle_horizontal(left, right, run);
	if (on_ground() && !up) tux.can_jump = 1;
	if (up || tux.jumping) handle_vertical(up);

	/* Physic::apply (frame_ratio 1) */
	const fx g = tux.gravity_on ? grav_fx : 0;
	const fx ox = tux.x, oy = tux.y;
	const fx tx = ox + tux.vx + tux.ax;
	const fx ty = oy + tux.vy + g;
	tux.vx += tux.ax;
	tux.vy += g;

	/* collision_swept_object_map */
	move_axis(&tux.x, tx, 1);
	move_axis(&tux.y, ty, 0);
	if (tux.x != tx) tux.vx = 0;      /* "Don't accelerate Tux if he is running against a wall" */

	/* the screen's left edge (the camera never goes back - back_scrolling is off) and the
	 * level's right edge */
	if (tux.x < scroll_x) { tux.x = scroll_x; if (tux.vx < 0) tux.vx = 0; }
	if (tux.x > ((lw * 32 - 32) << 16)) tux.x = (lw * 32 - 32) << 16;

	int jumped_in_solid = 0;
	if (!on_ground())
	{
		tux.gravity_on = 1;
		if (under_solid()) { tux.vy = 0; jumped_in_solid = 1; }
	}
	else
	{
		if (tux.vy > 0)               /* landing (velocity_y < 0: falling) */
		{
			tux.y = (fx)((FX_INT(tux.y) / 32) * 32) << 16;
			tux.vy = 0;
		}
		tux.gravity_on = 0;
	}
	/* grabdistros: the four corners */
	const int bx = FX_INT(tux.x), by = FX_INT(tux.y);
	grab_distro(bx, by); grab_distro(bx + 31, by);
	grab_distro(bx, by + 32); grab_distro(bx + 31, by + 32);
	if (jumped_in_solid)
	{
		tux.y += ONE;
		if (on_ground()) tux.jumping = 0;
	}
	if (tux.skid) tux.skid--;

	/* World::scrolling ("no debug", back_scrolling off) */
	const fx tux_pos_x = tux.x + (16 << 16);
	if (tux.vx > 0 && scroll_x < tux_pos_x - ((SCREEN_W_FULL - X_SPACE) << 16))
		scroll_x = tux_pos_x - ((SCREEN_W_FULL - X_SPACE) << 16);
	else if (tux.dir == 1 && scroll_x < tux_pos_x - ((SCREEN_W_FULL - X_SPACE) << 16))
		scroll_x = tux_pos_x - ((SCREEN_W_FULL - X_SPACE) << 16);
	if (scroll_x < 0) scroll_x = 0;
	if (scroll_x > ((lw * 32 - SCREEN_W_FULL) << 16)) scroll_x = (lw * 32 - SCREEN_W_FULL) << 16;

	/* the camera up / down (ours: SuperTux 0.1.3 shows all 480 lines, the 32X view 352 of
	 * them): it moves only when Tux leaves the middle band, so the canvas (yt.h) seldom
	 * re-anchors vertically */
	const fx top = tux.y - (96 << 16), bottom = tux.y + (32 - VIEW_H_FULL + 96) * ONE;
	if (top < cam_yf) cam_yf = top;
	if (bottom > cam_yf) cam_yf = bottom;
	if (cam_yf > ((lh * 32 - VIEW_H_FULL) << 16)) cam_yf = (lh * 32 - VIEW_H_FULL) << 16;
	if (cam_yf < 0) cam_yf = 0;

	if (FX_INT(tux.y) > lh * 32) tux.dead = 1;         /* fell out of the level */
}

/* ------------------------------------------------------------------ the panel (HUD + timing) */
static uint8_t panel_rows[YT_PANEL_H][YT_VIEW_W + 8];

static void panel_text(int x, int y, const char *t, uint8_t colour)
{
	for (; *t; t++, x += 8)
	{
		const unsigned char *g = msx + (unsigned char)*t * 8;
		for (int r = 0; r < 8; r++)
			for (int b = 0; b < 8; b++)
				if (g[r] & (0x80 >> b))
				{
					if (y + r + 1 < YT_PANEL_H && x + b + 1 < YT_VIEW_W) panel_rows[y + r + 1][x + b + 1] = col_black;   /* shadow */
					if (y + r < YT_PANEL_H && x + b < YT_VIEW_W) panel_rows[y + r][x + b] = colour;
				}
	}
}

/* the panel into the back buffer: 16-bit writes only (Kobo rule 4), shifted by one byte when the
 * 32X's 1-pixel shift is on (it applies to every line, the panel's too) */
static void panel_put(int shift)
{
	volatile uint8_t *pb = plat_scroll_panel();
	for (int y = 0; y < YT_PANEL_H; y++)
	{
		const uint8_t *s = panel_rows[y];
		volatile uint8_t *d = pb + y * YT_VIEW_W;
		for (int x = 0; x < YT_VIEW_W; x += 2)
		{
			const uint8_t a = x - shift >= 0 ? s[x - shift] : col_panel, b = s[x + 1 - shift];
#ifdef OJ32X_MARS
			*(volatile uint16_t *)(d + x) = (uint16_t)(a << 8 | b);
#else
			d[x] = a; d[x + 1] = b;
#endif
		}
	}
}

/* ------------------------------------------------------------------ main */
static const int walk_order[2] = { SPR_WALK_L, SPR_WALK_R };

static void draw_tux(int cx, int cy)
{
	int s;
	if (tux.skid) s = tux.dir ? SPR_SKID_R : SPR_SKID_L;
	else if (tux.vy != 0) s = tux.dir ? SPR_JUMP_R : SPR_JUMP_L;
	else if (fxabs(tux.vx) < FX(1.0)) s = tux.dir ? SPR_STAND_R : SPR_STAND_L;
	else s = walk_order[tux.dir];
	const Sprite *sp = &spr[s];
	const int f = sp->first + (int)((game_ms * (uint32_t)sp->fps10 / 10000u) % (uint32_t)sp->n);
	const uint8_t *fe = spr_frames + 8 * f;
	const int w = be16(fe), h = be16(fe + 2);
	const uint8_t *px = spr_bin + be32(fe + 4);
	const int vx = ((FX_INT(tux.x) - sp->hx) >> 1) - cx;
	const int vy = ((FX_INT(tux.y) - sp->hy) >> 1) - cy;
	yt_sprite(vx, vy, w, h, px);
}

int main(int argc, char *argv[])
{
	(void)argc; (void)argv;
	plat_init();
	load_resident();
	plat_border_colour(0);
	int start = 0;
#ifndef OJ32X_MARS
	{ extern int host_start_level(void); start = host_start_level(); }
#endif
	load_level(start);
	tux_start();

	enum { T_LOGIC, T_TILES, T_SPR, T_HUD, T_WAIT, T_N };
	uint32_t tacc[T_N] = { 0 }, tshow[T_N] = { 0 }, tframes = 0, tms = 0, fps10 = 0, tiles_acc = 0, tiles_show = 0;
	uint32_t last_ms = plat_ticks_ms(), acc = 0;
	uint16_t pad_prev = 0;
	int paused = 0, pick = 0;
	static char line1[48], line2[64];

	for (;;)
	{
		/* time: 100 steps a second (SuperTux's frame_ratio 1 = 10 ms), at most 10 a frame */
		const uint32_t now = plat_ticks_ms();
		uint32_t dms = now - last_ms;
		last_ms = now;
		if (dms > 200) dms = 200;
		acc += dms;
		int steps = (int)(acc / 10u);
		acc -= (uint32_t)steps * 10u;
		if (steps > 10) steps = 10;

		const uint16_t pad = plat_read_pad();
		const uint16_t hit = pad & ~pad_prev;
		pad_prev = pad;
		uint32_t t0 = plat_prof_us(), t1;

		if (hit & PAD_START)
		{
			paused ^= 1;
			pick = level_n;
		}
		if (paused)
		{
			if (hit & PAD_LEFT) pick = (pick + 25) % 26;
			if (hit & PAD_RIGHT) pick = (pick + 1) % 26;
			if (hit & (PAD_A | PAD_C)) { paused = 0; load_level(pick); tux_start(); last_ms = plat_ticks_ms(); acc = 0; continue; }
			steps = 0;
		}
		for (int s = 0; s < steps; s++)
		{
			game_ms += 10;
			tux_step(pad);
			if (!end_steps && time_left_steps > 0 && --time_left_steps == 0) tux.dead = 1;
			if (!end_steps)
			{
				const int bx = FX_INT(tux.x);
				if (bx >= (lw - 5) * 32 || (flags_at(bx + 16, FX_INT(tux.y) + 16) & TF_GOAL))
				{
					end_steps = END_STEPS;
					plat_music(TRACK_LEVELDONE | 0x8000);   /* once */
				}
			}
			else if (--end_steps == 0)
			{
				load_level((level_n + 1) % 26);
				tux_start();
				last_ms = plat_ticks_ms(); acc = 0;
				break;
			}
			if (tux.dead)
			{
#ifndef OJ32X_MARS
				if (getenv("ST_LOG")) fprintf(stderr, "tux died at %d,%d (time %d)\n", FX_INT(tux.x), FX_INT(tux.y), time_left_steps);
#endif
				plat_sfx(SFX_HURT);
				if (--lives < 0) { lives = START_LIVES; coins = 0; load_level(0); }
				else level_reset_map();
				tux_start();
				plat_music(ltrack);
				break;
			}
		}
		t1 = plat_prof_us(); tacc[T_LOGIC] += t1 - t0; t0 = t1;

		/* draw into the back buffer */
		plat_wait_flip();
		t1 = plat_prof_us(); tacc[T_WAIT] += t1 - t0; t0 = t1;
		const int cx = FX_INT(scroll_x) >> 1, cy = FX_INT(cam_yf) >> 1;
		yt_begin(cx, cy);
		tiles_acc += (uint32_t)yt_layers(0, 2);
		t1 = plat_prof_us(); tacc[T_TILES] += t1 - t0; t0 = t1;
		draw_tux(cx, cy);
		t1 = plat_prof_us(); tacc[T_SPR] += t1 - t0; t0 = t1;
		tiles_acc += (uint32_t)yt_layers(3, 3);   /* foreground over Tux */
		t1 = plat_prof_us(); tacc[T_TILES] += t1 - t0; t0 = t1;

		/* the panel: SuperTux's HUD and the timing line */
		memset(panel_rows, col_panel, sizeof panel_rows);
		{
			char *q = put_str(line1, "COINS ");
			q = put_dec(q, (uint32_t)coins, 2);
			q = put_str(q, " TIME ");
			q = put_dec(q, (uint32_t)(time_left_steps + 99) / 100, 3);
			q = put_str(q, " TUX ");
			q = put_dec(q, (uint32_t)(lives < 0 ? 0 : lives), 1);
			q = put_str(q, paused ? " LEVEL<" : (end_steps ? " DONE " : " LEVEL "));
			q = put_dec(q, (uint32_t)((paused ? pick : level_n) + 1), 2);
			if (paused) put_str(q, ">");
			panel_text(4, 2, line1, end_steps ? col_yellow : col_white);
		}
		{
			/* ms per frame, averaged over 16 frames: L logic, T tiles, S sprite, H hud, W wait for
			 * the swap; N tiles drawn per frame */
			char *q = put_str(line2, "F");
			q = put_dec(q, fps10 / 10, 2); q = put_str(q, "."); q = put_dec(q, fps10 % 10, 1);
			static const char *nm[T_N] = { " L", " T", " S", " H", " W" };
			for (int i = 0; i < T_N; i++)
			{
				q = put_str(q, nm[i]);
				q = put_dec(q, tshow[i] / 1000, 1); q = put_str(q, "."); q = put_dec(q, (tshow[i] / 100) % 10, 1);
			}
			q = put_str(q, " N");
			put_dec(q, tiles_show, 1);
			panel_text(4, 13, line2, col_white);
		}
		panel_put(yt_scroll_x() & 1);
		t1 = plat_prof_us(); tacc[T_HUD] += t1 - t0;

		if (++tframes == 16)
		{
			const uint32_t ms = plat_ticks_ms();
			fps10 = ms != tms ? 16u * 10000u / (ms - tms) : 0;
			tms = ms;
			for (int i = 0; i < T_N; i++) { tshow[i] = tacc[i] / 16; tacc[i] = 0; }
			tiles_show = tiles_acc / 16;
			tiles_acc = 0;
			tframes = 0;
		}
		plat_present_scroll(palette, yt_scroll_y(), yt_scroll_x(), YT_VIEW_H);
	}
}
