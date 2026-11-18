/*
 * path.h - vector paths (Easel 0.9): subpaths of cubic Bezier anchors, each with a handle in and a
 * handle out, open or closed; what the Pen tool draws, the Paths panel lists and shape layers are
 * made of. Portable C99.
 *
 * Coordinates are canvas pixels (doubles; a pixel's centre is at +0.5). An anchor's handles are
 * absolute positions; a handle on its anchor means "none" (a straight segment on that side).
 * Segment k of a subpath runs from anchor k to anchor k + 1 (and, closed, from the last to the
 * first) as the cubic (anchor k, its out handle, the next one's in handle, the next anchor).
 *
 * A document keeps saved paths and a work path (doc.h's 0.9 block). Every change to them made
 * between doc_begin and doc_end after doc_note_paths is one undo step: the doc_path_ functions do
 * that themselves; to edit a path in place (the Pen and Direct Selection tools), call
 *   doc_begin(d, "Move Anchor"); doc_note_paths(d); path_move_anchor(doc_path(d, i), ...); doc_end(d);
 * (a drag can stay inside one step: doc_begin on the press, doc_end on the release).
 *
 * The rest - the selection, filling and stroking - is in pathops.h.
 */
#ifndef EASEL_PATH_H
#define EASEL_PATH_H

#include "doc.h"

enum { PA_CORNER, PA_SMOOTH };        /* smooth: the two handles stay in line when one is moved */
typedef struct {
  double x, y;                        /* the anchor */
  double ix, iy, ox, oy;              /* its handles: in (from the segment before) and out */
  u8 type;                            /* PA_ */
  u8 sel;                             /* chosen with Direct Selection (the UI's to set) */
  u8 pad[6];
} PathAnchor;
typedef struct {
  i32 first, n;                       /* its anchors: a[first .. first + n) */
  u8 closed;
  u8 pad[7];
} SubPath;
typedef struct Path {
  PathAnchor *a; i32 na, capa;
  SubPath *s; i32 ns, caps;
  u8 evenodd;                         /* fill rule: 0 non-zero winding, 1 even-odd */
  u8 view;                            /* the arrays belong to someone else (shape_path's view): never freed */
} Path;

/* ------------------------------------------------------------------ the model */
Path *path_new(void);                                       /* empty; NULL if out of memory */
void  path_free(Path *p);                                   /* NULL is fine */
Path *path_copy(const Path *p);                             /* a deep copy (also of a view) */
long  path_bytes(const Path *p);                            /* memory it holds (for undo's budget) */
void  path_clear(Path *p);
/* drawing like PostScript: each returns 0 if out of memory. path_move_to starts a subpath (its
   index comes back through the return value minus one, so test > 0); the others add to the last */
int   path_move_to(Path *p, double x, double y);
int   path_line_to(Path *p, double x, double y);
int   path_curve_to(Path *p, double c1x, double c1y, double c2x, double c2y, double x, double y);
void  path_close(Path *p);                                  /* the last subpath closed (its first anchor's in handle kept) */
int   path_append(Path *p, const Path *q);                  /* q's subpaths added to p */
/* the Pen tool's editing. Anchors are addressed by subpath s and index k inside it. Each returns
   0 if there is no such anchor (or out of memory) */
int   path_add_anchor(Path *p, int s, int k, double x, double y);     /* a corner anchor put before k (k = n: after the last) */
int   path_split_segment(Path *p, int s, int seg, double t);         /* Add Anchor Point: segment seg cut at t (0-1), the shape kept; the new anchor's index */
int   path_delete_anchor(Path *p, int s, int k);                     /* (a subpath left with no anchors goes too) */
int   path_delete_subpath(Path *p, int s);
int   path_move_anchor(Path *p, int s, int k, double dx, double dy); /* its handles go with it */
/* a handle (0 in, 1 out) dragged to x, y: a smooth anchor's other handle turns to stay opposite
   (keeping its length) unless alone is set (Alt-drag), which also makes the anchor a corner */
int   path_move_handle(Path *p, int s, int k, int which, double x, double y, int alone);
/* both handles set symmetrically: out at x, y and in mirrored (the Pen's click-and-drag) */
int   path_drag_handles(Path *p, int s, int k, double x, double y);
/* Convert Point: to a corner (handles pulled in) or smooth (handles a third of the way to the
   neighbours, along the line between them) */
int   path_convert_anchor(Path *p, int s, int k, int type);
void  path_reverse(Path *p, int s);                         /* a subpath's direction turned round */
/* every point mapped by m: x' = m0 x + m1 y + m2, y' = m3 x + m4 y + m5 */
void  path_transform(Path *p, const double *m);
/* the extent of the curves (flattened); 0 if the path is empty */
int   path_bounds(const Path *p, double *x0, double *y0, double *x1, double *y1);
/* the point on segment seg of subpath s at t, and the segment's four control points */
void  path_seg_ctrl(const Path *p, int s, int seg, double *c);   /* c: 8 doubles */
int   path_nsegs(const Path *p, int s);                          /* n - 1 open, n closed */
void  bez_point(const double *c, double t, double *x, double *y);

/* ------------------------------------------------------------------ polygons */
/* contours of points: contour c is xy[2 * start .. 2 * end[c]) with start = end[c - 1] (or 0) */
typedef struct Poly {                 /* (r3: named, so brush.h can point at one) */
  double *xy; i32 n, cap;             /* points */
  i32 *end; u8 *closed; i32 nc, capc;
} Poly;
void  poly_init(Poly *pl);
void  poly_free(Poly *pl);
int   poly_add(Poly *pl, double x, double y);              /* to the contour being built (0: out of memory) */
int   poly_end(Poly *pl, int closed);                      /* the contour ended */
/* the path's curves cut into lines no further than tol pixels from them (0: a quarter pixel),
   appended to pl; 0 if out of memory */
int   path_flatten(const Path *p, double tol, Poly *pl);
/* signed area of the closed contours (positive clockwise on screen, y down), as non-zero filling counts it */
double poly_area(const Poly *pl);

/* coverage of the closed contours (open ones are closed for filling) in box clip, anti-aliased:
   cov[(y - clip.y0) * stride + (x - clip.x0)] gets 0-255 (overwritten). Non-zero winding unless
   evenodd. Exact across, 16 sample rows down a pixel. 0 if out of memory */
int   poly_raster(const Poly *pl, int evenodd, Box clip, u8 *cov, long stride);
/* the extent of the points, as whole pixels (empty for none) */
Box   poly_box(const Poly *pl);

/* strokes: a polyline set as outlines to fill (non-zero) */
enum { SA_CENTER, SA_INSIDE, SA_OUTSIDE };   /* where a closed shape's stroke goes */
enum { CAP_BUTT, CAP_ROUND, CAP_SQUARE };
enum { JOIN_MITER, JOIN_ROUND, JOIN_BEVEL };
typedef struct {
  double width;
  u8 cap, join;
  double miter;                       /* miter limit, in widths (0: 4) */
  double dash[8]; int ndash;          /* on, off, on, off... pixels (ndash 0: solid) */
  double dash_off;
} StrokeStyle;
/* the outline of pl stroked by st, appended to out as contours all turning the same way */
int   poly_stroke(const Poly *pl, const StrokeStyle *st, Poly *out);
/* pl cut by a dash pattern into open contours */
int   poly_dash(const Poly *pl, const double *dash, int ndash, double off, Poly *out);

/* ------------------------------------------------------------------ hit testing (the UI's tools) */
enum { PH_NONE, PH_ANCHOR, PH_IN, PH_OUT, PH_SEGMENT };
typedef struct { int what, s, k; double t, dist; } PathHit;  /* PH_SEGMENT: k the segment, t along it */
/* the nearest anchor, handle (only those of anchors with sel set, or all if handles_all) or
   segment within tol pixels of x, y; anchors win over handles, handles over segments. PH_NONE if none */
int   path_hit(const Path *p, double x, double y, double tol, int handles_all, PathHit *h);

/* ------------------------------------------------------------------ SVG path data */
/* "M x y C x1 y1 x2 y2 x y ... Z" (absolute, 3 decimals), malloc'd; "" for an empty path */
char *path_to_svg(const Path *p);
/* SVG path data (M L H V C S Q T Z, absolute and relative; A arcs as curves) into a new path;
   NULL if out of memory. Smooth anchors are found where the handles are in line */
Path *path_from_svg(const char *d);
/* the anchors' types as a string, one 'c' or 's' each (for keeping them exactly), and back */
char *path_types(const Path *p);
void  path_set_types(Path *p, const char *t);

/* ------------------------------------------------------------------ the document's paths */
/* path i (-1: the work path, made empty if there is none and make is set); NULL if none */
Path *doc_path(Doc *d, int i, int make);
int   doc_path_new(Doc *d, const char *name);              /* an empty saved path (name NULL: "Path N"); its index or -1 */
int   doc_path_save_work(Doc *d, const char *name);        /* the work path becomes a saved one; its index or -1 */
int   doc_path_delete(Doc *d, int i);                      /* -1: the work path */
int   doc_path_duplicate(Doc *d, int i);
int   doc_path_rename(Doc *d, int i, const char *name);
int   doc_path_move(Doc *d, int from, int to);
/* path i (-1 work, created if needed) replaced by a copy of p; one undo step called action */
int   doc_path_set(Doc *d, int i, const Path *p, const char *action);
#endif
