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
        free(t->s); free(t->z); free(t->zf); free(t->lt); free(t->lt_tmp);
        free(t->col_sb); free(t->col_cb); free(t->col_se); free(t->col_ce); free(t->row_se); free(t->row_ce);
        t->s = malloc(sizeof(u32) * n);
        t->z = malloc(sizeof(float) * n);
        t->zf = malloc(sizeof(float) * n);
        t->lt = malloc(sizeof(u16) * n);
        t->lt_tmp = malloc(n);
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
    p->fog = false;
    p->n = 0;
    p->zmode = Z_NONE;
    p->ztest = false;
    p->zfar_write = p->zfar_test = false;
    return p;
}

static inline u32 fog_at(float d)
{
    float i = d * enh_fog_scale;
    return (u32)enh_fog_lut[i < (float)(ENH_FOG_STEPS - 1) ? (int)i : ENH_FOG_STEPS - 1] << 24;
}

/* ENH: a light (the headlight beams, OR faces): instead of ORing its bits into the colours of the samples it
 * covers, it marks them lit in the light buffer; a beam is several polygons, so their union is softened as a
 * whole afterwards (soften_lights) and the composition blends the lit colours in by the amount. */
static void raster_light(const EnhTarget *t, const EnhPrim *p, int y0, int y1)
{
    u16 lit = (u16)(0xFF | (p->value & 0xFF) << 8);
    float ymin = p->y[0], ymax = p->y[0];
    for (int i = 1; i < p->n; i++) {
        if (p->y[i] < ymin) ymin = p->y[i];
        if (p->y[i] > ymax) ymax = p->y[i];
    }
    int r0 = (int)ceilf(ymin - 0.5f), r1 = (int)ceilf(ymax - 0.5f);
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
        if (!t->use_lt) {                                    /* the original's beam: its bits ORed in */
            u32 *row = t->s + (size_t)r * t->w;
            for (int c = c0; c < c1; c++) row[c] |= p->value & 0xFFFF;
            continue;
        }
        u16 *lt = t->lt + (size_t)r * t->w;
        for (int c = c0; c < c1; c++) lt[c] = (u16)((lt[c] & ~LT_BLOCKED) | lit);   /* lit again over what blocked an earlier light */
    }
}

/* what a primitive drawn over the samples leaves in the light buffer: LT_BLOCKED after the first light, else
 * nothing (0: the buffer is still clear there, or not in use) */
static inline u16 lt_clear(const EnhTarget *t, const EnhPrim *p)
{
    return t->use_lt && (int)(p - t->prim) > t->first_light ? LT_BLOCKED : 0;
}
#define LT_OVER(l, lc) do { if (lc) (l) = (u16)(((l) & 0xFF) | (lc)); } while (0)

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
        float *z = t->z + (size_t)r * t->w, *zf = t->zf + (size_t)r * t->w;
        u16 *lt = t->lt + (size_t)r * t->w, lc = lt_clear(t, p);
        if (p->zmode == Z_CLEAR) {
            for (int c = c0; c < c1; c++) { row[c] = p->value; z[c] = zf[c] = FLT_MAX; LT_OVER(lt[c], lc); }
            continue;
        }
        bool test = p->zfar_test && p->zmode != Z_NONE;
        bool fog = p->fog && p->zmode == Z_PLANE;               /* ENH: the haze of each sample by its distance */
        u32 v0 = fog ? p->value & 0xFFFFFFu : p->value;
        for (int c = c0; c < c1; c++) {
            float d = p->zmode == Z_PLANE ? plane_depth(t, p, c, r) : p->zc;
            if (test && d > zf[c] * 1.01f) continue;            /* the far ring is nearer here */
            row[c] = fog && d < FLT_MAX ? v0 | fog_at(d) : p->value;
            LT_OVER(lt[c], lc);
            if (p->zmode == Z_NONE) continue;
            z[c] = d;
            if (p->zfar_write) zf[c] = d;
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
        u16 *lt = t->lt + (size_t)r * t->w, lc = lt_clear(t, p);
        const float *z = t->z + (size_t)r * t->w;
        for (int c = c0; c < c1; c++) {
            if (p->ztest && !(p->zval < z[c])) continue;              /* behind what is there */
            int i = (int)(((float)c + 0.5f - p->sx0) * fx);
            if (i < 0) i = 0;
            if (i >= img->w) i = img->w - 1;
            u8 v = src[i];
            if (v) { row[c] = ENH_SOLID(v) | p->value; LT_OVER(lt[c], lc); }
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
        u16 *lt = t->lt + (size_t)r * t->w, lc = lt_clear(t, p);
        for (int c = c0; c < c1; c++) if (c < k0 || c >= k1) { row[c] = p->value; LT_OVER(lt[c], lc); }
    }
}

static void raster_blocks(const EnhTarget *t, const EnhPrim *p, const EnhSnap *snap, int y0, int y1)
{
    int k = t->k, wpx = t->w / k;
    u16 lc = lt_clear(t, p);
    for (int n = p->first; n < p->first + p->count; n++) {
        u32 e = snap->ovpix[n];
        int off = (int)(e >> 8), c = off % 320, r = off / 320;
        if (c >= wpx) continue;
        int ra = r * k, rb = ra + k;
        if (ra < y0) ra = y0;
        if (rb > y1) rb = y1;
        for (int y = ra; y < rb; y++) {
            u32 *row = t->s + (size_t)y * t->w + (size_t)c * k;
            u16 *lt = t->lt + (size_t)y * t->w + (size_t)c * k;
            for (int x = 0; x < k; x++) { row[x] = ENH_SOLID(e & 0xFF); LT_OVER(lt[x], lc); }
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
        float *zf = t->zf + (size_t)r * t->w;
        for (int c = 0; c < t->w; c++) z[c] = zf[c] = FLT_MAX;          /* nothing there yet */
        if (t->use_lt) memset(t->lt + (size_t)r * t->w, 0, sizeof(u16) * (size_t)t->w);
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
        case P_POLY:   if (p->or_mode) raster_light(t, p, y0, y1); else raster_poly(t, p, y0, y1); break;
        case P_SPRITE: raster_sprite(t, p, y0, y1); break;
        case P_BLOCKS: raster_blocks(t, p, rc->snap, y0, y1); break;
        case P_SKY:    raster_sky(t, p, y0, y1); break;
        case P_ELLIPSE: raster_ellipse(t, p, y0, y1); break;
        }
    }
}

/* ENH: the lights' soft rim. The lit amounts (0 or 255, the union of the beams' polygons) are box-filtered
 * over --beam-soft view pixels each way, along the rows and then the columns, and eased (smoothstep): the
 * edge fades out over twice that width, half inside the beam, half outside, and the polygons' shared edges
 * leave no trace. Samples drawn over after the lights (the dashboard) keep the amount below them for the filter, so the beam does not fade along them, but are not lit. */
static void soften_rows(int i, void *vctx)
{
    const EnhTarget *t = vctx;
    int w = t->w, R = t->light_soft * t->k, r = t->lt_y0 + i, x0 = t->lt_x0, x1 = t->lt_x1;
    const u16 *lt = t->lt + (size_t)r * w;
    u8 *o = t->lt_tmp + (size_t)r * w;
    int sum = 0;
    for (int c = x0 - R; c < x1 + R; c++) {
        int in = c + R, out = c - R - 1;
        if (in >= 0 && in < w) sum += lt[in] & 0xFF;
        if (out >= 0 && out < w) sum -= lt[out] & 0xFF;
        if (c >= x0 && c < x1) o[c] = (u8)(sum / (2 * R + 1));
    }
}

static void soften_cols(int band, void *vctx)
{
    EnhTarget *t = vctx;
    int w = t->w, R = t->light_soft * t->k, y0 = t->lt_y0, y1 = t->lt_y1;
    int c0 = t->lt_x0 + band * 64, c1 = c0 + 64 < t->lt_x1 ? c0 + 64 : t->lt_x1;
    for (int c = c0; c < c1; c++) {
        int sum = 0;
        for (int r = y0 - R; r < y1 + R; r++) {
            int in = r + R, out = r - R - 1;
            if (in >= y0 && in < y1) sum += t->lt_tmp[(size_t)in * w + c];
            if (out >= y0 && out < y1) sum -= t->lt_tmp[(size_t)out * w + c];
            if (r < y0 || r >= y1) continue;
            u16 *l = &t->lt[(size_t)r * w + c];
            if (*l & LT_BLOCKED) { *l = 0; continue; }
            float u = (float)sum / (float)(2 * R + 1) / 255.0f;
            u32 a = (u32)(u * u * (3 - 2 * u) * (float)t->light_max + 0.5f);
            *l = a ? (u16)(a | t->light_bits) : 0;
        }
    }
}

void enh_target_raster(EnhTarget *t, const EnhSnap *snap)
{
    if (!t->s || t->h <= 0) return;
    target_rays(t);
    /* the lights, and the samples their soft rims can reach (the softening's area) */
    t->first_light = t->nprim;
    t->light_bits = 0;
    float lx0 = INFINITY, ly0 = INFINITY, lx1 = -INFINITY, ly1 = -INFINITY;
    for (int i = 0; i < t->nprim; i++) {
        const EnhPrim *p = &t->prim[i];
        if (p->kind != P_POLY || !p->or_mode) continue;
        if (t->first_light == t->nprim) t->first_light = i;
        t->light_bits |= (u16)((p->value & 0xFF) << 8);
        for (int k = 0; k < p->n; k++) {
            lx0 = fminf(lx0, p->x[k]); lx1 = fmaxf(lx1, p->x[k]);
            ly0 = fminf(ly0, p->y[k]); ly1 = fmaxf(ly1, p->y[k]);
        }
    }
    t->use_lt = enh_lights && t->first_light < t->nprim;
    if (t->use_lt) {
        int R = t->light_soft * t->k;
        t->lt_x0 = (int)floorf(lx0) - R; t->lt_x1 = (int)ceilf(lx1) + R + 1;
        t->lt_y0 = (int)floorf(ly0) - R; t->lt_y1 = (int)ceilf(ly1) + R + 1;
        if (t->lt_x0 < 0) t->lt_x0 = 0;
        if (t->lt_y0 < 0) t->lt_y0 = 0;
        if (t->lt_x1 > t->w) t->lt_x1 = t->w;
        if (t->lt_y1 > t->h) t->lt_y1 = t->h;
        if (t->lt_x1 <= t->lt_x0 || t->lt_y1 <= t->lt_y0) t->lt_x1 = t->lt_x0, t->lt_y1 = t->lt_y0;
    }
    RasterCtx rc = { t, snap, 16 };
    host_parallel_for((t->h + rc.band - 1) / rc.band, raster_band, &rc);
    if (t->use_lt && t->lt_y1 > t->lt_y0) {
        host_parallel_for(t->lt_y1 - t->lt_y0, soften_rows, t);
        host_parallel_for((t->lt_x1 - t->lt_x0 + 63) / 64, soften_cols, t);
    }
}
