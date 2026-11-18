/*
 * aspect.h - Easel 1.2: pixel aspect ratios. View > Pixel Aspect Ratio's presets (square, the
 * Amiga's PAL and NTSC modes, and PhotoCraft's video ones), and the vertical scale the canvas
 * draws a picture at so that non-square pixels look as they will on their screen (View > Pixel
 * Aspect Ratio Correction). Portable C99.
 *
 * A document's aspect is doc.h's aspect_x:aspect_y, the pixel's width to its height as IFF BMHD
 * keeps it (0 or 1:1 square). The canvas keeps its horizontal zoom and draws rows aspect_y/aspect_x
 * times as tall (a hires pixel, 5:11, is drawn 2.2 times as tall as it is wide).
 */
#ifndef EASEL_ASPECT_H
#define EASEL_ASPECT_H

#include "util.h"

typedef struct {
  const char *name;                   /* English, for the menu (tr()) */
  u8 ax, ay;                          /* pixel width : height */
} AspectPreset;

int  aspect_count(void);
const AspectPreset *aspect_get(int i);                /* NULL out of range */
/* the preset with this ratio (any multiple of it), -1 if none (Custom) */
int  aspect_find(int ax, int ay);
/* the vertical scale for a document's aspect: ay / ax, kept within 1/4 to 4; 1 when on is 0 or
   the aspect is unset or square */
double aspect_yscale(int ax, int ay, int on);
/* a ratio such as 0.91 (width / height) as the nearest x:y with both at most 255 */
void aspect_from_ratio(double r, u8 *ax, u8 *ay);

#endif
