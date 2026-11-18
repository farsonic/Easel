/*
 * adjustx.h - more of Image > Adjustments: Color Lookup (3D LUTs from .cube files, trilinear or
 * tetrahedral, with dither), Equalize's choice of area when there is a selection, and Black & White
 * Auto (the six colour sliders set from the picture).
 */
#ifndef EASEL_ADJUSTX_H
#define EASEL_ADJUSTX_H

#include "doc.h"

/* a 3D lookup table: n x n x n entries of RGB in 0..1, red changing fastest (the .cube order), for
   inputs from dmin to dmax (the domain, usually 0..1) */
typedef struct { int n; float *t; float dmin[3], dmax[3]; char title[64]; } Lut3D;
#define LUT_MAXN 65

/* a .cube file's text (Adobe/Resolve: LUT_3D_SIZE, or LUT_1D_SIZE made a 3D table, DOMAIN_MIN/MAX,
   TITLE, # comments) as a table; NULL with a reason in err if it isn't one or memory runs out */
Lut3D *lut_parse_cube(const char *text, long len, char *err, int errcap);
Lut3D *lut_load_cube(const char *path, char *err, int errcap);
Lut3D *lut_identity(int n);
void   lut_free(Lut3D *l);
/* the table as .cube text (malloc'd, its length in *len) */
char  *lut_format_cube(const Lut3D *l, long *len);
/* one colour (0..255 each) through the table: tetrahedral (else trilinear) interpolation; out in 0..1 */
void   lut_lookup(const Lut3D *l, const u8 *rgb, int tetrahedral, double *out);
/* Image > Adjustments > Color Lookup on the active layer (through the selection, one undo step);
   opacity 0-100 blends with the original; dither spreads the rounding to 8 bits (an ordered dither).
   1 if anything changed */
int    adj_color_lookup(Doc *d, const Lut3D *l, int tetrahedral, int dither, int opacity);

/* Equalize with a choice when part of the layer is selected: EQ_SELECTED evens out the selected
   area by its own histogram (what Equalize did); EQ_ENTIRE evens out the whole layer by the
   selected area's histogram. With no selection both even out the whole layer */
enum { EQ_SELECTED, EQ_ENTIRE };
int    adj_equalize_ex(Doc *d, int mode);

/* Black & White's Auto: the six colour weights (per cent, -200..300, Reds Yellows Greens Cyans
   Blues Magentas) that spread the picture's greys widest without clipping, from the active layer's
   pixels (in the selection, if any); weights6 is where the search starts and gets the answer.
   0 if there are no pixels to look at */
int    bw_auto_weights(Doc *d, int *weights6);
#endif
