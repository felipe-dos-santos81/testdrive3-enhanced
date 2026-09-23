/* Enhanced renderer: draw lists and their rasterisation into sample buffers (ENHANCED.md "Rasterising").
 * A draw list is filled back to front by enh_scene.c; the buffer is then drawn in horizontal bands on the
 * host's worker threads, every band walking the whole list (painter's algorithm, as the original). */
#include "enh_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../host.h"

EnhTarget enh_front, enh_mirror;

void enh_target_reset(EnhTarget *t, int w_px, int h_px, int k)
{
    int w = w_px * k, h = h_px * k;
    if (w != t->w || h != t->h || !t->s) {
        free(t->s);
        t->s = malloc(sizeof(u32) * (size_t)(w > 0 ? w : 1) * (size_t)(h > 0 ? h : 1));
        t->w = w;
        t->h = h;
    }
    t->k = k;
    t->nprim = 0;
}

EnhPrim *enh_prim_add(EnhTarget *t, EnhPrimKind kind)
{
    if (t->nprim == t->cap) {
        int cap = t->cap ? t->cap * 2 : 4096;
        EnhPrim *p = realloc(t->prim, sizeof(EnhPrim) * (size_t)cap);
        if (!p) return NULL;
        t->prim = p;
        t->cap = cap;
    }
    EnhPrim *p = &t->prim[t->nprim++];
    p->kind = kind;
    p->or_mode = false;
    p->n = 0;
    return p;
}

static inline void put(u32 *d, u32 v, bool or_mode)
{
    if (or_mode) *d |= v & 0xFFFF;
    else *d = v;
}

static void raster_poly(const EnhTarget *t, const EnhPrim *p, int y0, int y1)
{
    float ymin = p->y[0], ymax = p->y[0];
    for (int i = 1; i < p->n; i++) {
        if (p->y[i] < ymin) ymin = p->y[i];
        if (p->y[i] > ymax) ymax = p->y[i];
    }
    int r0 = (int)ceilf(ymin - 0.5f), r1 = (int)ceilf(ymax - 0.5f);   /* rows whose centre is inside */
    if (r0 < y0) r0 = y0;
    if (r1 > y1) r1 = y1;
    for (int r = r0; r < r1; r++) {
        float yc = (float)r + 0.5f, xl = INFINITY, xr = -INFINITY;
        for (int i = 0; i < p->n; i++) {
            int j = i + 1 == p->n ? 0 : i + 1;
            float ya = p->y[i], yb = p->y[j];
            if (ya == yb) continue;
            float lo = ya < yb ? ya : yb, hi = ya < yb ? yb : ya;
            if (yc < lo || yc >= hi) continue;
            float x = p->x[i] + (yc - ya) * (p->x[j] - p->x[i]) / (yb - ya);
            if (x < xl) xl = x;
            if (x > xr) xr = x;
        }
        if (!(xl < xr)) continue;
        int c0 = (int)ceilf(xl - 0.5f), c1 = (int)ceilf(xr - 0.5f);
        if (c0 < 0) c0 = 0;
        if (c1 > t->w) c1 = t->w;
        u32 *row = t->s + (size_t)r * t->w;
        if (p->or_mode) for (int c = c0; c < c1; c++) row[c] |= p->value & 0xFFFF;
        else for (int c = c0; c < c1; c++) row[c] = p->value;
    }
}

static void raster_sprite(const EnhTarget *t, const EnhPrim *p, int y0, int y1)
{
    const EnhSprImg *img = p->img;
    float dw = p->sx1 - p->sx0, dh = p->sy1 - p->sy0;
    if (dw <= 0 || dh <= 0) return;
    int r0 = (int)ceilf(p->sy0 - 0.5f), r1 = (int)ceilf(p->sy1 - 0.5f);
    int c0 = (int)ceilf(p->sx0 - 0.5f), c1 = (int)ceilf(p->sx1 - 0.5f);
    if (r0 < y0) r0 = y0;
    if (r1 > y1) r1 = y1;
    if (c0 < 0) c0 = 0;
    if (c1 > t->w) c1 = t->w;
    float fx = (float)img->w / dw, fy = (float)img->h / dh;
    for (int r = r0; r < r1; r++) {
        int j = (int)(((float)r + 0.5f - p->sy0) * fy);
        if (j < 0) j = 0;
        if (j >= img->h) j = img->h - 1;
        const u8 *src = img->pix + (size_t)j * img->w;
        u32 *row = t->s + (size_t)r * t->w;
        for (int c = c0; c < c1; c++) {
            int i = (int)(((float)c + 0.5f - p->sx0) * fx);
            if (i < 0) i = 0;
            if (i >= img->w) i = img->w - 1;
            u8 v = src[i];
            if (v) row[c] = ENH_SOLID(v);
        }
    }
}

static void raster_blocks(const EnhTarget *t, const EnhPrim *p, const EnhSnap *snap, int y0, int y1)
{
    int k = t->k, wpx = t->w / k;
    for (int n = p->first; n < p->first + p->count; n++) {
        u32 e = snap->ovpix[n];
        int off = (int)(e >> 8), c = off % 320, r = off / 320;
        if (c >= wpx) continue;
        int ra = r * k, rb = ra + k;
        if (ra < y0) ra = y0;
        if (rb > y1) rb = y1;
        for (int y = ra; y < rb; y++) {
            u32 *row = t->s + (size_t)y * t->w + (size_t)c * k;
            for (int x = 0; x < k; x++) row[x] = ENH_SOLID(e & 0xFF);
        }
    }
}

/* the sky: flat, or the original's five bands of four rows above the horizon blended into a gradient */
static void raster_sky(const EnhTarget *t, const EnhPrim *p, int y0, int y1)
{
    float k = (float)t->k;
    for (int r = y0; r < y1; r++) {
        u32 *row = t->s + (size_t)r * t->w;
        if (!p->gradient) {
            for (int c = 0; c < t->w; c++) row[c] = p->value;
            continue;
        }
        float yc = (float)r + 0.5f;
        for (int c = 0; c < t->w; c++) {
            float d = (p->hy0 + p->hslope * ((float)c + 0.5f) - yc) / k;   /* rows above the horizon */
            float lv = (22.0f - d) * 0.25f;                                 /* band k centred 22 - 4k rows up */
            if (lv < 0.0f) lv = 0.0f;
            if (lv > 5.0f) lv = 5.0f;
            int i = (int)lv;
            if (i > 4) i = 4;
            u32 w = (u32)((lv - (float)i) * 256.0f);
            if (w > 255) w = 255;
            u32 lo = (u8)(p->sky_base + i), hi = (u8)(p->sky_base + i + 1);
            row[c] = lo | hi << 8 | w << 16;
        }
    }
}

typedef struct { EnhTarget *t; const EnhSnap *snap; int band; } RasterCtx;

static void raster_band(int b, void *vctx)
{
    const RasterCtx *rc = vctx;
    const EnhTarget *t = rc->t;
    int y0 = b * rc->band, y1 = y0 + rc->band;
    if (y1 > t->h) y1 = t->h;
    for (int i = 0; i < t->nprim; i++) {
        const EnhPrim *p = &t->prim[i];
        switch (p->kind) {
        case P_POLY:   raster_poly(t, p, y0, y1); break;
        case P_SPRITE: raster_sprite(t, p, y0, y1); break;
        case P_BLOCKS: raster_blocks(t, p, rc->snap, y0, y1); break;
        case P_SKY:    raster_sky(t, p, y0, y1); break;
        }
    }
}

void enh_target_raster(EnhTarget *t, const EnhSnap *snap)
{
    if (!t->s || t->h <= 0) return;
    RasterCtx rc = { t, snap, 16 };
    host_parallel_for((t->h + rc.band - 1) / rc.band, raster_band, &rc);
}
