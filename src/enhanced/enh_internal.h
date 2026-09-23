#pragma once
/* Enhanced renderer internals (ENHANCED.md): snapshots of the game's frames, the smooth view state, the
 * draw lists and the sample buffers. */
#include "enhanced.h"
#include "../mem.h"
#include "../symbols.h"

#define ENH_MAX_VERTS   0x640          /* the game's vertex arrays */
#define ENH_MAX_FACES   0x640
#define ENH_MAX_OBJS    160
#define ENH_MAX_SPRITES 0x98           /* the sprite list DS:8902.. */
#define ENH_MAX_OVPIX   8192           /* overlay pixels of one frame */
#define ENH_MAX_OVCMD   64

/* ---- snapshots (enhanced.c) ---- */

typedef struct {
    u16 x, z, y8, heading;             /* obj_x / obj_z / obj_y8 / obj_heading */
    u16 flags;
    u16 vbase, vcount;                 /* vertices emitted this frame (moving vehicles; 0 = none) */
    u16 rbase, rcount;                 /* obj_vert_base / obj_vert_count as they are (lamp owners) */
} EnhObj;

typedef struct {
    u16 inst;                          /* sprite instance index */
    u16 id;                            /* sprite_id word (low byte: class/number, high: flags/countdown) */
    s16 x, y, z;
} EnhSpr;

typedef enum { OV_PIXELS, OV_QUAD, OV_LINE } EnhOvKind;
typedef struct {
    EnhOvKind kind;
    int first, count;                  /* OV_PIXELS: range in ov_pix[] */
    float x[4], y[4];                  /* OV_QUAD: the cycle in V pixels (x from 1/32 px); OV_LINE: 2 points */
    u16 colour;                        /* colour pair */
    bool or_mode;
    u16 w0;                            /* OV_LINE: w0 of the record (width flags) */
} EnhOvCmd;

typedef struct {
    bool valid;
    u64  t_ns;                         /* tick time of the frame */
    u16  frame_counter;
    /* view */
    u16  w32m;                         /* DS:BA95: (view width - 1) * 32 */
    u8   cx_hi;                        /* DS:BA99 */
    u16  rows;                         /* DS:BA91 */
    u16  mirror_base;                  /* DS:BD3D */
    bool mirror_on;                    /* the mirror pass ran (B6D2, not in the replay layout) */
    bool ext_view, menu_preview;
    bool day;                          /* DS:95C7 colour mode: OR faces of pair 0707h are hidden by day */
    /* camera */
    u16  cam_x4, cam_z4, heading;
    s16  cam_y, cam_row, pitch;        /* cam_row = pitch + horizon base (9496); pitch = car pitch 948F */
    s8   roll;                         /* DS:94A0 roll code */
    /* sky / ground */
    u16  sky_pair, ground_pair;        /* DS:BAA2 / BAA4 */
    u8   sky_base;                     /* first colour of the gradient */
    bool gradient;
    /* geometry */
    int  nv, nf, static_vert_end;
    s16  vx[ENH_MAX_VERTS], vy[ENH_MAX_VERTS], vz[ENH_MAX_VERTS];
    u16  face[ENH_MAX_FACES][5];       /* w0, w1, w2, w3, colour pair */
    int  nobj;
    EnhObj obj[ENH_MAX_OBJS];
    u8   point_sizes[4], line_widths[4];
    /* sprites */
    int  nspr;
    EnhSpr spr[ENH_MAX_SPRITES];
    u16  spr_min_dist, spr_far_limit;  /* DS:95CD, DS:B6E2 */
    u8   sprite_kind[32];              /* DS:95E1 */
    u8   sprite_max_size;              /* DS:BA0D */
    /* cockpit overlays, in drawing order */
    int  novcmd, novpix;
    EnhOvCmd ov[ENH_MAX_OVCMD];
    u32  ovpix[ENH_MAX_OVPIX];         /* V offset << 8 | colour */
    /* V as the frame left it (after the overlays) */
    u8   vbuf[0x7800];
} EnhSnap;

/* The two newest frames (prev, cur). */
extern EnhSnap *enh_prev, *enh_cur;

/* Where the last view_present put V and M on the screen. */
typedef struct {
    bool active;
    int  x0, y0;                       /* screen position of V column 0 / row 0 */
    int  w, rows;                      /* V columns and rows shown */
    bool hole;                         /* V rows 0..13 skip screen x 168..255 (the mirror) */
    int  msg_rows;                     /* rows of V not copied because of a message box (msg_protect) */
    bool mirror;                       /* M was copied with 3D content (mirror on, not invalid) */
    u8   vbuf[0x7800];                 /* V as presented */
    u8   mbuf[88 * 19];                /* M as presented */
} EnhPresent;
extern EnhPresent enh_present;

/* ---- options (enhanced.c) ---- */
extern int enh_scale, enh_aa, enh_motion_delay;

/* ---- the smooth view state of one displayed frame (enhanced.c) ---- */
typedef struct {
    double cam_x4, cam_z4, cam_y, heading, cam_row, pitch;
    double roll_slope;                 /* front rows per 1/32 px of x (the roll term), 0 = level */
    double mroll_slope;                /* mirror: rows per 1/32 px of (sx - 9400h) before the halving */
    s8     roll;                       /* nearest code (sky flat/gradient choice) */
    /* vertices of the current snapshot, moving vehicles carried to the view time */
    float  vx[ENH_MAX_VERTS], vy[ENH_MAX_VERTS], vz[ENH_MAX_VERTS];
    /* per object: heading at the view time (u16 units, unwrapped) */
    double obj_heading[ENH_MAX_OBJS];
    float  spr_x[ENH_MAX_SPRITES], spr_y[ENH_MAX_SPRITES], spr_z[ENH_MAX_SPRITES];
} EnhView;

/* ---- sprites (enh_sprite.c) ---- */
typedef struct {
    int w, h;                          /* native size */
    u8 *pix;                           /* w * h, 0 = transparent (NULL: none) */
} EnhSprImg;
void enh_sprites_capture(void);
/* The image and the drawn size (in view pixels) of sprite number s (low 6 bits of the id) at the apparent
 * size a (8-pixel units, continuous). Returns false if nothing is drawn. */
bool enh_sprite_pick(u8 s, double a, const EnhSprImg **img, double *w, double *h);

/* ---- rasterising (enh_raster.c) ---- */

/* A sample holds a colour pair and the weight of its high colour: lo | hi << 8 | weight << 16
 * (weight 128 = the original's two-colour dither seen from afar). */
#define ENH_PAIR(p)          ((u32)(p) | 0x800000u)
#define ENH_SOLID(c)         ((u32)(c) * 0x101u | 0x800000u)

typedef enum { P_POLY, P_SPRITE, P_BLOCKS, P_SKY } EnhPrimKind;
typedef struct {
    EnhPrimKind kind;
    u32 value;                         /* sample value (P_POLY, P_BLOCKS) */
    bool or_mode;                      /* OR 08h into both colours instead of storing */
    int n;                             /* P_POLY: points */
    float x[16], y[16];                /* P_POLY: sample coordinates */
    /* P_SPRITE: destination rectangle in samples, image */
    float sx0, sy0, sx1, sy1;
    const EnhSprImg *img;
    /* P_BLOCKS: overlay pixels [first, first + count) of the snapshot, K x K samples each */
    int first, count;
    /* P_SKY: horizon row at sample x = 0 and its slope (rows per sample), base colour, gradient on */
    float hy0, hslope;
    u8 sky_base;
    bool gradient;
} EnhPrim;

typedef struct {
    int w, h;                          /* samples */
    u32 *s;
    EnhPrim *prim;
    int nprim, cap;
    int k;                             /* samples per view pixel */
} EnhTarget;

void enh_target_reset(EnhTarget *t, int w_px, int h_px, int k);
EnhPrim *enh_prim_add(EnhTarget *t, EnhPrimKind kind);
void enh_target_raster(EnhTarget *t, const EnhSnap *snap);

/* ---- the scene (enh_scene.c) ---- */
extern EnhTarget enh_front, enh_mirror;
void enh_scene_build(const EnhSnap *s, const EnhView *v);
