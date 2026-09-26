/* Enhanced renderer: draw lists and their rasterisation into sample buffers (ENHANCED.md "Rasterising").
 * A draw list is filled back to front by enh_scene.c; the buffer is then drawn in horizontal bands on the
 * host's worker threads, every band walking the whole list (painter's algorithm, as the original). */
#include "enh_internal.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../host.h"

EnhTarget enh_front, enh_mirror;

void enh_target_reset(EnhTarget *t, int w_px, int h_px, int k)
{
    int w = w_px * k, h = h_px * k;
    if (w != t->w || h != t->h || !t->s) {
        size_t n = (size_t)(w > 0 ? w : 1) * (size_t)(h > 0 ? h : 1), wc = (size_t)(w > 0 ? w : 1),
               hc = (size_t)(h > 0 ? h : 1);
        free(t->s); free(t->z);
        free(t->col_sb); free(t->col_cb); free(t->col_se); free(t->col_ce); free(t->row_se); free(t->row_ce);
        t->s = malloc(sizeof(u32) * n);
        t->z = malloc(sizeof(float) * n);
        t->col_sb = malloc(sizeof(float) * wc); t->col_cb = malloc(sizeof(float) * wc);
        t->col_se = malloc(sizeof(float) * wc); t->col_ce = malloc(sizeof(float) * wc);
        t->row_se = malloc(sizeof(float) * hc); t->row_ce = malloc(sizeof(float) * hc);
        t->w = w;
        t->h = h;
    }
    t->k = k;
    t->nprim = 0;
}

/* the lines of sight of the target's columns and rows (the inverse of the projection, EnhMap) */
static void target_rays(EnhTarget *t)
{
    const EnhMap *m = &t->map;
    const double rad2a = 65536.0 / (2.0 * M_PI), rad2px = 1024.0 / M_PI;
    for (int c = 0; c < t->w; c++) {
        double x32 = (c + 0.5) * 32.0 / t->k, sx, e;
        if (!m->mirror) {
            sx = x32;
            e = m->roll_slope * (sx - m->cx32) / rad2px;
        } else {
            sx = m->mirror_base + 0x1600 - 2.0 * x32;
            double d = fmod(sx - 0x9400, 65536.0);
            if (d >= 32768.0) d -= 65536.0;
            if (d < -32768.0) d += 65536.0;
            e = -m->mroll * d / rad2px;
        }
        double b = (sx - m->base) / rad2a;
        t->col_sb[c] = (float)sin(b); t->col_cb[c] = (float)cos(b);
        t->col_se[c] = (float)sin(e); t->col_ce[c] = (float)cos(e);
    }
    for (int r = 0; r < t->h; r++) {
        double y = (r + 0.5) / t->k;
        double e = !m->mirror ? (m->cam_row - y) / rad2px : -((y - 10.0) * 2.0 + m->pitch) / rad2px;
        t->row_se[r] = (float)sin(e); t->row_ce[r] = (float)cos(e);
    }
}

/* the distance along the line of sight of sample (c, r) to the plane of p; FLT_MAX when it does not meet it */
static inline float plane_depth(const EnhTarget *t, const EnhPrim *p, int c, int r)
{
    float ce = t->row_ce[r] * t->col_ce[c] - t->row_se[r] * t->col_se[c];
    float se = t->row_se[r] * t->col_ce[c] + t->row_ce[r] * t->col_se[c];
    float den = p->nx * t->col_sb[c] * ce + p->ny * se + p->nz * t->col_cb[c] * ce;
    if (den > -1e-7f && den < 1e-7f) return FLT_MAX;
    float d = p->d / den;
    return d > 0 ? d : FLT_MAX;
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
    p->zmode = Z_NONE;
    p->ztest = false;
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
        float *z = t->z + (size_t)r * t->w;
        switch (p->zmode) {
        case Z_CLEAR: for (int c = c0; c < c1; c++) z[c] = FLT_MAX; break;
        case Z_CONST: for (int c = c0; c < c1; c++) z[c] = p->zc; break;
        case Z_PLANE: for (int c = c0; c < c1; c++) z[c] = plane_depth(t, p, c, r); break;
        default: break;
        }
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
        const float *z = t->z + (size_t)r * t->w;
        for (int c = c0; c < c1; c++) {
            if (p->ztest && !(p->zval < z[c])) continue;              /* behind what is there */
            int i = (int)(((float)c + 0.5f - p->sx0) * fx);
            if (i < 0) i = 0;
            if (i >= img->w) i = img->w - 1;
            u8 v = src[i];
            if (v) row[c] = ENH_SOLID(v) | p->value;
        }
    }
}

/* the columns [*c0, *c1) of row r whose sample centres are inside the ellipse inscribed in (x0, y0) - (x1, y1) */
static bool ellipse_span(float x0, float y0, float x1, float y1, int r, int *c0, int *c1)
{
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, rx = (x1 - x0) * 0.5f, ry = (y1 - y0) * 0.5f;
    if (rx <= 0 || ry <= 0) return false;
    float dy = ((float)r + 0.5f - cy) / ry;
    if (dy * dy >= 1.0f) return false;
    float hw = rx * sqrtf(1.0f - dy * dy);
    *c0 = (int)ceilf(cx - hw - 0.5f);
    *c1 = (int)ceilf(cx + hw - 0.5f);
    return *c0 < *c1;
}

/* the ellipse inscribed in the rectangle (sx0, sy0) - (sx1, sy1) less the cut one, coverage at sample centres */
static void raster_ellipse(const EnhTarget *t, const EnhPrim *p, int y0, int y1)
{
    int r0 = (int)ceilf(p->sy0 - 0.5f), r1 = (int)ceilf(p->sy1 - 0.5f);
    if (r0 < y0) r0 = y0;
    if (r1 > y1) r1 = y1;
    for (int r = r0; r < r1; r++) {
        int c0, c1, k0 = 0, k1 = 0;
        if (!ellipse_span(p->sx0, p->sy0, p->sx1, p->sy1, r, &c0, &c1)) continue;
        if (p->cut && !ellipse_span(p->kx0, p->ky0, p->kx1, p->ky1, r, &k0, &k1)) k0 = k1 = 0;
        if (c0 < 0) c0 = 0;
        if (c1 > t->w) c1 = t->w;
        u32 *row = t->s + (size_t)r * t->w;
        for (int c = c0; c < c1; c++) if (c < k0 || c >= k1) row[c] = p->value;
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

/* the sky: flat, or the original's five bands of four rows above the horizon blended into a gradient; the
 * ground below the horizon, hazed towards the horizon by its distance */
static void raster_sky(const EnhTarget *t, const EnhPrim *p, int y0, int y1)
{
    float k = (float)t->k;
    for (int r = y0; r < y1; r++) {
        u32 *row = t->s + (size_t)r * t->w;
        float *z = t->z + (size_t)r * t->w;
        float yc = (float)r + 0.5f;
        for (int c = 0; c < t->w; c++) z[c] = FLT_MAX;                  /* nothing there yet */
        for (int c = 0; c < t->w; c++) {
            float d = (p->hy0 + p->hslope * ((float)c + 0.5f) - yc) / k;   /* rows above the horizon */
            if (d <= 0.0f) {                                                /* the ground */
                u32 h = 0;
                if (p->ground_haze) {
                    int i = (int)(-d * 32.0f);
                    h = p->ground_haze[i < ENH_HAZE_ROWS ? i : ENH_HAZE_ROWS - 1];
                }
                row[c] = p->ground | ENH_HAZE(h);
                continue;
            }
            if (!p->gradient) {
                row[c] = p->value;
                continue;
            }
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
        case P_ELLIPSE: raster_ellipse(t, p, y0, y1); break;
        }
    }
}

void enh_target_raster(EnhTarget *t, const EnhSnap *snap)
{
    if (!t->s || t->h <= 0) return;
    target_rays(t);
    RasterCtx rc = { t, snap, 16 };
    host_parallel_for((t->h + rc.band - 1) / rc.band, raster_band, &rc);
}
