/*
 * viewx.h - Easel 1.2: the core side of the View, Image and Window menu additions: pixel aspect
 * ratio presets, New Guide Layout and New Guides From Shape, Print Size and fitting a box on the
 * screen, Trim with its options, the transparency checkerboard and grid line styles the
 * preferences choose, and the pixel grid. Portable C99.
 */
#ifndef EASEL_VIEWX_H
#define EASEL_VIEWX_H
#include "doc.h"

/* ------------------------------------------------------------------ pixel aspect ratio */
/* a preset: its name and the pixel's width:height (IFF BMHD's xAspect:yAspect) */
typedef struct { const char *name; u8 ax, ay; } ParPreset;
#define PAR_N 13
extern const ParPreset par_presets[PAR_N];
/* the fraction nearest w/h (a pixel's width over its height) with both terms 1-255 */
void   par_approx(double w_over_h, u8 *ax, u8 *ay);
/* the picture's pixel aspect set (one undo step; 0 if it is that already or the terms are 0) */
int    par_set(Doc *d, int ax, int ay);
/* the preset the picture's aspect equals (1:1 when unset), or -1 */
int    par_find(const Doc *d);
/* how tall a pixel is drawn for its width: ay/ax when correction is on and the aspect is set, else 1 */
double par_view_ratio(const Doc *d, int correct);

/* ------------------------------------------------------------------ guides */
/* View > New Guide Layout: columns and rows (0 none) with their width or height (0: shared out
   evenly) and gutter, margins, columns and rows centred when they are narrower than the space;
   clear removes the guides there were first. How many guides it added */
typedef struct {
  int cols, colw, colgut;
  int rows, rowh, rowgut;
  int mtop, mleft, mbottom, mright;
  int centre, clear;
} GuideLayout;
int    guide_layout(Doc *d, const GuideLayout *g);
/* View > New Guides From Shape: guides at a box's edges and centre lines; how many were added */
int    guides_from_box(Doc *d, Box b);
/* the box New Guides From Shape uses: the active layer's content (a shape's or type's drawing, or
   a pixel layer's opaque part), else the selection's; 0 if there is none */
int    guide_shape_box(Doc *d, Box *b);

/* ------------------------------------------------------------------ zoom */
/* View > Print Size: the zoom that shows the picture at its printed size on a screen of screen_dpi */
double zoom_print_size(int doc_dpi, int screen_dpi);
/* View > Fit Layer(s) on Screen: the zoom that fits box b in an area W x H (with a margin of 8
   pixels), its pixels shown ratio times as tall as wide */
double zoom_fit_box(Box b, long W, long H, double ratio);
/* the box Fit Layer(s) on Screen fits: the picked layers' content (or the active one's) */
int    fit_layers_box(Doc *d, Box *b);

/* ------------------------------------------------------------------ Image > Trim */
enum { TRIM_TRANSPARENT, TRIM_TOPLEFT, TRIM_BOTTOMRIGHT };
#define TRIM_TOP 1
#define TRIM_LEFT 2
#define TRIM_BOTTOM 4
#define TRIM_RIGHT 8
#define TRIM_ALL 15
/* the box Trim would leave: what differs from the transparent edge or the corner pixel's colour,
   cutting only the sides asked for. 0 if nothing would be cut (or out of memory) */
int    trim_box(Doc *d, int based_on, int sides, Box *b);
int    image_trim_ex(Doc *d, int based_on, int sides);       /* the picture cropped to it (one undo step) */

/* ------------------------------------------------------------------ the canvas's looks */
/* the transparency checkerboard (Preferences > Transparency): size 0 none, 1 small (4), 2 medium
   (8), 3 large (16); colours: one of the CHK_ schemes, or the custom two */
enum { CHK_LIGHT, CHK_MEDIUM, CHK_DARK, CHK_RED, CHK_ORANGE, CHK_GREEN, CHK_BLUE, CHK_PURPLE, CHK_CUSTOM, CHK_N };
extern const char *const checker_names[CHK_N];
extern const char *const checker_size_names[5];
/* the checkerboard's colour (0xRRGGBB) at view pixel x, y */
u32    checker_at(long x, long y, int size, int scheme, u32 c1, u32 c2);
/* grid and guide line styles: lines, dashed, dots; whether position p along a line is drawn */
enum { LS_LINES, LS_DASHED, LS_DOTS, LS_N };
extern const char *const line_style_names[LS_N + 1];
int    line_style_on(int style, long p);
/* the pixel grid (View > Show > Pixel Grid): drawn from 8 times up; whether screen column (or row)
   s, at zoom z with the image starting at o, is the first of an image pixel */
#define PIXGRID_MIN_ZOOM 8.0
int    pixgrid_line(long s, long o, double z);
/* View > Pattern Preview: an image coordinate wrapped into 0..size-1 */
long   wrap_coord(long v, long size);
#endif
