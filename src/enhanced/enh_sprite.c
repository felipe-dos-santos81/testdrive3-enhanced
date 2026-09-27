/* Enhanced renderer: sprites (ENHANCED.md "Sprites"). The original draws pre-scaled copies of each sprite
 * (sprites_prescale, render3d.md §4.13) chosen by the apparent size in steps; here every sprite is drawn
 * from its native image at a size that follows the apparent size continuously, through the sizes of the
 * original's copies (so a sprite is as large as the original draws it at each step, and grows smoothly in
 * between). */
#include "enh_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_IDS   32
#define MAX_STEPS 17

typedef struct {
    EnhSprImg img;
    int nsteps;
    u8 w[MAX_STEPS], h[MAX_STEPS];    /* size of the copy for each step */
} SprInfo;

static SprInfo spr[MAX_IDS];
static u8 kind[MAX_IDS];
static u8 size_to_step[0x31];
static u8 max_size;

void enh_sprites_loaded(void)
{
    if (!enh_enabled()) return;
    enh_sprites_capture();
}

void enh_sprites_capture(void)
{
    for (int i = 0; i < MAX_IDS; i++) {
        free(spr[i].img.pix);
        memset(&spr[i], 0, sizeof spr[i]);
        kind[i] = DSB(DS_sprite_kind + i);
    }
    for (int i = 0; i < 0x31; i++) size_to_step[i] = DSB(DS_size_to_step + i);
    max_size = DSB(DS_sprite_max_size);
    int nsteps = DSB(DS_sprite_scale_count);
    if (nsteps > MAX_STEPS) nsteps = MAX_STEPS;
    int count = DSW(DS_spr_count);
    if (count > MAX_IDS) count = MAX_IDS;
    u16 cseg = DSW(DS_sprite_cache_seg);

    for (int id = 1; id < count; id++) {
        SprInfo *e = &spr[id];
        u16 rec = (u16)(0x2500 + 8 * id);
        int w = DSB(rec), h = DSB(rec + 1);
        u8 mode = kind[id] & 7;
        if (mode >= 2 || w == 0 || h == 0) continue;
        /* native image, rows centred as sprite_scale_row pads them */
        u8 *pix = calloc((size_t)w * h, 1);
        if (!pix) continue;
        u16 ptrs = (u16)(0x2500 + DSW(rec + 2)), lens = (u16)(0x2500 + DSW(rec + 4));
        for (int j = 0; j < h; j++) {
            u16 src = (u16)(0x2500 + DSW((u16)(ptrs + 2 * j)));
            int m = DSB((u16)(lens + 2 * j)) & 0x3F;
            int pad = (u8)(w - m) >> 1;
            for (int i = 0; i < m && pad + i < w; i++) pix[j * w + pad + i] = DSB((u16)(src + i));
        }
        e->img.w = w;
        e->img.h = h;
        e->img.pix = pix;
        /* sizes of the cached copies: dir[id] -> step table -> copy {h, W, H} */
        u16 dir = rd16(cseg, (u16)(2 * id));
        e->nsteps = nsteps;
        for (int k = 0; k < nsteps; k++) {
            u16 copy = rd16(cseg, (u16)(dir + 2 * k));
            e->w[k] = rd8(cseg, (u16)(copy + 1));
            e->h[k] = rd8(cseg, (u16)(copy + 2));
        }
    }
}

static void dims_at(const SprInfo *e, int a, double *w, double *h)
{
    if (a > max_size) a = max_size;
    if (a < 0) a = 0;
    int k = size_to_step[a < 0x31 ? a : 0x30];
    if (k >= e->nsteps) k = e->nsteps - 1;
    *w = e->w[k];
    *h = e->h[k];
}

bool enh_sprite_pick(u8 s, double a, const EnhSprImg **img, double *w, double *h)
{
    s &= 0x3F;
    if (s >= MAX_IDS) return false;
    u8 mode = kind[s] & 7;
    int sid = mode < 2 ? s : mode == 2 ? s - 1 : s - 2;
    if (sid <= 0 || sid >= MAX_IDS) return false;
    const SprInfo *e = &spr[sid];
    if (!e->img.pix || e->nsteps == 0) return false;
    *img = &e->img;
    if (mode == 1) {                                   /* fixed size: the 1:1 copy at every distance */
        *w = e->w[0];
        *h = e->h[0];
    } else {
        if (a < 0) a = 0;
        int a0 = (int)floor(a);
        double f = a - a0, w0, h0, w1, h1;
        dims_at(e, a0, &w0, &h0);
        dims_at(e, a0 + 1, &w1, &h1);
        *w = w0 + (w1 - w0) * f;
        *h = h0 + (h1 - h0) * f;
        if (a < 1.0) { *w = w1 * a; *h = h1 * a; }    /* ENH: beyond the game's range, in proportion (far ring) */
    }
    return *w > 0.0 && *h > 0.0;
}
