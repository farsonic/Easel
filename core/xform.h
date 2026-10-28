/*
 * xform.h - Edit > Free Transform and Edit > Transform (Easel 0.7): scale, rotate, skew, distort and
 * perspective of a layer or of the selected pixels, Transform Selection, Transform Again, exact
 * flips and quarter turns of the selected region, and Alt-drag duplicating with the Move tool.
 *
 * Every transform is a projective map (a 3 x 3 homography) taking a source box onto a destination
 * quadrilateral: its four corners, which the UI's handles move. Pixels are resampled from the
 * destination back to the source (nearest, bilinear or bicubic, as paint.h's RS_), with
 * anti-aliased edges; shrinking more than 1.5 times is supersampled. A transform that only swaps
 * or mirrors whole pixels (flips, quarter turns, whole-pixel moves) is done exactly.
 *
 * Coordinates are image pixels with pixel corners on whole numbers (pixel x, y covers x..x+1);
 * angles are degrees clockwise on the screen (y grows downwards).
 */
#ifndef EASEL_XFORM_H
#define EASEL_XFORM_H

#include "doc.h"

/* a transform: the source box, and where its corners go: q[0..7] = top left x, y, top right,
   bottom right, bottom left (clockwise on the screen) */
typedef struct {
  Box src;
  double q[8];
} XForm;
enum { XC_TL, XC_TR, XC_BR, XC_BL };                       /* corner numbers: q[2 * c], q[2 * c + 1] */
enum { XE_TOP, XE_RIGHT, XE_BOTTOM, XE_LEFT };              /* edge e runs from corner e to corner (e + 1) % 4 */

/* ------------------------------------------------------------------ building a transform */
/* what Free Transform starts from: the selection's box, or else the layer's non-transparent pixels
   (empty if there is nothing) */
Box  xform_source_box(Doc *d, int layer);
void xform_init(XForm *t, Box src);                         /* the identity: corners on the box's */
/* the box scaled by sx, sy, skewed by skx (x moves by tan(skx) * y) and sky, rotated by angle and
   moved by dx, dy, all about the reference point rx, ry (the box's centre is the usual one; Free
   Transform's options bar shows these numbers). Replaces t's corners */
void xform_set_affine(XForm *t, Box src, double rx, double ry, double sx, double sy, double angle,
                      double skx, double sky, double dx, double dy);
/* the corners taken further through a projective map h (3 x 3, row major) or simple steps */
void xform_compose(XForm *t, const double *h);
void xform_translate(XForm *t, double dx, double dy);
void xform_scale_about(XForm *t, double sx, double sy, double cx, double cy);
void xform_rotate_about(XForm *t, double angle, double cx, double cy);
/* the handles' modes: Distort moves one corner freely; Perspective moves the corner and mirrors the
   move on its neighbour along the edge it moved along (a horizontal drag moves the corner sharing
   its top or bottom edge the other way); Skew slides one edge (both its corners) along itself */
void xform_distort(XForm *t, int corner, double x, double y);
void xform_perspective(XForm *t, int corner, double x, double y);
void xform_skew(XForm *t, int edge, double dx, double dy);

/* ------------------------------------------------------------------ the maths */
int  xform_matrix(const XForm *t, double *h);               /* source to destination; 0 if degenerate */
int  xform_inverse(const XForm *t, double *h);              /* destination to source; 0 if degenerate */
void xform_point(const double *h, double x, double y, double *ox, double *oy);
Box  xform_bounds(const XForm *t);                          /* the pixels the quad touches */
int  xform_valid(const XForm *t);                           /* a convex, non-degenerate quad (what can be applied) */
/* the 3 x 3 maps from the unit square to quad q (4 corners, as XForm's) and between two quads */
int  hom_square_to_quad(const double *q, double *h);
int  hom_quad_to_quad(const double *from, const double *to, double *h);
int  hom_invert(const double *h, double *inv);
void hom_mul(const double *a, const double *b, double *out);   /* out = a * b (b applied first) */

/* ------------------------------------------------------------------ applying it */
/* layer's pixels in t->src (only the selected ones when there is a selection: they float, and the
   selection goes with them) put through t, interp RS_. One undo step; the area changed (empty if
   the layer can't be changed or t isn't valid). Remembered for Transform Again */
Box  xform_layer(Doc *d, int layer, const XForm *t, int interp);
/* Select > Transform Selection: the selection's mask alone put through t. One undo step; 0 if nothing done */
int  xform_selection(Doc *d, const XForm *t);
/* Edit > Transform > Again: the last transform, about the centre of the box it was made on, applied
   to layer's xform_source_box. xform_again_get only builds it (0 if there is none) */
int  xform_again_get(Doc *d, Box src, XForm *t);
Box  xform_again(Doc *d, int layer, int interp);
/* the selected region of layer flipped or turned by quarter turns (1 clockwise, 2, 3 anticlockwise)
   within the selection's box, exactly; the whole layer when there is no selection */
Box  xform_flip(Doc *d, int layer, int vertical);
Box  xform_rotate90(Doc *d, int layer, int quarter_turns);
/* the same on the selection's mask alone (Transform Selection's Flip and Rotate items) */
int  sel_flip(Doc *d, int vertical);
int  sel_rotate90(Doc *d, int quarter_turns);

/* ------------------------------------------------------------------ the Move tool */
/* Alt-drag: the selected pixels of the active layer copied (not cut) dx, dy along, over what is
   there; the selection goes with the copy. With no selection the layer is duplicated and the copy
   moved (it becomes the active layer). One undo step; the area changed */
Box  move_selection_copy(Doc *d, i32 dx, i32 dy);
#endif
