/*
 * filtr3.h - round 3's filters: Blur Gallery > Field Blur, Image > Adjustments > HDR Toning, Oil
 * Paint with PhotoCraft's controls (stylization, cleanliness, scale, bristle detail, lighting),
 * Render > Flame, Picture Frame and Tree, Lens Correction's perspective and Auto Scale, and Liquify.
 *
 * Like filters10.h's, they work on the active layer through the selection (feathered edges blend),
 * keep the alpha lock, are one undo step each and call progress() as they go (so Abort works).
 * Field Blur, HDR Toning and Oil Paint work in bands of rows (each band with the rows above and
 * below that the filter reaches), so they need little more memory than the undo copy; r3_memory
 * says how much. The renderers draw lists of anti-aliased primitives, band by band.
 *
 * Oil Paint follows PhotoCraft's crates/algo/src/oil.rs (MIT; see Licenses.txt): a structure
 * tensor gives the stroke directions, a line integral convolution smears the colour along them,
 * noise convolved the same way makes the bristles, and the relief is lit by a directional light.
 */
#ifndef EASEL_FILTR3_H
#define EASEL_FILTR3_H

#include "doc.h"
#include "path.h"

/* the bytes a filter needs beyond the picture (its bands and the undo copy of the area), for the
   interface's memory check: kind R3M_, reach the pixels it looks around each one */
enum { R3M_BAND, R3M_WHOLE };
long r3_memory(Doc *d, int kind, int reach);

/* ------------------------------------------------------------------ Field Blur */
#define FB_PINS 8
typedef struct { double x, y, blur; } BlurPin;          /* canvas pixels; blur radius (pixels, 0-200) */
/* each pixel blurred by the pins' radii, weighted by the inverse square of the distance to each */
int  flt_field_blur(Doc *d, const BlurPin *pins, int n);
/* the radius Field Blur uses at x, y */
double field_blur_radius(const BlurPin *pins, int n, double x, double y);

/* ------------------------------------------------------------------ Lens Blur with a depth map */
enum { LBD_NONE, LBD_TRANSPARENCY, LBD_MASK, LBD_CHANNEL };   /* the depth map: none (all blurred alike),
                                                                  the layer's transparency, its mask, an alpha channel */
typedef struct {
  int radius;                         /* 1-100 px: the blur at the far end of the depth */
  int blades;                         /* the iris: 0 a circle, else 3-8 blades */
  int rotation;                       /* degrees the iris turns */
  int depth;                          /* LBD_ */
  int channel;                        /* LBD_CHANNEL: which */
  int focal;                          /* 0-255: the depth that stays sharp */
  int invert;                         /* the depth map read the other way */
  int brightness, threshold;          /* specular highlights: 0-100 % brighter at or over the threshold (0-255) */
} LensBlurParams;
void lens_blur_defaults(LensBlurParams *p);
/* each pixel blurred by the iris at radius x |its depth - focal| / 255 (between four levels) */
int  flt_lens_blur_ex(Doc *d, const LensBlurParams *p);
/* the iris's rows: for each dy in -r..r, the run lo..hi it covers (2r + 1 entries each) */
void lens_iris_runs(int r, int blades, int rotation, int *lo, int *hi);

/* ------------------------------------------------------------------ HDR Toning */
typedef struct {
  int radius;                         /* 1-500 px: how far the local adaptation looks */
  int strength;                       /* 1-40 (x0.1): how firmly edges stop it (Edge Glow's strength) */
  int gamma;                          /* 10-200 (x0.01): the tones' contrast, 100 unchanged */
  int exposure;                       /* -500-500 (x0.01 stops) */
  int detail;                         /* -100-300 % */
  int shadow, highlight;              /* -100-100 */
  int vibrance, saturation;           /* -100-100 */
} HdrParams;
void hdr_defaults(HdrParams *p);      /* Photoshop's Default preset: radius 15, strength 0.5, detail 30 ... */
int  flt_hdr_toning(Doc *d, const HdrParams *p);

/* ------------------------------------------------------------------ Oil Paint */
typedef struct {
  int stylization, cleanliness, scale, bristle;   /* 0-100 (x0.1, as PhotoCraft's 0-10) */
  int lighting;                       /* lit relief on */
  int angle;                          /* -180-180 degrees: where the light comes from */
  int shine;                          /* 0-100 (x0.1) */
} OilParams;
void oil_defaults(OilParams *p);
int  flt_oil_paint_ex(Doc *d, const OilParams *p);

/* ------------------------------------------------------------------ Render: Flame, Picture Frame, Tree */
enum { FLAME_ONE_ALONG, FLAME_MANY_ALONG, FLAME_MANY_UP, FLAME_N };     /* the flames along the path */
enum { FLS_NORMAL, FLS_VIOLENT, FLS_FLAT, FLS_N };
typedef struct {
  int type;                           /* FLAME_ */
  int length, width;                  /* pixels */
  int interval;                       /* pixels between flames (the many) */
  int angle;                          /* FLAME_MANY_UP: degrees from straight up */
  int turbulence, jag;                /* 0-100 */
  int lines;                          /* strands in a flame, 1-30 */
  int style;                          /* FLS_ */
  int opacity;                        /* 1-100 % */
  int custom_color; u32 color;        /* the flames' colour (else a natural flame's) */
  unsigned seed;
} FlameParams;
void flame_defaults(FlameParams *p);
/* flames along the path (each subpath; NULL: a line across the lower part of the canvas) */
int  flt_flame(Doc *d, const Path *path, const FlameParams *p);

enum { TREE_OAK, TREE_PINE, TREE_MAPLE, TREE_BIRCH, TREE_WILLOW, TREE_PALM, TREE_BUSH, TREE_CHERRY, TREE_DEAD, TREE_CYPRESS, TREE_N };
extern const char *const tree_names[TREE_N];
typedef struct {
  int type;                           /* TREE_ */
  int base_x;                         /* % across the canvas where the trunk stands (its foot on the bottom edge) */
  int size;                           /* % of the canvas's height */
  int branch_height;                  /* % of the trunk where the branches start */
  int thickness;                      /* 10-300 % of the type's own */
  int leaves;                         /* 0-100 % of the type's own number */
  int leaf_size;                      /* 10-300 % */
  int light;                          /* -90-90 degrees: where the light comes from (shading) */
  int custom_colors; u32 leaf_color, branch_color;
  unsigned seed;
} TreeParams;
void tree_defaults(TreeParams *p);
int  flt_tree(Doc *d, const TreeParams *p);

enum { PF_VINE, PF_FLOWERS, PF_LEAVES, PF_DOTS, PF_WAVES, PF_STARS, PF_BORDER, PF_N };
extern const char *const frame_names[PF_N];
typedef struct {
  int kind;                           /* PF_ */
  int margin;                         /* pixels from the canvas's edge to the frame's middle */
  int size;                           /* the ornaments' size, pixels */
  int count;                          /* ornaments along the longer side */
  int arrangement;                    /* 0-100: how much the ornaments vary (and wander) */
  int vine_color_on; u32 vine_color, flower_color, leaf_color;
  unsigned seed;
} FrameParams;
void frame_defaults(FrameParams *p, i32 w, i32 h);
int  flt_picture_frame(Doc *d, const FrameParams *p);

/* ------------------------------------------------------------------ Lens Correction */
/* filters10.h's controls plus the Transform group: vertical and horizontal perspective (-100-100),
   angle (degrees, -180-180, the picture turned), and Auto Scale (the scale that keeps the picture's
   corners covered, so no edge shows) */
typedef struct {
  int distortion, fringe_rc, fringe_by, vig_amount, vig_mid, scale;    /* as flt_lens_correction */
  int vperspective, hperspective;
  double angle;
  int auto_scale;
} LensParams;
void lens_defaults(LensParams *p);
int  flt_lens_correction_ex(Doc *d, const LensParams *p);
/* the map Lens Correction's transform uses: canvas pixel (x, y) of the result comes from (sx, sy)
   of the original (perspective, angle and scale, before the distortion); and the scale Auto Scale
   picks */
void lens_map(const LensParams *p, i32 w, i32 h, double scale, double x, double y, double *sx, double *sy);
double lens_auto_scale(const LensParams *p, i32 w, i32 h);

/* ------------------------------------------------------------------ Liquify */
enum { LQ_WARP, LQ_RECONSTRUCT, LQ_SMOOTH, LQ_TWIRL_CW, LQ_TWIRL_CCW, LQ_PUCKER, LQ_BLOAT, LQ_PUSH_LEFT, LQ_N };
extern const char *const liquify_tool_names[LQ_N];
/* the warp: a mesh of displacements (one every cell pixels) telling where each point of the result
   is taken from (dx, dy added to its own position) */
typedef struct { i32 w, h, cell, mw, mh; float *dx, *dy; } LiquifyMesh;
LiquifyMesh *liquify_new(i32 w, i32 h);             /* NULL if out of memory */
void liquify_free(LiquifyMesh *m);
/* a brush stroke from (x0, y0) to (x1, y1): size (diameter, pixels), density 0-100 (how soft its
   edge), pressure 0-100, rate 0-100 (twirl, pucker, bloat, reconstruct: how fast at each step) */
void liquify_stroke(LiquifyMesh *m, int tool, double x0, double y0, double x1, double y1, double size, int density, int pressure, int rate);
void liquify_reset(LiquifyMesh *m);                 /* Restore All */
/* where the result's pixel x, y comes from */
void liquify_at(const LiquifyMesh *m, double x, double y, double *sx, double *sy);
/* the warp applied to the active layer, through the selection; one step */
int  flt_liquify(Doc *d, const LiquifyMesh *m);
/* the warp of a w x h RGBA picture held in memory into out (the preview) */
void liquify_apply_rgba(const LiquifyMesh *m, const u8 *src, u8 *out, i32 w, i32 h);
/* the same in box b only (out the picture's size): the preview while a stroke goes on */
void liquify_apply_box(const LiquifyMesh *m, const u8 *src, u8 *out, i32 w, i32 h, Box b);
/* the box a stroke from (x0, y0) to (x1, y1) of size changes (canvas pixels, with the mesh's reach) */
Box  liquify_stroke_box(const LiquifyMesh *m, double x0, double y0, double x1, double y1, double size);
#endif
