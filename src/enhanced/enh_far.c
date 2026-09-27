/* Enhanced renderer: the far ring (ENHANCED.md "Draw distance"). The game builds the 3, 6 or 10 map cells nearest
 * the camera (world_build_visible, its per-octant table); here, for every game frame, the cells within
 * enh_draw_dist cells of the camera that it did not build are built as well, with their static objects, from
 * the same tile and object models: model_place / vertex_rotate / model_emit_faces read-only, into the snapshot's
 * own arrays. Nothing of the game's state is written. The faces are sorted farthest first by the original's key
 * rule at the snapshot's camera (per game frame, so coplanar faces do not swap between displayed frames). */
#include "enh_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int enh_draw_dist = ENH_DEFAULT_DRAW_DIST;

/* the cells of the game's last world build (enh_world_begin / enh_world_cell from world_build_visible) */
static bool game_cell[512];

void enh_world_begin(void) { memset(game_cell, 0, sizeof game_cell); }
void enh_world_cell(u16 c) { game_cell[c & 0x1FF] = true; }

static EnhSnap *F;

#define FAR_REACH 0x7A00                                  /* x4 units from the camera: 16-bit coordinates wrap at 8000h */

static void emit(u16 es, u16 a, int nf, int nv, u16 px, u16 pz, u16 py, u8 rot, s8 shear);

/* 0e12:7653 vertex_rotate on the model's arrays A (height), B, C at si, stride 2 nv */
static void rotate(u16 es, u16 si, u16 stride, u8 rot, s8 shear, s16 *a_out, s16 *b_out, s16 *c_out)
{
    s16 dx = (s16)rd16(es, si), ax = (s16)rd16(es, (u16)(si + stride)), cx = (s16)rd16(es, (u16)(si + 2 * stride));
    if (shear) {
        u8 n = (u8)(shear < 0 ? -shear : shear);
        s16 v = (s16)(ax << 1), bp = n >= 15 ? (s16)(v < 0 ? -1 : 0) : (s16)(v >> n);
        if (shear >= 0) bp = (s16)-bp;
        dx = (s16)(dx + bp);
    }
    u8 r = rot;
    if (r & 0x3F) {
        u16 i2 = (u16)((r & 0x3F) * 2);
        s16 sn = DSS(DS_sin64 + i2), cs = DSS(DS_cos64 + i2);
        s16 nb = (s16)((s16)(((s32)ax * cs) >> 15) + (s16)(((s32)cx * sn) >> 15));
        s16 nc = (s16)((s16)-(s16)(((s32)ax * sn) >> 15) + (s16)(((s32)cx * cs) >> 15));
        ax = nb; cx = nc;
        r &= 0xC0;
    }
    if (r >= 0x40) {
        if (r == 0x40)      { s16 t = ax; ax = cx; cx = (s16)-t; }
        else if (r == 0x80) { ax = (s16)-ax; cx = (s16)-cx; }
        else                { s16 t = ax; ax = (s16)-cx; cx = t; }
    }
    *a_out = dx; *b_out = ax; *c_out = cx;
}

/* model_emit_faces' colour pair of the codes c1, c2 (w1, w2 >> 11) */
static u16 face_pair(u8 c1, u8 c2)
{
    u16 fseg = DSW(DS_face_block + 2), remap = DSW(DS_remap_ptr);
    u8 cl = rd8(fseg, (u16)(remap + c1)), ch = rd8(fseg, (u16)(remap + c2));
    if (DSW(DS_video_mode) != 0x13) return (u16)(((u16)ch << 8 | cl) & 0x0F0F);
    if (!(ch & 0x10) && !(cl & 0x10)) return DSW(DS_colour_pairs + 2 * (u8)((u8)(ch << 4) | cl));
    if (ch & 0x10) ch = DSB(DS_colour_direct + (ch & 0x0F));
    if (cl & 0x10) cl = DSB(DS_colour_direct + (cl & 0x0F));
    return (u16)((u16)ch << 8 | cl);
}

/* a sprite of the far ring (not in the game's list: instance FFFFh) */
static void add_sprite(u16 inst, u16 id, u16 x, u16 y, u16 z)
{
    if (F->nspr >= ENH_MAX_SPRITES_ALL) return;
    if (abs((s16)(u16)((u16)(x << 2) - F->cam_x4)) > FAR_REACH || abs((s16)(u16)((u16)(z << 2) - F->cam_z4)) > FAR_REACH)
        return;
    EnhSpr *e = &F->spr[F->nspr++];
    e->inst = inst;
    e->id = id;
    e->x = (s16)x; e->y = (s16)y; e->z = (s16)z;
}

/* model_place's sprite children of a tile (trees, signs...): after the faces, placed by quarter turns, those
 * from id 12h on thinned by the vegetation seed rotated by the cell's position, exactly as the game does, so
 * that a cell looks the same when the game builds it itself */
static void children(u16 es, u16 si, u16 px, u16 pz, u16 py, u8 rot)
{
    int nc = rd8(es, (u16)(si + 2));
    u16 c = (u16)(si + 4 + rd8(es, si) * 8 + rd8(es, (u16)(si + 1)) * 6);
    u16 di = DSW(DS_veg_seed);
    u8 cnt = (u8)((u16)(px + pz) >> 12);
    if (cnt) di = (u16)((di << cnt) | (di >> (16 - cnt)));
    for (int k = 0; k < nc; k++, c = (u16)(c + 8)) {
        u16 id = rd16(es, c);
        if ((u8)id >= 0x12) {
            u16 carry = di & 1;
            di = (u16)((di >> 1) | (carry << 15));
            if (!carry) continue;
        }
        u16 x = rd16(es, (u16)(c + 2)), z = rd16(es, (u16)(c + 4)), h = rd16(es, (u16)(c + 6));
        if (rot >= 0x40) {
            if (rot == 0x40)      { u16 t = x; x = z; z = (u16)-t; }
            else if (rot == 0x80) { x = (u16)-x; z = (u16)-z; }
            else                  { u16 t = x; x = (u16)-z; z = t; }
        }
        if (id) add_sprite(0xFFFF, id, (u16)(x + px), (u16)(h + py), (u16)(z + pz));
    }
}

/* model_place (0e12:72f8) without its sprite children: model al of the tiles (kind 0) or the objects (kind 1) at
 * place x / z (position units), height y, rotation and shear */
static void place(u8 al, int kind, u16 px, u16 pz, u16 py, u8 rot, s8 shear)
{
    u16 es, set, bx, cut = 0;
    if (kind) {
        es = DSW(DS_objects_set + 2); set = DSW(DS_objects_set);
        bx = (u16)(set + 2 * al);
    } else {
        if (al >= 0x40) { es = DSW(DS_tiles_scene + 2); set = DSW(DS_tiles_scene); al = (u8)(al - 0x40); }
        else            { es = DSW(DS_tiles_shared + 2); set = DSW(DS_tiles_shared); }
        bx = (u16)(set + 2 * al);
        u16 e = rd16(es, bx);
        if (e <= 0x10) { cut = e; bx = (u16)(bx - 2); }       /* alias of the previous model */
    }
    if (es == 0) return;
    u16 si = (u16)(rd16(es, bx) + set);
    int nf = rd8(es, si), nv = rd8(es, (u16)(si + 1));
    if (!kind) children(es, si, px, pz, py, rot);
    if (nf == 0 || nv == 0) return;
    nf -= cut;
    emit(es, (u16)(si + 4), nf, nv, px, pz, py, rot, shear);
}

/* the vertices at a (arrays A, B, C) and the nf faces after them of a model placed at px, pz (position
 * units), height py; dropped whole if a vertex would lie beyond FAR_REACH of the camera (16-bit wrap) */
static void emit(u16 es, u16 a, int nf, int nv, u16 px, u16 pz, u16 py, u8 rot, s8 shear)
{
    if (nf <= 0 || nv <= 0 || F->nfv + nv > ENH_MAX_FAR_VERTS || F->nff + nf > ENH_MAX_FAR_FACES) return;
    int base = F->nfv;
    u16 stride = (u16)(nv * 2), x4 = (u16)(px << 2), z4 = (u16)(pz << 2);
    for (int k = 0; k < nv; k++) {
        s16 va, vb, vc;
        rotate(es, (u16)(a + 2 * k), stride, rot, shear, &va, &vb, &vc);
        F->fvy[base + k] = (s16)(va + py);
        F->fvx[base + k] = (s16)(vb + x4);
        F->fvz[base + k] = (s16)(vc + z4);
        if (abs((s16)(u16)(F->fvx[base + k] - F->cam_x4)) > FAR_REACH ||
            abs((s16)(u16)(F->fvz[base + k] - F->cam_z4)) > FAR_REACH) return;   /* nothing kept */
    }
    F->nfv += nv;
    u16 f = (u16)(a + 3 * stride);
    for (int k = 0; k < nf; k++, f = (u16)(f + 8)) {
        u16 w0 = rd16(es, f), w1 = rd16(es, (u16)(f + 2)), w2 = rd16(es, (u16)(f + 4)), w3 = rd16(es, (u16)(f + 6));
        int n = (w0 >> 14) + 1;
        u16 v[4] = { (u16)(w0 & 0x7FF), (u16)(w1 & 0x7FF), (u16)(w2 & 0x7FF), (u16)(w3 & 0x7FF) };
        bool ok = true;
        for (int j = 0; j < n; j++) { if (v[j] >= nv) ok = false; v[j] = (u16)(v[j] + F->nv + base); }
        if (!ok) continue;
        EnhFarFace *e = &F->ff[F->nff++];
        e->w0 = (u16)(w0 & 0xF800);
        e->w3 = (u16)(w3 & 0xF800);
        e->pair = face_pair((u8)(w1 >> 11), (u8)(w2 >> 11));
        for (int j = 0; j < 4; j++) e->v[j] = j < n ? v[j] : v[0];
    }
}

/* the original's depth key (361c) at the snapshot's camera */
static double key_depth(int v)
{
    int i = v - F->nv;
    double dx = (s16)(u16)(F->fvx[i] - F->cam_x4), dz = (s16)(u16)(F->fvz[i] - F->cam_z4);
    return fabs((double)F->fvy[i] - F->cam_y) + sqrt(dx * dx + dz * dz);
}

static double far_key(const EnhFarFace *e)
{
    int n = e->w0 >> 14;
    double d0 = key_depth(e->v[0]);
    if (n == 0) return d0;
    double d1 = key_depth(e->v[1]), d2 = n >= 2 ? key_depth(e->v[2]) : d0, d3 = n == 3 ? key_depth(e->v[3]) : d0;
    if (e->w0 & 0x2000) return fmax(fmax(d0, d1), fmax(d2, d3));
    if (n == 1) return (d0 + d1) / 2;
    if (n == 2) return (d0 + d1 + d2) * 11.0 / 32.0;
    return (d0 + d1 + d2 + d3) / 4;
}

static double keys[ENH_MAX_FAR_FACES];
static int cmp_far(const void *a, const void *b)
{
    int ia = *(const int *)a, ib = *(const int *)b;
    return keys[ia] > keys[ib] ? -1 : keys[ia] < keys[ib] ? 1 : ia - ib;
}

/* ENH: sprite_animate (3352) drifts the sprites with id bit 40h / 80h (the aeroplane, clouds, birds) only while
 * they are in the game's sprite list, which reaches a short distance; the far ring draws the leg's own sprites
 * much farther, where they then hung still. Those not in the list this frame drift here by the same step while
 * the ring shows them (the game's own rule, at the ring's reach). Called right after sprite_animate. */
void enh_drift_far_sprites(void)
{
    if (!enh_enabled() || enh_draw_dist <= 0 || DSB(DS_menu_preview) != 0) return;
    static bool listed[320];
    memset(listed, 0, sizeof listed);
    for (u16 k = 0; k < DSW(DS_sprite_vis_count); k++) {
        u16 i = (u16)(DSW(DS_spr_inst + 2 * k) >> 1);
        if (i < 320) listed[i] = true;
    }
    int R = enh_draw_dist, ccol = DSB(DS_cam_x + 1) >> 2, crow = 15 - (DSB(DS_cam_z + 1) >> 2);
    int n = DSW(DS_sprite_count);
    static double plane_acc[320][2];                          /* the aeroplane's fraction of a unit, z / x */
    s16 dz = DSS(DS_sprite_drift_z), dx = DSS(DS_sprite_drift_x);
    for (int i = 9; i < n && i < 320; i++) {
        u16 id = DSW(DS_sprite_id + 2 * i);
        if (!id || !(id & 0xC0)) continue;
        u16 x = DSW(DS_sprite_x + 2 * i), z = DSW(DS_sprite_z + 2 * i);
        bool plane = enh_is_plane(id);
        if (listed[i]) {
            if (!plane) continue;                             /* the game drifted it */
            if (id & 0x40) z = (u16)(z - dz);                 /* the aeroplane: undo the game's step */
            if (id & 0x80) x = (u16)(x - dx);
        } else {
            int dc = (x >> 10) - ccol, dr = (15 - (z >> 10)) - crow;
            if (dc * dc + dr * dr > R * R + R) continue;      /* as the ring's sprites (enh_far_build) */
        }
        if (plane) {                                          /* ENH_PLANE_SPEED of the step, with its fraction */
            double *a = plane_acc[i];
            if (id & 0x40) { a[0] += dz * ENH_PLANE_SPEED; s16 k = (s16)trunc(a[0]); a[0] -= k; z = (u16)(z + k); }
            if (id & 0x80) { a[1] += dx * ENH_PLANE_SPEED; s16 k = (s16)trunc(a[1]); a[1] -= k; x = (u16)(x + k); }
        } else {
            if (id & 0x40) z = (u16)(z + dz);
            if (id & 0x80) x = (u16)(x + dx);
        }
        DSW(DS_sprite_z + 2 * i) = z;
        DSW(DS_sprite_x + 2 * i) = x;
    }
}

void enh_far_build(EnhSnap *s)
{
    F = s;
    s->nfv = s->nff = 0;
    s->nspr = s->nspr_game;
    if (enh_draw_dist <= 0 || s->menu_preview) return;
    int R = enh_draw_dist;
    int ccol = DSB(DS_cam_x + 1) >> 2, crow = 15 - (DSB(DS_cam_z + 1) >> 2);
    /* cells farthest first, so that a full buffer drops the far ones */
    int cells[(2 * 8 + 1) * (2 * 8 + 1)], nc = 0;
    double cx = DSW(DS_cam_x) / 1024.0, cz = DSW(DS_cam_z) / 1024.0;   /* the camera in cells */
    for (int dr = -R - 1; dr <= R + 1; dr++)
        for (int dc = -R - 1; dc <= R + 1; dc++) {
            int col = ccol + dc, row = crow + dr;
            if (col < 0 || col > 31 || row < 0 || row > 15) continue;
            /* the cell's centre within R cells of the camera, and all of it (half a cell, and a quarter for
             * what overhangs) within 7.5 cells on each axis */
            double ex = col + 0.5 - cx, ez = (15 - row) + 0.5 - cz;
            if (ex * ex + ez * ez > (double)R * R) continue;
            if (fabs(ex) > 6.75 || fabs(ez) > 6.75) continue;
            int c = col + row * 32;
            if (game_cell[c]) continue;
            cells[nc++] = c;
        }
    for (int i = 1; i < nc; i++)                               /* nearest first */
        for (int j = i; j > 0; j--) {
            int a = cells[j], b = cells[j - 1];
            int da = ((a & 31) - ccol) * ((a & 31) - ccol) + ((a >> 5) - crow) * ((a >> 5) - crow);
            int db = ((b & 31) - ccol) * ((b & 31) - ccol) + ((b >> 5) - crow) * ((b >> 5) - crow);
            if (da >= db) break;
            cells[j] = b; cells[j - 1] = a;
        }
    for (int i = 0; i < nc; i++) {
        int c = cells[i], col = c & 31, row = c >> 5;
        u16 m = DSW(DS_leg_map + 2 * c);
        u16 px = (u16)(col * 0x400 + 0x200), pz = (u16)((15 - row) * 0x400 + 0x200);
        place((u8)m, 0, px, pz, (u16)(m & 0x3F00), (u8)((m >> 8) & 0xC0), 0);
        /* its static objects, as world_build_visible selects them */
        u8 ch_x = (u8)(col << 2), cz = (u8)((15 - row) << 2);
        s16 lim = (s16)(2 * DSW(DS_obj_first_static) - 2);
        for (s16 so = (s16)(2 * DSW(DS_obj_count) - 2); so >= lim && so >= 0; so -= 2) {
            u16 o = (u16)so;
            if ((u8)(DSB(DS_obj_x + o + 1) & 0x7C) != ch_x) continue;
            u16 z = DSW(DS_obj_z + o);
            if ((u8)((z >> 8) & 0x3C) != cz) continue;
            u16 f = DSW(DS_obj_flags + o);
            if ((f >> 8) & 0x30) continue;                     /* moving / parked */
            if ((f & 0x3F) == 0) continue;
            place((u8)f, 1, DSW(DS_obj_x + o), z, (u16)(DSW(DS_obj_y8 + o) >> 3),
                  (u8)((DSW(DS_obj_heading + o) >> 8) & 0xC0), (s8)(DSW(DS_obj_pitch + o) >> 8));
        }
    }
    /* parked objects (houses, boats: 1000h set, 2000h clear) that the game draws only within its window
     * (509b, B6E0 around the car): those it did not emit this frame, within the ring */
    for (int o = 1; o < DSW(DS_obj_first_static) && o < 160; o++) {
        u16 f = DSW(DS_obj_flags + 2 * o);
        if ((f & 0x3000) != 0x1000 || (f & 0x3F) <= 3) continue;
        if ((f >> 8) == 0x1F) continue;             /* the lightning bolt: the game shows it itself, in a storm */
        if (DSW(DS_obj_vert_count + 2 * o) != 0) continue;          /* the game's own */
        u16 x = DSW(DS_obj_x + 2 * o), z = DSW(DS_obj_z + 2 * o);
        double ex = x / 1024.0 - cx, ez = z / 1024.0 - cz;
        if (ex * ex + ez * ez > (double)R * R) continue;
        u16 es = DSW(DS_objects_set + 2), set = DSW(DS_objects_set);
        if (es == 0) continue;
        u16 p = (u16)(rd16(es, (u16)(set + 2 * (f & 0x3F))) + set);
        int nf = rd8(es, p), nv = rd8(es, (u16)(p + 1));
        emit(es, (u16)(p + 8), nf, nv, x, z, (u16)(DSW(DS_obj_y8 + 2 * o) >> 3),
             (u8)(DSW(DS_obj_heading + 2 * o) >> 8), (s8)(DSW(DS_obj_pitch + 2 * o) >> 8));
    }

    /* the leg's own sprites (trees, rocks, animals: instances 9 .. sprite_count - 1) that the game lists only
     * within a cell of the car, as far as the ring reaches; their ids are live (a knocked-over sign) */
    static bool listed[320];
    memset(listed, 0, sizeof listed);
    for (int k = 0; k < s->nspr_game; k++) if (s->spr[k].inst < 320) listed[s->spr[k].inst] = true;
    int n = DSW(DS_sprite_count);
    for (int i = 9; i < n && i < 320; i++) {
        u16 id = DSW(DS_sprite_id + 2 * i);
        if (!id || listed[i]) continue;
        u16 x = DSW(DS_sprite_x + 2 * i), z = DSW(DS_sprite_z + 2 * i);
        int dc = (x >> 10) - ccol, dr = (15 - (z >> 10)) - crow;
        if (dc * dc + dr * dr > R * R + R) continue;
        add_sprite((u16)i, id, x, DSW(DS_sprite_y + 2 * i), z);
    }

    /* farthest first */
    static int idx[ENH_MAX_FAR_FACES];
    static EnhFarFace tmp[ENH_MAX_FAR_FACES];
    for (int i = 0; i < s->nff; i++) { keys[i] = far_key(&s->ff[i]); idx[i] = i; }
    qsort(idx, (size_t)s->nff, sizeof idx[0], cmp_far);
    for (int i = 0; i < s->nff; i++) tmp[i] = s->ff[idx[i]];
    memcpy(s->ff, tmp, sizeof tmp[0] * (size_t)s->nff);
}
