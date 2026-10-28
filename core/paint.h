/*
 * paint.h - what Easel's tools do to a document: brush strokes (brush, pencil, eraser, clone),
 * fills and gradients, shapes, selections, moving and transforming layers, and the canvas.
 * Everything respects the selection and records undo (doc.h).
 */
#ifndef EASEL_PAINT_H
#define EASEL_PAINT_H

#include "doc.h"

/* ------------------------------------------------------------------ brush strokes */
enum { PT_BRUSH, PT_PENCIL, PT_ERASER, PT_CLONE, PT_BLUR, PT_SHARPEN, PT_SMUDGE, PT_DODGE, PT_BURN, PT_SPONGE };
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
} Brush;
typedef struct Stroke Stroke;
/* a stroke on the active layer, begun at x, y (an undo step "name" while it lasts); then
   continued to each point the mouse goes to, and ended. The area changed comes back in *changed
   (for redrawing). NULL if the layer can't be painted (locked, hidden) */
Stroke *stroke_begin(Doc *d, const Brush *b, u32 color, double x, double y, Box *changed);
void    stroke_to(Stroke *s, double x, double y, Box *changed);
void    stroke_end(Stroke *s);

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
#endif
