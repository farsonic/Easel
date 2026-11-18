/*
 * resample.h - Image Size's further resampling methods (paint.h's image_resize takes them):
 * Bicubic Smoother (a Mitchell-Netravali cubic, softer, for enlarging), Bicubic Sharper (for
 * reducing: the area average, then sharpened a little) and Preserve Details (Lanczos-3, for
 * enlarging with the most detail).
 */
#ifndef EASEL_RESAMPLE_H
#define EASEL_RESAMPLE_H

#include "doc.h"

/* (the values carry on paint.h's RS_NEAREST, RS_BILINEAR, RS_BICUBIC) */
enum { RS_SMOOTHER = 3, RS_SHARPER, RS_DETAILS };
/* one sample of the RGBA picture px (w x h) at x, y (pixel centres at .5) with the method's
   kernel (RS_SMOOTHER: Mitchell; RS_SHARPER: Catmull-Rom; RS_DETAILS: Lanczos-3), premultiplied,
   the edge repeated */
void resample_kernel(const u8 *px, i32 w, i32 h, double x, double y, int method, u8 *out);
/* Bicubic Sharper's finish after a reduction: an unsharp mask of radius about a pixel, half strength */
void resample_sharpen(u8 *px, i32 w, i32 h);
#endif
