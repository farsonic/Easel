/*
 * selx.c - marquee styles, Single Row and Column, and Color Range (see selx.h).
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "selx.h"
#include "paint.h"
#include "channels.h"

const char *const color_range_names[CR_N] = { "Sampled Colors", "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas",
  "Highlights", "Midtones", "Shadows", "Skin Tones" };

/* ------------------------------------------------------------------ marquee */
static i32 rnd(double v) { return (i32)floor(v + 0.5); }
Box marquee_box(int style, double rw, double rh, double ax, double ay, double px, double py, int square, int from_centre)
{
  double dx = px - ax, dy = py - ay, w = fabs(dx), h = fabs(dy), sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
  double x0, y0, x1, y1;
  if (style == MQ_FIXED_SIZE && rw >= 1 && rh >= 1) {
    w = rw; h = rh;
    if (from_centre) { x0 = ax - w / 2; y0 = ay - h / 2; }
    else { x0 = sx < 0 ? ax - w : ax; y0 = sy < 0 ? ay - h : ay; }
    return box_make(rnd(x0), rnd(y0), rnd(x0) + rnd(w), rnd(y0) + rnd(h));
  }
  if (style == MQ_FIXED_RATIO && rw > 0 && rh > 0) {
    /* the box reaches the pointer on its farther side */
    double s = w / rw > h / rh ? w / rw : h / rh;
    w = s * rw; h = s * rh;
  } else if (square) w = h = w > h ? w : h;
  if (from_centre) { x0 = ax - w; x1 = ax + w; y0 = ay - h; y1 = ay + h; }
  else {
    x0 = ax; x1 = ax + sx * w; y0 = ay; y1 = ay + sy * h;
    if (x1 < x0) { double t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { double t = y0; y0 = y1; y1 = t; }
  }
  return box_make(rnd(x0), rnd(y0), rnd(x1), rnd(y1));
}

/* ------------------------------------------------------------------ single row and column */
void sel_single_row(Doc *d, i32 y, int op)
{
  u8 *m;
  if (y < 0 || y >= d->h || !(m = (u8 *)calloc((size_t)d->w * d->h, 1))) return;
  memset(m + (long)y * d->w, 255, (size_t)d->w);
  sel_apply_mask(d, m, op, "Single Row Marquee");
}
void sel_single_col(Doc *d, i32 x, int op)
{
  u8 *m;
  i32 y;
  if (x < 0 || x >= d->w || !(m = (u8 *)calloc((size_t)d->w * d->h, 1))) return;
  for (y = 0; y < d->h; y++) m[(long)y * d->w + x] = 255;
  sel_apply_mask(d, m, op, "Single Column Marquee");
}

/* ------------------------------------------------------------------ Color Range */
/* 0-1 ramp: 0 at or below a, 1 at or above b (a > b runs downwards) */
static double ramp(double v, double a, double b)
{
  double t = (v - a) / (b - a);
  return t <= 0 ? 0 : t >= 1 ? 1 : t * t * (3 - 2 * t);
}
/* how much of a hue range an RGB colour is in: full within 30 degrees of the range's centre,
   none past 60, and only as much as it is saturated (greys are in no hue range) */
static double hue_in(const u8 *p, double centre)
{
  double h, s, v, dh;
  rgb_to_hsv(p[0], p[1], p[2], &h, &s, &v);
  dh = fabs(h - centre);
  if (dh > 180) dh = 360 - dh;
  return (1 - ramp(dh, 30, 60)) * ramp(s * v * 255, 8, 48);
}
static double luma(const u8 *p) { return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]; }
static double preset_value(const ColorRange *cr, const u8 *p)
{
  double r = (cr->range > 0 ? cr->range : 50) / 50.0, l = luma(p), h, s, v;
  switch (cr->preset) {
  case CR_REDS: return hue_in(p, 0);
  case CR_YELLOWS: return hue_in(p, 60);
  case CR_GREENS: return hue_in(p, 120);
  case CR_CYANS: return hue_in(p, 180);
  case CR_BLUES: return hue_in(p, 240);
  case CR_MAGENTAS: return hue_in(p, 300);
  case CR_HIGHLIGHTS: return ramp(l, 255 - 128 * r, 255 - 64 * r);
  case CR_SHADOWS: return 1 - ramp(l, 64 * r, 128 * r);
  case CR_MIDTONES: { double dl = fabs(l - 128); return 1 - ramp(dl, 32 * r, 80 * r); }
  case CR_SKIN:
    /* the usual skin hues (orange-red, 5-45 degrees), moderately saturated, not dark */
    rgb_to_hsv(p[0], p[1], p[2], &h, &s, &v);
    if (h > 180) h -= 360;
    return (1 - ramp(fabs(h - 22), 20, 35)) * ramp(s, 0.08, 0.18) * (1 - ramp(s, 0.65, 0.8)) * ramp(v, 0.2, 0.35);
  }
  return 0;
}
/* closeness to colour c within fuzz (Euclidean RGB): 1 at the colour, fading to 0 at fuzz */
static double near_col(const u8 *p, u32 c, int fuzz)
{
  double dr = p[0] - (double)((c >> 16) & 255), dg = p[1] - (double)((c >> 8) & 255), db = p[2] - (double)(c & 255);
  double dist = sqrt(dr * dr + dg * dg + db * db);
  if (fuzz < 1) return dist < 0.5;
  return dist >= fuzz ? 0 : 1 - dist / fuzz;
}
u8 *color_range_mask(Doc *d, const ColorRange *cr)
{
  long k, n = (long)d->w * d->h;
  int all = cr->all_layers || d->active < 0 || d->active >= d->nlay || !d->lay[d->active]->px, i;
  u8 *img = all ? (u8 *)malloc((size_t)n * 4) : NULL, *m;
  const u8 *src;
  int fuzz = cr->fuzziness < 0 ? 0 : cr->fuzziness > 200 ? 200 : cr->fuzziness;
  if (all && !img) return NULL;
  if (!(m = (u8 *)malloc((size_t)n))) { free(img); return NULL; }
  if (all) { doc_composite(d, doc_box(d), img, (long)d->w * 4); src = img; } else src = d->lay[d->active]->px;
  for (k = 0; k < n; k++) {
    const u8 *p = src + k * 4;
    double v = 0;
    if (!(k & 4095) && progress(k, n)) break;              /* (0.8: progress, and Abort) */
    if (cr->preset == CR_SAMPLED) {
      for (i = 0; i < cr->ncolors && i < CR_MAXCOLORS; i++) { double t = near_col(p, cr->colors[i], fuzz); if (t > v) v = t; }
    } else v = preset_value(cr, p);
    for (i = 0; i < cr->nminus && i < CR_MAXCOLORS; i++) v *= 1 - near_col(p, cr->minus[i], fuzz);
    if (cr->invert) v = 1 - v;
    m[k] = (u8)(v * 255 + 0.5);
  }
  free(img);
  if (k < n) { free(m); return NULL; }
  return m;
}
void sel_color_range_ex(Doc *d, const ColorRange *cr, int op)
{
  u8 *m = color_range_mask(d, cr);
  if (m) sel_apply_mask(d, m, op, "Color Range");
}
