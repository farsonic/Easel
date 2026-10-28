/*
 * adjust.c - adjustments and filters (see adjust.h). Each works out new pixels for the active
 * layer in the selection's box; where the selection is partial (feathered) the new pixels are
 * blended with the old in proportion. Blurs work on premultiplied pixels, so transparent areas
 * don't bleed dark edges into what's around them.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "adjust.h"
#include "adjust10.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int clamp255(double v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5); }
static int lum8(const u8 *p) { return (p[0] * 77 + p[1] * 150 + p[2] * 29 + 128) >> 8; }

/* ------------------------------------------------------------------ the frame every operation shares */
typedef struct { Doc *d; Layer *l; Box b; u8 *orig; } Op;
/* an operation begun: the layer, its working box, an undo step, and (copy) the layer as it was */
static int op_begin(Op *o, Doc *d, const char *name, int copy)
{
  Layer *l;
  o->d = d; o->orig = NULL;
  if (d->active < 0 || d->active >= d->nlay) return 0;
  l = d->lay[d->active];
  if (!l->px || l->locked || l->lock_all || !l->visible) return 0;
  o->l = l;
  o->b = d->sel ? box_clip(d->selbox, doc_box(d)) : doc_box(d);
  if (box_empty(o->b)) return 0;
  if (copy || d->sel) {
    if (!(o->orig = (u8 *)malloc((size_t)d->w * d->h * 4))) return 0;
    memcpy(o->orig, l->px, (size_t)d->w * d->h * 4);
  }
  doc_begin(d, name);
  doc_note_layer(d, d->active, o->b);
  return 1;
}
/* finished: where the selection is partial, the new pixels blended with the old */
static int op_end(Op *o)
{
  Doc *d = o->d;
  i32 x, y;
  if (d->sel && o->orig)
    for (y = o->b.y0; y < o->b.y1; y++)
      for (x = o->b.x0; x < o->b.x1; x++) {
        long k = (long)y * d->w + x;
        int s = d->sel[k], c;
        u8 *p = o->l->px + k * 4;
        const u8 *q = o->orig + k * 4;
        if (s == 255) continue;
        for (c = 0; c < 4; c++) p[c] = (u8)(q[c] + ((p[c] - q[c]) * s + (p[c] >= q[c] ? 127 : -127)) / 255);
      }
  free(o->orig);
  doc_end(d);
  return 1;
}
/* three lookup tables (R, G, B) applied */
static int apply_luts(Doc *d, const char *name, const u8 *lut)
{
  Op o;
  i32 x, y;
  if (!op_begin(&o, d, name, 0)) return 0;
  for (y = o.b.y0; y < o.b.y1; y++) {
    u8 *p = o.l->px + ((long)y * d->w + o.b.x0) * 4;
    for (x = o.b.x0; x < o.b.x1; x++, p += 4) { p[0] = lut[p[0]]; p[1] = lut[256 + p[1]]; p[2] = lut[512 + p[2]]; }
  }
  return op_end(&o);
}
/* a function of each pixel's colour applied */
typedef void (*PixFn)(u8 *p, const double *arg);
static int apply_fn(Doc *d, const char *name, PixFn fn, const double *arg)
{
  Op o;
  i32 x, y;
  if (!op_begin(&o, d, name, 0)) return 0;
  for (y = o.b.y0; y < o.b.y1; y++) {
    u8 *p = o.l->px + ((long)y * d->w + o.b.x0) * 4;
    for (x = o.b.x0; x < o.b.x1; x++, p += 4) if (p[3]) fn(p, arg);
  }
  return op_end(&o);
}

/* ------------------------------------------------------------------ adjustments */
int adj_brightness_contrast(Doc *d, int br, int ct)
{
  u8 lut[768];
  int i;
  double c = ct >= 0 ? 1 + ct / 50.0 * (ct > 0 ? 1.5 : 1) : 1 + ct / 100.0;
  if (ct >= 100) c = 255;
  for (i = 0; i < 256; i++) {
    /* brightness moves the midtones (as Photoshop does now), contrast pivots about the middle */
    double v = br >= 0 ? 255 * pow(i / 255.0, 1 / (1 + br / 100.0)) : 255 * pow(i / 255.0, 1 - br / 100.0);
    v = (v - 127.5) * c + 127.5;
    lut[i] = lut[256 + i] = lut[512 + i] = (u8)clamp255(v);
  }
  return apply_luts(d, "Brightness/Contrast", lut);
}
static void levels_lut(u8 *t, int ib, int iw, double g, int ob, int ow)
{
  int i;
  if (iw <= ib) iw = ib + 1;
  if (g < 0.01) g = 0.01;
  for (i = 0; i < 256; i++) {
    double v = (i - ib) / (double)(iw - ib);
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    t[i] = (u8)clamp255(ob + pow(v, 1 / g) * (ow - ob));
  }
}
int adj_levels(Doc *d, int ch, int ib, int iw, double g, int ob, int ow)
{
  u8 lut[768];
  int c, i;
  for (i = 0; i < 768; i++) lut[i] = (u8)(i & 255);
  for (c = 0; c < 3; c++) if (ch == 0 || ch == c + 1) levels_lut(lut + 256 * c, ib, iw, g, ob, ow);
  return apply_luts(d, "Levels", lut);
}
/* a smooth curve through the points (monotone cubic, so it never overshoots), as a table */
void curve_lut(u8 *t, const int *pts, int n)
{
  double m[64], dx[64], dy[64];
  int i, k;
  if (n < 2) { for (i = 0; i < 256; i++) t[i] = (u8)i; return; }
  if (n > 64) n = 64;
  for (k = 0; k < n - 1; k++) { dx[k] = pts[2 * k + 2] - pts[2 * k]; dy[k] = (pts[2 * k + 3] - pts[2 * k + 1]) / (dx[k] ? dx[k] : 1); }
  m[0] = dy[0]; m[n - 1] = dy[n - 2];
  for (k = 1; k < n - 1; k++) m[k] = dy[k - 1] * dy[k] <= 0 ? 0 : (dy[k - 1] + dy[k]) / 2;
  for (k = 0; k < n - 1; k++) {
    if (dy[k] == 0) { m[k] = m[k + 1] = 0; continue; }
    {
      double a = m[k] / dy[k], b = m[k + 1] / dy[k], s = a * a + b * b;
      if (s > 9) { double tt = 3 / sqrt(s); m[k] = tt * a * dy[k]; m[k + 1] = tt * b * dy[k]; }
    }
  }
  for (i = 0; i < 256; i++) {
    double v;
    if (i <= pts[0]) v = pts[1];
    else if (i >= pts[2 * n - 2]) v = pts[2 * n - 1];
    else {
      for (k = 0; k < n - 2 && i > pts[2 * k + 2]; k++) continue;
      {
        double h = dx[k], s = (i - pts[2 * k]) / h, h00 = (2 * s - 3) * s * s + 1, h10 = ((s - 2) * s + 1) * s, h01 = (3 - 2 * s) * s * s, h11 = (s - 1) * s * s;
        v = h00 * pts[2 * k + 1] + h10 * h * m[k] + h01 * pts[2 * k + 3] + h11 * h * m[k + 1];
      }
    }
    t[i] = (u8)clamp255(v);
  }
}
int adj_curves(Doc *d, int ch, const int *pts, int npts)
{
  u8 lut[768], t[256];
  int c, i;
  for (i = 0; i < 768; i++) lut[i] = (u8)(i & 255);
  curve_lut(t, pts, npts);
  for (c = 0; c < 3; c++) if (ch == 0 || ch == c + 1) memcpy(lut + 256 * c, t, 256);
  return apply_luts(d, "Curves", lut);
}
/* hue/saturation/lightness through HSL */
static void rgb_hsl(const u8 *p, double *h, double *s, double *l)
{
  double r = p[0] / 255.0, g = p[1] / 255.0, b = p[2] / 255.0, mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b), c = mx - mn;
  *l = (mx + mn) / 2;
  *s = c == 0 ? 0 : c / (1 - fabs(2 * *l - 1));
  if (c == 0) *h = 0;
  else if (mx == r) *h = fmod((g - b) / c + 6, 6) * 60;
  else if (mx == g) *h = ((b - r) / c + 2) * 60;
  else *h = ((r - g) / c + 4) * 60;
}
static void hsl_rgb(double h, double s, double l, u8 *p)
{
  double c = (1 - fabs(2 * l - 1)) * s, hp = fmod(h + 360, 360) / 60, x = c * (1 - fabs(fmod(hp, 2) - 1)), m = l - c / 2, r = 0, g = 0, b = 0;
  if (hp < 1) { r = c; g = x; } else if (hp < 2) { r = x; g = c; } else if (hp < 3) { g = c; b = x; }
  else if (hp < 4) { g = x; b = c; } else if (hp < 5) { r = x; b = c; } else { r = c; b = x; }
  p[0] = (u8)clamp255((r + m) * 255); p[1] = (u8)clamp255((g + m) * 255); p[2] = (u8)clamp255((b + m) * 255);
}
static void fn_hsl(u8 *p, const double *a)
{
  double h, s, l;
  rgb_hsl(p, &h, &s, &l);
  if (a[3]) { h = a[0] < 0 ? a[0] + 360 : a[0]; s = (a[1] + 100) / 200.0; if (s < 0.25) s = 0.25 * (1 + a[1] / 100.0); }
  else { h += a[0]; s = a[1] >= 0 ? s + (1 - s) * a[1] / 100.0 * (s > 0 ? 1 : 0) : s * (1 + a[1] / 100.0); }
  if (s < 0) s = 0;
  if (s > 1) s = 1;
  l = a[2] >= 0 ? l + (1 - l) * a[2] / 100.0 : l * (1 + a[2] / 100.0);
  hsl_rgb(h, s, l, p);
}
int adj_hue_saturation(Doc *d, int hue, int sat, int light, int colorize)
{
  double a[4];
  a[0] = hue; a[1] = sat; a[2] = light; a[3] = colorize;
  return apply_fn(d, "Hue/Saturation", fn_hsl, a);
}
static void fn_balance(u8 *p, const double *a)
{
  int c, l0 = lum8(p);
  double l = l0 / 255.0, ws = (1 - l) * (1 - l), wh = l * l, wm = 1 - ws - wh;
  if (wm < 0) wm = 0;
  for (c = 0; c < 3; c++) {
    double shift = (a[c] * ws + a[3 + c] * wm + a[6 + c] * wh) * 0.6;
    p[c] = (u8)clamp255(p[c] + shift);
  }
  if (a[9]) {
    /* brightness kept: each channel moved by the change in luminosity */
    int dl = l0 - lum8(p);
    for (c = 0; c < 3; c++) p[c] = (u8)clamp255(p[c] + dl);
  }
}
int adj_color_balance(Doc *d, const int *sh, const int *mi, const int *hi, int preserve)
{
  double a[10];
  int c;
  for (c = 0; c < 3; c++) { a[c] = sh[c]; a[3 + c] = mi[c]; a[6 + c] = hi[c]; }
  a[9] = preserve;
  return apply_fn(d, "Color Balance", fn_balance, a);
}
int adj_exposure(Doc *d, double ex, double off, double g)
{
  u8 lut[768];
  int i;
  if (g < 0.01) g = 0.01;
  for (i = 0; i < 256; i++) {
    /* in linear light: exposure in stops, then the offset, then gamma */
    double v = pow(i / 255.0, 2.2) * pow(2, ex) + off;
    if (v < 0) v = 0;
    v = pow(v, 1 / 2.2);
    v = pow(v > 1 ? 1 : v, 1 / g);
    lut[i] = lut[256 + i] = lut[512 + i] = (u8)clamp255(v * 255);
  }
  return apply_luts(d, "Exposure", lut);
}
static void fn_vibrance(u8 *p, const double *a)
{
  double h, s, l, k;
  rgb_hsl(p, &h, &s, &l);
  /* vibrance raises dull colours more than strong ones */
  k = a[0] / 100.0 * (1 - s);
  s = s * (1 + k) * (1 + a[1] / 100.0);
  if (s > 1) s = 1;
  if (s < 0) s = 0;
  hsl_rgb(h, s, l, p);
}
int adj_vibrance(Doc *d, int vib, int sat)
{
  double a[2];
  a[0] = vib; a[1] = sat;
  return apply_fn(d, "Vibrance", fn_vibrance, a);
}
static void fn_bw(u8 *p, const double *a)
{
  int g = clamp255((p[0] * a[0] + p[1] * a[1] + p[2] * a[2]) / 100.0);
  p[0] = p[1] = p[2] = (u8)g;
}
int adj_black_white(Doc *d, int r, int g, int b)
{
  double a[3];
  a[0] = r; a[1] = g; a[2] = b;
  return apply_fn(d, "Black & White", fn_bw, a);
}
static void fn_photo(u8 *p, const double *a)
{
  int c, l0 = lum8(p);
  for (c = 0; c < 3; c++) p[c] = (u8)clamp255(p[c] * (1 - a[3]) + (p[c] * a[c] / 255.0) * a[3] * 1.3 + a[c] * a[3] * 0.15);
  if (a[4]) { int dl = l0 - lum8(p); for (c = 0; c < 3; c++) p[c] = (u8)clamp255(p[c] + dl); }
}
int adj_photo_filter(Doc *d, u32 color, int density, int preserve)
{
  double a[5];
  a[0] = (color >> 16) & 255; a[1] = (color >> 8) & 255; a[2] = color & 255; a[3] = density / 100.0; a[4] = preserve;
  return apply_fn(d, "Photo Filter", fn_photo, a);
}
int adj_invert(Doc *d)
{
  u8 lut[768];
  int i;
  for (i = 0; i < 768; i++) lut[i] = (u8)(255 - (i & 255));
  return apply_luts(d, "Invert", lut);
}
static void fn_desat(u8 *p, const double *a)
{
  int mx = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
  int mn = p[0] < p[1] ? (p[0] < p[2] ? p[0] : p[2]) : (p[1] < p[2] ? p[1] : p[2]);
  (void)a;
  p[0] = p[1] = p[2] = (u8)((mx + mn) / 2);
}
int adj_desaturate(Doc *d) { return apply_fn(d, "Desaturate", fn_desat, NULL); }
static void fn_threshold(u8 *p, const double *a) { p[0] = p[1] = p[2] = (u8)(lum8(p) >= a[0] ? 255 : 0); }
int adj_threshold(Doc *d, int level) { double a = level; return apply_fn(d, "Threshold", fn_threshold, &a); }
int adj_posterize(Doc *d, int levels)
{
  u8 lut[768];
  int i;
  if (levels < 2) levels = 2;
  if (levels > 255) levels = 255;
  for (i = 0; i < 256; i++) {
    int q = (i * levels) / 256;
    lut[i] = lut[256 + i] = lut[512 + i] = (u8)(q * 255 / (levels - 1));
  }
  return apply_luts(d, "Posterize", lut);
}
void histogram(Doc *d, int all_layers, u32 *h)
{
  Box b = d->sel ? box_clip(d->selbox, doc_box(d)) : doc_box(d);
  u8 *row = (u8 *)malloc((size_t)d->w * 4);
  i32 x, y;
  memset(h, 0, 4 * 256 * sizeof(u32));
  if (!row) return;
  for (y = b.y0; y < b.y1; y++) {
    const u8 *p;
    if (all_layers) { doc_composite(d, box_make(b.x0, y, b.x1, y + 1), row, (long)d->w * 4); p = row; }
    else p = d->lay[d->active]->px + ((long)y * d->w + b.x0) * 4;
    for (x = b.x0; x < b.x1; x++, p += 4) {
      if (!p[3] || sel_at(d, x, y) < 128) continue;
      h[lum8(p)]++; h[256 + p[0]]++; h[512 + p[1]]++; h[768 + p[2]]++;
    }
  }
  free(row);
}
/* the levels below and above which 0.1% of pixels lie in a histogram */
static void clip_points(const u32 *h, int *lo, int *hi)
{
  u32 total = 0, acc = 0, cut;
  int i;
  for (i = 0; i < 256; i++) total += h[i];
  cut = total / 1000;
  *lo = 0; *hi = 255;
  for (i = 0; i < 256; i++) { acc += h[i]; if (acc > cut) { *lo = i; break; } }
  for (acc = 0, i = 255; i >= 0; i--) { acc += h[i]; if (acc > cut) { *hi = i; break; } }
  if (*hi <= *lo) { *lo = 0; *hi = 255; }
}
int adj_auto_tone(Doc *d)
{
  u32 h[1024];
  u8 lut[768];
  int c;
  histogram(d, 0, h);
  for (c = 0; c < 3; c++) { int lo, hi; clip_points(h + 256 * (c + 1), &lo, &hi); levels_lut(lut + 256 * c, lo, hi, 1, 0, 255); }
  return apply_luts(d, "Auto Tone", lut);
}
int adj_auto_contrast(Doc *d)
{
  u32 h[1024], all[256];
  u8 lut[768];
  int i, lo, hi;
  histogram(d, 0, h);
  for (i = 0; i < 256; i++) all[i] = h[256 + i] + h[512 + i] + h[768 + i];
  clip_points(all, &lo, &hi);
  levels_lut(lut, lo, hi, 1, 0, 255);
  memcpy(lut + 256, lut, 256); memcpy(lut + 512, lut, 256);
  return apply_luts(d, "Auto Contrast", lut);
}
int adj_auto_color(Doc *d)
{
  u32 h[1024];
  u8 lut[768];
  int c, i;
  histogram(d, 0, h);
  /* each channel stretched, then its midtone moved so mid-grey stays grey */
  for (c = 0; c < 3; c++) {
    int lo, hi;
    double mean = 0, n = 0, g;
    clip_points(h + 256 * (c + 1), &lo, &hi);
    for (i = lo; i <= hi; i++) { mean += (double)h[256 * (c + 1) + i] * (i - lo) / (hi - lo + 1); n += h[256 * (c + 1) + i]; }
    mean = n ? mean / n / 255.0 * 255 / (hi - lo + 1) * (hi - lo + 1) / 255.0 : 0.5;
    g = mean > 0.02 && mean < 0.98 ? log(mean) / log(0.5) : 1;
    if (g < 0.5) g = 0.5;
    if (g > 2) g = 2;
    levels_lut(lut + 256 * c, lo, hi, 1 / g, 0, 255);
  }
  return apply_luts(d, "Auto Color", lut);
}
int adj_equalize(Doc *d)
{
  u32 h[1024];
  u8 lut[768];
  double total = 0, acc = 0;
  int i;
  histogram(d, 0, h);
  for (i = 0; i < 256; i++) total += h[i];
  if (total <= 0) return 0;
  for (i = 0; i < 256; i++) { acc += h[i]; lut[i] = (u8)clamp255(acc / total * 255); }
  /* the brightness mapping, applied to each channel */
  memcpy(lut + 256, lut, 256); memcpy(lut + 512, lut, 256);
  return apply_luts(d, "Equalize", lut);
}

/* ------------------------------------------------------------------ blurs */
/* region r of src (canvas w x h, straight RGBA) blurred with three box passes of the given radii
   into dst (the same canvas), premultiplied as it goes. The passes read past r by the radii */
static void box_passes(const u8 *src, u8 *dst, i32 w, i32 h, Box r, const int *rad, int npass)
{
  int margin = 0, p, c;
  Box e;
  i32 ew, eh, x, y;
  u16 *buf, *line;
  for (p = 0; p < npass; p++) margin += rad[p];
  e = box_clip(box_make(r.x0 - margin, r.y0 - margin, r.x1 + margin, r.y1 + margin), box_make(0, 0, w, h));
  ew = e.x1 - e.x0; eh = e.y1 - e.y0;
  buf = (u16 *)malloc((size_t)ew * eh * 4 * sizeof(u16));
  line = (u16 *)malloc((size_t)(ew > eh ? ew : eh) * 4 * sizeof(u16));
  if (!buf || !line) { free(buf); free(line); return; }
  /* premultiplied, eight bits more for precision */
  for (y = 0; y < eh; y++)
    for (x = 0; x < ew; x++) {
      const u8 *s = src + ((long)(y + e.y0) * w + x + e.x0) * 4;
      u16 *q = buf + ((long)y * ew + x) * 4;
      q[3] = (u16)(s[3] << 8);
      for (c = 0; c < 3; c++) q[c] = (u16)(s[c] * s[3]);
    }
  for (p = 0; p < npass; p++) {
    int rr = rad[p];
    long div = 2 * rr + 1;
    if (rr < 1) continue;
    for (y = 0; y < eh; y++) {
      u16 *row = buf + (long)y * ew * 4;
      memcpy(line, row, (size_t)ew * 4 * sizeof(u16));
      for (c = 0; c < 4; c++) {
        long sum = 0;
        for (x = -rr; x <= rr; x++) sum += line[(x < 0 ? 0 : x >= ew ? ew - 1 : x) * 4 + c];
        for (x = 0; x < ew; x++) {
          row[x * 4 + c] = (u16)(sum / div);
          sum += line[(x + rr + 1 >= ew ? ew - 1 : x + rr + 1) * 4 + c] - line[(x - rr < 0 ? 0 : x - rr) * 4 + c];
        }
      }
    }
    for (x = 0; x < ew; x++) {
      for (y = 0; y < eh; y++) memcpy(line + y * 4, buf + ((long)y * ew + x) * 4, 4 * sizeof(u16));
      for (c = 0; c < 4; c++) {
        long sum = 0;
        for (y = -rr; y <= rr; y++) sum += line[(y < 0 ? 0 : y >= eh ? eh - 1 : y) * 4 + c];
        for (y = 0; y < eh; y++) {
          buf[((long)y * ew + x) * 4 + c] = (u16)(sum / div);
          sum += line[(y + rr + 1 >= eh ? eh - 1 : y + rr + 1) * 4 + c] - line[(y - rr < 0 ? 0 : y - rr) * 4 + c];
        }
      }
    }
  }
  for (y = r.y0; y < r.y1; y++)
    for (x = r.x0; x < r.x1; x++) {
      const u16 *q = buf + ((long)(y - e.y0) * ew + x - e.x0) * 4;
      u8 *o = dst + ((long)y * w + x) * 4;
      int a = q[3] >> 8;
      o[3] = (u8)a;
      for (c = 0; c < 3; c++) o[c] = a ? (u8)(q[c] / a > 255 ? 255 : q[c] / a) : 0;
    }
  free(buf); free(line);
}
/* the three box radii nearest a Gaussian of standard deviation sigma */
static void gauss_boxes(double sigma, int *rad)
{
  double wi = sqrt(12 * sigma * sigma / 3 + 1);
  int wl = (int)floor(wi), wu, m, i;
  if (wl % 2 == 0) wl--;
  wu = wl + 2;
  m = (int)floor((12 * sigma * sigma - 3 * wl * wl - 12 * wl - 9) / (-4.0 * wl - 4) + 0.5);
  for (i = 0; i < 3; i++) rad[i] = ((i < m ? wl : wu) - 1) / 2;
}
static int blur_into(Op *o, double sigma)
{
  int rad[3];
  if (sigma < 0.3) return 1;
  gauss_boxes(sigma, rad);
  box_passes(o->orig, o->l->px, o->d->w, o->d->h, o->b, rad, 3);
  return 1;
}
int flt_gaussian_blur(Doc *d, double radius)
{
  Op o;
  if (!op_begin(&o, d, "Gaussian Blur", 1)) return 0;
  blur_into(&o, radius);
  return op_end(&o);
}
int flt_box_blur(Doc *d, int radius)
{
  Op o;
  if (!op_begin(&o, d, "Box Blur", 1)) return 0;
  box_passes(o.orig, o.l->px, d->w, d->h, o.b, &radius, 1);
  return op_end(&o);
}
int flt_blur(Doc *d) { int r = 1; Op o; if (!op_begin(&o, d, "Blur", 1)) return 0; box_passes(o.orig, o.l->px, d->w, d->h, o.b, &r, 1); return op_end(&o); }

/* each pixel from samples of the original at points fn gives (n of them), averaged premultiplied */
typedef void (*SampleFn)(double x, double y, int i, int n, const double *arg, double *sx, double *sy);
static void sample_average(Op *o, SampleFn fn, int n, const double *arg)
{
  Doc *d = o->d;
  i32 x, y;
  for (y = o->b.y0; y < o->b.y1; y++)
    for (x = o->b.x0; x < o->b.x1; x++) {
      double acc[4] = { 0, 0, 0, 0 };
      int i, c;
      u8 *p = o->l->px + ((long)y * d->w + x) * 4;
      for (i = 0; i < n; i++) {
        double sx, sy;
        i32 ix, iy;
        const u8 *q;
        fn(x + 0.5, y + 0.5, i, n, arg, &sx, &sy);
        ix = (i32)floor(sx); iy = (i32)floor(sy);
        if (ix < 0) ix = 0;
        if (iy < 0) iy = 0;
        if (ix >= d->w) ix = d->w - 1;
        if (iy >= d->h) iy = d->h - 1;
        q = o->orig + ((long)iy * d->w + ix) * 4;
        for (c = 0; c < 3; c++) acc[c] += q[c] * q[3];
        acc[3] += q[3];
      }
      p[3] = (u8)clamp255(acc[3] / n);
      for (c = 0; c < 3; c++) p[c] = acc[3] > 0 ? (u8)clamp255(acc[c] / acc[3]) : 0;
    }
}
static void s_motion(double x, double y, int i, int n, const double *a, double *sx, double *sy)
{
  double t = n > 1 ? (i / (double)(n - 1) - 0.5) * a[2] : 0;
  *sx = x + a[0] * t; *sy = y + a[1] * t;
}
int flt_motion_blur(Doc *d, double angle, int dist)
{
  Op o;
  double a[3];
  int n = dist < 2 ? 2 : dist > 200 ? 200 : dist;
  if (!op_begin(&o, d, "Motion Blur", 1)) return 0;
  a[0] = cos(angle * M_PI / 180); a[1] = -sin(angle * M_PI / 180); a[2] = dist;
  sample_average(&o, s_motion, n, a);
  return op_end(&o);
}
static void s_radial(double x, double y, int i, int n, const double *a, double *sx, double *sy)
{
  double dx = x - a[0], dy = y - a[1], t = n > 1 ? i / (double)(n - 1) - 0.5 : 0;
  if (a[3]) { double f = 1 + t * a[2] / 100.0 * 0.5; *sx = a[0] + dx * f; *sy = a[1] + dy * f; }
  else { double an = t * a[2] * M_PI / 180 * 0.5, c = cos(an), s = sin(an); *sx = a[0] + dx * c - dy * s; *sy = a[1] + dx * s + dy * c; }
}
int flt_radial_blur(Doc *d, int amount, int zoom)
{
  Op o;
  double a[4];
  if (!op_begin(&o, d, zoom ? "Radial Blur (Zoom)" : "Radial Blur", 1)) return 0;
  a[0] = (o.b.x0 + o.b.x1) / 2.0; a[1] = (o.b.y0 + o.b.y1) / 2.0; a[2] = amount; a[3] = zoom;
  sample_average(&o, s_radial, 16, a);
  return op_end(&o);
}

/* ------------------------------------------------------------------ sharpening and the like */
int flt_unsharp_mask(Doc *d, int amount, double radius, int threshold)
{
  Op o;
  u8 *blur;
  i32 x, y;
  if (!op_begin(&o, d, "Unsharp Mask", 1)) return 0;
  if (!(blur = (u8 *)malloc((size_t)d->w * d->h * 4))) { op_end(&o); return 0; }
  {
    int rad[3];
    gauss_boxes(radius < 0.3 ? 0.3 : radius, rad);
    box_passes(o.orig, blur, d->w, d->h, o.b, rad, 3);
  }
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      long k = ((long)y * d->w + x) * 4;
      int c;
      for (c = 0; c < 3; c++) {
        int diff = o.orig[k + c] - blur[k + c];
        if (abs(diff) * 2 < threshold) continue;
        o.l->px[k + c] = (u8)clamp255(o.orig[k + c] + diff * amount / 100.0);
      }
    }
  free(blur);
  return op_end(&o);
}
/* a 3x3 convolution of the colour (alpha kept) */
static int convolve3(Doc *d, const char *name, const int *kern, int div, int bias)
{
  Op o;
  i32 x, y;
  if (!op_begin(&o, d, name, 1)) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      int c, i, j;
      for (c = 0; c < 3; c++) {
        long s = 0;
        for (j = -1; j <= 1; j++) for (i = -1; i <= 1; i++) {
          i32 xx = x + i < 0 ? 0 : x + i >= d->w ? d->w - 1 : x + i, yy = y + j < 0 ? 0 : y + j >= d->h ? d->h - 1 : y + j;
          s += o.orig[((long)yy * d->w + xx) * 4 + c] * kern[(j + 1) * 3 + i + 1];
        }
        o.l->px[((long)y * d->w + x) * 4 + c] = (u8)clamp255((double)s / div + bias);
      }
    }
  return op_end(&o);
}
int flt_sharpen(Doc *d) { static const int k[9] = { 0, -1, 0, -1, 8, -1, 0, -1, 0 }; return convolve3(d, "Sharpen", k, 4, 0); }
int flt_sharpen_more(Doc *d) { static const int k[9] = { -1, -1, -1, -1, 12, -1, -1, -1, -1 }; return convolve3(d, "Sharpen More", k, 4, 0); }
int flt_find_edges(Doc *d)
{
  Op o;
  i32 x, y;
  if (!op_begin(&o, d, "Find Edges", 1)) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      int c;
      for (c = 0; c < 3; c++) {
        int v[9], i, j, gx, gy;
        for (j = -1; j <= 1; j++) for (i = -1; i <= 1; i++) {
          i32 xx = x + i < 0 ? 0 : x + i >= d->w ? d->w - 1 : x + i, yy = y + j < 0 ? 0 : y + j >= d->h ? d->h - 1 : y + j;
          v[(j + 1) * 3 + i + 1] = o.orig[((long)yy * d->w + xx) * 4 + c];
        }
        gx = v[2] + 2 * v[5] + v[8] - v[0] - 2 * v[3] - v[6];
        gy = v[6] + 2 * v[7] + v[8] - v[0] - 2 * v[1] - v[2];
        /* edges dark on white, as Photoshop draws them */
        o.l->px[((long)y * d->w + x) * 4 + c] = (u8)clamp255(255 - sqrt((double)gx * gx + (double)gy * gy) / 2);
      }
    }
  return op_end(&o);
}
int flt_emboss(Doc *d, double angle, int height, int amount)
{
  Op o;
  i32 x, y;
  double dx = cos(angle * M_PI / 180) * height, dy = -sin(angle * M_PI / 180) * height;
  if (!op_begin(&o, d, "Emboss", 1)) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      i32 ax = (i32)floor(x + dx + 0.5), ay = (i32)floor(y + dy + 0.5), bx = (i32)floor(x - dx + 0.5), by = (i32)floor(y - dy + 0.5);
      int v;
      if (ax < 0) ax = 0;
      if (ay < 0) ay = 0;
      if (bx < 0) bx = 0;
      if (by < 0) by = 0;
      if (ax >= d->w) ax = d->w - 1;
      if (bx >= d->w) bx = d->w - 1;
      if (ay >= d->h) ay = d->h - 1;
      if (by >= d->h) by = d->h - 1;
      v = clamp255(128 + (lum8(o.orig + ((long)ay * d->w + ax) * 4) - lum8(o.orig + ((long)by * d->w + bx) * 4)) * amount / 100.0);
      o.l->px[((long)y * d->w + x) * 4] = o.l->px[((long)y * d->w + x) * 4 + 1] = o.l->px[((long)y * d->w + x) * 4 + 2] = (u8)v;
    }
  return op_end(&o);
}
int flt_high_pass(Doc *d, double radius)
{
  Op o;
  u8 *blur;
  i32 x, y;
  if (!op_begin(&o, d, "High Pass", 1)) return 0;
  if (!(blur = (u8 *)malloc((size_t)d->w * d->h * 4))) { op_end(&o); return 0; }
  { int rad[3]; gauss_boxes(radius < 0.3 ? 0.3 : radius, rad); box_passes(o.orig, blur, d->w, d->h, o.b, rad, 3); }
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      long k = ((long)y * d->w + x) * 4;
      int c;
      for (c = 0; c < 3; c++) o.l->px[k + c] = (u8)clamp255(128 + o.orig[k + c] - blur[k + c]);
    }
  free(blur);
  return op_end(&o);
}

/* ------------------------------------------------------------------ noise and rank filters */
static unsigned long rnd_state = 12345;
static double frand(void) { rnd_state = rnd_state * 1103515245UL + 12345UL; return ((rnd_state >> 8) & 0xFFFFFF) / (double)0x1000000; }
static double grand(void) { double u = frand() + 1e-9, v = frand(); return sqrt(-2 * log(u)) * cos(2 * M_PI * v); }
int flt_add_noise(Doc *d, double amount, int gaussian, int mono)
{
  Op o;
  i32 x, y;
  double amp = amount * 2.55;
  if (!op_begin(&o, d, "Add Noise", 0)) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      u8 *p = o.l->px + ((long)y * d->w + x) * 4;
      double n = gaussian ? grand() * amp / 2 : (frand() - 0.5) * amp;
      int c;
      for (c = 0; c < 3; c++) {
        if (!mono && c) n = gaussian ? grand() * amp / 2 : (frand() - 0.5) * amp;
        p[c] = (u8)clamp255(p[c] + n);
      }
    }
  return op_end(&o);
}
/* the median (rank 0.5), least (0) or most (1) of each channel within radius */
static int rank_filter(Doc *d, const char *name, int r, int which)
{
  Op o;
  i32 x, y;
  if (r < 1) r = 1;
  if (r > 16) r = 16;
  if (!op_begin(&o, d, name, 1)) return 0;
  for (y = o.b.y0; y < o.b.y1; y++) {
    int hist[3][256], c, i, j, cnt = 0;
    memset(hist, 0, sizeof hist);
    for (x = o.b.x0; x < o.b.x1; x++) {
      if (x == o.b.x0) {
        memset(hist, 0, sizeof hist); cnt = 0;
        for (j = -r; j <= r; j++) for (i = -r; i <= r; i++) {
          i32 xx = x + i < 0 ? 0 : x + i >= d->w ? d->w - 1 : x + i, yy = y + j < 0 ? 0 : y + j >= d->h ? d->h - 1 : y + j;
          for (c = 0; c < 3; c++) hist[c][o.orig[((long)yy * d->w + xx) * 4 + c]]++;
          cnt++;
        }
      } else {
        /* the window slid one pixel: the column left behind out, the new one in */
        for (j = -r; j <= r; j++) {
          i32 yy = y + j < 0 ? 0 : y + j >= d->h ? d->h - 1 : y + j, xo = x - r - 1 < 0 ? 0 : x - r - 1, xn = x + r >= d->w ? d->w - 1 : x + r;
          for (c = 0; c < 3; c++) { hist[c][o.orig[((long)yy * d->w + xo) * 4 + c]]--; hist[c][o.orig[((long)yy * d->w + xn) * 4 + c]]++; }
        }
      }
      for (c = 0; c < 3; c++) {
        int target = which == 0 ? 1 : which == 2 ? cnt : (cnt + 1) / 2, acc = 0, v;
        for (v = 0; v < 256; v++) { acc += hist[c][v]; if (acc >= target) break; }
        o.l->px[((long)y * d->w + x) * 4 + c] = (u8)(v > 255 ? 255 : v);
      }
    }
  }
  return op_end(&o);
}
int flt_median(Doc *d, int radius) { return rank_filter(d, "Median", radius, 1); }
int flt_despeckle(Doc *d) { return rank_filter(d, "Despeckle", 1, 1); }
int flt_minimum(Doc *d, int radius) { return rank_filter(d, "Minimum", radius, 0); }
int flt_maximum(Doc *d, int radius) { return rank_filter(d, "Maximum", radius, 2); }

/* ------------------------------------------------------------------ pixelate, distort, render */
int flt_mosaic(Doc *d, int cell)
{
  Op o;
  i32 x, y, i, j;
  if (cell < 2) cell = 2;
  if (!op_begin(&o, d, "Mosaic", 1)) return 0;
  for (y = (o.b.y0 / cell) * cell; y < o.b.y1; y += cell)
    for (x = (o.b.x0 / cell) * cell; x < o.b.x1; x += cell) {
      double acc[4] = { 0, 0, 0, 0 };
      long n = 0;
      int c;
      u8 v[4];
      for (j = y; j < y + cell && j < d->h; j++) for (i = x; i < x + cell && i < d->w; i++) {
        const u8 *q = o.orig + ((long)j * d->w + i) * 4;
        for (c = 0; c < 3; c++) acc[c] += q[c] * q[3];
        acc[3] += q[3]; n++;
      }
      v[3] = (u8)clamp255(acc[3] / n);
      for (c = 0; c < 3; c++) v[c] = acc[3] > 0 ? (u8)clamp255(acc[c] / acc[3]) : 0;
      for (j = y; j < y + cell && j < d->h; j++) for (i = x; i < x + cell && i < d->w; i++)
        if (j >= o.b.y0 && j < o.b.y1 && i >= o.b.x0 && i < o.b.x1) memcpy(o.l->px + ((long)j * d->w + i) * 4, v, 4);
    }
  return op_end(&o);
}
/* each pixel from the original at a point fn works out (bilinear), centred on the box */
typedef void (*MapFn)(double dx, double dy, double rx, double ry, const double *a, double *sx, double *sy);
static int distort(Doc *d, const char *name, MapFn fn, const double *a)
{
  Op o;
  i32 x, y;
  double cx, cy, rx, ry;
  if (!op_begin(&o, d, name, 1)) return 0;
  cx = (o.b.x0 + o.b.x1) / 2.0; cy = (o.b.y0 + o.b.y1) / 2.0; rx = (o.b.x1 - o.b.x0) / 2.0; ry = (o.b.y1 - o.b.y0) / 2.0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      double sx, sy, fx, fy, acc[4] = { 0, 0, 0, 0 };
      i32 ix, iy;
      int c, k;
      fn(x + 0.5 - cx, y + 0.5 - cy, rx, ry, a, &sx, &sy);
      sx += cx - 0.5; sy += cy - 0.5;
      ix = (i32)floor(sx); iy = (i32)floor(sy); fx = sx - ix; fy = sy - iy;
      for (k = 0; k < 4; k++) {
        i32 xx = ix + (k & 1), yy = iy + (k >> 1);
        double wgt = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
        const u8 *q;
        if (xx < 0) xx = 0;
        if (yy < 0) yy = 0;
        if (xx >= d->w) xx = d->w - 1;
        if (yy >= d->h) yy = d->h - 1;
        q = o.orig + ((long)yy * d->w + xx) * 4;
        for (c = 0; c < 3; c++) acc[c] += q[c] * q[3] * wgt;
        acc[3] += q[3] * wgt;
      }
      {
        u8 *p = o.l->px + ((long)y * d->w + x) * 4;
        p[3] = (u8)clamp255(acc[3]);
        for (c = 0; c < 3; c++) p[c] = acc[3] > 0 ? (u8)clamp255(acc[c] / acc[3]) : 0;
      }
    }
  return op_end(&o);
}
static void m_twirl(double dx, double dy, double rx, double ry, const double *a, double *sx, double *sy)
{
  double r = sqrt(dx * dx + dy * dy), R = rx < ry ? rx : ry, an;
  if (r >= R || R <= 0) { *sx = dx; *sy = dy; return; }
  an = a[0] * M_PI / 180 * (1 - r / R);
  *sx = dx * cos(an) - dy * sin(an); *sy = dx * sin(an) + dy * cos(an);
}
int flt_twirl(Doc *d, double angle) { return distort(d, "Twirl", m_twirl, &angle); }
static void m_pinch(double dx, double dy, double rx, double ry, const double *a, double *sx, double *sy)
{
  double R = rx < ry ? rx : ry, r = sqrt(dx * dx + dy * dy), f;
  if (r >= R || R <= 0) { *sx = dx; *sy = dy; return; }
  f = pow(sin(M_PI / 2 * r / R), -a[0] / 100.0);
  *sx = dx * f; *sy = dy * f;
}
int flt_pinch(Doc *d, int amount) { double a = amount; return distort(d, "Pinch", m_pinch, &a); }
static void m_sphere(double dx, double dy, double rx, double ry, const double *a, double *sx, double *sy)
{
  double nx = rx > 0 ? dx / rx : 0, ny = ry > 0 ? dy / ry : 0, r = sqrt(nx * nx + ny * ny), f;
  if (r >= 1) { *sx = dx; *sy = dy; return; }
  f = r > 0 ? asin(r) / (M_PI / 2) / r : 1;
  f = 1 + (f - 1) * a[0] / 100.0;
  *sx = dx * f; *sy = dy * f;
}
int flt_spherize(Doc *d, int amount) { double a = amount; return distort(d, "Spherize", m_sphere, &a); }
static void m_ripple(double dx, double dy, double rx, double ry, const double *a, double *sx, double *sy)
{
  (void)rx; (void)ry;
  *sx = dx + a[0] * sin(dy * 2 * M_PI / a[1]); *sy = dy + a[0] * sin(dx * 2 * M_PI / a[1]);
}
int flt_ripple(Doc *d, int amount, int size)
{
  double a[2];
  a[0] = amount / 25.0; a[1] = size == 0 ? 8 : size == 1 ? 16 : 32;
  return distort(d, "Ripple", m_ripple, a);
}
static void m_wave(double dx, double dy, double rx, double ry, const double *a, double *sx, double *sy)
{
  (void)rx; (void)ry;
  *sx = dx + a[1] * sin(dy * 2 * M_PI / a[0]); *sy = dy;
}
int flt_wave(Doc *d, int wavelength, int amplitude)
{
  double a[2];
  a[0] = wavelength < 2 ? 2 : wavelength; a[1] = amplitude;
  return distort(d, "Wave", m_wave, a);
}
/* value noise: smooth random values on a lattice, octaves added */
static double lattice(int x, int y, unsigned seed)
{
  unsigned long n = (unsigned long)x * 374761393UL + (unsigned long)y * 668265263UL + seed * 2147483647UL;
  n = (n ^ (n >> 13)) * 1274126177UL;
  return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0;
}
static double vnoise(double x, double y, unsigned seed)
{
  int ix = (int)floor(x), iy = (int)floor(y);
  double fx = x - ix, fy = y - iy, a, b, c, dd;
  fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
  a = lattice(ix, iy, seed); b = lattice(ix + 1, iy, seed); c = lattice(ix, iy + 1, seed); dd = lattice(ix + 1, iy + 1, seed);
  return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + dd * fx) * fy;
}
int flt_clouds(Doc *d, u32 c0, u32 c1, unsigned seed)
{
  Op o;
  i32 x, y;
  if (!op_begin(&o, d, "Clouds", 0)) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      double v = 0, amp = 0.5, f = 1 / 128.0, t;
      int k, c;
      u8 *p = o.l->px + ((long)y * d->w + x) * 4;
      for (k = 0; k < 7; k++, amp /= 2, f *= 2) v += amp * vnoise(x * f, y * f, seed + k * 7919);
      t = v / 0.99;
      for (c = 0; c < 3; c++) { int a = (c0 >> (16 - 8 * c)) & 255, b = (c1 >> (16 - 8 * c)) & 255; p[c] = (u8)clamp255(a + (b - a) * t); }
      p[3] = 255;
    }
  return op_end(&o);
}
int flt_offset(Doc *d, i32 dx, i32 dy, int wrap)
{
  Op o;
  i32 x, y;
  if (!op_begin(&o, d, "Offset", 1)) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      i32 sx = x - dx, sy = y - dy, bw = o.b.x1 - o.b.x0, bh = o.b.y1 - o.b.y0;
      u8 *p = o.l->px + ((long)y * d->w + x) * 4;
      if (wrap) { sx = o.b.x0 + (((sx - o.b.x0) % bw) + bw) % bw; sy = o.b.y0 + (((sy - o.b.y0) % bh) + bh) % bh; }
      if (sx < o.b.x0 || sy < o.b.y0 || sx >= o.b.x1 || sy >= o.b.y1) { memset(p, 0, 4); continue; }
      memcpy(p, o.orig + ((long)sy * d->w + sx) * 4, 4);
    }
  return op_end(&o);
}
int flt_solarize(Doc *d)
{
  u8 lut[768];
  int i;
  for (i = 0; i < 768; i++) { int v = i & 255; lut[i] = (u8)(v > 127 ? 255 - v : v); }
  return apply_luts(d, "Solarize", lut);
}
int flt_oil_paint(Doc *d, int r, int levels)
{
  Op o;
  i32 x, y;
  if (r < 1) r = 1;
  if (r > 8) r = 8;
  if (levels < 4) levels = 4;
  if (levels > 64) levels = 64;
  if (!op_begin(&o, d, "Oil Paint", 1)) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      int cnt[64], i, j, best = 0;
      long sum[64][3];
      memset(cnt, 0, sizeof cnt); memset(sum, 0, sizeof sum);
      /* the most common brightness about the pixel, and that brightness's average colour */
      for (j = -r; j <= r; j++) for (i = -r; i <= r; i++) {
        i32 xx = x + i < 0 ? 0 : x + i >= d->w ? d->w - 1 : x + i, yy = y + j < 0 ? 0 : y + j >= d->h ? d->h - 1 : y + j;
        const u8 *q = o.orig + ((long)yy * d->w + xx) * 4;
        int bin = lum8(q) * levels / 256;
        cnt[bin]++; sum[bin][0] += q[0]; sum[bin][1] += q[1]; sum[bin][2] += q[2];
      }
      for (i = 1; i < levels; i++) if (cnt[i] > cnt[best]) best = i;
      if (cnt[best]) {
        u8 *p = o.l->px + ((long)y * d->w + x) * 4;
        p[0] = (u8)(sum[best][0] / cnt[best]); p[1] = (u8)(sum[best][1] / cnt[best]); p[2] = (u8)(sum[best][2] / cnt[best]);
      }
    }
  return op_end(&o);
}
int flt_vignette(Doc *d, int amount)
{
  Op o;
  i32 x, y;
  double cx, cy, rr;
  if (!op_begin(&o, d, "Vignette", 0)) return 0;
  cx = (o.b.x0 + o.b.x1) / 2.0; cy = (o.b.y0 + o.b.y1) / 2.0;
  rr = sqrt(((o.b.x1 - o.b.x0) / 2.0) * ((o.b.x1 - o.b.x0) / 2.0) + ((o.b.y1 - o.b.y0) / 2.0) * ((o.b.y1 - o.b.y0) / 2.0));
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      double r = sqrt((x + 0.5 - cx) * (x + 0.5 - cx) + (y + 0.5 - cy) * (y + 0.5 - cy)) / rr, f = 1 - amount / 100.0 * r * r;
      u8 *p = o.l->px + ((long)y * d->w + x) * 4;
      int c;
      if (f < 0) f = 0;
      for (c = 0; c < 3; c++) p[c] = (u8)clamp255(p[c] * f);
    }
  return op_end(&o);
}

/* ------------------------------------------------------------------ adjustment programs (0.3)
   Any adjustment, as AdjParams, compiled once into something quick to apply to a row of pixels:
   three lookup tables when it treats each channel alone, or else a 33 x 33 x 33 colour cube
   looked up with trilinear interpolation (the exact maths runs only at the cube's corners). The
   adjustment layers apply these as they composite; Image > Adjustments uses them too. */
struct AdjProg {
  int kind;
  int haslut;                       /* lut is used (else the cube) */
  u8 lut[768];
  u8 *cube;                         /* 33^3 * 3 */
  AdjParams p;
  Adj10Fn fn; double a[48];         /* 1.0: applied to each pixel exactly (Replace Color) */
};

/* channel mixer: rows R, G, B each (r, g, b, constant) in per cent; v[12] monochrome (row R used) */
static void fn_mixer(u8 *px, const double *a)
{
  int c;
  double in[3], o[3];
  for (c = 0; c < 3; c++) in[c] = px[c];
  for (c = 0; c < 3; c++) {
    const double *r = a + (a[12] ? 0 : 4 * c);
    o[c] = (in[0] * r[0] + in[1] * r[1] + in[2] * r[2]) / 100.0 + r[3] * 2.55;
  }
  for (c = 0; c < 3; c++) px[c] = (u8)clamp255(o[c]);
}
/* gradient map: the luminosity looked up in stops (position 0-255, 0xRRGGBB) */
static void gradmap_lut(u8 *lut, const int *pts, int n, int reverse)
{
  int i, k;
  for (i = 0; i < 256; i++) {
    int t = reverse ? 255 - i : i;
    u32 c0, c1;
    int p0, p1;
    if (n <= 0) { lut[i * 3] = lut[i * 3 + 1] = lut[i * 3 + 2] = (u8)t; continue; }
    for (k = 0; k < n - 1 && pts[2 * (k + 1)] < t; k++) continue;
    p0 = pts[2 * k]; c0 = (u32)pts[2 * k + 1];
    if (k + 1 < n && t > p0) { p1 = pts[2 * k + 2]; c1 = (u32)pts[2 * k + 3]; }
    else { p1 = p0; c1 = c0; }
    {
      int c;
      for (c = 0; c < 3; c++) {
        int a = (int)((c0 >> (16 - 8 * c)) & 255), b = (int)((c1 >> (16 - 8 * c)) & 255);
        lut[i * 3 + c] = (u8)(p1 > p0 ? a + (b - a) * (t - p0) / (p1 - p0) : a);
      }
    }
  }
}
/* selective colour: for each of reds, yellows, greens, cyans, blues, magentas, whites, neutrals and
   blacks, cyan/magenta/yellow/black moved -100..100 (relative or absolute), as Photoshop does */
static void fn_selective(u8 *px, const double *a)
{
  double r = px[0] / 255.0, g = px[1] / 255.0, b = px[2] / 255.0;
  double mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
  double w[9], ch[3], kk;
  int i, c;
  /* each pixel's share of each colour range */
  double reds = (r == mx) ? mx - (g > b ? g : b) : 0, greens = (g == mx) ? mx - (r > b ? r : b) : 0, blues = (b == mx) ? mx - (r > g ? r : g) : 0;
  double cyans = (r == mn) ? (g < b ? g : b) - mn : 0, magentas = (g == mn) ? (r < b ? r : b) - mn : 0, yellows = (b == mn) ? (r < g ? r : g) - mn : 0;
  w[0] = reds; w[1] = yellows; w[2] = greens; w[3] = cyans; w[4] = blues; w[5] = magentas;
  w[6] = mn > 0.5 ? (mn - 0.5) * 2 : 0;
  w[7] = 1 - fabs(mx - 0.5) - fabs(mn - 0.5); if (w[7] < 0) w[7] = 0;
  w[8] = mx < 0.5 ? (0.5 - mx) * 2 : 0;
  ch[0] = 1 - r; ch[1] = 1 - g; ch[2] = 1 - b;              /* as CMY */
  kk = 0;
  for (i = 0; i < 9; i++) {
    if (w[i] <= 0) continue;
    for (c = 0; c < 3; c++) {
      double adj = a[i * 4 + c] / 100.0;
      ch[c] += a[36] ? adj * w[i] : adj * w[i] * (adj < 0 ? ch[c] : 1 - ch[c]);
    }
    kk += a[i * 4 + 3] / 100.0 * w[i];
  }
  for (c = 0; c < 3; c++) {
    double v = ch[c] + kk * (1 - ch[c]);
    px[c] = (u8)clamp255((1 - (v < 0 ? 0 : v > 1 ? 1 : v)) * 255);
  }
}

AdjProg *adj_prog_make(const AdjParams *p)
{
  AdjProg *g = (AdjProg *)calloc(1, sizeof *g);
  int i, c;
  PixFn fn = NULL;
  double a[48];
  int exact = 0, r10;
  Adj10Fn fn10;
  if (!g) return NULL;
  g->kind = p->kind; g->p = *p;
  memset(a, 0, sizeof a);
  for (i = 0; i < 768; i++) g->lut[i] = (u8)(i & 255);
  g->haslut = 1;
  /* 1.0: Hue/Saturation's ranges, six-colour Black & White, Replace Color, four-curve Curves */
  r10 = adj10_pixfn(p, &fn10, a, &exact);
  if (r10 == 2) { /* (they change nothing: the tables as they are) */ }
  else if (r10) {
    fn = (PixFn)fn10;
    if (exact) { g->haslut = 0; g->fn = fn10; memcpy(g->a, a, sizeof g->a); return g; }
  } else if (p->kind == AK_CURVES && p->v[1] == 1) adj10_curves_lut(p, g->lut);
  else switch (p->kind) {
  case AK_BRIGHTNESS: {
    int br = p->v[0], ct = p->v[1];
    if (p->v[2]) { adj10_brightness_lut(p, g->lut); break; }     /* 1.0: Use Legacy */
    double cc = ct >= 0 ? 1 + ct / 50.0 * (ct > 0 ? 1.5 : 1) : 1 + ct / 100.0;
    if (ct >= 100) cc = 255;
    for (i = 0; i < 256; i++) {
      double v = br >= 0 ? 255 * pow(i / 255.0, 1 / (1 + br / 100.0)) : 255 * pow(i / 255.0, 1 - br / 100.0);
      v = (v - 127.5) * cc + 127.5;
      g->lut[i] = g->lut[256 + i] = g->lut[512 + i] = (u8)clamp255(v);
    }
    break; }
  case AK_LEVELS: {
    u8 t[256];
    levels_lut(t, p->v[0], p->v[1] ? p->v[1] : 255, p->v[2] ? p->v[2] / 100.0 : 1, p->v[3], p->v[4] ? p->v[4] : 255);
    for (c = 0; c < 3; c++) {
      u8 u[256];
      if (p->npts >= 15) {
        const int *q = p->pts + c * 5;
        levels_lut(u, q[0], q[1] ? q[1] : 255, q[2] ? q[2] / 100.0 : 1, q[3], q[4] ? q[4] : 255);
        for (i = 0; i < 256; i++) g->lut[256 * c + i] = t[u[i]];
      } else memcpy(g->lut + 256 * c, t, 256);
    }
    break; }
  case AK_CURVES: {
    u8 t[256];
    curve_lut(t, p->pts, p->npts);
    for (c = 0; c < 3; c++) if (p->v[0] == 0 || p->v[0] == c + 1) memcpy(g->lut + 256 * c, t, 256);
    break; }
  case AK_EXPOSURE: {
    double ex = p->v[0] / 100.0, off = p->v[1] / 1000.0, gm = p->v[2] ? p->v[2] / 100.0 : 1;
    for (i = 0; i < 256; i++) {
      double v = pow(i / 255.0, 2.2) * pow(2, ex) + off;
      if (v < 0) v = 0;
      v = pow(v, 1 / 2.2);
      v = pow(v > 1 ? 1 : v, 1 / gm);
      g->lut[i] = g->lut[256 + i] = g->lut[512 + i] = (u8)clamp255(v * 255);
    }
    break; }
  case AK_INVERT: for (i = 0; i < 768; i++) g->lut[i] = (u8)(255 - (i & 255)); break;
  case AK_POSTERIZE: {
    int lv = p->v[0] < 2 ? 2 : p->v[0] > 255 ? 255 : p->v[0];
    for (i = 0; i < 256; i++) g->lut[i] = g->lut[256 + i] = g->lut[512 + i] = (u8)(((i * lv) / 256) * 255 / (lv - 1));
    break; }
  case AK_HUESAT: fn = fn_hsl; a[0] = p->v[0]; a[1] = p->v[1]; a[2] = p->v[2]; a[3] = p->v[3]; break;
  case AK_COLORBALANCE: fn = fn_balance; for (i = 0; i < 10; i++) a[i] = p->v[i]; break;
  case AK_VIBRANCE: fn = fn_vibrance; a[0] = p->v[0]; a[1] = p->v[1]; break;
  case AK_BLACKWHITE: fn = fn_bw; a[0] = p->v[0]; a[1] = p->v[1]; a[2] = p->v[2]; break;
  case AK_PHOTOFILTER:
    fn = fn_photo; a[0] = ((u32)p->v[0] >> 16) & 255; a[1] = ((u32)p->v[0] >> 8) & 255; a[2] = (u32)p->v[0] & 255;
    a[3] = p->v[1] / 100.0; a[4] = p->v[2]; break;
  case AK_CHANNELMIXER: fn = fn_mixer; for (i = 0; i < 13; i++) a[i] = p->v[i]; break;
  case AK_THRESHOLD: fn = fn_threshold; a[0] = p->v[0]; break;
  case AK_SELECTIVECOLOR: fn = fn_selective; for (i = 0; i < 36 && i < 2 * 32; i++) a[i] = p->pts[i]; a[36] = p->v[0]; break;
  case AK_GRADIENTMAP: {
    u8 t[768];
    g->haslut = 0;
    gradmap_lut(t, p->pts, p->npts, p->v[0]);
    /* luminosity to colour: a cube made straight from the table */
    if (!(g->cube = (u8 *)malloc(33 * 33 * 33 * 3))) { free(g); return NULL; }
    {
      int r, gg, b;
      for (r = 0; r < 33; r++) for (gg = 0; gg < 33; gg++) for (b = 0; b < 33; b++) {
        u8 q[3];
        int l;
        q[0] = (u8)(r * 255 / 32); q[1] = (u8)(gg * 255 / 32); q[2] = (u8)(b * 255 / 32);
        l = lum8(q);
        memcpy(g->cube + ((r * 33 + gg) * 33 + b) * 3, t + l * 3, 3);
      }
    }
    return g; }
  default: break;
  }
  if (fn) {
    int r, gg, b;
    g->haslut = 0;
    if (!(g->cube = (u8 *)malloc(33 * 33 * 33 * 3))) { free(g); return NULL; }
    for (r = 0; r < 33; r++) for (gg = 0; gg < 33; gg++) for (b = 0; b < 33; b++) {
      u8 q[4];
      q[0] = (u8)(r * 255 / 32); q[1] = (u8)(gg * 255 / 32); q[2] = (u8)(b * 255 / 32); q[3] = 255;
      fn(q, a);
      memcpy(g->cube + ((r * 33 + gg) * 33 + b) * 3, q, 3);
    }
  }
  return g;
}
void adj_prog_free(AdjProg *g) { if (g) { free(g->cube); free(g); } }
/* one pixel through the cube: trilinear between the 8 corners around it (fixed point) */
static void cube_px(const u8 *cube, u8 *p)
{
  int r = p[0] * 32, gg = p[1] * 32, b = p[2] * 32;
  int r0 = r / 255, g0 = gg / 255, b0 = b / 255, fr, fg, fb, c;
  const u8 *q;
  fr = (r - r0 * 255) * 256 / 255; fg = (gg - g0 * 255) * 256 / 255; fb = (b - b0 * 255) * 256 / 255;
  if (r0 > 31) { r0 = 31; fr = 256; }
  if (g0 > 31) { g0 = 31; fg = 256; }
  if (b0 > 31) { b0 = 31; fb = 256; }
  q = cube + ((r0 * 33 + g0) * 33 + b0) * 3;
  for (c = 0; c < 3; c++) {
    /* corners: q[o + c] with o for (dr, dg, db) */
    long c000 = q[c], c001 = q[3 + c], c010 = q[99 + c], c011 = q[102 + c];
    long c100 = q[3267 + c], c101 = q[3270 + c], c110 = q[3366 + c], c111 = q[3369 + c];
    long c00 = c000 * (256 - fb) + c001 * fb, c01 = c010 * (256 - fb) + c011 * fb;
    long c10 = c100 * (256 - fb) + c101 * fb, c11 = c110 * (256 - fb) + c111 * fb;
    long c0 = c00 * (256 - fg) + c01 * fg, c1 = c10 * (256 - fg) + c11 * fg;
    long v = (c0 * (256 - fr) + c1 * fr + (1L << 23)) >> 24;
    p[c] = (u8)(v > 255 ? 255 : v);
  }
}
void adj_prog_row(const AdjProg *g, u8 *px, int n)
{
  int i;
  if (g->haslut) {
    for (i = 0; i < n; i++, px += 4) if (px[3]) { px[0] = g->lut[px[0]]; px[1] = g->lut[256 + px[1]]; px[2] = g->lut[512 + px[2]]; }
  } else if (g->cube) {
    for (i = 0; i < n; i++, px += 4) if (px[3]) cube_px(g->cube, px);
  } else if (g->fn) {
    for (i = 0; i < n; i++, px += 4) if (px[3]) g->fn(px, g->a);
  }
}
/* any adjustment applied to the active layer (Image > Adjustments with AdjParams) */
int adj_apply(Doc *d, const AdjParams *p, const char *name)
{
  AdjProg *g;
  Op o;
  i32 y;
  if (!(g = adj_prog_make(p))) return 0;
  if (!op_begin(&o, d, name, 0)) { adj_prog_free(g); return 0; }
  for (y = o.b.y0; y < o.b.y1; y++) adj_prog_row(g, o.l->px + ((long)y * d->w + o.b.x0) * 4, o.b.x1 - o.b.x0);
  adj_prog_free(g);
  return op_end(&o);
}
int adj_channel_mixer(Doc *d, const int *rows, int mono)
{
  AdjParams p;
  int i;
  memset(&p, 0, sizeof p);
  p.kind = AK_CHANNELMIXER;
  for (i = 0; i < 12; i++) p.v[i] = rows[i];
  p.v[12] = mono;
  return adj_apply(d, &p, "Channel Mixer");
}
int adj_gradient_map(Doc *d, const int *stops, int n, int reverse)
{
  AdjParams p;
  memset(&p, 0, sizeof p);
  p.kind = AK_GRADIENTMAP;
  if (n > 32) n = 32;
  memcpy(p.pts, stops, (size_t)n * 2 * sizeof(int)); p.npts = n; p.v[0] = reverse;
  return adj_apply(d, &p, "Gradient Map");
}
int adj_selective_color(Doc *d, const int *cmyk36, int absolute)
{
  AdjParams p;
  memset(&p, 0, sizeof p);
  p.kind = AK_SELECTIVECOLOR;
  memcpy(p.pts, cmyk36, 36 * sizeof(int)); p.v[0] = absolute;
  return adj_apply(d, &p, "Selective Color");
}
/* default settings for a new adjustment layer of each kind */
void adj_defaults(int kind, AdjParams *p)
{
  memset(p, 0, sizeof *p);
  p->kind = (u8)kind;
  switch (kind) {
  case AK_LEVELS: p->v[1] = 255; p->v[2] = 100; p->v[4] = 255; break;
  case AK_CURVES: p->npts = 2; p->pts[0] = 0; p->pts[1] = 0; p->pts[2] = 255; p->pts[3] = 255; break;
  case AK_EXPOSURE: p->v[2] = 100; break;
  case AK_BLACKWHITE: p->v[0] = 40; p->v[1] = 40; p->v[2] = 20; break;
  case AK_PHOTOFILTER: p->v[0] = 0xEC8A00; p->v[1] = 25; p->v[2] = 1; break;
  case AK_CHANNELMIXER: p->v[0] = 100; p->v[5] = 100; p->v[10] = 100; break;
  case AK_POSTERIZE: p->v[0] = 4; break;
  case AK_THRESHOLD: p->v[0] = 128; break;
  case AK_GRADIENTMAP: p->npts = 2; p->pts[0] = 0; p->pts[1] = 0x000000; p->pts[2] = 255; p->pts[3] = 0xFFFFFF; break;
  default: break;
  }
  adj10_defaults(kind, p);           /* 1.0 */
}
const char *const adj_names[AK_N] = { "Brightness/Contrast", "Levels", "Curves", "Exposure", "Vibrance", "Hue/Saturation",
  "Color Balance", "Black & White", "Photo Filter", "Channel Mixer", "Invert", "Posterize", "Threshold", "Gradient Map", "Selective Color",
  "Replace Color" };
/* adjustment layers: doc.c's compositor reaches the programs through these hooks */
static void *hook_make(const AdjParams *p) { return adj_prog_make(p); }
static void hook_free(void *g) { adj_prog_free((AdjProg *)g); }
static void hook_row(const void *g, u8 *px, int n) { adj_prog_row((const AdjProg *)g, px, n); }
void adjlayer_init(void) { adjlayer_make = hook_make; adjlayer_free = hook_free; adjlayer_row = hook_row; }
