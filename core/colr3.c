/*
 * colr3.c - the colour picker's CMYK and web colours (see colr3.h). Easel round 3.
 */
#include "colr3.h"

static int rnd(double v) { return (int)(v + 0.5); }
void rgb_to_cmyk100(u32 rgb, int *c, int *m, int *y, int *k)
{
  double r = ((rgb >> 16) & 255) / 255.0, g = ((rgb >> 8) & 255) / 255.0, b = (rgb & 255) / 255.0;
  double mx = r > g ? (r > b ? r : b) : (g > b ? g : b), kk = 1 - mx;
  *k = rnd(kk * 100);
  if (mx <= 0) { *c = *m = *y = 0; return; }
  *c = rnd((mx - r) / mx * 100); *m = rnd((mx - g) / mx * 100); *y = rnd((mx - b) / mx * 100);
}
u32 cmyk100_to_rgb(int c, int m, int y, int k)
{
  double kk, v[3];
  int i, ch[3];
  ch[0] = c; ch[1] = m; ch[2] = y;
  if (k < 0) k = 0;
  if (k > 100) k = 100;
  kk = 1 - k / 100.0;
  for (i = 0; i < 3; i++) {
    int q = ch[i] < 0 ? 0 : ch[i] > 100 ? 100 : ch[i];
    v[i] = (1 - q / 100.0) * kk * 255;
  }
  return (u32)rnd(v[0]) << 16 | (u32)rnd(v[1]) << 8 | (u32)rnd(v[2]);
}
u32 web_safe(u32 rgb)
{
  u32 o = 0;
  int s;
  for (s = 16; s >= 0; s -= 8) o |= (u32)(((((rgb >> s) & 255) + 25) / 51) * 51) << s;
  return o;
}
int is_web_safe(u32 rgb) { return web_safe(rgb) == (rgb & 0xFFFFFF); }
