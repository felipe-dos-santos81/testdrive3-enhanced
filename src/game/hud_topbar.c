/* HUD top bar (hud.md §4.9): race clock, compass window, radar detector. Near routines of segment
 * 0e12 called from frame_update; they draw on the current page (page 0 at that point). */
#include "game/game.h"
#include "host.h"
#include "portcfg.h"

#include <limits.h>
#include <math.h>

/* Scratch bytes the original uses for its divisions / loop counters (render3d names, other uses). */
#define SCR_A DSB(DS_place_rot)       /* DS:946A */
#define SCR_B DSB(DS_place_nverts)    /* DS:946B */

/* One 4x5 top-bar digit (digit font DS:B75B, 5 bytes each) at the current pen. */
static void topbar_digit(u8 d)
{
    gfx_draw_bitmap((u16)(DS_digit_font + (u8)(d * 5)), 1, 5);
}

/* 0e12:0b1d race_clock_hud — hud.md §4.9 (topbar_update) */
void race_clock_hud(void)
{
    /* ENH: the clock counts real time (timer ticks) instead of frames. The original advances it one second
     * every 5 frames, its design rate; at a faster game speed (--frame-ticks below 29) the whole game runs
     * faster, and the clock and race times stay in real seconds. clock_frames / clock_sub keep their meaning
     * (fifths of the second, and the digit derived from them). A frame counts at most two frames' worth of
     * ticks, so a message box or pause does not advance the clock (the original's frames stop then too). */
    static u32 acc_mticks;                                /* host-side: thousandths of a tick this second */
    const u32 sec_mticks = 145652;                        /* 1193182 / 8192 ticks a second, x 1000 */
    if (DSW(DS_race_state) != 1 || DSB(DS_ext_view) != 0) {
        DSB(DS_clock_frames) = 0;
        acc_mticks = 0;
        return;
    }
    if (DSB(DS_clock_running) != 0) {
        u32 t = DSB(DS_frame_ticks), cap = 2u * (u32)host_frame_ticks();
        acc_mticks += (t < cap ? t : cap) * 1000u;
        if (acc_mticks < sec_mticks) {
            u8 f = (u8)(acc_mticks * 5u / sec_mticks);
            DSB(DS_clock_frames) = f;
            DSB(DS_clock_sub) = (u8)((u8)(f + (f >> 1)) >> 1);
            goto draw;
        }
        acc_mticks -= sec_mticks;
        if (acc_mticks >= sec_mticks) acc_mticks = 0;
        DSB(DS_clock_sec)++;
        DSB(DS_clock_sub) = 0;
    }
    DSB(DS_clock_frames) = 0;
    if (DSB(DS_clock_sec) >= 0x3C) {
        DSB(DS_clock_sec) = 0;
        DSB(DS_clock_min)++;
        if (DSB(DS_clock_min) >= 0x3C) DSB(DS_clock_min)--;   /* stops at 59 */
    }
draw:
    clock_draw();
    compass_draw();
    radar_detector();
}

/* 0e12:0b85 clock_draw — hud.md §4.9 */
void clock_draw(void)
{
    u8 s = DSB(DS_clock_sec);
    if (s == DSB(DS_clock_drawn)) return;
    DSB(DS_clock_drawn) = s;
    u16 qr = div16_8(s, 10);
    SCR_A = (u8)qr;
    SCR_B = (u8)(qr >> 8);
    gfx_set_colour(0);
    gfx_fill_rect(0x35, 0x3E, 8, 13);
    gfx_fill_rect(0x29, 0x32, 8, 13);                 /* y0, y1 re-used from the stack */
    gfx_set_colour(DSB(DS_car_colours + 9));          /* DS:CEB7 */
    gfx_move_to(0x35, 13);
    topbar_digit(SCR_A);
    gfx_move_to(0x3A, 13);
    topbar_digit(SCR_B);
    u8 m = DSB(DS_clock_min);
    if (m == 0) return;
    qr = div16_8(m, 10);
    SCR_A = (u8)qr;
    SCR_B = (u8)(qr >> 8);
    gfx_move_to(0x29, 13);
    if ((u8)(SCR_A << 1) != 0) topbar_digit(SCR_A);   /* no leading zero */
    gfx_move_to(0x2E, 13);
    topbar_digit(SCR_B);
}

/* ENH: the leg's finish is the one cell whose tile has a face of type 1Fh (the gas station, world.md). The tile
 * models as model_place finds them: types 40h.. in the scene's set, the rest shared; an entry <= 10h is the
 * previous model without its last faces. */
static bool tile_is_finish(u8 type)
{
    u16 es, set;
    if (type >= 0x40) { es = DSW(DS_tiles_scene + 2); set = DSW(DS_tiles_scene); type = (u8)(type - 0x40); }
    else              { es = DSW(DS_tiles_shared + 2); set = DSW(DS_tiles_shared); }
    if (es == 0) return false;
    u16 bx = (u16)(set + 2 * type), cut = 0, e = rd16(es, bx);
    if (e <= 0x10) { cut = e; bx = (u16)(bx - 2); }
    u16 si = (u16)(rd16(es, bx) + set);
    int nf = rd8(es, si) - cut, nv = rd8(es, (u16)(si + 1));
    u16 f = (u16)(si + 4 + nv * 6);
    for (int k = 0; k < nf; k++, f = (u16)(f + 8))
        if ((rd16(es, (u16)(f + 6)) >> 11) == 0x1F) return true;
    return false;
}

/* the finish cell (0..511), -1 = none (the demo leg); remembered while that cell still holds the finish */
static int finish_cell(void)
{
    static int cell = -1;
    if (cell >= 0 && tile_is_finish((u8)DSW(DS_leg_map + 2 * cell))) return cell;
    cell = -1;
    for (int i = 0; i < 512; i++)
        if (tile_is_finish((u8)DSW(DS_leg_map + 2 * i))) { cell = i; break; }
    return cell;
}

/* ENH: the finish's direction relative to the heading, in compass pixels (128 a turn, + = to the right);
 * INT_MIN without a finish or with the marker off */
static int finish_offset(void)
{
    if (!portcfg.finish_marker) return INT_MIN;
    int c = finish_cell();
    if (c < 0) return INT_MIN;
    double fx = (c & 31) * 0x400 + 0x200, fz = (15 - (c >> 5)) * 0x400 + 0x200;   /* cell centre */
    double dx = fx - DSW(DS_obj_x), dz = fz - DSW(DS_obj_z);
    double b = atan2(dx, dz) * (65536.0 / (2.0 * M_PI)) - DSW(DS_cam_heading);    /* forward = (sin, cos) */
    b = fmod(b, 65536.0);
    if (b >= 32768.0) b -= 65536.0;
    if (b < -32768.0) b += 65536.0;
    return (int)lround(b / 512.0);
}

/* ENH: the marker in the compass window (x 8..31, y 7..14): a small upward triangle at the bottom at the
 * finish's bearing, or an arrow at the window's edge when the finish lies outside it */
static void finish_marker_draw(int off)
{
    int x = 20 + off;                                  /* the heading is under the window's centre */
    gfx_set_colour(0x0A);                              /* light green */
    if (x >= 9 && x <= 30) {
        gfx_fill_rect((s16)(x - 1), (s16)(x + 1), 14, 14);
        gfx_fill_rect((s16)x, (s16)x, 13, 13);
    } else if (x < 9) {
        gfx_fill_rect(8, 8, 11, 11);
        gfx_fill_rect(9, 9, 10, 12);
    } else {
        gfx_fill_rect(31, 31, 11, 11);
        gfx_fill_rect(30, 30, 10, 12);
    }
}

/* 0e12:0cbe compass_draw — hud.md §4.9 */
void compass_draw(void)
{
    if (DSB(DS_debug_keys) == 0) {
        u8 x = (u8)((u8)((DSW(DS_cam_heading) >> 8) + DSB(DS_leg_compass_offset)) >> 1);
        u16 mode = DSW(DS_video_mode);
        if ((u8)mode != 0x13) {                        /* EGA / Tandy byte alignment (not reached) */
            x &= 0xFE;
            if ((u8)mode != 9) x &= 0xF8;
        }
        /* ENH: redrawn also when the finish marker moves */
        static int marker_drawn = INT_MIN;
        int off = finish_offset();
        if (off != INT_MIN && off < -12) off = -12;    /* all positions outside the window look alike */
        if (off != INT_MIN && off > 11) off = 11;
        if (x == DSB(DS_compass_drawn) && off == marker_drawn) return;
        DSB(DS_compass_drawn) = x;
        marker_drawn = off;
        gfx_copy_rect(x, (s16)(x + 0x17), 0, 7, 8, 14, 1, 0);   /* page-1 strip -> screen */
        if (off != INT_MIN) finish_marker_draw(off);
        return;
    }
    gfx_set_colour(0);
    gfx_fill_rect(10, 0x13, 8, 13);
    gfx_set_colour(DSB(DS_car_colours + 8));          /* DS:CEB6 */
    u16 qr = div16_8(DSB(DS_frame_ticks), 10);
    SCR_A = (u8)qr;
    SCR_B = (u8)(qr >> 8);
    gfx_move_to(10, 13);
    topbar_digit(SCR_A);
    gfx_move_to(15, 13);
    topbar_digit(SCR_B);
}

/* 0e12:0db4 radar_detector — hud.md §4.9 (radar_draw) */
void radar_detector(void)
{
    DSB(DS_radar_phase)++;
    u8 ph = DSB(DS_radar_phase);
    u8 lvl = 0xFF;                                    /* blank phase */
    if (!(ph & 8)) {
        lvl = (u8)(DSB(DS_radar_level) >> 4);
        if (!(ph & 7) && lvl != 0) sfx_play_ax(0x0E); /* blip every 16 frames */
    }
    if (lvl == DSB(DS_radar_drawn)) return;
    DSB(DS_radar_drawn) = lvl;
    if ((s8)lvl < 0) {
        gfx_set_colour(0);
        gfx_fill_rect(0x120, 0x137, 9, 11);
        return;
    }
    SCR_A = lvl;
    gfx_set_colour(DSB(DS_car_colours + 0));          /* DS:CEAE power light */
    gfx_fill_rect(0x11C, 0x11F, 9, 10);
    gfx_set_colour(DSB(DS_car_colours + 1));          /* DS:CEAF bars */
    DSW(DS_vert_base) = 0x127;                        /* DS:945E used as the bar x */
    while ((s8)--SCR_A >= 0) {
        gfx_fill_rect((s16)(DSW(DS_vert_base) - 1), (s16)DSW(DS_vert_base), 9, 10);
        DSW(DS_vert_base) = (u16)(DSW(DS_vert_base) + 3);
    }
}
