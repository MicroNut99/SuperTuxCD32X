/*
 * SuperTux CD32X - the 32X side of Vic's tiler: the two registers it needs, and what YATSSD's
 * hw_32x.c would have provided (hw_32x.c itself is not linked: it sets up the VDP, the line
 * table, the interrupts and the 68000 link its own way, which is the boot path's job here).
 */
#include "yt.h"
#include "32x.h"            /* yatssd/32x.h: MARS_FRAMEBUFFER, MARS_SYS_COMM6 */
#include "plat.h"

int16_t *yt_extrafb(void)
{
	/* exactly the expression in dtiles.c (draw_dirtyrect, draw_handle_layercmd) */
	return (int16_t *)&MARS_FRAMEBUFFER + 0x100 + ((YT_PITCH * YT_YAW) >> 1);
}

void yt_tile_counter_reset(void)
{
	MARS_SYS_COMM6 = 0;        /* draw_tile_layer: "MARS_SYS_COMM6 = 0" before the layer */
}

/* dtiles.c's init_tilemap() and draw_tile_layer() / draw_tilemap() refer to these YATSSD
 * hw_32x.c functions. They are not called in this port (game/yt_layers.c replaces those three
 * functions); if one ever were, the error screen names it instead of the program running on. */
void Hw32xUpdateLineTable(int hscroll, int vscroll, int lineskip)
{ (void)hscroll; (void)vscroll; (void)lineskip; plat_fatal("YATSSD HW32XUPDATELINETABLE: NOT USED HERE"); }
void Hw32xSetFGOverlayPriorityBit(int p) { (void)p; plat_fatal("YATSSD HW32XSETFGOVERLAYPRIORITYBIT: NOT USED HERE"); }
void Hw32xSetBGOverlayPriorityBit(int p) { (void)p; plat_fatal("YATSSD HW32XSETBGOVERLAYPRIORITYBIT: NOT USED HERE"); }
void HwMdSetPlaneBitmap(char plane, void *data) { (void)plane; (void)data; plat_fatal("YATSSD HWMDSETPLANEBITMAP: NOT USED HERE"); }
void HwMdClearPlanes(void) { plat_fatal("YATSSD HWMDCLEARPLANES: NOT USED HERE"); }
void HwMdHScrollPlane(char plane, int s) { (void)plane; (void)s; plat_fatal("YATSSD HWMDHSCROLLPLANE: NOT USED HERE"); }
void HwMdVScrollPlane(char plane, int s) { (void)plane; (void)s; plat_fatal("YATSSD HWMDVSCROLLPLANE: NOT USED HERE"); }
