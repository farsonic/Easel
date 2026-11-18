/*
 * colr3.h - round 3: the colour picker's CMYK fields and Only Web Colors. The CMYK numbers are the
 * plain device conversion (no profile: black is what the brightest channel leaves, and cyan,
 * magenta and yellow what remains of each), as a picker shows them for picking; Image > Mode >
 * CMYK Color converts through the press or a profile instead (colorx.h).
 */
#ifndef EASEL_COLR3_H
#define EASEL_COLR3_H
#include "util.h"
/* 0xRRGGBB as C, M, Y, K per cent (0-100, rounded) and back */
void rgb_to_cmyk100(u32 rgb, int *c, int *m, int *y, int *k);
u32  cmyk100_to_rgb(int c, int m, int y, int k);
/* the nearest of the 216 web-safe colours (each channel a multiple of 0x33) */
u32  web_safe(u32 rgb);
int  is_web_safe(u32 rgb);
#endif
