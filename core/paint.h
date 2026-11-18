/*
 * paint.h - what Easel's tools do to a document: brush strokes (brush, pencil, eraser, clone),
 * fills and gradients, shapes, selections, moving and transforming layers, and the canvas.
 * Everything respects the selection and records undo (doc.h).
 */
#ifndef EASEL_PAINT_H
#define EASEL_PAINT_H

#include "doc.h"
#include "brush.h"

/* ------------------------------------------------------------------ brush strokes */
/* (0.5: the stroke engine is in stroke.c, the brushes' pieces in brush.h) */
enum { PT_BRUSH, PT_PENCIL, PT_ERASER, PT_CLONE, PT_BLUR, PT_SHARPEN, PT_SMUDGE, PT_DODGE, PT_BURN, PT_SPONGE,
       /* 0.5 */
       PT_PATTERN,                    /* Pattern Stamp: paints b.pattern */
       PT_HISTORY,                    /* History Brush: paints b.snapshot back */
       PT_REPLACE,                    /* Color Replacement */
       PT_BGERASE,                    /* Background Eraser */
       /* 1.0 (retouch.h) */
       PT_HEAL,                       /* Healing Brush: paints as the Clone Stamp, healed when the stroke ends */
       PT_SPOTHEAL,                   /* Spot Healing Brush: marks the area, filled from round it when the stroke ends */
       PT_N };
enum { ER_BRUSH, ER_PENCIL, ER_BLOCK };                    /* the Eraser's modes */
enum { SMP_CONTINUOUS, SMP_ONCE, SMP_SWATCH };             /* Color Replacement and Background Eraser: what they sample */
enum { LIM_CONTIGUOUS, LIM_DISCONTIGUOUS, LIM_FINDEDGES }; /* and where they act */
enum { RPL_HUE, RPL_SATURATION, RPL_COLOR, RPL_LUMINOSITY };
struct Pattern;
/* a brush's settings. Everything after strength is 0.5's, and zero there is always "off" or
   "as before", so a Brush cleared with memset paints as Easel 0.4 did */
typedef struct {
  int tool;                           /* PT_ */
  int size;                           /* diameter in pixels, 1-2500 */
  int hardness;                       /* 0-100 */
  int opacity;                        /* 0-100: the most a stroke covers */
  int flow;                           /* 0-100: each dab's share */
  int spacing;                        /* per cent of the size between dabs, 1-1000 */
  int blend;                          /* BM_ for the brush and pencil */
  i32 clone_dx, clone_dy;             /* PT_CLONE: where it copies from, relative to where it paints */
  int strength;                       /* PT_BLUR..PT_SPONGE: 0-100 */
  /* 0.5: the tip */
  int angle;                          /* degrees, anticlockwise, -180..180 */
  int roundness;                      /* 1-100 per cent (0: 100, round) */
  int flip_x, flip_y;
  int square;                         /* an aliased square tip (what the Eraser's Block mode paints) */
  const Tip *tip;                     /* a sampled tip instead of the round one (kept by the caller) */
  const CustomBrush *custom;          /* a DPaint brush, painted in custom_mode (kept by the caller) */
  int custom_mode;                    /* CB_ */
  u32 bg;                             /* the background colour 0xRRGGBB (Color Dynamics, Auto Erase, swatch sampling) */
  /* 0.5: tool options */
  int eraser_mode;                    /* PT_ERASER: ER_ */
  int auto_erase;                     /* PT_PENCIL: starting on the foreground colour paints the background colour */
  int sample_all;                     /* PT_CLONE: copies from everything visible, not the active layer */
  const struct Pattern *pattern;      /* PT_PATTERN (kept by the caller) */
  int pattern_aligned;                /* PT_PATTERN: the tiles stay put from stroke to stroke (else start at each stroke) */
  const Snapshot *snapshot;           /* PT_HISTORY (kept by the caller) */
  int sampling, limits, tolerance;    /* PT_REPLACE, PT_BGERASE: SMP_, LIM_, 0-255 */
  int protect_fg;                     /* PT_BGERASE: keeps the foreground colour */
  int replace_mode;                   /* PT_REPLACE: RPL_ */
  BrushDynamics dyn;                  /* Brush Settings' sections */
  /* 1.1 (toolopts.h); zero is still "as before" */
  int tone_range, protect_tones;      /* PT_DODGE, PT_BURN: TR_ (TR_ALL: 1.0's even change), Protect Tones */
  int sponge_saturate, vibrance;      /* PT_SPONGE: Saturate instead of Desaturate; Vibrance */
  int erase_history;                  /* PT_ERASER: Erase to History, the snapshot painted back */
  int finger_painting;                /* PT_SMUDGE: each stroke starts with the foreground colour */
  int protect_detail;                 /* PT_SHARPEN: gentler, without haloes or noise */
  /* (also 1.1: blend is the Clone Stamp's, Blur's, Sharpen's and Smudge's Mode; sample_all takes
     SAMPLE_BELOW, the layers up to the active one) */
} Brush;
typedef struct Stroke Stroke;
/* a stroke on the active layer, begun at x, y (an undo step "name" while it lasts); then
   continued to each point the mouse goes to, and ended. The area changed comes back in *changed
   (for redrawing). NULL if the layer can't be painted (locked, hidden) */
Stroke *stroke_begin(Doc *d, const Brush *b, u32 color, double x, double y, Box *changed);
void    stroke_to(Stroke *s, double x, double y, Box *changed);
void    stroke_end(Stroke *s);
/* 0.5: the same with tablet pressure 0-1 (stroke_to is pressure 1); stroke_begin_p starts with it */
Stroke *stroke_begin_p(Doc *d, const Brush *b, u32 color, double x, double y, double pressure, Box *changed);
void    stroke_to_p(Stroke *s, double x, double y, double pressure, Box *changed);
/* airbrush build-up and smoothing's catch-up while the pointer holds still: call every
   stroke_tick_ms(b) milliseconds from a timer while the button is down. *changed as for stroke_to */
void    stroke_tick(Stroke *s, Box *changed);
int     stroke_tick_ms(const Brush *b);     /* 0: this brush needs no timer */
int     stroke_wants_tick(const Brush *b);
/* the dabs laid so far (tests and the status bar) */
long    stroke_dabs(const Stroke *s);
/* where smoothing holds the brush back from the pointer: the brush's place (for drawing the
   string), 0 if it isn't held back */
int     stroke_string(const Stroke *s, double *bx, double *by);

/* what a stroke paints into: straight RGBA (bpp 4) or an 8-bit mask (bpp 1, e.g. a layer mask:
   paint lays its colour's grey, the eraser the background's), w x h like the canvas. note is called
   once for each part before it first changes (undo); ctx is passed back to it */
typedef struct {
  u8 *px; i32 w, h; int bpp;
  int alpha_lock;                     /* RGBA: the transparency is kept */
  void (*note)(void *ctx, Box r);
  void *ctx;
} PaintTarget;
Stroke *stroke_begin_target(Doc *d, const PaintTarget *t, const Brush *b, u32 color, double x, double y, double pressure, Box *changed);

/* a dab's coverage as the brush tip would lay it (size, angle, roundness, hardness, sampled tip,
   custom brush shape), *w x *h malloc'd, for previews in the Brushes panel. NULL if out of memory */
u8     *brush_dab_preview(const Brush *b, int *w, int *h);

/* ------------------------------------------------------------------ fills */
/* Paint Bucket at x, y: the area like the pixel there (tolerance 0-255; contiguous or every such
   pixel; sampling all layers or the active one) filled with color at opacity 0-100 */
Box fill_bucket(Doc *d, i32 x, i32 y, u32 color, int tolerance, int contiguous, int all_layers, int opacity, int antialias);
/* the selection (or everything) filled with color (Edit > Fill), or emptied (Edit > Clear:
   transparent, or the background colour on a layer that can't be transparent) */
Box fill_selection(Doc *d, u32 color, int opacity, int blend);
Box clear_selection(Doc *d);
/* a gradient from x0, y0 to x1, y1: linear, radial, angle, reflected or diamond, from c0 to c1
   (c1 transparent if fade), reversed or not, dithered */
enum { GR_LINEAR, GR_RADIAL, GR_ANGLE, GR_REFLECTED, GR_DIAMOND };
Box fill_gradient(Doc *d, double x0, double y0, double x1, double y1, int kind, u32 c0, u32 c1, int fade, int reverse, int opacity, int blend);
/* 0.5: a multi-stop gradient (gradient.h) from x0, y0 to x1, y1 in style kind (GR_), reversed or
   not, dithered or not. The gradient's foreground and background stops take d->fg and d->bg */
struct Gradient;
Box fill_gradient_ex(Doc *d, double x0, double y0, double x1, double y1, int kind, const struct Gradient *g, int reverse, int dither, int opacity, int blend);
/* 0.5: the selection (or everything) filled with a pattern (pattern.h) tiled from ox, oy
   (Edit > Fill > Pattern), and the Paint Bucket filling with one */
Box fill_pattern(Doc *d, const struct Pattern *p, i32 ox, i32 oy, int opacity, int blend);
Box fill_bucket_pattern(Doc *d, i32 x, i32 y, const struct Pattern *p, int tolerance, int contiguous, int all_layers, int opacity, int antialias);
/* shapes drawn on the active layer: a rectangle (corner radius r), ellipse, or a line w wide */
Box shape_rect(Doc *d, double x0, double y0, double x1, double y1, double radius, u32 color, int opacity);
Box shape_ellipse(Doc *d, double x0, double y0, double x1, double y1, u32 color, int opacity);
Box shape_line(Doc *d, double x0, double y0, double x1, double y1, double width, u32 color, int opacity);
/* the selection's edge drawn width pixels wide, inside, centred or outside (Edit > Stroke) */
Box stroke_selection(Doc *d, int width, int where, u32 color, int opacity);

/* ------------------------------------------------------------------ selections */
enum { SEL_REPLACE, SEL_ADD, SEL_SUBTRACT, SEL_INTERSECT };
void sel_rect(Doc *d, i32 x0, i32 y0, i32 x1, i32 y1, int op, int feather);
void sel_ellipse(Doc *d, i32 x0, i32 y0, i32 x1, i32 y1, int op, int feather);
void sel_polygon(Doc *d, const double *xy, int n, int op, int feather);      /* the lassos */
void sel_wand(Doc *d, i32 x, i32 y, int tolerance, int contiguous, int all_layers, int op);
void sel_all(Doc *d);
void sel_none(Doc *d);
void sel_invert(Doc *d);
void sel_feather(Doc *d, double radius);
void sel_grow(Doc *d, int by);                 /* expand (by > 0) or contract (by < 0) */
void sel_border(Doc *d, int width);
void sel_smooth(Doc *d, int radius);
void sel_from_alpha(Doc *d, int layer);        /* the layer's opaque pixels */
void sel_color_range(Doc *d, u32 color, int fuzziness);
int  sel_bounds(Doc *d, Box *b);               /* 0 if nothing is selected */
/* the outline of the selection for marching ants: horizontal and vertical edge runs between
   pixels at least half selected, within box r; each segment x0,y0,x1,y1 (pixel corners), n of them */
int  sel_edges(Doc *d, Box r, i32 **out);

/* ------------------------------------------------------------------ moving and transforming */
/* the active layer's pixels (within the selection, if any) moved by dx, dy */
Box move_layer(Doc *d, i32 dx, i32 dy);
/* the active layer (or the selection's part of it) scaled, turned and moved: the inverse map
   from new to old is m = { a, b, c, d, e, f }: old x = a x + b y + c, old y = d x + e y + f */
Box transform_layer(Doc *d, const double *m);
void flip_layer(Doc *d, int vertical);
/* the whole image */
void image_flip(Doc *d, int vertical);
void image_rotate(Doc *d, int quarter_turns);             /* 1 = 90 clockwise, 2 = 180, 3 = 90 anticlockwise */
int  image_rotate_angle(Doc *d, double degrees);          /* any angle; the canvas grows to fit */
enum { RS_NEAREST, RS_BILINEAR, RS_BICUBIC };
int  image_resize(Doc *d, i32 w, i32 h, int method);
/* anchor: 0-8, the 3x3 grid read across (4 the middle) */
int  canvas_resize(Doc *d, i32 w, i32 h, int anchor, u32 fill);
int  image_crop(Doc *d, Box b);
int  image_trim(Doc *d);                                  /* transparent edges cut off */
/* the image's layers turned into one pixel buffer by a bilinear or bicubic sample at x, y */
void sample_bicubic(const u8 *px, i32 w, i32 h, double x, double y, u8 *out);
void sample_bilinear(const u8 *px, i32 w, i32 h, double x, double y, u8 *out);

/* ------------------------------------------------------------------ the clipboard (Easel's own) */
typedef struct { i32 w, h, x, y; u8 *px; } Clip;           /* x, y: where it came from */
Clip *clip_copy(Doc *d, int merged);                      /* the selection's pixels (or all), NULL if none */
Box   clip_cut(Doc *d);                                   /* (copy first) */
int   clip_paste(Doc *d, const Clip *c, int in_place);     /* as a new layer */
void  clip_free(Clip *c);

/* colour helpers */
u32  doc_pick(Doc *d, i32 x, i32 y, int all_layers, int radius);   /* the eyedropper */
void rgb_to_hsv(int r, int g, int b, double *h, double *s, double *v);
void hsv_to_rgb(double h, double s, double v, int *r, int *g, int *b);

/* ------------------------------------------------------------------ 0.7 */
/* the lassos, the elliptical marquee and the magic wand with the Anti-alias option (sel_polygon and
   sel_wand are these with it on); off, an edge pixel is in when it is half covered or more */
void sel_polygon_ex(Doc *d, const double *xy, int n, int op, int feather, int antialias);
void sel_ellipse_ex(Doc *d, i32 x0, i32 y0, i32 x1, i32 y1, int op, int feather, int antialias);
void sel_wand_ex(Doc *d, i32 x, i32 y, int tolerance, int contiguous, int all_layers, int op, int antialias);
/* the whole picture (every layer, the selection, the alpha channels) remade at nw x nh through a
   projective map inv (3 x 3, row major) from new pixel-centre positions to old ones; RS_ interp.
   One undo step called name; 0 if out of memory */
int  image_remap(Doc *d, i32 nw, i32 nh, const double *inv, int interp, const char *name);
#endif
