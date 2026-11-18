/*
 * modex.c - Easel round 3: Indexed Color's Matte (modex.h).
 */
#include "modex.h"

const char *const matte_names[MT_N + 1] = { "None", "Foreground Color", "Background Color", "White", "Black", "50% Grey", "Netscape Grey", NULL };

u32 matte_color(const Doc *d, int m)
{
  switch (m) {
  case MT_FG: return d->fg & 0xFFFFFF;
  case MT_BG: return d->bg & 0xFFFFFF;
  case MT_BLACK: return 0;
  case MT_GRAY50: return 0x808080;
  case MT_NETSCAPE: return 0xC0C0C0;
  default: return 0xFFFFFF;
  }
}
long layers_matte(Doc *d, u32 rgb, int keep)
{
  int i, c, m[3];
  long k, np = (long)d->w * d->h, n = 0;
  m[0] = (int)(rgb >> 16 & 255); m[1] = (int)(rgb >> 8 & 255); m[2] = (int)(rgb & 255);
  for (i = 0; i < d->nlay; i++) {
    Layer *l = d->lay[i];
    if (l->kind != LK_PIXEL || !l->px) continue;
    for (k = 0; k < np; k++) {
      u8 *p = l->px + k * 4;
      int a = p[3];
      if (a == 255 || (keep && a == 0)) continue;
      for (c = 0; c < 3; c++) p[c] = (u8)((p[c] * a + m[c] * (255 - a) + 127) / 255);
      p[3] = 255;
      n++;
    }
  }
  return n;
}
