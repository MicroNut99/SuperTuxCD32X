/*
 * SuperTux CD32X - a level being played: SuperTux 0.1.3's src/gameloop.cpp (GameSession: the
 * level intro, the steps, the end sequence, time, music, dying and starting again at a reset
 * point, game over) and world.cpp (World: the order of the objects' actions, the camera, the
 * brick / box / coin rules, the particles), with Vic's tiler for the drawing. See game/stx.h.
 *
 * TIME: SuperTux moves everything by frame_ratio = elapsed / 10 ms. Here the world always takes
 * whole 10 ms steps (step_clock: as many as the time since the last frame, at most 10), so the
 * rules give the same results however fast the 32X draws.
 *
 * THE SCREEN: the 32X shows SuperTux's whole width (640 units = 320 pixels) and 352 of its 480
 * lines (176 pixels); the camera follows Tux up and down (ours: SuperTux 0.1.3 never scrolls
 * vertically). Under the view: the panel (score, time, coins, lives, the level, the timing).
 */
#include "stx.h"

PlayerStatus ps = { 0, 0, START_LIVES, 0 };
fx scroll_x;
int cam_y;
uint32_t game_ms, global_frame;
int opt_music = 1, opt_timing = 0, opt_swap = 0, opt_lives = START_LIVES;

void ps_reset(void)
{
	ps.score = 0;
	ps.distros = 0;
	ps.lives = opt_lives;
	ps.bonus = 0;
}

/* ------------------------------------------------------------------ music, buttons, time */
static int track_now = -1;

/* only when it changes (OpenLara lesson 5: never restart a track every frame) */
void music(int track)
{
	if (!opt_music && track) track = 0;
	if (track == track_now) return;
	track_now = track;
	plat_music(track);
}
static int track_wanted;
static void music_want(int track) { track_wanted = track; music(track); }
void music_again(void)
{
	track_now = -1;
	music(opt_music ? track_wanted : 0);
}

static uint16_t pad_prev = 0xFFFF;     /* nothing counts as pressed until it is let go once */
uint16_t pad_read(uint16_t *hit)
{
	uint16_t p = plat_read_pad();
	if (opt_swap)
		p = (uint16_t)((p & ~(PAD_A | PAD_B)) | ((p & PAD_A) ? PAD_B : 0) | ((p & PAD_B) ? PAD_A : 0));
	if (hit) *hit = p & ~pad_prev;
	pad_prev = p;
	return p;
}

static uint32_t clock_last, clock_acc;
void step_clock_reset(void) { clock_last = plat_ticks_ms(); clock_acc = 0; }
int step_clock(void)
{
	const uint32_t now = plat_ticks_ms();
	uint32_t d = now - clock_last;
	clock_last = now;
	if (d > 200) d = 200;
	clock_acc += d;
	int steps = (int)(clock_acc / STEP_MS);
	clock_acc -= (uint32_t)steps * STEP_MS;
	return steps > 10 ? 10 : steps;
}

void present_view(void) { plat_present_scroll(palette, yt_scroll_y(), yt_scroll_x(), YT_VIEW_H); }

/* ------------------------------------------------------------------ the world's rules (world.cpp) */
static int counting_distros, distro_counter;

void trybreakbrick(int x, int y, int small, int dir)
{
	const int t = tile_at(x, y);
	const TileDesc *d = &tdesc[t];
	if (d->flags & TF_BRICK)
	{
		if (d->data > 0)
		{
			/* a brick with coins: five of them, then it becomes its next tile */
			add_bouncy_distro(((x + 1) / 32) * 32, (y / 32) * 32);
			if (!counting_distros) { counting_distros = 1; distro_counter = 5; }
			else distro_counter--;
			if (distro_counter <= 0)
			{
				counting_distros = 0;
				level_change(x, y, d->next);
			}
			plat_sfx(SFX_DISTRO);
			ps.score += SCORE_DISTRO;
			ps.distros++;
		}
		else if (!small)
		{
			level_change(x, y, d->next);
			add_broken_brick(t, ((x + 1) / 32) * 32, (y / 32) * 32);
			plat_sfx(SFX_BRICK);
			ps.score += SCORE_BRICK;
		}
	}
	else if (d->flags & TF_FULLBOX) tryemptybox(x, y, dir);
}

void tryemptybox(int x, int y, int dir)
{
	const int t = tile_at(x, y);
	const TileDesc *d = &tdesc[t];
	if (!(d->flags & TF_FULLBOX)) return;
	const int side = dir == 0 ? 1 : 0;   /* the upgrade goes away from the side it was hit on */
	const int px = ((x + 1) / 32) * 32, py = (y / 32) * 32 - 32;
	switch (d->data)
	{
	case 1:                            /* a coin */
		add_bouncy_distro(px, py);
		plat_sfx(SFX_DISTRO);
		ps.score += SCORE_DISTRO;
		ps.distros++;
		break;
	case 2:                            /* small Tux: the egg (grow), big Tux: the ice flower */
		add_upgrade(px, py, side, tux.size == SMALL ? UP_GROWUP : UP_ICEFLOWER);
		plat_sfx(SFX_UPGRADE);
		break;
	case 3: add_upgrade(px, py, side, UP_HERRING); break;
	case 4: add_upgrade(px, py, side, UP_1UP); break;
	default: break;
	}
	level_change(x, y, d->next);
}

void trygrabdistro(int x, int y, int bounce)
{
	const int t = tile_at(x, y);
	if (!(tdesc[t].flags & TF_DISTRO)) return;
	level_change(x, y, tdesc[t].next);
	plat_sfx(SFX_DISTRO);
	if (bounce) add_bouncy_distro(((x + 1) / 32) * 32, (y / 32) * 32);
	ps.score += SCORE_DISTRO;
	ps.distros++;
}

void bumpbrick(int x, int y)           /* gameloop.cpp bumpbrick */
{
	add_bouncy_brick(((x + 1) / 32) * 32, (y / 32) * 32);
	plat_sfx(SFX_BRICK);
}

/* ------------------------------------------------------------------ particles (particlesystem.cpp) */
/* Snow: SuperTux makes 128 flakes over twice the screen's width; every flake drawn makes the
 * tiler redraw the cells under it in the next frame, so the 32X has 32 (the same density as
 * SuperTux's in view, a quarter of it). Clouds: 15 over 2000 units, as SuperTux. Both stay on
 * the screen while the level scrolls under them (x: virtual width, y: the view). */
#define NSNOW   32
#define NCLOUD  15
typedef struct { fx x, y, speed; uint8_t layer, img; } Particle;
static Particle parts[NSNOW > NCLOUD ? NSNOW : NCLOUD];
static int nparts, part_kind, part_vw;

static void particles_start(void)
{
	part_kind = lparticles;
	nparts = 0;
	if (part_kind == 1)
	{
		part_vw = VIEW_W_U * 2;        /* SuperTux: virtual_width = screen width x 2 */
		for (int i = 0; i < NSNOW; i++)
		{
			Particle *p = &parts[nparts++];
			p->x = (fx)(rnd() % (uint32_t)part_vw) << 16;
			p->y = (fx)(rnd() % VIEW_H_U) << 16;
			p->layer = (uint8_t)(i % 2);
			p->img = (uint8_t)(rnd() % 3);
			/* speed = size / 60 + (rand % 10) / 300, at least 0.01, times the gravity */
			fx sp;
			do sp = (fx)(p->img * 65536 / 60) + (fx)((rnd() % 10) * 65536 / 300); while (sp < FX(0.01));
			p->speed = sp * lgrav;
		}
	}
	else if (part_kind == 2)
	{
		part_vw = 2000;
		for (int i = 0; i < NCLOUD; i++)
		{
			Particle *p = &parts[nparts++];
			p->x = (fx)(rnd() % 2000u) << 16;
			p->y = (fx)(rnd() % VIEW_H_U) << 16;
			p->layer = 0;
			p->img = 0;
			p->speed = -(fx)((250 + rnd() % 200) * 65536 / 1000);
		}
	}
}

static void particles_step(void)
{
	for (int i = 0; i < nparts; i++)
	{
		Particle *p = &parts[i];
		if (part_kind == 1)
		{
			p->y += p->speed;          /* simulate(frame_ratio): speed a step */
			if (FX_INT(p->y) > VIEW_H_U)
			{
				p->y -= (fx)VIEW_H_U << 16;
				p->x = (fx)(rnd() % (uint32_t)part_vw) << 16;
			}
		}
		else
		{
			p->x += p->speed;
			if (p->x < 0) p->x += (fx)part_vw << 16;
		}
	}
}

static void particles_draw(int layer)
{
	const int sx = FX_INT(scroll_x);
	for (int i = 0; i < nparts; i++)
	{
		const Particle *p = &parts[i];
		if (p->layer != layer) continue;
		int x = (FX_INT(p->x) - sx) % part_vw;
		if (x < 0) x += part_vw;
		if (x > VIEW_W_U) x -= part_vw;
		const int s = part_kind == 1 ? S_SNOW0 + p->img : S_CLOUD;
		int w, h;
		const uint8_t *px = spr_frame(s, 0, &w, &h);
		yt_sprite(px_of(x), px_of(FX_INT(p->y)), w, h, px);
	}
}

/* ------------------------------------------------------------------ timing (OPTIONS - TIMING) */
enum { T_LOGIC, T_TILES, T_SPR, T_HUD, T_WAIT, T_N };
static uint32_t tacc[T_N], tshow[T_N], tframes, tms, fps10, tiles_acc, tiles_show;

static void timing_frame(void)
{
	if (++tframes < 16) return;
	const uint32_t ms = plat_ticks_ms();
	fps10 = ms != tms ? 16u * 10000u / (ms - tms) : 0;
	tms = ms;
	for (int i = 0; i < T_N; i++) { tshow[i] = tacc[i] / 16; tacc[i] = 0; }
	tiles_show = tiles_acc / 16;
	tiles_acc = 0;
	tframes = 0;
}

/* F frames a second; ms a frame: L the rules, T the tile layers, S the sprites, H the panel,
 * W waiting for the swap; N tiles drawn a frame */
void timing_line(char *out)
{
	char *q = put_str(out, "F");
	q = put_dec(q, fps10 / 10, 2); q = put_str(q, "."); q = put_dec(q, fps10 % 10, 1);
	static const char *const nm[T_N] = { " L", " T", " S", " H", " W" };
	for (int i = 0; i < T_N; i++)
	{
		q = put_str(q, nm[i]);
		q = put_dec(q, tshow[i] / 1000, 1); q = put_str(q, "."); q = put_dec(q, (tshow[i] / 100) % 10, 1);
	}
	q = put_str(q, " N");
	put_dec(q, tiles_show, 1);
}

/* ------------------------------------------------------------------ the camera (World::scrolling) */
#define X_SPACE (400 - 16)
#define CHANGE_DIR_SCROLL_STEPS 200    /* CHANGE_DIR_SCROLL_SPEED 2000 ms */
static int scrolling_timer;

static void scrolling(void)
{
	if (lautoscroll)
	{
		scroll_x += (fx)lautoscroll << 8;   /* hor_autoscroll_speed x frame_ratio */
	}
	else
	{
		const fx tux_pos_x = tux.x + (16 << 16);
		const fx right = tux_pos_x - ((VIEW_W_U - X_SPACE) << 16), left = tux_pos_x - (X_SPACE << 16);
		if (lback_scroll)
		{
			if (tux.old_dir != tux.dir) scrolling_timer = CHANGE_DIR_SCROLL_STEPS;
			if (scrolling_timer)
			{
				scrolling_timer--;
				fx final = scroll_x;
				if (tux.ph.vx > 0) final = right;
				else if (tux.ph.vx < 0) final = left;
				else if (tux.dir == 1) final = right;
				else final = left;
				scroll_x += (final - scroll_x) / (CHANGE_DIR_SCROLL_STEPS / 10) + tux.ph.vx + tux.ph.ax;
			}
			else
			{
				if (tux.ph.vx > 0 && scroll_x < right) scroll_x = right;
				else if (tux.ph.vx < 0 && scroll_x > left) scroll_x = left;
				else if (tux.dir == 1 && scroll_x < right) scroll_x = right;
				else if (tux.dir == 0 && scroll_x > left) scroll_x = left;
			}
		}
		else
		{
			if (tux.ph.vx > 0 && scroll_x < right) scroll_x = right;
			else if (tux.dir == 1 && scroll_x < right) scroll_x = right;
		}
	}
	if (scroll_x < 0) scroll_x = 0;
	if (scroll_x > ((lw * 32 - VIEW_W_U) << 16)) scroll_x = (lw * 32 - VIEW_W_U) << 16;
}

/* ours: up and down. The camera moves when Tux leaves the middle band of the view, and not
 * while he falls out of the level (so he leaves the screen, as in SuperTux). */
static void camera_y(int snap)
{
	const int maxy = lh * 32 - VIEW_H_U;
	const int y = FX_INT(tux.y);
	if (snap) cam_y = y + tux.h / 2 - VIEW_H_U / 2;
	else if (tux.dying == DYING_NOT)
	{
		if (y - 96 < cam_y) cam_y = y - 96;
		if (y + tux.h + 112 > cam_y + VIEW_H_U) cam_y = y + tux.h + 112 - VIEW_H_U;
	}
	if (cam_y > maxy) cam_y = maxy;
	if (cam_y < 0) cam_y = 0;
}

/* Player::check_bounds */
#define AUTOSCROLL_DEAD_INTERVAL 300
static void check_bounds(void)
{
	if (tux.x < 0) tux.x = 0;
	if (FX_INT(tux.y) > lh * 32 && tux.dying == DYING_NOT) tux_kill(0);
	if (tux.x < scroll_x && (!lback_scroll || lautoscroll)) tux.x = scroll_x;
	if (lautoscroll)
	{
		const int x = FX_INT(tux.x), y = FX_INT(tux.y);
		if (tux.x == scroll_x && tux.dying == DYING_NOT
		    && (issolid(x + 32, y) || (tux.size != SMALL && !tux.duck && issolid(x + 32, y + 32))))
			tux_kill(0);
		if (tux.x + (32 << 16) > scroll_x + (VIEW_W_U << 16)) tux.x = scroll_x + ((VIEW_W_U - 32) << 16);
	}
}

static int tux_is_dead(void)
{
	return FX_INT(tux.y) > lh * 32 || FX_INT(tux.x) < FX_INT(scroll_x) - AUTOSCROLL_DEAD_INTERVAL;
}

/* World::action */
static void world_action(void)
{
	tux_action();
	check_bounds();
	scrolling();
	objects_action();
	particles_step();
	objects_collisions();
	camera_y(0);
}

/* ------------------------------------------------------------------ the session's state */
enum { NO_ENDSEQUENCE, ENDSEQUENCE_RUNNING, ENDSEQUENCE_WAITING };
enum { MUS_NONE, MUS_LEVEL, MUS_HURRY, MUS_HERRING };
static int end_sequence, end_steps, end_half, last_x_pos, time_left, exit_status, music_type;
static int session_level;

static void play_music_type(int t)
{
	music_type = t;
	switch (t)
	{
	case MUS_LEVEL: music_want(ltrack); break;
	case MUS_HURRY:                    /* the song's "-fast" version: the next track (stx_convert.py MUSIC) */
		music_want(ltrack == 3 || ltrack == 5 || ltrack == 7 || ltrack == 9 || ltrack == 11 ? ltrack + 1 : ltrack);
		break;
	case MUS_HERRING: music_want(TRACK_HERRING); break;
	default: music_want(0); break;
	}
}

/* the level as it starts (World's constructor), Tux at the start or - after a life was lost -
 * at the nearest reset point behind where he was (GameSession::restart_level) */
static void world_start(int from_x)
{
	level_reset_map();
	counting_distros = 0;
	objects_start();
	tux_init(lstart_x, lstart_y);
	scroll_x = 0;
	scrolling_timer = 0;
	if (from_x >= 0)
	{
		int bx = -1, by = -1;
		for (int i = 0; i < lnresets; i++)
		{
			const int x = (int16_t)be16(lresets + 4 * i), y = (int16_t)be16(lresets + 4 * i + 2);
			if (x - 320 < from_x && bx < x) { bx = x; by = y; }
		}
		if (bx != -1)
		{
			tux.x = tux.px = (fx)bx << 16;
			tux.y = tux.py = (fx)by << 16;
			if (tux.size == BIG) { tux.y -= 32 << 16; tux.py = tux.y; }
			scroll_x = (fx)(bx - VIEW_W_U / 2) << 16;
		}
	}
	scrolling();
	camera_y(1);
	particles_start();
	end_sequence = NO_ENDSEQUENCE;
	time_left = ltime * 100;
	exit_status = ES_NONE;
	music_type = MUS_NONE;
}

/* ------------------------------------------------------------------ drawing */
static int view_cx, view_cy;

void game_draw_world(void)
{
	uint32_t t0 = plat_prof_us(), t1;
	int cx = px_of(FX_INT(scroll_x)), cy = px_of(cam_y);
	if (cx > lw * YT_TILE - YT_VIEW_W) cx = lw * YT_TILE - YT_VIEW_W;   /* as yt_begin clamps it */
	if (cy > lh * YT_TILE - YT_VIEW_H) cy = lh * YT_TILE - YT_VIEW_H;
	if (cx < 0) cx = 0;
	if (cy < 0) cy = 0;
	view_cx = cx; view_cy = cy;
	level_animate(game_ms);
	yt_begin(cx, cy);
	level_mark_animated(cx, cy);
	tiles_acc += (uint32_t)yt_layers(0, 0);          /* the background picture */
	t1 = plat_prof_us(); tacc[T_TILES] += t1 - t0; t0 = t1;
	particles_draw(0);                               /* clouds, the far snow */
	t1 = plat_prof_us(); tacc[T_SPR] += t1 - t0; t0 = t1;
	tiles_acc += (uint32_t)yt_layers(1, 2);          /* background-tm, interactive-tm */
	t1 = plat_prof_us(); tacc[T_TILES] += t1 - t0; t0 = t1;
	objects_draw_back(cx, cy, game_ms);
	tux_draw(cx, cy, game_ms, global_frame);
	t1 = plat_prof_us(); tacc[T_SPR] += t1 - t0; t0 = t1;
	tiles_acc += (uint32_t)yt_layers(3, 3);          /* foreground-tm: over Tux */
	t1 = plat_prof_us(); tacc[T_TILES] += t1 - t0; t0 = t1;
	particles_draw(1);                               /* the near snow */
	objects_draw_front(cx, cy);                      /* floating scores */
	t1 = plat_prof_us(); tacc[T_SPR] += t1 - t0;
}

/* the panel: SuperTux's status line (drawstatus) and the level's name, or the timing line */
static void draw_status(void)
{
	uint32_t t0 = plat_prof_us();
	static char l1[48], l2[48], l3[48];
	char *q = put_dec(put_str(l1, "SCORE "), ps.score, 1);
	q = put_str(q, "  ");
	if (time_left <= 0) q = put_str(q, "TIME'S UP");
	else if (time_left > STEPS(TIME_WARNING) || (global_frame % 10) < 5)
		q = put_dec(put_str(q, "TIME "), (uint32_t)(time_left + 99) / 100, 1);
	else q = put_str(q, "        ");
	q = put_dec(put_str(q, "  COINS "), (uint32_t)ps.distros, 1);
	q = put_dec(put_str(l2, "LIVES "), (uint32_t)(ps.lives < 0 ? 0 : ps.lives), 1);
	q = put_str(q, "  ");
	const char *t = level_title(session_level);
	int n = 0;
	while (t[n] && n < 28) { q[n] = t[n]; n++; }
	q[n] = 0;
	if (opt_timing) timing_line(l3);
	else put_str(l3, "");
	panel_show(l1, l2, l3, end_sequence ? col_yellow : col_white);
	tacc[T_HUD] += plat_prof_us() - t0;
}

/* GameSession::levelintro: the name, "TUX x lives", "by author" over the level, 1 to 3 s.
 * Before it, while the level is read from the disc (4-7 s), the same words over the title
 * picture - SuperTux's own loading has no screen of its own. */
static void intro_words(int li, int loading)
{
	static char t[32];
	const char *name = level_title(li);
	text_mid(fit_font(name), 50, name);
	put_dec(put_str(t, "TUX x "), (uint32_t)(ps.lives < 0 ? 0 : ps.lives), 1);
	text_mid(F_WHITE, 74, t);
	const char *a = level_author(li);
	if (*a)
	{
		static char by[56];
		put_str(put_str(by, "by "), a);
		text_mid(F_SMALL, 140, by);
	}
	if (loading) text_mid(F_SMALL, 160, "LOADING ...");
}

static void bg_loading(void) { bg_picture(); intro_words(session_level, 1); }
/* levelintro draws the level's background picture alone under the words */
static void bg_intro(void)
{
	int cx = px_of(FX_INT(scroll_x)), cy = px_of(cam_y);
	if (cx > lw * YT_TILE - YT_VIEW_W) cx = lw * YT_TILE - YT_VIEW_W;
	if (cy > lh * YT_TILE - YT_VIEW_H) cy = lh * YT_TILE - YT_VIEW_H;
	if (cx < 0) cx = 0;
	if (cy < 0) cy = 0;
	yt_begin(cx, cy);
	yt_layers(0, 0);
	intro_words(session_level, 0);
}

static void level_intro(void)
{
	music(0);                          /* halt_music */
	message_screen(bg_intro, 0, 0, 0, 1000, 3000);
}

static void bg_gameover(void)
{
	static char t[40];
	game_draw_world();
	text_mid(F_GOLD, 56, "GAMEOVER");
	put_dec(put_str(t, "SCORE: "), ps.score, 1);
	text_mid(F_GOLD, 80, t);
	put_dec(put_str(t, "COINS: "), (uint32_t)ps.distros, 1);
	text_mid(F_GOLD, 100, t);
}

/* ------------------------------------------------------------------ GameSession::check_end_conditions */
static int goal_tile(void)
{
	const int bx = FX_INT(tux.x), by = FX_INT(tux.y);
	for (int x = bx / 32; x * 32 < bx + tux.w; x++)
		for (int y = by / 32; y * 32 < by + tux.h; y++)
		{
			const int t = tile_at(x * 32, y * 32);
			if (tdesc[t].flags & TF_GOAL) return t;
		}
	return 0;
}

static void start_end_sequence(int kind)
{
	end_sequence = kind;
	last_x_pos = -1;
	music(TRACK_LEVELDONE | 0x8000);   /* played once */
	end_steps = STEPS(7000);
	tux.invincible_t = STEPS(7000);
}

static void check_end_conditions(void)
{
	const int endpos = (lw - 5) * 32;
	const int goal = goal_tile();
	if (!end_sequence && FX_INT(tux.x) >= endpos) start_end_sequence(ENDSEQUENCE_WAITING);
	else if (end_sequence && end_steps <= 0) exit_status = ES_LEVEL_FINISHED;
	else if (end_sequence == ENDSEQUENCE_RUNNING && goal && tdesc[goal].data >= 1) end_sequence = ENDSEQUENCE_WAITING;
	else if (!end_sequence && goal && tdesc[goal].data == 0) start_end_sequence(ENDSEQUENCE_RUNNING);
	else if (!end_sequence && tux_is_dead())
	{
		ps.bonus = 0;
		if (ps.lives < 0)
		{
			music(0);
			message_screen(bg_gameover, 0, 0, 0, 2000, 5000);
			exit_status = ES_GAME_OVER;
		}
		else
		{
			world_start(FX_INT(tux.x));
			level_intro();
			step_clock_reset();
		}
	}
}

/* ------------------------------------------------------------------ GameSession::run */
static void read_input(uint16_t pad)
{
	tux.in_old_fire = tux.in_fire;
	if (end_sequence != NO_ENDSEQUENCE)
	{
		/* the end sequence: Tux walks right by himself and jumps when something stops him */
		tux.in_fire = 0; tux.in_left = 0; tux.in_right = 1; tux.in_down = 0;
		tux.in_up = FX_INT(tux.x) == last_x_pos;
		last_x_pos = FX_INT(tux.x);
		return;
	}
	tux.in_left = (pad & PAD_LEFT) != 0;
	tux.in_right = (pad & PAD_RIGHT) != 0;
	tux.in_down = (pad & PAD_DOWN) != 0;
	tux.in_up = (pad & (PAD_C | PAD_A)) != 0;        /* jump */
	tux.in_fire = (pad & PAD_B) != 0;                /* run, shoot, carry */
}

int game_play(int li)
{
	session_level = li;
	if (!level_cached(li))
	{
		/* the words first, then the disc read (the screen stays as it is meanwhile) */
		music(0);
		plat_wait_flip();
		yt_screen();
		bg_loading();
		panel_show("", "", "", col_white);
		present_view();
		plat_wait_flip();
	}
	load_level(li);
	world_start(-1);
#ifndef OJ32X_MARS
	if (getenv("ST_X")) { tux.x = tux.px = (fx)atoi(getenv("ST_X")) << 16; tux.y = tux.py = 0; scrolling(); }   /* PC checks */
#endif
	level_intro();
	step_clock_reset();
	uint32_t frame_25 = 0;
	while (exit_status == ES_NONE)
	{
		uint32_t t0 = plat_prof_us(), t1;
		uint16_t hit;
		const uint16_t pad = pad_read(&hit);
		if (hit & PAD_START)
		{
			const int r = pause_menu();
			step_clock_reset();
			if (r == ES_LEVEL_ABORT || r == ES_QUIT) { exit_status = r; break; }
			if (r == -1) { world_start(-1); level_intro(); step_clock_reset(); continue; }
			music_again();
		}
		read_input(pad);
		const int steps = step_clock();
		for (int s = 0; s < steps && exit_status == ES_NONE; s++)
		{
			check_end_conditions();
			if (exit_status != ES_NONE) break;
			if (end_sequence == ENDSEQUENCE_RUNNING) { if ((end_half ^= 1) != 0) world_action(); }   /* frame_ratio / 2 */
			else if (end_sequence == NO_ENDSEQUENCE) world_action();
			if (end_sequence) end_steps--;
			if (time_left > 0) time_left--;
			if (time_left <= 0 && tux.dying == DYING_NOT && !end_sequence) tux_kill(0);
			game_ms += STEP_MS;
			/* the music: the herring's, hurry up (less than 20 s), the level's */
			if (!end_sequence)
			{
				if (tux.invincible_t) { if (music_type != MUS_HERRING) play_music_type(MUS_HERRING); }
				else if (time_left < STEPS(TIME_WARNING)) { if (music_type != MUS_HURRY) play_music_type(MUS_HURRY); }
				else if (music_type != MUS_LEVEL) play_music_type(MUS_LEVEL);
			}
		}
		if (exit_status != ES_NONE) break;
		frame_25 = game_ms / 25;       /* SuperTux's global_frame_counter: one every 25 ms */
		global_frame = frame_25;
		t1 = plat_prof_us(); tacc[T_LOGIC] += t1 - t0; t0 = t1;
		plat_wait_flip();
		t1 = plat_prof_us(); tacc[T_WAIT] += t1 - t0;
		game_draw_world();
		draw_status();
		timing_frame();
		present_view();
	}
	if (exit_status == ES_LEVEL_FINISHED)
	{
		/* worldmap.cpp: what Tux has at the end goes into the next level */
		ps.bonus = tux.got_coffee ? 2 : tux.size == BIG ? 1 : 0;
	}
	return exit_status;
}

/* ------------------------------------------------------------------ the title's demo (title.cpp) */
static int demo_walking = 1, demo_timer;
#define DEMO_LEVEL 76                  /* misc/menu.stl (tools/stx_convert.py LEVELS.TXT) */

void demo_start(void)
{
	session_level = DEMO_LEVEL;
	if (!level_cached(DEMO_LEVEL))
	{
		music(0);
		plat_wait_flip();
		yt_screen();
		bg_picture();
		panel_show("", "", "", col_white);
		present_view();
		plat_wait_flip();
	}
	const PlayerStatus keep = ps;
	load_level(DEMO_LEVEL);
	world_start(-1);
	ps = keep;
	demo_timer = 0;
	demo_walking = 1;
	music_want(ltrack);
	step_clock_reset();
}

void demo_run(void)
{
	const PlayerStatus keep = ps;      /* the demo's coins and lives are not the player's */
	const int steps = step_clock();
	for (int s = 0; s < steps; s++)
	{
		tux.in_old_fire = tux.in_fire;
		tux.in_right = 1; tux.in_left = 0; tux.in_fire = 0; tux.in_down = 0;
		if (demo_timer > 0)
		{
			demo_timer--;
			tux.in_up = !demo_walking;
		}
		else
		{
			demo_timer = (int)STEPS(rnd() % 3000 + 3000);
			demo_walking = !demo_walking;
		}
		/* back to the start at the level's end (or when he fell) */
		if (lw * 32 - 320 < FX_INT(tux.x) || tux_is_dead())
		{
			tux_init(lstart_x, lstart_y);
			scroll_x = 0;
			camera_y(1);
		}
		tux.can_jump = 1;
		const fx last = tux.x;
		world_action();
		if (last == tux.x) demo_walking = 0;
		game_ms += STEP_MS;
	}
	global_frame = game_ms / 25;
	ps = keep;
	game_draw_world();
}
