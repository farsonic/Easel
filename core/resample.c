/*
 * resample.c - Image Size's Bicubic Smoother, Bicubic Sharper and Preserve Details (see resample.h).
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "resample.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int clamp255(double v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5); }
/* Mitchell-Netravali with B = C = 1/3 */
static double mitchell(double t)
{
  t = fabs(t);
  if (t < 1) return (7 * t * t * t - 12 * t * t + 16.0 / 3) / 6;
  if (t < 2) return (-7.0 / 3 * t * t * t + 12 * t * t - 20 * t + 32.0 / 3) / 6;
  return 0;
}
static double catmull(double t)
{
  t = fabs(t);
  if (t < 1) return (1.5 * t - 2.5) * t * t + 1;
  if (t < 2) return ((-0.5 * t + 2.5) * t - 4) * t + 2;
  return 0;
}
static double lanczos3(double t)
{
  double a;
  t = fabs(t);
  if (t < 1e-9) return 1;
  if (t >= 3) return 0;
  a = M_PI * t;
  return 3 * sin(a) * sin(a / 3) / (a * a);
}
void resample_kernel(const u8 *px, i32 w, i32 h, double x, double y, int method, u8 *out)
{
  int r = method == RS_DETAILS ? 3 : 2, i, j, k;
  double cx = x - 0.5, cy = y - 0.5, wx[6], wy[6], sx = 0, sy = 0, acc[4] = { 0, 0, 0, 0 };
  i32 x0 = (i32)floor(cx) - r + 1, y0 = (i32)floor(cy) - r + 1;
  for (i = 0; i < 2 * r; i++) {
    double tx = cx - (x0 + i), ty = cy - (y0 + i);
    wx[i] = method == RS_SMOOTHER ? mitchell(tx) : method == RS_DETAILS ? lanczos3(tx) : catmull(tx);
    wy[i] = method == RS_SMOOTHER ? mitchell(ty) : method == RS_DETAILS ? lanczos3(ty) : catmull(ty);
    sx += wx[i]; sy += wy[i];
  }
  for (i = 0; i < 2 * r; i++) { wx[i] /= sx; wy[i] /= sy; }
  for (j = 0; j < 2 * r; j++) {
    i32 yy = y0 + j;
    if (yy < 0) yy = 0;
    if (yy >= h) yy = h - 1;
    for (i = 0; i < 2 * r; i++) {
      i32 xx = x0 + i;
      const u8 *p;
      double wgt = wx[i] * wy[j];
      if (xx < 0) xx = 0;
      if (xx >= w) xx = w - 1;
      p = px + ((long)yy * w + xx) * 4;
      for (k = 0; k < 3; k++) acc[k] += (double)p[k] * p[3] * wgt;
      acc[3] += p[3] * 255.0 * wgt;
    }
  }
  for (k = 0; k < 4; k++) if (acc[k] < 0) acc[k] = 0;
  if (acc[3] > 255 * 255) { double f = 255.0 * 255 / acc[3]; for (k = 0; k < 4; k++) acc[k] *= f; }
  if (acc[3] <= 0.5) { out[0] = out[1] = out[2] = out[3] = 0; return; }
  for (k = 0; k < 3; k++) out[k] = (u8)clamp255(acc[k] / acc[3] * 255);
  out[3] = (u8)clamp255(acc[3] / 255);
}
void resample_sharpen(u8 *px, i32 w, i32 h)
{
  /* a 3 x 3 blur (1 2 1) subtracted at half strength, row by row with three rows kept */
  u8 *rows = (u8 *)malloc((size_t)w * 4 * 3);
  i32 x, y;
  if (!rows || w < 3 || h < 3) { free(rows); return; }
  memcpy(rows, px, (size_t)w * 4);
  memcpy(rows + (long)w * 4, px, (size_t)w * 4);
  for (y = 0; y < h; y++) {
    u8 *up = rows + (long)(y % 3) * w * 4, *mid = rows + (long)((y + 1) % 3) * w * 4, *dn = rows + (long)((y + 2) % 3) * w * 4;
    memcpy(dn, px + (long)(y + 1 < h ? y + 1 : h - 1) * w * 4, (size_t)w * 4);
    if (y == 0) memcpy(up, mid, (size_t)w * 4);
    for (x = 0; x < w; x++) {
      i32 xl = x > 0 ? x - 1 : 0, xr = x + 1 < w ? x + 1 : w - 1;
      int c;
      u8 *o = px + ((long)y * w + x) * 4;
      for (c = 0; c < 3; c++) {
        double b = (up[xl * 4 + c] + 2 * up[x * 4 + c] + up[xr * 4 + c] + 2 * (mid[xl * 4 + c] + 2 * mid[x * 4 + c] + mid[xr * 4 + c]) +
                    dn[xl * 4 + c] + 2 * dn[x * 4 + c] + dn[xr * 4 + c]) / 16.0;
        o[c] = (u8)clamp255(mid[x * 4 + c] + (mid[x * 4 + c] - b) * 0.5);
      }
    }
  }
  free(rows);
}
