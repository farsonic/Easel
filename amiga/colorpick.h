/*
 * colorpick.h - Easel 0.5's colour picker, as PhotoCraft lays it out: the big square (saturation
 * across, brightness up, for the hue), the hue strip beside it, the new colour over the old one,
 * and fields for H, S and B, R, G and B, L, a and b (CIE Lab, D65) and the hex value.
 *
 * WIRING (main.c): colorpick_run() takes pick_color()'s place (same arguments and result); it is
 * modal (run_modal). Clicking the old swatch takes the colour back.
 */
#ifndef EASEL_COLORPICK_H
#define EASEL_COLORPICK_H

#include "easel.h"

#define MUIA_CPick_Color   (QTAG + 0x1800)    /* (set/get, notify) the colour, 0xRRGGBB */
#define MUIA_CPick_Old     (QTAG + 0x1801)    /* (init/set) the colour before */

/* 0xRRGGBB chosen, or -1 if cancelled */
LONG colorpick_run(const char *title, ULONG start);
void colorpick_free(void);                    /* the class, at the end */
/* conversions (also for other windows): sRGB to and from CIE Lab (D65), L 0-100, a and b about -128..127 */
void rgb_to_lab(int r, int g, int b, double *L, double *A, double *B);
void lab_to_rgb(double L, double A, double B, int *r, int *g, int *b);

#endif
