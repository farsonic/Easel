/*
 * shape.h - shape layers (Easel 0.9): a vector path filled and stroked, rendered anti-aliased by the
 * compositor whenever it is wanted (so it stays sharp through Image Size and edits), plus the
 * generators the Shape tools use: rectangle (corner radius), rounded rectangle, ellipse, triangle,
 * polygon and star, line with arrowheads, and a custom shape from any path.
 *
 * A shape layer's settings are a ShapeParams with its path stored after it in one block (Layer's
 * live): the generator's settings are kept too, so the Properties panel can change them (the path
 * is made again from them; SH_CUSTOM keeps its path as it is).
 */
#ifndef EASEL_SHAPE_H
#define EASEL_SHAPE_H

#include "doc.h"
#include "path.h"

enum { SH_RECT, SH_ROUNDRECT, SH_ELLIPSE, SH_TRIANGLE, SH_POLYGON, SH_LINE, SH_CUSTOM, SH_N };

typedef struct ShapeParams {
  u8 kind;                            /* SH_ */
  u8 fill_on, stroke_on;              /* filled with fill_color; stroked */
  u8 align;                           /* SA_ (path.h): the stroke inside, centred on or outside the edge */
  u8 cap, join;                       /* CAP_, JOIN_ */
  u8 antialias;                       /* 1 smooth edges (the default), 0 hard */
  u8 evenodd;                         /* fill rule */
  u32 fill_color, stroke_color;       /* 0xRRGGBB */
  u8 fill_alpha, stroke_alpha;        /* 255 - opacity (0: opaque), so a zeroed ShapeParams is opaque */
  u8 star;                            /* SH_POLYGON: a star (indent used) */
  u8 arrow_start, arrow_end;          /* SH_LINE */
  u8 pad[3];
  double stroke_width;                /* pixels */
  double miter;                       /* miter limit (0: 4) */
  double dash[8]; i32 ndash;          /* pixels on, off, ... (0: solid) */
  i32 sides;                          /* SH_POLYGON: 3-100 */
  double dash_off;
  /* where: the box x0, y0 - x1, y1 (SH_LINE: from x0, y0 to x1, y1) */
  double x0, y0, x1, y1;
  double radius;                      /* SH_RECT, SH_ROUNDRECT: corner radius */
  double indent;                      /* SH_POLYGON star: how far the inner points come in, per cent (0-99) */
  double angle;                       /* SH_POLYGON: degrees the first point turns clockwise from straight up */
  double line_width;                  /* SH_LINE: thickness (pixels) */
  double arrow_w, arrow_len;          /* SH_LINE: arrowhead width and length, per cent of the line width (0: 500, 1000) */
  double arrow_concave;               /* SH_LINE: per cent the arrowhead's base is cut in (0-50) */
  /* r3: the fill as a gradient or a pattern (PhotoCraft's Fill::Gradient / Fill::Pattern on a shape),
     and a radius per corner for rectangles */
  u8 fill_kind;                       /* SFILL_ */
  u8 grad_style;                      /* GR_ (paint.h): linear, radial, angle, reflected, diamond */
  u8 grad_reverse, grad_nc, grad_no;  /* reversed; colour and opacity stops used (up to SH_GSTOPS) */
  u8 radii_on, pad3[2];               /* SH_RECT, SH_ROUNDRECT: radii[] used instead of radius */
  double grad_angle;                  /* degrees, anticlockwise from left to right */
  double grad_scale;                  /* per cent of the shape's box (0: 100); the pattern's scale too */
  double grad_cpos[8], grad_cmid[8]; u32 grad_rgb[8];     /* colour stops: 0-1, midpoint 0.05-0.95, 0xRRGGBB */
  double grad_opos[8], grad_omid[8], grad_op[8];          /* opacity stops: 0-1, midpoint, opacity 0-1 */
  double radii[4];                    /* top left, top right, bottom right, bottom left (pixels) */
  /* the path that follows (kept by shape_alloc; read through shape_path) */
  i32 na, ns;
  u8 evenodd_path, pad2[7];
} ShapeParams;

#define SH_GSTOPS 8
enum { SFILL_COLOR, SFILL_GRADIENT, SFILL_PATTERN, SFILL_N };   /* r3: what fills a shape */
struct Gradient;
/* r3: the shape's fill a gradient (g's stops, up to SH_GSTOPS of each, its foreground and background
   stops taken as g->fg and g->bg), in style GR_, at angle degrees, scale per cent of the shape's box */
void shape_set_gradient(ShapeParams *sp, const struct Gradient *g, int style, double angle, double scale, int reverse);
/* the gradient a shape's fill holds (two stops from its fill colour when it has none) */
void shape_get_gradient(const ShapeParams *sp, struct Gradient *g);
/* a rectangle with a radius per corner (r: top left, top right, bottom right, bottom left) */
int  shape_rect_path4(Path *p, double x0, double y0, double x1, double y1, const double *r);
/* r3: shape layer i's fill: SFILL_COLOR, SFILL_GRADIENT (gradient g, as shape_set_gradient) or SFILL_PATTERN
   (pattern pat at scale per cent; NULL keeps the layer's pattern); one undo step. 0 on failure */
struct Pattern;
int  doc_set_shape_fill(Doc *d, int i, int kind, const struct Gradient *g, int style, double angle, double scale, int reverse,
                        const struct Pattern *pat);

/* a ShapeParams set to defaults: kind, the box, filled with fill (stroke off, smooth edges) */
void shape_defaults(ShapeParams *sp, int kind, double x0, double y0, double x1, double y1, u32 fill);
/* the generators: a path for the shape (appended to p); 0 if out of memory */
int  shape_rect_path(Path *p, double x0, double y0, double x1, double y1, double radius);
int  shape_ellipse_path(Path *p, double x0, double y0, double x1, double y1);
int  shape_triangle_path(Path *p, double x0, double y0, double x1, double y1);
/* a regular polygon (or a star with indent per cent) inside the ellipse in the box, the first point
   angle degrees clockwise from the top */
int  shape_polygon_path(Path *p, double x0, double y0, double x1, double y1, int sides, int star, double indent, double angle);
/* a line w wide from x0, y0 to x1, y1 as an outline, with arrowheads at either end (aw, al: the
   arrowhead's width and length in pixels; concave: per cent its base is cut in) */
int  shape_line_path(Path *p, double x0, double y0, double x1, double y1, double w, int astart, int aend, double aw, double al, double concave);
/* the path sp's generator makes (custom: NULL) appended to p */
int  shape_make_path(const ShapeParams *sp, Path *p);

/* a shape's block: sp with path (or, when path is NULL, the generator's) after it; malloc'd, its
   length in *len. NULL if out of memory */
ShapeParams *shape_alloc(const ShapeParams *sp, const Path *path, u32 *len);
/* the path stored in a shape's block, as a view (don't change or free its arrays; path_copy it) */
Path shape_path(const ShapeParams *sp);
/* a layer's settings, NULL if it isn't a shape layer */
const ShapeParams *layer_shape(const Layer *l);

/* the shape rendered: RGBA over transparency in box *b (the shape's extent clipped to the canvas
   w x h), malloc'd into *px. 1 with *px NULL when nothing shows; 0 if out of memory */
int  shape_render(const ShapeParams *sp, i32 w, i32 h, u8 **px, Box *b);
/* r3: the same for layer l, whose pattern (layerx.h) a pattern fill shows (l may be NULL) */
int  shape_render_layer(const ShapeParams *sp, const Layer *l, i32 w, i32 h, u8 **px, Box *b);

/* a new shape layer above the active one (its path from path, or the generator's when NULL); one
   undo step. Its index, -1 on failure */
int  doc_new_shape(Doc *d, const ShapeParams *sp, const Path *path, const char *name);
/* shape layer i's settings changed (path NULL: made again by the generator, or kept for SH_CUSTOM);
   one undo step */
int  doc_set_shape(Doc *d, int i, const ShapeParams *sp, const Path *path);
#endif
