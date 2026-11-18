/*
 * aspect.c - pixel aspect presets and the canvas's vertical scale (see aspect.h).
 */
#include "aspect.h"

/* the Amiga figures are amigamodes.c's (the EA ILBM specification's, and the display database's
   ticks for NTSC); the video ones are PhotoCraft's (and Photoshop's) */
static const AspectPreset presets[] = {
  { "Square", 1, 1 },
  { "Amiga PAL Low Res", 10, 11 },
  { "Amiga PAL Low Res Laced", 20, 11 },
  { "Amiga PAL High Res", 5, 11 },
  { "Amiga PAL Super-High Res", 5, 22 },
  { "Amiga NTSC Low Res", 44, 52 },
  { "Amiga NTSC Low Res Laced", 44, 26 },
  { "Amiga NTSC High Res", 22, 52 },
  { "D1/DV NTSC (0.91)", 10, 11 },
  { "D1/DV PAL (1.09)", 12, 11 },
  { "D1/DV NTSC Widescreen (1.21)", 40, 33 },
  { "D1/DV PAL Widescreen (1.46)", 16, 11 },
  { "HDV 1080/DVCPRO HD 720 (1.33)", 4, 3 },
  { "DVCPRO HD 1080 (1.5)", 3, 2 },
  { "Anamorphic 2:1 (2)", 2, 1 },
};
#define NPRESETS (int)(sizeof presets / sizeof presets[0])

int aspect_count(void) { return NPRESETS; }
const AspectPreset *aspect_get(int i) { return i >= 0 && i < NPRESETS ? &presets[i] : NULL; }

int aspect_find(int ax, int ay)
{
  int i;
  if (ax <= 0 || ay <= 0) ax = ay = 1;
  for (i = 0; i < NPRESETS; i++) if ((long)presets[i].ax * ay == (long)presets[i].ay * ax) return i;
  return -1;
}

double aspect_yscale(int ax, int ay, int on)
{
  double s;
  if (!on || ax <= 0 || ay <= 0 || ax == ay) return 1.0;
  s = (double)ay / ax;
  if (s < 0.25) s = 0.25;
  if (s > 4.0) s = 4.0;
  return s;
}

void aspect_from_ratio(double r, u8 *ax, u8 *ay)
{
  int y, bx = 1, by = 1;
  double best = 1e9;
  if (!(r > 0)) r = 1;
  if (r > 255) r = 255;
  if (r < 1.0 / 255) r = 1.0 / 255;
  for (y = 1; y <= 255; y++) {
    int x = (int)(r * y + 0.5);
    double e;
    if (x < 1 || x > 255) continue;
    e = (double)x / y - r; if (e < 0) e = -e;
    if (e < best - 1e-9) { best = e; bx = x; by = y; }
  }
  *ax = (u8)bx; *ay = (u8)by;
}
