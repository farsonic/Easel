/*
 * selx.h - the selection tools' extras (Easel 0.7): the marquee's Fixed Ratio and Fixed Size styles,
 * the Single Row and Single Column marquees, and Select > Color Range with sampled colours, ranges
 * (Reds to Magentas, Highlights, Midtones, Shadows, Skin Tones), fuzziness and Invert.
 * (Anti-aliasing for the lassos, the elliptical marquee and the wand is in paint.h: sel_*_ex.)
 */
#ifndef EASEL_SELX_H
#define EASEL_SELX_H

#include "doc.h"

/* ------------------------------------------------------------------ the marquee's styles */
enum { MQ_NORMAL, MQ_FIXED_RATIO, MQ_FIXED_SIZE };
/* the box a marquee drag makes, from the anchor ax, ay (where the button went down) to the pointer
   px, py: MQ_NORMAL follows the pointer (square: width = height, for Shift); MQ_FIXED_RATIO keeps
   width : height = rw : rh, as large as the pointer allows; MQ_FIXED_SIZE is rw x rh pixels with a
   corner on the anchor, on the pointer's side. from_centre (Alt): the anchor is the box's centre.
   The UI calls it on every pointer move to draw the box and once more on release, then passes the
   box to sel_rect or sel_ellipse_ex. Rounded to whole pixels; x1, y1 exclusive */
Box  marquee_box(int style, double rw, double rh, double ax, double ay, double px, double py, int square, int from_centre);

/* ------------------------------------------------------------------ Single Row and Single Column */
void sel_single_row(Doc *d, i32 y, int op);                /* the 1-pixel row through y, by SEL_ op */
void sel_single_col(Doc *d, i32 x, int op);

/* ------------------------------------------------------------------ Color Range */
enum { CR_SAMPLED, CR_REDS, CR_YELLOWS, CR_GREENS, CR_CYANS, CR_BLUES, CR_MAGENTAS,
       CR_HIGHLIGHTS, CR_MIDTONES, CR_SHADOWS, CR_SKIN, CR_N };
extern const char *const color_range_names[CR_N];
#define CR_MAXCOLORS 16
typedef struct {
  int preset;                         /* CR_ */
  u32 colors[CR_MAXCOLORS]; int ncolors;   /* CR_SAMPLED: the colours picked (the eyedropper, then +) */
  u32 minus[CR_MAXCOLORS]; int nminus;     /* colours taken away (the - eyedropper) */
  int fuzziness;                      /* 0-200: how far from a sampled colour still counts, fading out */
  int range;                          /* Highlights, Midtones, Shadows: how wide (0-100; 0 means 50) */
  int invert;
  int all_layers;                     /* sample what shows (1) or the active layer (0) */
  /* r4: Localized Color Clusters - each sampled colour counts only near where it was picked (x, y
     in canvas pixels; has_xy 0 for a colour from the foreground, which counts everywhere), fading
     to nothing at lrange per cent of the picture's longer side */
  int localized, lrange;
  i32 cx[CR_MAXCOLORS], cy[CR_MAXCOLORS]; u8 has_xy[CR_MAXCOLORS];
} ColorRange;
/* the soft mask Color Range would make (w * h, 0-255, malloc'd; the dialog's preview), NULL if out of memory */
u8  *color_range_mask(Doc *d, const ColorRange *cr);
/* and it made the selection by SEL_ op (one undo step) */
void sel_color_range_ex(Doc *d, const ColorRange *cr, int op);
#endif
