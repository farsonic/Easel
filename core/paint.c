/*
 * paint.c - the tools' work on the document (see paint.h): fills, gradients, shapes, selections,
 * moving and transforming, the canvas, and Easel's own clipboard. (Brush strokes are in stroke.c.)
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "paint.h"
#include "live.h"                     /* 0.9 */
#include "gradient.h"
#include "pattern.h"
#include "palette.h"
#include "resample.h"                 /* (Image Size's further methods) */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
/* the number in a name "Layer N", if it is one */
static int sscanf_like_layer(const char *s, int *v)
{
  if (strncmp(s, "Layer ", 6) || s[6] < '0' || s[6] > '9') return 0;
  *v = atoi(s + 6);
  return 1;
}

static int mul255(int a, int b) { int t = a * b + 128; return (t + (t >> 8)) >> 8; }
static int clamp255(double v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5); }
static void rgb_of(u32 c, u8 *p) { p[0] = (u8)(c >> 16); p[1] = (u8)(c >> 8); p[2] = (u8)c; }

/* a pixel of layer l given colour col (RGB) at coverage cov (0-255) in blend mode; with alpha lock
   its transparency stays as it was */
void paint_px_mode(u8 *p, const u8 *col, int cov, int blend)
{
  u8 s[4];
  s[0] = col[0]; s[1] = col[1]; s[2] = col[2]; s[3] = (u8)cov;
  if (blend == BM_BEHIND) { blend_pixel(s, p, 255, BM_NORMAL); memcpy(p, s, 4); }   /* (what is there laid over the paint) */
  else if (blend == BM_CLEAR) p[3] = (u8)mul255(p[3], 255 - cov);
  else blend_pixel(p, s, 255, blend > BM_CLEAR ? BM_NORMAL : blend);
}
static void put_px(Layer *l, u8 *p, const u8 *col, int cov, int blend)
{
  int a = p[3];
  if (cov <= 0) return;
  paint_px_mode(p, col, cov, blend);
  if (l->alpha_lock) {
    if (!a) { p[0] = p[1] = p[2] = 0; }
    p[3] = (u8)a;
  }
}
/* the active layer, if it can be painted */
static Layer *paintable(Doc *d)
{
  Layer *l;
  if (d->active < 0 || d->active >= d->nlay) return NULL;
  l = d->lay[d->active];
  return !l->px || l->kind != LK_PIXEL || l->locked || l->lock_all || !l->visible ? NULL : l;
}
/* the area tools work in: the selection's box, or the canvas */
static Box work_box(Doc *d) { return d->sel ? box_clip(d->selbox, doc_box(d)) : doc_box(d); }

/* ------------------------------------------------------------------ brush strokes */
/* (0.5: moved to stroke.c, with the brushes' pieces in brush.c) */

/* ------------------------------------------------------------------ masks laid on the active layer */
/* coverage mask m (w*h for the canvas, 0-255) in box b laid on the active layer in color at
   opacity (0-100), through the selection; one undo step */
static Box apply_mask(Doc *d, const u8 *m, Box b, u32 color, int opacity, int blend, const char *name)
{
  Layer *l = paintable(d);
  u8 col[3];
  i32 x, y;
  b = box_clip(b, work_box(d));
  if (!l || box_empty(b)) return box_make(0, 0, 0, 0);
  rgb_of(color, col);
  doc_begin(d, name);
  doc_note_layer(d, d->active, b);
  for (y = b.y0; y < b.y1; y++)
    for (x = b.x0; x < b.x1; x++) {
      long k = (long)y * d->w + x;
      int c = m ? m[k] : 255;
      if (c) put_px(l, l->px + k * 4, col, mul255(mul255(c, opacity * 255 / 100), sel_at(d, x, y)), blend);
    }
  doc_end(d);
  return b;
}
Box fill_selection(Doc *d, u32 color, int opacity, int blend) { return apply_mask(d, NULL, work_box(d), color, opacity, blend, "Fill"); }
Box clear_selection(Doc *d)
{
  Layer *l = paintable(d);
  Box b = work_box(d);
  i32 x, y;
  int opaque_bg = d->active == 0 && !strcmp(d->lay[0]->name, "Background");
  u8 bgc[3];
  if (!l) return box_make(0, 0, 0, 0);
  rgb_of(d->bg, bgc);
  doc_begin(d, "Clear");
  doc_note_layer(d, d->active, b);
  for (y = b.y0; y < b.y1; y++)
    for (x = b.x0; x < b.x1; x++) {
      u8 *p = l->px + ((long)y * d->w + x) * 4;
      int s = sel_at(d, x, y);
      if (!s) continue;
      if (opaque_bg) put_px(l, p, bgc, s, BM_NORMAL);
      else p[3] = (u8)mul255(p[3], 255 - s);
    }
  doc_end(d);
  return b;
}

/* ------------------------------------------------------------------ flood fills (bucket, wand) */
/* the pixels like the one at x, y (within tolerance, every channel) marked 255 in a new mask; from
   the active layer or the composite */
static u8 *similar(Doc *d, i32 x, i32 y, int tol, int contiguous, int all_layers)
{
  if (!d->lay[d->active]->px) all_layers = 1;            /* a group, adjustment or fill layer: what shows */
  u8 *img = all_layers ? (u8 *)malloc((size_t)d->w * d->h * 4) : NULL, *m, seed[4];
  const u8 *src;
  long n = (long)d->w * d->h, k;
  if (all_layers) { if (!img) return NULL; doc_composite(d, doc_box(d), img, (long)d->w * 4); src = img; }
  else src = d->lay[d->active]->px;
  if (!(m = (u8 *)calloc((size_t)n, 1))) { free(img); return NULL; }
  memcpy(seed, src + ((long)y * d->w + x) * 4, 4);
#define LIKE(q) (abs((q)[0] - seed[0]) <= tol && abs((q)[1] - seed[1]) <= tol && abs((q)[2] - seed[2]) <= tol && abs((q)[3] - seed[3]) <= tol)
  if (!contiguous) {
    for (k = 0; k < n; k++) if (LIKE(src + k * 4)) m[k] = 255;
  } else {
    /* a scanline fill with a stack of seeds */
    long cap = 1024, top = 0;
    i32 *st = (i32 *)malloc((size_t)cap * 2 * sizeof(i32));
    if (!st) { free(m); free(img); return NULL; }
    st[top * 2] = x; st[top * 2 + 1] = y; top++;
    while (top > 0) {
      i32 sx, sy, l, r, i;
      top--; sx = st[top * 2]; sy = st[top * 2 + 1];
      if (m[(long)sy * d->w + sx] || !LIKE(src + ((long)sy * d->w + sx) * 4)) continue;
      for (l = sx; l > 0 && !m[(long)sy * d->w + l - 1] && LIKE(src + ((long)sy * d->w + l - 1) * 4); l--) ;
      for (r = sx; r < d->w - 1 && !m[(long)sy * d->w + r + 1] && LIKE(src + ((long)sy * d->w + r + 1) * 4); r++) ;
      for (i = l; i <= r; i++) m[(long)sy * d->w + i] = 255;
      for (i = l; i <= r; i++) {
        int dy;
        for (dy = -1; dy <= 1; dy += 2) {
          i32 ny = sy + dy;
          if (ny < 0 || ny >= d->h || m[(long)ny * d->w + i] || !LIKE(src + ((long)ny * d->w + i) * 4)) continue;
          if (i > l && !m[(long)ny * d->w + i - 1] && LIKE(src + ((long)ny * d->w + i - 1) * 4)) continue;   /* (one seed a run) */
          if (top == cap) { i32 *ns = (i32 *)realloc(st, (size_t)cap * 4 * sizeof(i32)); if (!ns) { top = 0; break; } st = ns; cap *= 2; }
          st[top * 2] = i; st[top * 2 + 1] = ny; top++;
        }
      }
    }
    free(st);
  }
#undef LIKE
  free(img);
  return m;
}
/* a mask's hard edge softened a little (anti-aliasing for fills) */
static void soften(u8 *m, i32 w, i32 h)
{
  u8 *t = (u8 *)malloc((size_t)w * h);
  i32 x, y;
  if (!t) return;
  memcpy(t, m, (size_t)w * h);
  for (y = 1; y < h - 1; y++)
    for (x = 1; x < w - 1; x++) {
      long k = (long)y * w + x;
      int s = t[k] * 4 + t[k - 1] + t[k + 1] + t[k - w] + t[k + w];
      if (t[k] != t[k - 1] || t[k] != t[k + 1] || t[k] != t[k - w] || t[k] != t[k + w]) m[k] = (u8)(s / 8);
    }
  free(t);
}
static Box mask_bounds(const u8 *m, i32 w, i32 h)
{
  Box b = box_make(w, h, 0, 0);
  i32 x, y;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      if (m[(long)y * w + x]) {
        if (x < b.x0) b.x0 = x;
        if (x >= b.x1) b.x1 = x + 1;
        if (y < b.y0) b.y0 = y;
        if (y >= b.y1) b.y1 = y + 1;
      }
  if (box_empty(b)) b = box_make(0, 0, 0, 0);
  return b;
}
Box fill_bucket(Doc *d, i32 x, i32 y, u32 color, int tol, int contiguous, int all_layers, int opacity, int antialias)
{
  u8 *m;
  Box b;
  if (x < 0 || y < 0 || x >= d->w || y >= d->h || !paintable(d)) return box_make(0, 0, 0, 0);
  if (!(m = similar(d, x, y, tol, contiguous, all_layers))) return box_make(0, 0, 0, 0);
  if (antialias) soften(m, d->w, d->h);
  b = mask_bounds(m, d->w, d->h);
  b = apply_mask(d, m, b, color, opacity, BM_NORMAL, "Paint Bucket");
  free(m);
  return b;
}

/* ------------------------------------------------------------------ gradients */
Box fill_gradient(Doc *d, double x0, double y0, double x1, double y1, int kind, u32 c0, u32 c1, int fade, int reverse, int opacity, int blend)
{
  static const u8 bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
  Layer *l = paintable(d);
  Box b = work_box(d);
  double dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy, len = sqrt(len2), base = atan2(dy, dx);
  u8 a[3], z[3];
  i32 x, y;
  if (!l || len2 < 1e-9) return box_make(0, 0, 0, 0);
  rgb_of(reverse ? c1 : c0, a); rgb_of(reverse ? c0 : c1, z);
  doc_begin(d, "Gradient");
  doc_note_layer(d, d->active, b);
  for (y = b.y0; y < b.y1; y++)
    for (x = b.x0; x < b.x1; x++) {
      double px = x + 0.5 - x0, py = y + 0.5 - y0, t, dith = (bayer[(y & 3) * 4 + (x & 3)] - 7.5) / 16.0;
      u8 col[3];
      int k, s = sel_at(d, x, y), alpha;
      if (!s) continue;
      switch (kind) {
      case GR_RADIAL: t = sqrt(px * px + py * py) / len; break;
      case GR_ANGLE: t = atan2(py, px) - base; while (t < 0) t += 2 * M_PI; t /= 2 * M_PI; break;
      case GR_REFLECTED: t = fabs((px * dx + py * dy) / len2); break;
      case GR_DIAMOND: t = (fabs(px * dx + py * dy) + fabs(-px * dy + py * dx)) / len2; break;
      default: t = (px * dx + py * dy) / len2;
      }
      if (t < 0) t = 0;
      if (t > 1) t = 1;
      if (reverse && fade) t = 1 - t;
      for (k = 0; k < 3; k++) col[k] = (u8)clamp255(fade ? (reverse ? z[k] : a[k]) : a[k] + (z[k] - a[k]) * t + dith);
      alpha = fade ? clamp255(255 * (1 - t) + dith) : 255;
      put_px(l, l->px + ((long)y * d->w + x) * 4, col, mul255(mul255(alpha, opacity * 255 / 100), s), blend);
    }
  doc_end(d);
  return b;
}

/* 0.5: a multi-stop gradient, through a table of 1024 samples (gradient.h) */
#define GLUT 1024
Box fill_gradient_ex(Doc *d, double x0, double y0, double x1, double y1, int kind, const Gradient *gsrc, int reverse, int dither, int opacity, int blend)
{
  static const u8 bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
  Layer *l = paintable(d);
  Box b = work_box(d);
  double dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy, base = atan2(dy, dx);
  Gradient *g;
  u8 *lut;
  i32 x, y;
  if (!l || len2 < 1e-9 || !gsrc) return box_make(0, 0, 0, 0);
  if (!(g = (Gradient *)malloc(sizeof *g))) return box_make(0, 0, 0, 0);
  if (!(lut = (u8 *)malloc(GLUT * 4))) { free(g); return box_make(0, 0, 0, 0); }
  *g = *gsrc;
  g->fg = d->fg; g->bg = d->bg;
  gradient_lut(g, lut, GLUT);
  free(g);
  doc_begin(d, "Gradient");
  doc_note_layer(d, d->active, b);
  for (y = b.y0; y < b.y1; y++)
    for (x = b.x0; x < b.x1; x++) {
      double px = x + 0.5 - x0, py = y + 0.5 - y0, t, dith = dither ? (bayer[(y & 3) * 4 + (x & 3)] - 7.5) / 16.0 : 0;
      u8 col[3];
      const u8 *e;
      int k, s = sel_at(d, x, y), idx;
      if (!s) continue;
      switch (kind) {
      case GR_RADIAL: t = sqrt((px * px + py * py) / len2); break;
      case GR_ANGLE: t = atan2(py, px) - base; while (t < 0) t += 2 * M_PI; t /= 2 * M_PI; break;
      case GR_REFLECTED: t = fabs((px * dx + py * dy) / len2); break;
      case GR_DIAMOND: t = (fabs(px * dx + py * dy) + fabs(-px * dy + py * dx)) / len2; break;
      default: t = (px * dx + py * dy) / len2;
      }
      if (t < 0) t = 0;
      if (t > 1) t = 1;
      if (reverse) t = 1 - t;
      idx = (int)(t * (GLUT - 1) + 0.5);
      e = lut + idx * 4;
      for (k = 0; k < 3; k++) col[k] = (u8)clamp255(e[k] + dith);
      put_px(l, l->px + ((long)y * d->w + x) * 4, col, mul255(mul255(clamp255(e[3] + dith), opacity * 255 / 100), s), blend);
    }
  doc_end(d);
  free(lut);
  return b;
}
/* 0.5: a pattern laid through coverage mask m (NULL: everything) in box b */
static Box apply_pattern(Doc *d, const u8 *m, Box b, const Pattern *pt, i32 ox, i32 oy, int opacity, int blend, const char *name)
{
  Layer *l = paintable(d);
  i32 x, y;
  b = box_clip(b, work_box(d));
  if (!l || box_empty(b) || !pt || !pt->px || pt->w < 1 || pt->h < 1) return box_make(0, 0, 0, 0);
  doc_begin(d, name);
  doc_note_layer(d, d->active, b);
  for (y = b.y0; y < b.y1; y++)
    for (x = b.x0; x < b.x1; x++) {
      long k = (long)y * d->w + x;
      int c = m ? m[k] : 255;
      const u8 *q;
      if (!c) continue;
      q = pattern_at(pt, x, y, ox, oy);
      put_px(l, l->px + k * 4, q, mul255(mul255(mul255(c, q[3]), opacity * 255 / 100), sel_at(d, x, y)), blend);
    }
  doc_end(d);
  return b;
}
Box fill_pattern(Doc *d, const Pattern *p, i32 ox, i32 oy, int opacity, int blend) { return apply_pattern(d, NULL, work_box(d), p, ox, oy, opacity, blend, "Fill"); }
Box fill_bucket_pattern(Doc *d, i32 x, i32 y, const Pattern *p, int tol, int contiguous, int all_layers, int opacity, int antialias)
{
  u8 *m;
  Box b;
  if (x < 0 || y < 0 || x >= d->w || y >= d->h || !paintable(d) || !p) return box_make(0, 0, 0, 0);
  if (!(m = similar(d, x, y, tol, contiguous, all_layers))) return box_make(0, 0, 0, 0);
  if (antialias) soften(m, d->w, d->h);
  b = mask_bounds(m, d->w, d->h);
  b = apply_pattern(d, m, b, p, 0, 0, opacity, BM_NORMAL, "Paint Bucket");
  free(m);
  return b;
}

/* ------------------------------------------------------------------ shapes */
/* coverage of a polygon (points xy, n; even-odd), 4 sub-rows a pixel row with exact horizontal
   coverage, into mask m (w x h); its box */
static Box poly_mask(const double *xy, int n, u8 *m, i32 w, i32 h)
{
  double y0 = 1e30, y1 = -1e30, x0 = 1e30, x1 = -1e30;
  i32 y, i, j, k;
  u16 *acc;
  double *xs;
  Box b;
  if (n < 3) return box_make(0, 0, 0, 0);
  for (i = 0; i < n; i++) {
    if (xy[2 * i + 1] < y0) y0 = xy[2 * i + 1];
    if (xy[2 * i + 1] > y1) y1 = xy[2 * i + 1];
    if (xy[2 * i] < x0) x0 = xy[2 * i];
    if (xy[2 * i] > x1) x1 = xy[2 * i];
  }
  b = box_clip(box_make((i32)floor(x0), (i32)floor(y0), (i32)ceil(x1) + 1, (i32)ceil(y1) + 1), box_make(0, 0, w, h));
  if (box_empty(b)) return b;
  acc = (u16 *)malloc((size_t)(b.x1 - b.x0 + 1) * sizeof(u16));
  xs = (double *)malloc((size_t)(n + 1) * sizeof(double));
  if (!acc || !xs) { free(acc); free(xs); return box_make(0, 0, 0, 0); }
  for (y = b.y0; y < b.y1; y++) {
    int sub;
    memset(acc, 0, (size_t)(b.x1 - b.x0 + 1) * sizeof(u16));
    for (sub = 0; sub < 4; sub++) {
      double sy = y + (sub + 0.5) / 4;
      int m2 = 0;
      for (i = 0; i < n; i++) {
        double ax = xy[2 * i], ay = xy[2 * i + 1], bx = xy[2 * ((i + 1) % n)], by = xy[2 * ((i + 1) % n) + 1];
        if ((ay <= sy) != (by <= sy)) xs[m2++] = ax + (sy - ay) * (bx - ax) / (by - ay);
      }
      for (i = 1; i < m2; i++) for (j = i; j > 0 && xs[j - 1] > xs[j]; j--) { double t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
      for (i = 0; i + 1 < m2; i += 2) {
        double xa = xs[i] < b.x0 ? b.x0 : xs[i], xb = xs[i + 1] > b.x1 ? b.x1 : xs[i + 1];
        i32 ia, ib;
        if (xb <= xa) continue;
        ia = (i32)floor(xa); ib = (i32)floor(xb);
        if (ia == ib) { acc[ia - b.x0] += (u16)((xb - xa) * 64 + 0.5); continue; }
        acc[ia - b.x0] += (u16)((ia + 1 - xa) * 64 + 0.5);
        for (k = ia + 1; k < ib; k++) acc[k - b.x0] += 64;
        if (ib < b.x1) acc[ib - b.x0] += (u16)((xb - ib) * 64 + 0.5);
      }
    }
    for (k = b.x0; k < b.x1; k++) { int v = acc[k - b.x0]; m[(long)y * w + k] = (u8)(v >= 256 ? 255 : v); }
  }
  free(acc); free(xs);
  return b;
}
/* an ellipse in box x0..x1, y0..y1 as a polygon (enough points to look round) */
static double *ellipse_poly(double x0, double y0, double x1, double y1, int *n)
{
  double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = fabs(x1 - x0) / 2, ry = fabs(y1 - y0) / 2, *p;
  int k, m = (int)((rx + ry) * 1.5) + 16;
  if (m > 2000) m = 2000;
  if (!(p = (double *)malloc((size_t)m * 2 * sizeof(double)))) return NULL;
  for (k = 0; k < m; k++) { p[2 * k] = cx + rx * cos(2 * M_PI * k / m); p[2 * k + 1] = cy + ry * sin(2 * M_PI * k / m); }
  *n = m;
  return p;
}
/* a rectangle with rounded corners (radius r) as a polygon */
static double *rect_poly(double x0, double y0, double x1, double y1, double r, int *n)
{
  double *p, t;
  int k, seg, m = 0;
  if (x1 < x0) { t = x0; x0 = x1; x1 = t; }
  if (y1 < y0) { t = y0; y0 = y1; y1 = t; }
  if (r > (x1 - x0) / 2) r = (x1 - x0) / 2;
  if (r > (y1 - y0) / 2) r = (y1 - y0) / 2;
  seg = r < 1 ? 1 : (int)(r / 2) + 4;
  if (!(p = (double *)malloc((size_t)(seg + 1) * 4 * 2 * sizeof(double)))) return NULL;
  for (k = 0; k < 4; k++) {
    double cx = k == 0 || k == 3 ? x1 - r : x0 + r, cy = k < 2 ? y1 - r : y0 + r, a0 = k * M_PI / 2;
    int s;
    for (s = 0; s <= seg; s++) { double a = a0 + (M_PI / 2) * s / seg; p[2 * m] = cx + r * cos(a); p[2 * m + 1] = cy + r * sin(a); m++; }
  }
  *n = m;
  return p;
}
static Box shape_poly(Doc *d, double *p, int n, u32 color, int opacity, const char *name)
{
  u8 *m;
  Box b;
  if (!p) return box_make(0, 0, 0, 0);
  if (!(m = (u8 *)calloc((size_t)d->w * d->h, 1))) { free(p); return box_make(0, 0, 0, 0); }
  b = poly_mask(p, n, m, d->w, d->h);
  b = apply_mask(d, m, b, color, opacity, BM_NORMAL, name);
  free(m); free(p);
  return b;
}
Box shape_rect(Doc *d, double x0, double y0, double x1, double y1, double radius, u32 color, int opacity)
{
  int n;
  double *p = rect_poly(x0, y0, x1, y1, radius, &n);
  return shape_poly(d, p, n, color, opacity, "Rectangle Tool");
}
Box shape_ellipse(Doc *d, double x0, double y0, double x1, double y1, u32 color, int opacity)
{
  int n;
  double *p = ellipse_poly(x0, y0, x1, y1, &n);
  return shape_poly(d, p, n, color, opacity, "Ellipse Tool");
}
Box shape_line(Doc *d, double x0, double y0, double x1, double y1, double width, u32 color, int opacity)
{
  double dx = x1 - x0, dy = y1 - y0, len = sqrt(dx * dx + dy * dy), nx, ny, *p;
  if (len < 1e-6) return box_make(0, 0, 0, 0);
  if (width < 1) width = 1;
  nx = -dy / len * width / 2; ny = dx / len * width / 2;
  if (!(p = (double *)malloc(8 * sizeof(double)))) return box_make(0, 0, 0, 0);
  p[0] = x0 + nx; p[1] = y0 + ny; p[2] = x1 + nx; p[3] = y1 + ny; p[4] = x1 - nx; p[5] = y1 - ny; p[6] = x0 - nx; p[7] = y0 - ny;
  return shape_poly(d, p, 4, color, opacity, "Line Tool");
}

/* ------------------------------------------------------------------ selections */
static void sel_update_box(Doc *d)
{
  if (!d->sel) { d->selbox = doc_box(d); return; }
  d->selbox = mask_bounds(d->sel, d->w, d->h);
  if (box_empty(d->selbox)) { free(d->sel); d->sel = NULL; d->selbox = doc_box(d); }
}
/* mask m (taken) combined with the selection by op */
static void sel_combine(Doc *d, u8 *m, int op, const char *name)
{
  long k, n = (long)d->w * d->h;
  doc_begin(d, name);
  doc_note_sel(d);
  if (op == SEL_REPLACE || (!d->sel && op == SEL_ADD)) { free(d->sel); d->sel = m; }
  else if (!d->sel && op == SEL_INTERSECT) { free(d->sel); d->sel = m; }
  else if (!d->sel && op == SEL_SUBTRACT) { for (k = 0; k < n; k++) m[k] = (u8)(255 - m[k]); free(d->sel); d->sel = m; }
  else {
    for (k = 0; k < n; k++) {
      int a = d->sel[k], b = m[k];
      d->sel[k] = (u8)(op == SEL_ADD ? (a + b - mul255(a, b)) : op == SEL_SUBTRACT ? mul255(a, 255 - b) : mul255(a, b));
    }
    free(m);
  }
  sel_update_box(d);
  doc_end(d);
}
/* a box blur of the mask, radius r, three passes (near enough a Gaussian) */
static void mask_blur(u8 *m, i32 w, i32 h, int r)
{
  u8 *t;
  i32 x, y;
  int pass;
  if (r < 1) return;
  if (!(t = (u8 *)malloc((size_t)(w > h ? w : h)))) return;
  for (pass = 0; pass < 3; pass++) {
    for (y = 0; y < h && !progress((long)pass * (w + h) + y, 3L * (w + h)); y++) {   /* (0.8: progress, and Abort) */
      u8 *row = m + (long)y * w;
      long sum = 0;
      memcpy(t, row, (size_t)w);
      for (x = -r; x <= r; x++) sum += t[x < 0 ? 0 : x >= w ? w - 1 : x];
      for (x = 0; x < w; x++) {
        row[x] = (u8)(sum / (2 * r + 1));
        sum += t[x + r + 1 >= w ? w - 1 : x + r + 1] - t[x - r < 0 ? 0 : x - r];
      }
    }
    for (x = 0; x < w && !progress((long)pass * (w + h) + h + x, 3L * (w + h)); x++) {
      long sum = 0;
      for (y = 0; y < h; y++) t[y] = m[(long)y * w + x];
      for (y = -r; y <= r; y++) sum += t[y < 0 ? 0 : y >= h ? h - 1 : y];
      for (y = 0; y < h; y++) {
        m[(long)y * w + x] = (u8)(sum / (2 * r + 1));
        sum += t[y + r + 1 >= h ? h - 1 : y + r + 1] - t[y - r < 0 ? 0 : y - r];
      }
    }
  }
  free(t);
}
static u8 *new_mask(Doc *d) { return (u8 *)calloc((size_t)d->w * d->h, 1); }
void sel_rect(Doc *d, i32 x0, i32 y0, i32 x1, i32 y1, int op, int feather)
{
  u8 *m;
  i32 x, y, t;
  Box b;
  if (x1 < x0) { t = x0; x0 = x1; x1 = t; }
  if (y1 < y0) { t = y0; y0 = y1; y1 = t; }
  b = box_clip(box_make(x0, y0, x1, y1), doc_box(d));
  if (box_empty(b) && op == SEL_REPLACE) { sel_none(d); return; }
  if (!(m = new_mask(d))) return;
  for (y = b.y0; y < b.y1; y++) for (x = b.x0; x < b.x1; x++) m[(long)y * d->w + x] = 255;
  if (feather > 0) mask_blur(m, d->w, d->h, (feather + 1) / 2);
  sel_combine(d, m, op, "Rectangular Marquee");
}
void sel_ellipse(Doc *d, i32 x0, i32 y0, i32 x1, i32 y1, int op, int feather)
{
  u8 *m;
  int n;
  double *p;
  if ((x1 == x0 || y1 == y0) && op == SEL_REPLACE) { sel_none(d); return; }
  if (!(m = new_mask(d))) return;
  if ((p = ellipse_poly(x0, y0, x1, y1, &n))) { poly_mask(p, n, m, d->w, d->h); free(p); }
  if (feather > 0) mask_blur(m, d->w, d->h, (feather + 1) / 2);
  sel_combine(d, m, op, "Elliptical Marquee");
}
void sel_polygon(Doc *d, const double *xy, int n, int op, int feather) { sel_polygon_ex(d, xy, n, op, feather, 1); }
void sel_wand(Doc *d, i32 x, i32 y, int tol, int contiguous, int all_layers, int op) { sel_wand_ex(d, x, y, tol, contiguous, all_layers, op, 1); }
/* 0.7: the same with the Anti-alias option: off, edges are hard (half covered or more is in) */
static void harden(u8 *m, long n) { long k; for (k = 0; k < n; k++) m[k] = m[k] >= 128 ? 255 : 0; }
void sel_polygon_ex(Doc *d, const double *xy, int n, int op, int feather, int antialias)
{
  u8 *m;
  if (n < 3) { if (op == SEL_REPLACE) sel_none(d); return; }
  if (!(m = new_mask(d))) return;
  poly_mask(xy, n, m, d->w, d->h);
  if (!antialias) harden(m, (long)d->w * d->h);
  if (feather > 0) mask_blur(m, d->w, d->h, (feather + 1) / 2);
  sel_combine(d, m, op, "Lasso");
}
void sel_ellipse_ex(Doc *d, i32 x0, i32 y0, i32 x1, i32 y1, int op, int feather, int antialias)
{
  u8 *m;
  int n;
  double *p;
  if ((x1 == x0 || y1 == y0) && op == SEL_REPLACE) { sel_none(d); return; }
  if (!(m = new_mask(d))) return;
  if ((p = ellipse_poly(x0, y0, x1, y1, &n))) { poly_mask(p, n, m, d->w, d->h); free(p); }
  if (!antialias) harden(m, (long)d->w * d->h);
  if (feather > 0) mask_blur(m, d->w, d->h, (feather + 1) / 2);
  sel_combine(d, m, op, "Elliptical Marquee");
}
void sel_wand_ex(Doc *d, i32 x, i32 y, int tol, int contiguous, int all_layers, int op, int antialias)
{
  u8 *m;
  if (x < 0 || y < 0 || x >= d->w || y >= d->h) return;
  if (!(m = similar(d, x, y, tol, contiguous, all_layers))) return;
  if (antialias) soften(m, d->w, d->h);
  sel_combine(d, m, op, "Magic Wand");
}
void sel_all(Doc *d)
{
  u8 *m = new_mask(d);
  if (!m) return;
  memset(m, 255, (size_t)d->w * d->h);
  sel_combine(d, m, SEL_REPLACE, "Select All");
}
void sel_none(Doc *d)
{
  if (!d->sel) return;
  doc_begin(d, "Deselect");
  doc_note_sel(d);
  free(d->sel); d->sel = NULL; d->selbox = doc_box(d);
  doc_end(d);
}
void sel_invert(Doc *d)
{
  long k, n = (long)d->w * d->h;
  u8 *m = new_mask(d);
  if (!m) return;
  for (k = 0; k < n; k++) m[k] = (u8)(255 - (d->sel ? d->sel[k] : 0));
  sel_combine(d, m, SEL_REPLACE, "Inverse");
}
/* the selection changed by fn (on a copy), as one step */
static void sel_modify(Doc *d, const char *name, void (*fn)(Doc *, u8 *, int), int arg)
{
  u8 *m;
  if (!d->sel || !(m = (u8 *)malloc((size_t)d->w * d->h))) return;
  memcpy(m, d->sel, (size_t)d->w * d->h);
  fn(d, m, arg);
  if (progress_aborted) { free(m); return; }             /* (0.8: Abort pressed: the selection as it was) */
  sel_combine(d, m, SEL_REPLACE, name);
}
static void do_feather(Doc *d, u8 *m, int r) { mask_blur(m, d->w, d->h, r); }
void sel_feather(Doc *d, double radius) { sel_modify(d, "Feather", do_feather, (int)(radius / 2 + 0.5) < 1 ? 1 : (int)(radius / 2 + 0.5)); }
/* the mask grown (or shrunk) by r: the most (least) in each row then column within r */
static void do_grow(Doc *d, u8 *m, int by)
{
  i32 w = d->w, h = d->h, x, y, k;
  int r = by < 0 ? -by : by, grow = by > 0;
  u8 *t = (u8 *)malloc((size_t)(w > h ? w : h));
  if (!t || !r) { free(t); return; }
  for (y = 0; y < h && !progress(y, (long)w + h); y++) {          /* (0.8: progress, and Abort) */
    u8 *row = m + (long)y * w;
    memcpy(t, row, (size_t)w);
    for (x = 0; x < w; x++) {
      int v = t[x];
      for (k = x - r; k <= x + r; k++) {
        int q = k < 0 || k >= w ? (grow ? 0 : 255) : t[k];
        if (grow ? q > v : q < v) v = q;
      }
      row[x] = (u8)v;
    }
  }
  for (x = 0; x < w && !progress((long)h + x, (long)w + h); x++) {
    for (y = 0; y < h; y++) t[y] = m[(long)y * w + x];
    for (y = 0; y < h; y++) {
      int v = t[y];
      for (k = y - r; k <= y + r; k++) {
        int q = k < 0 || k >= h ? (grow ? 0 : 255) : t[k];
        if (grow ? q > v : q < v) v = q;
      }
      m[(long)y * w + x] = (u8)v;
    }
  }
  free(t);
}
void sel_grow(Doc *d, int by) { sel_modify(d, by > 0 ? "Expand" : "Contract", do_grow, by); }
static void do_border(Doc *d, u8 *m, int width)
{
  u8 *in = (u8 *)malloc((size_t)d->w * d->h);
  long k, n = (long)d->w * d->h;
  if (!in) return;
  memcpy(in, m, (size_t)n);
  do_grow(d, m, (width + 1) / 2);
  do_grow(d, in, -(width / 2 > 0 ? width / 2 : 1));
  for (k = 0; k < n; k++) m[k] = (u8)mul255(m[k], 255 - in[k]);
  free(in);
}
void sel_border(Doc *d, int width) { sel_modify(d, "Border", do_border, width < 1 ? 1 : width); }
static void do_smooth(Doc *d, u8 *m, int r)
{
  long k, n = (long)d->w * d->h;
  mask_blur(m, d->w, d->h, r);
  for (k = 0; k < n; k++) m[k] = m[k] >= 128 ? 255 : 0;
  soften(m, d->w, d->h);
}
void sel_smooth(Doc *d, int radius) { sel_modify(d, "Smooth", do_smooth, radius < 1 ? 1 : radius); }
void sel_from_alpha(Doc *d, int li)
{
  u8 *m;
  long k, n = (long)d->w * d->h;
  Layer *l;
  if (li < 0 || li >= d->nlay || !(m = new_mask(d))) return;
  l = d->lay[li];
  if (l->px) for (k = 0; k < n; k++) m[k] = l->px[k * 4 + 3];
  else if (l->live) {
    /* 0.9: a type or shape layer: its rendering's coverage */
    Box b;
    i32 x, y;
    if (!l->live_ok) live_render(d, l);
    b = l->livebox;
    if (l->livepx)
      for (y = b.y0; y < b.y1; y++) for (x = b.x0; x < b.x1; x++) m[(long)y * d->w + x] = l->livepx[((size_t)(y - b.y0) * (b.x1 - b.x0) + (x - b.x0)) * 4 + 3];
  } else { free(m); return; }
  sel_combine(d, m, SEL_REPLACE, "Load Selection");
}
void sel_color_range(Doc *d, u32 color, int fuzz)
{
  u8 *img = (u8 *)malloc((size_t)d->w * d->h * 4), *m, c[3];
  long k, n = (long)d->w * d->h;
  if (!img || !(m = new_mask(d))) { free(img); return; }
  doc_composite(d, doc_box(d), img, (long)d->w * 4);
  rgb_of(color, c);
  if (fuzz < 1) fuzz = 1;
  for (k = 0; k < n; k++) {
    int dd = abs(img[k * 4] - c[0]), e = abs(img[k * 4 + 1] - c[1]), f = abs(img[k * 4 + 2] - c[2]), mx = dd > e ? dd : e;
    if (f > mx) mx = f;
    m[k] = (u8)(mx >= fuzz ? 0 : 255 - mx * 255 / fuzz);
  }
  free(img);
  sel_combine(d, m, SEL_REPLACE, "Color Range");
}
int sel_bounds(Doc *d, Box *b)
{
  if (!d->sel) { *b = doc_box(d); return 0; }
  *b = d->selbox;
  return !box_empty(*b);
}
int sel_edges(Doc *d, Box r, i32 **out)
{
  i32 x, y, n = 0, cap = 256, *e;
  *out = NULL;
  if (!d->sel) return 0;
  r = box_clip(r, box_clip(box_make(d->selbox.x0 - 1, d->selbox.y0 - 1, d->selbox.x1 + 1, d->selbox.y1 + 1), doc_box(d)));
  if (box_empty(r) || !(e = (i32 *)malloc((size_t)cap * 4 * sizeof(i32)))) return 0;
#define IN(x, y) ((x) >= 0 && (y) >= 0 && (x) < d->w && (y) < d->h && d->sel[(long)(y) * d->w + (x)] >= 128)
#define ADD(a, b, c, dd) do { if (n == cap) { i32 *ne = (i32 *)realloc(e, (size_t)cap * 8 * sizeof(i32)); if (!ne) goto done; e = ne; cap *= 2; } \
    e[n * 4] = (a); e[n * 4 + 1] = (b); e[n * 4 + 2] = (c); e[n * 4 + 3] = (dd); n++; } while (0)
  /* horizontal edges: runs along each row boundary */
  for (y = r.y0; y <= r.y1; y++) {
    i32 start = -1;
    for (x = r.x0; x <= r.x1; x++) {
      int edge = x < r.x1 && IN(x, y) != IN(x, y - 1);
      if (edge && start < 0) start = x;
      if (!edge && start >= 0) { ADD(start, y, x, y); start = -1; }
    }
  }
  for (x = r.x0; x <= r.x1; x++) {
    i32 start = -1;
    for (y = r.y0; y <= r.y1; y++) {
      int edge = y < r.y1 && IN(x, y) != IN(x - 1, y);
      if (edge && start < 0) start = y;
      if (!edge && start >= 0) { ADD(x, start, x, y); start = -1; }
    }
  }
done:
#undef IN
#undef ADD
  *out = e;
  return n;
}
Box stroke_selection(Doc *d, int width, int where, u32 color, int opacity)
{
  u8 *outer, *inner;
  long k, n = (long)d->w * d->h;
  Box b;
  if (!d->sel || width < 1 || !(outer = (u8 *)malloc((size_t)n)) || !(inner = (u8 *)malloc((size_t)n))) return box_make(0, 0, 0, 0);
  memcpy(outer, d->sel, (size_t)n); memcpy(inner, d->sel, (size_t)n);
  /* where: 0 inside, 1 centred, 2 outside */
  do_grow(d, outer, where == 0 ? 0 : where == 1 ? (width + 1) / 2 : width);
  do_grow(d, inner, where == 0 ? -width : where == 1 ? -(width / 2) : 0);
  for (k = 0; k < n; k++) outer[k] = (u8)mul255(outer[k], 255 - inner[k]);
  free(inner);
  {
    /* drawn past the selection, so the selection is set aside while it is */
    u8 *s = d->sel;
    Box sb = d->selbox;
    d->sel = NULL; d->selbox = doc_box(d);
    b = mask_bounds(outer, d->w, d->h);
    b = apply_mask(d, outer, b, color, opacity, BM_NORMAL, "Stroke");
    d->sel = s; d->selbox = sb;
  }
  free(outer);
  return b;
}

/* ------------------------------------------------------------------ sampling */
static void px_at(const u8 *px, i32 w, i32 h, i32 x, i32 y, int *o)
{
  const u8 *p;
  if (x < 0 || y < 0 || x >= w || y >= h) { o[0] = o[1] = o[2] = o[3] = 0; return; }
  p = px + ((long)y * w + x) * 4;
  /* premultiplied, so transparent pixels don't darken what's around them */
  o[0] = p[0] * p[3]; o[1] = p[1] * p[3]; o[2] = p[2] * p[3]; o[3] = p[3] * 255;
}
static void unpremul(const double *a, u8 *out)
{
  int k;
  if (a[3] <= 0.5) { out[0] = out[1] = out[2] = out[3] = 0; return; }
  for (k = 0; k < 3; k++) out[k] = (u8)clamp255(a[k] / a[3] * 255);
  out[3] = (u8)clamp255(a[3] / 255);
}
void sample_bilinear(const u8 *px, i32 w, i32 h, double x, double y, u8 *out)
{
  i32 x0 = (i32)floor(x - 0.5), y0 = (i32)floor(y - 0.5);
  double fx = x - 0.5 - x0, fy = y - 0.5 - y0, acc[4] = { 0, 0, 0, 0 };
  int p[4][4], k;
  px_at(px, w, h, x0, y0, p[0]); px_at(px, w, h, x0 + 1, y0, p[1]); px_at(px, w, h, x0, y0 + 1, p[2]); px_at(px, w, h, x0 + 1, y0 + 1, p[3]);
  for (k = 0; k < 4; k++) acc[k] = (p[0][k] * (1 - fx) + p[1][k] * fx) * (1 - fy) + (p[2][k] * (1 - fx) + p[3][k] * fx) * fy;
  unpremul(acc, out);
}
static double cubic(double t)
{
  t = fabs(t);
  if (t < 1) return (1.5 * t - 2.5) * t * t + 1;
  if (t < 2) return ((-0.5 * t + 2.5) * t - 4) * t + 2;
  return 0;
}
void sample_bicubic(const u8 *px, i32 w, i32 h, double x, double y, u8 *out)
{
  i32 x0 = (i32)floor(x - 0.5), y0 = (i32)floor(y - 0.5), i, j;
  double fx = x - 0.5 - x0, fy = y - 0.5 - y0, acc[4] = { 0, 0, 0, 0 }, wx[4], wy[4];
  int p[4], k;
  for (i = 0; i < 4; i++) { wx[i] = cubic(fx - (i - 1)); wy[i] = cubic(fy - (i - 1)); }
  for (j = 0; j < 4; j++)
    for (i = 0; i < 4; i++) {
      i32 xx = x0 + i - 1, yy = y0 + j - 1;
      /* the edge repeated (not transparency) past the image */
      if (xx < 0) xx = 0;
      if (yy < 0) yy = 0;
      if (xx >= w) xx = w - 1;
      if (yy >= h) yy = h - 1;
      px_at(px, w, h, xx, yy, p);
      for (k = 0; k < 4; k++) acc[k] += p[k] * wx[i] * wy[j];
    }
  for (k = 0; k < 4; k++) if (acc[k] < 0) acc[k] = 0;
  if (acc[3] > 255 * 255) { double f = 255.0 * 255 / acc[3]; for (k = 0; k < 4; k++) acc[k] *= f; }
  unpremul(acc, out);
}

/* ------------------------------------------------------------------ moving and transforming */
/* the active layer split: what is selected lifted off (returned, canvas size) and taken out of the
   layer; without a selection the whole layer is lifted */
static u8 *lift(Doc *d, Layer *l)
{
  long k, n = (long)d->w * d->h;
  u8 *f = (u8 *)malloc((size_t)n * 4);
  if (!f) return NULL;
  memcpy(f, l->px, (size_t)n * 4);
  if (!d->sel) { memset(l->px, 0, (size_t)n * 4); return f; }
  for (k = 0; k < n; k++) {
    int s = d->sel[k];
    f[k * 4 + 3] = (u8)mul255(f[k * 4 + 3], s);
    l->px[k * 4 + 3] = (u8)mul255(l->px[k * 4 + 3], 255 - s);
  }
  return f;
}
Box move_layer(Doc *d, i32 dx, i32 dy)
{
  Layer *l = paintable(d);
  u8 *f;
  i32 x, y;
  if (!l || l->lock_pos || (!dx && !dy)) return box_make(0, 0, 0, 0);
  doc_begin(d, "Move");
  doc_note_layer(d, d->active, doc_box(d));
  if (d->sel) doc_note_sel(d);
  if (l->x && layerx_move_hook && !d->sel) layerx_move_hook(d, d->active, dx, dy);   /* 1.2 */
  /* a linked mask moves with the layer (0.3) */
  if (l->mask && l->mask_linked && !d->sel) {
    u8 *m = (u8 *)malloc((size_t)d->w * d->h);
    doc_note_mask(d, d->active, doc_box(d));
    if (m) {
      memset(m, 255, (size_t)d->w * d->h);
      for (y = 0; y < d->h; y++) for (x = 0; x < d->w; x++) {
        i32 sx = x - dx, sy = y - dy;
        if (sx >= 0 && sy >= 0 && sx < d->w && sy < d->h) m[(long)y * d->w + x] = l->mask[(long)sy * d->w + sx];
      }
      memcpy(l->mask, m, (size_t)d->w * d->h); free(m); l->maskfx_ok = 0;
    }
  }
  if (!(f = lift(d, l))) { doc_end(d); return box_make(0, 0, 0, 0); }
  for (y = 0; y < d->h; y++)
    for (x = 0; x < d->w; x++) {
      i32 sx = x - dx, sy = y - dy;
      const u8 *s;
      if (sx < 0 || sy < 0 || sx >= d->w || sy >= d->h) continue;
      s = f + ((long)sy * d->w + sx) * 4;
      if (s[3]) blend_pixel(l->px + ((long)y * d->w + x) * 4, s, 255, BM_NORMAL);
    }
  free(f);
  /* the selection goes with what it selected */
  if (d->sel) {
    u8 *m = new_mask(d);
    if (m) {
      for (y = 0; y < d->h; y++) for (x = 0; x < d->w; x++) {
        i32 sx = x - dx, sy = y - dy;
        if (sx >= 0 && sy >= 0 && sx < d->w && sy < d->h) m[(long)y * d->w + x] = d->sel[(long)sy * d->w + sx];
      }
      free(d->sel); d->sel = m; sel_update_box(d);
    }
  }
  doc_end(d);
  return doc_box(d);
}
Box transform_layer(Doc *d, const double *m)
{
  Layer *l = paintable(d);
  u8 *f;
  i32 x, y;
  if (!l) return box_make(0, 0, 0, 0);
  doc_begin(d, "Free Transform");
  doc_note_layer(d, d->active, doc_box(d));
  if (d->sel) doc_note_sel(d);
  if (!(f = lift(d, l))) { doc_end(d); return box_make(0, 0, 0, 0); }
  for (y = 0; y < d->h; y++)
    for (x = 0; x < d->w; x++) {
      double sx = m[0] * (x + 0.5) + m[1] * (y + 0.5) + m[2], sy = m[3] * (x + 0.5) + m[4] * (y + 0.5) + m[5];
      u8 s[4];
      if (sx < -1 || sy < -1 || sx > d->w + 1 || sy > d->h + 1) continue;
      sample_bilinear(f, d->w, d->h, sx, sy, s);
      if (s[3]) blend_pixel(l->px + ((long)y * d->w + x) * 4, s, 255, BM_NORMAL);
    }
  if (d->sel) {
    u8 *nm = new_mask(d);
    if (nm) {
      for (y = 0; y < d->h; y++) for (x = 0; x < d->w; x++) {
        i32 sx = (i32)floor(m[0] * (x + 0.5) + m[1] * (y + 0.5) + m[2]), sy = (i32)floor(m[3] * (x + 0.5) + m[4] * (y + 0.5) + m[5]);
        if (sx >= 0 && sy >= 0 && sx < d->w && sy < d->h) nm[(long)y * d->w + x] = d->sel[(long)sy * d->w + sx];
      }
      free(d->sel); d->sel = nm; sel_update_box(d);
    }
  }
  free(f);
  doc_end(d);
  return doc_box(d);
}
void flip_layer(Doc *d, int vertical)
{
  double m[6];
  if (vertical) { m[0] = 1; m[1] = 0; m[2] = 0; m[3] = 0; m[4] = -1; m[5] = d->h; }
  else { m[0] = -1; m[1] = 0; m[2] = d->w; m[3] = 0; m[4] = 1; m[5] = 0; }
  if (!d->sel) {
    /* exactly, pixel for pixel */
    Layer *l = paintable(d);
    i32 x, y;
    if (!l) return;
    doc_begin(d, vertical ? "Flip Vertical" : "Flip Horizontal");
    doc_note_layer(d, d->active, doc_box(d));
    for (y = 0; y < (vertical ? d->h / 2 : d->h); y++)
      for (x = 0; x < (vertical ? d->w : d->w / 2); x++) {
        u8 *a = l->px + ((long)y * d->w + x) * 4, *b = vertical ? l->px + ((long)(d->h - 1 - y) * d->w + x) * 4 : l->px + ((long)y * d->w + d->w - 1 - x) * 4, t[4];
        memcpy(t, a, 4); memcpy(a, b, 4); memcpy(b, t, 4);
      }
    doc_end(d);
    return;
  }
  transform_layer(d, m);
}
/* every layer (and the selection) remade at w x h by fn(old pixels, new pixels, arg) */
typedef void (*Remap)(Doc *d, const u8 *src, u8 *dst, i32 nw, i32 nh, int bpp, const void *arg);
/* 0.8: the rows remade so far of all the planes remap_all remakes, for progress (and Abort) */
static long rm_done, rm_total;
static int rm_row(i32 y) { return progress(rm_done + y, rm_total); }
static int remap_all(Doc *d, i32 nw, i32 nh, Remap fn, const void *arg, const char *name, const double *fwd)
{
  u8 *np[MAXLAYERS], *ns = NULL, *nc[MAXCHANNELS], *nq = NULL, **nm, **ni;
  int i;
  if (nw < 1 || nh < 1 || nw > MAXDIM || nh > MAXDIM) return 0;
  /* 0.7: the layers' new masks and indices (off the stack) */
  if (!(nm = (u8 **)calloc(2 * MAXLAYERS, sizeof(u8 *)))) return 0;
  ni = nm + MAXLAYERS;
  rm_done = 0; rm_total = (long)nh * ((d->sel != NULL) + d->nchan + (d->qmask != NULL));
  for (i = 0; i < d->nlay; i++) rm_total += (long)nh * ((d->lay[i]->px != NULL) + (d->lay[i]->mask != NULL) + (d->lay[i]->idx != NULL));
  for (i = 0; i < d->nlay; i++) {
    /* 0.7: a group or adjustment layer has no pixels to remake; a layer mask is remade too (white
       where the canvas grows) */
    np[i] = nm[i] = ni[i] = NULL;
    if ((d->lay[i]->px && !(np[i] = (u8 *)calloc((size_t)nw * nh, 4))) ||
        (d->lay[i]->mask && !(nm[i] = (u8 *)malloc((size_t)nw * nh))) ||
        (d->lay[i]->idx && !(ni[i] = (u8 *)calloc((size_t)nw * nh, 1)))) {
      free(np[i]); free(nm[i]); free(ni[i]);
      while (i--) { free(np[i]); free(nm[i]); free(ni[i]); }
      free(nm);
      return 0;
    }
    if (np[i]) { fn(d, d->lay[i]->px, np[i], nw, nh, 4, arg); rm_done += nh; }
    if (ni[i]) { fn(d, d->lay[i]->idx, ni[i], nw, nh, 1, arg); rm_done += nh; }
    if (nm[i]) { memset(nm[i], 255, (size_t)nw * nh); fn(d, d->lay[i]->mask, nm[i], nw, nh, 1, arg); rm_done += nh; }
  }
  if (d->sel && (ns = (u8 *)calloc((size_t)nw * nh, 1))) { fn(d, d->sel, ns, nw, nh, 1, arg); rm_done += nh; }
  /* 0.7: the alpha channels and the Quick Mask plane go with the picture (one dropped if out of memory) */
  for (i = 0; i < d->nchan; i++) if ((nc[i] = (u8 *)calloc((size_t)nw * nh, 1)) != NULL) { fn(d, d->chan[i], nc[i], nw, nh, 1, arg); rm_done += nh; }
  if (d->qmask && (nq = (u8 *)calloc((size_t)nw * nh, 1)) != NULL) fn(d, d->qmask, nq, nw, nh, 1, arg);
  if (progress_aborted) {
    /* 0.8: Abort pressed: the new planes dropped, the picture left as it was */
    for (i = 0; i < d->nlay; i++) { free(np[i]); free(nm[i]); free(ni[i]); }
    for (i = 0; i < d->nchan; i++) free(nc[i]);
    free(ns); free(nq); free(nm);
    return 0;
  }
  doc_begin(d, name);
  doc_note_all(d);
  for (i = 0; i < d->nlay; i++) {
    Layer *l = d->lay[i];
    free(l->px); l->px = np[i];
    /* 0.7: the mask and an indexed layer's indices remade, the mask's feathered copy dropped */
    if (nm[i]) { free(l->mask); l->mask = nm[i]; }
    if (ni[i]) { free(l->idx); l->idx = ni[i]; }
    free(l->maskfx); l->maskfx = NULL; l->maskfx_ok = 0;
  }
  free(d->sel); d->sel = ns;
  {
    int k = 0, n = d->nchan;
    for (i = 0; i < n; i++) {
      free(d->chan[i]);
      if (nc[i]) { if (k != i) memcpy(d->chan_name[k], d->chan_name[i], sizeof d->chan_name[k]); d->chan[k++] = nc[i]; }
    }
    for (i = k; i < n; i++) d->chan[i] = NULL;
    d->nchan = k;
    if (d->qmask) { free(d->qmask); d->qmask = nq; }
  }
  free(nm);
  live_remap(d, fwd);                                      /* 0.9: type and shape layers and paths follow (fwd: old to new) */
  d->w = nw; d->h = nh;
  sel_update_box(d);
  doc_end(d);
  return 1;
}
static void remap_flip(Doc *d, const u8 *s, u8 *o, i32 nw, i32 nh, int bpp, const void *arg)
{
  int v = *(const int *)arg;
  i32 x, y;
  for (y = 0; y < nh && !rm_row(y); y++) for (x = 0; x < nw; x++)
    memcpy(o + ((long)y * nw + x) * bpp, s + ((long)(v ? d->h - 1 - y : y) * d->w + (v ? x : d->w - 1 - x)) * bpp, (size_t)bpp);
}
void image_flip(Doc *d, int vertical)
{
  double m[6] = { 1, 0, 0, 0, 1, 0 };
  if (vertical) { m[4] = -1; m[5] = d->h; } else { m[0] = -1; m[2] = d->w; }
  remap_all(d, d->w, d->h, remap_flip, &vertical, vertical ? "Flip Canvas Vertical" : "Flip Canvas Horizontal", m);
}
static void remap_rot(Doc *d, const u8 *s, u8 *o, i32 nw, i32 nh, int bpp, const void *arg)
{
  int q = *(const int *)arg;
  i32 x, y;
  for (y = 0; y < nh && !rm_row(y); y++) for (x = 0; x < nw; x++) {
    i32 sx, sy;
    if (q == 1) { sx = y; sy = d->h - 1 - x; }
    else if (q == 2) { sx = d->w - 1 - x; sy = d->h - 1 - y; }
    else { sx = d->w - 1 - y; sy = x; }
    memcpy(o + ((long)y * nw + x) * bpp, s + ((long)sy * d->w + sx) * bpp, (size_t)bpp);
  }
}
void image_rotate(Doc *d, int q)
{
  static const char *const names[] = { "", "Rotate 90 Clockwise", "Rotate 180", "Rotate 90 Counter Clockwise" };
  double m[6];
  q = ((q % 4) + 4) % 4;
  if (!q) return;
  if (q == 1) { m[0] = 0; m[1] = -1; m[2] = d->h; m[3] = 1; m[4] = 0; m[5] = 0; }
  else if (q == 2) { m[0] = -1; m[1] = 0; m[2] = d->w; m[3] = 0; m[4] = -1; m[5] = d->h; }
  else { m[0] = 0; m[1] = 1; m[2] = 0; m[3] = -1; m[4] = 0; m[5] = d->w; }
  remap_all(d, q == 2 ? d->w : d->h, q == 2 ? d->h : d->w, remap_rot, &q, names[q], m);
}
typedef struct { int method; } ResizeArg;
static void remap_resize(Doc *d, const u8 *s, u8 *o, i32 nw, i32 nh, int bpp, const void *arg)
{
  int method = ((const ResizeArg *)arg)->method;
  double sx = (double)d->w / nw, sy = (double)d->h / nh;
  i32 x, y;
  if (bpp == 1) {
    for (y = 0; y < nh && !rm_row(y); y++) for (x = 0; x < nw; x++) {
      i32 xx = (i32)((x + 0.5) * sx), yy = (i32)((y + 0.5) * sy);
      o[(long)y * nw + x] = s[(long)(yy < d->h ? yy : d->h - 1) * d->w + (xx < d->w ? xx : d->w - 1)];
    }
    return;
  }
  if ((sx > 1.5 || sy > 1.5) && method != RS_NEAREST) {
    /* shrinking: the average of each new pixel's area of old ones (premultiplied) */
    for (y = 0; y < nh && !rm_row(y); y++) for (x = 0; x < nw; x++) {
      i32 x0 = (i32)(x * sx), x1 = (i32)((x + 1) * sx), y0 = (i32)(y * sy), y1 = (i32)((y + 1) * sy), i, j;
      double acc[4] = { 0, 0, 0, 0 };
      long cnt = 0;
      if (x1 <= x0) x1 = x0 + 1;
      if (y1 <= y0) y1 = y0 + 1;
      for (j = y0; j < y1 && j < d->h; j++) for (i = x0; i < x1 && i < d->w; i++) {
        const u8 *p = s + ((long)j * d->w + i) * 4;
        acc[0] += p[0] * p[3]; acc[1] += p[1] * p[3]; acc[2] += p[2] * p[3]; acc[3] += p[3] * 255; cnt++;
      }
      if (cnt) { int k; for (k = 0; k < 4; k++) acc[k] /= cnt; }
      unpremul(acc, o + ((long)y * nw + x) * 4);
    }
    if (method == RS_SHARPER && !progress_aborted) resample_sharpen(o, nw, nh);     /* (resample.h) */
    return;
  }
  for (y = 0; y < nh && !rm_row(y); y++) for (x = 0; x < nw; x++) {
    double fx = (x + 0.5) * sx, fy = (y + 0.5) * sy;
    u8 *q = o + ((long)y * nw + x) * 4;
    if (method == RS_NEAREST) { i32 xx = (i32)fx, yy = (i32)fy; memcpy(q, s + ((long)(yy < d->h ? yy : d->h - 1) * d->w + (xx < d->w ? xx : d->w - 1)) * 4, 4); }
    else if (method == RS_BICUBIC) sample_bicubic(s, d->w, d->h, fx, fy, q);
    else if (method >= RS_SMOOTHER) resample_kernel(s, d->w, d->h, fx, fy, method, q);
    else {
      /* bilinear, the edge repeated */
      double cx = fx < 0.5 ? 0.5 : fx > d->w - 0.5 ? d->w - 0.5 : fx, cy = fy < 0.5 ? 0.5 : fy > d->h - 0.5 ? d->h - 0.5 : fy;
      sample_bilinear(s, d->w, d->h, cx, cy, q);
    }
  }
}
int image_resize(Doc *d, i32 w, i32 h, int method)
{
  ResizeArg a;
  double m[6];
  a.method = method;
  m[0] = (double)w / d->w; m[1] = 0; m[2] = 0; m[3] = 0; m[4] = (double)h / d->h; m[5] = 0;
  return remap_all(d, w, h, remap_resize, &a, "Image Size", m);
}
typedef struct { i32 ox, oy; u32 fill; } CanvasArg;
static void remap_canvas(Doc *d, const u8 *s, u8 *o, i32 nw, i32 nh, int bpp, const void *arg)
{
  const CanvasArg *c = (const CanvasArg *)arg;
  i32 x, y;
  int fillbg = bpp == 4 && s == d->lay[0]->px && c->fill != 0xFFFFFFFFUL;
  for (y = 0; y < nh && !rm_row(y); y++) for (x = 0; x < nw; x++) {
    i32 sx = x - c->ox, sy = y - c->oy;
    u8 *q = o + ((long)y * nw + x) * bpp;
    if (sx >= 0 && sy >= 0 && sx < d->w && sy < d->h) memcpy(q, s + ((long)sy * d->w + sx) * bpp, (size_t)bpp);
    else if (fillbg) { q[0] = (u8)(c->fill >> 16); q[1] = (u8)(c->fill >> 8); q[2] = (u8)c->fill; q[3] = 255; }
  }
}
int canvas_resize(Doc *d, i32 w, i32 h, int anchor, u32 fill)
{
  CanvasArg a;
  long k, n = (long)d->w * d->h;
  int opaque = 1;
  a.ox = anchor % 3 == 0 ? 0 : anchor % 3 == 1 ? (w - d->w) / 2 : w - d->w;
  a.oy = anchor / 3 == 0 ? 0 : anchor / 3 == 1 ? (h - d->h) / 2 : h - d->h;
  /* the background gets the fill colour where it grows, if it has no transparency of its own */
  for (k = 0; k < n && opaque; k++) if (!d->lay[0]->px || d->lay[0]->px[k * 4 + 3] != 255) opaque = 0;   /* (0.9: a bottom layer without pixels isn't opaque) */
  a.fill = opaque ? fill : 0xFFFFFFFFUL;
  {
    double m[6];
    m[0] = 1; m[1] = 0; m[2] = a.ox; m[3] = 0; m[4] = 1; m[5] = a.oy;
    return remap_all(d, w, h, remap_canvas, &a, "Canvas Size", m);
  }
}
int image_crop(Doc *d, Box b)
{
  CanvasArg a;
  b = box_clip(b, doc_box(d));
  if (box_empty(b)) return 0;
  a.ox = -b.x0; a.oy = -b.y0; a.fill = 0xFFFFFFFFUL;
  {
    double m[6];
    m[0] = 1; m[1] = 0; m[2] = a.ox; m[3] = 0; m[4] = 1; m[5] = a.oy;
    if (!remap_all(d, b.x1 - b.x0, b.y1 - b.y0, remap_canvas, &a, "Crop", m)) return 0;
  }
  return 1;
}
int image_trim(Doc *d)
{
  u8 *img = (u8 *)malloc((size_t)d->w * d->h * 4), *m;
  long k, n = (long)d->w * d->h;
  Box b;
  if (!img || !(m = new_mask(d))) { free(img); return 0; }
  doc_composite(d, doc_box(d), img, (long)d->w * 4);
  for (k = 0; k < n; k++) m[k] = img[k * 4 + 3];
  b = mask_bounds(m, d->w, d->h);
  free(img); free(m);
  if (box_empty(b) || (b.x0 == 0 && b.y0 == 0 && b.x1 == d->w && b.y1 == d->h)) return 0;
  return image_crop(d, b);
}
typedef struct { double c, s, cx, cy, ncx, ncy; } RotArg;
static void remap_angle(Doc *d, const u8 *s, u8 *o, i32 nw, i32 nh, int bpp, const void *arg)
{
  const RotArg *r = (const RotArg *)arg;
  i32 x, y;
  for (y = 0; y < nh && !rm_row(y); y++) for (x = 0; x < nw; x++) {
    double dx = x + 0.5 - r->ncx, dy = y + 0.5 - r->ncy, sx = r->c * dx + r->s * dy + r->cx, sy = -r->s * dx + r->c * dy + r->cy;
    if (bpp == 1) {
      i32 xx = (i32)floor(sx), yy = (i32)floor(sy);
      o[(long)y * nw + x] = xx >= 0 && yy >= 0 && xx < d->w && yy < d->h ? s[(long)yy * d->w + xx] : 0;
    } else sample_bilinear(s, d->w, d->h, sx, sy, o + ((long)y * nw + x) * 4);
  }
}
/* 0.7: the whole picture remade at nw x nh through a projective map from new to old pixel
   positions (inv: 3 x 3, row major, on pixel-centre coordinates); empty outside the old canvas */
typedef struct { const double *h; int interp; } HomArg;
static void remap_hom(Doc *d, const u8 *s, u8 *o, i32 nw, i32 nh, int bpp, const void *arg)
{
  const HomArg *a = (const HomArg *)arg;
  const double *m = a->h;
  i32 x, y;
  for (y = 0; y < nh && !rm_row(y); y++) for (x = 0; x < nw; x++) {
    double X = x + 0.5, Y = y + 0.5, wq = m[6] * X + m[7] * Y + m[8], sx, sy;
    if (wq == 0) continue;
    sx = (m[0] * X + m[1] * Y + m[2]) / wq; sy = (m[3] * X + m[4] * Y + m[5]) / wq;
    if (!(sx > -2 && sy > -2 && sx < d->w + 2 && sy < d->h + 2)) continue;
    if (bpp == 1) {
      /* bilinear on the mask, nothing past the edge */
      i32 x0 = (i32)floor(sx - 0.5), y0 = (i32)floor(sy - 0.5), i, j;
      double fx = sx - 0.5 - x0, fy = sy - 0.5 - y0, acc = 0;
      for (j = 0; j < 2; j++) for (i = 0; i < 2; i++) {
        i32 xx = x0 + i, yy = y0 + j;
        if (xx >= 0 && yy >= 0 && xx < d->w && yy < d->h) acc += s[(long)yy * d->w + xx] * (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
      }
      o[(long)y * nw + x] = (u8)clamp255(acc);
    } else if (a->interp == RS_NEAREST) {
      i32 xx = (i32)floor(sx), yy = (i32)floor(sy);
      if (xx >= 0 && yy >= 0 && xx < d->w && yy < d->h) memcpy(o + ((long)y * nw + x) * 4, s + ((long)yy * d->w + xx) * 4, 4);
    } else if (a->interp == RS_BICUBIC && sx >= 0 && sy >= 0 && sx <= d->w && sy <= d->h) sample_bicubic(s, d->w, d->h, sx, sy, o + ((long)y * nw + x) * 4);
    else sample_bilinear(s, d->w, d->h, sx, sy, o + ((long)y * nw + x) * 4);
  }
}
int image_remap(Doc *d, i32 nw, i32 nh, const double *inv, int interp, const char *name)
{
  HomArg a;
  double f[9], m[6], det;
  a.h = inv; a.interp = interp;
  /* 0.9: the forward map for live layers: inv's inverse, made affine about the old canvas's centre */
  det = inv[0] * (inv[4] * inv[8] - inv[5] * inv[7]) - inv[1] * (inv[3] * inv[8] - inv[5] * inv[6]) + inv[2] * (inv[3] * inv[7] - inv[4] * inv[6]);
  if (det == 0) { m[0] = m[4] = 1; m[1] = m[2] = m[3] = m[5] = 0; }
  else {
    double cx = d->w / 2.0, cy = d->h / 2.0, wq, X, Y, e = 1;
    f[0] = (inv[4] * inv[8] - inv[5] * inv[7]) / det; f[1] = (inv[2] * inv[7] - inv[1] * inv[8]) / det; f[2] = (inv[1] * inv[5] - inv[2] * inv[4]) / det;
    f[3] = (inv[5] * inv[6] - inv[3] * inv[8]) / det; f[4] = (inv[0] * inv[8] - inv[2] * inv[6]) / det; f[5] = (inv[2] * inv[3] - inv[0] * inv[5]) / det;
    f[6] = (inv[3] * inv[7] - inv[4] * inv[6]) / det; f[7] = (inv[1] * inv[6] - inv[0] * inv[7]) / det; f[8] = (inv[0] * inv[4] - inv[1] * inv[3]) / det;
    /* (the map and its derivatives at the centre) */
    wq = f[6] * cx + f[7] * cy + f[8];
    if (wq == 0) wq = 1e-12;
    X = (f[0] * cx + f[1] * cy + f[2]) / wq; Y = (f[3] * cx + f[4] * cy + f[5]) / wq;
    m[0] = (f[0] - X * f[6]) / wq * e; m[1] = (f[1] - X * f[7]) / wq * e;
    m[3] = (f[3] - Y * f[6]) / wq * e; m[4] = (f[4] - Y * f[7]) / wq * e;
    m[2] = X - m[0] * cx - m[1] * cy; m[5] = Y - m[3] * cx - m[4] * cy;
  }
  if (!remap_all(d, nw, nh, remap_hom, &a, name, m)) return 0;
  if (d->mode != MODE_RGB) doc_index_sync_all(d);          /* (resampled colours brought back to the palette) */
  return 1;
}
int image_rotate_angle(Doc *d, double deg)
{
  RotArg r;
  double a = deg * M_PI / 180, c = cos(a), s = sin(a);
  i32 nw = (i32)ceil(fabs(d->w * c) + fabs(d->h * s) - 1e-6), nh = (i32)ceil(fabs(d->w * s) + fabs(d->h * c) - 1e-6);
  double m[6];
  r.c = c; r.s = s; r.cx = d->w / 2.0; r.cy = d->h / 2.0; r.ncx = nw / 2.0; r.ncy = nh / 2.0;
  m[0] = c; m[1] = -s; m[2] = r.ncx - c * r.cx + s * r.cy; m[3] = s; m[4] = c; m[5] = r.ncy - s * r.cx - c * r.cy;
  return remap_all(d, nw, nh, remap_angle, &r, "Rotate Canvas", m);
}

/* ------------------------------------------------------------------ the clipboard */
Clip *clip_copy(Doc *d, int merged)
{
  Box b;
  Clip *c;
  u8 *src = NULL;
  i32 x, y;
  if (d->sel) { if (!sel_bounds(d, &b)) return NULL; } else b = doc_box(d);
  if (!d->lay[d->active]->px) merged = 1;
  if (merged) {
    if (!(src = (u8 *)malloc((size_t)d->w * d->h * 4))) return NULL;
    doc_composite(d, doc_box(d), src, (long)d->w * 4);
  }
  if (!(c = (Clip *)calloc(1, sizeof *c))) { free(src); return NULL; }
  c->w = b.x1 - b.x0; c->h = b.y1 - b.y0; c->x = b.x0; c->y = b.y0;
  if (!(c->px = (u8 *)malloc((size_t)c->w * c->h * 4))) { free(c); free(src); return NULL; }
  for (y = 0; y < c->h; y++)
    for (x = 0; x < c->w; x++) {
      long k = (long)(y + b.y0) * d->w + x + b.x0;
      u8 *q = c->px + ((long)y * c->w + x) * 4;
      memcpy(q, (merged ? src : d->lay[d->active]->px) + k * 4, 4);
      q[3] = (u8)mul255(q[3], sel_at(d, x + b.x0, y + b.y0));
    }
  free(src);
  return c;
}
Box clip_cut(Doc *d) { return clear_selection(d); }
int clip_paste(Doc *d, const Clip *c, int in_place)
{
  Layer *l;
  i32 x, y, ox, oy;
  if (!c || !(l = layer_new(d, "Layer"))) return 0;
  {
    /* "Layer N", the next number free */
    int k, n = d->nlay + 1;
    for (k = 0; k < d->nlay; k++) { int v; if (sscanf_like_layer(d->lay[k]->name, &v) && v >= n) n = v + 1; }
    sprintf(l->name, "Layer %d", n);
  }
  ox = in_place ? c->x : (d->w - c->w) / 2; oy = in_place ? c->y : (d->h - c->h) / 2;
  for (y = 0; y < c->h; y++)
    for (x = 0; x < c->w; x++) {
      i32 X = x + ox, Y = y + oy;
      if (X < 0 || Y < 0 || X >= d->w || Y >= d->h) continue;
      memcpy(l->px + ((long)Y * d->w + X) * 4, c->px + ((long)y * c->w + x) * 4, 4);
    }
  doc_begin(d, "Paste");
  doc_add_layer(d, l, d->active + 1);
  doc_end(d);
  return 1;
}
void clip_free(Clip *c) { if (c) { free(c->px); free(c); } }

/* ------------------------------------------------------------------ colours */
u32 doc_pick(Doc *d, i32 x, i32 y, int all_layers, int radius)
{
  long r = 0, g = 0, b = 0, n = 0;
  i32 i, j;
  u8 px[4];
  if (!d->lay[d->active]->px) all_layers = 1;
  for (j = y - radius; j <= y + radius; j++)
    for (i = x - radius; i <= x + radius; i++) {
      if (i < 0 || j < 0 || i >= d->w || j >= d->h) continue;
      if (all_layers) doc_composite(d, box_make(i, j, i + 1, j + 1), px, 4);
      else memcpy(px, d->lay[d->active]->px + ((long)j * d->w + i) * 4, 4);
      if (!px[3]) continue;
      r += px[0]; g += px[1]; b += px[2]; n++;
    }
  if (!n) return 0xFFFFFFFFUL;                   /* (nothing there: transparent) */
  return (u32)((r / n) << 16 | (g / n) << 8 | (b / n));
}
void rgb_to_hsv(int r, int g, int b, double *h, double *s, double *v)
{
  double mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b), c = mx - mn;
  *v = mx / 255.0;
  *s = mx > 0 ? c / mx : 0;
  if (c == 0) *h = 0;
  else if (mx == r) *h = fmod((g - b) / c + 6, 6) * 60;
  else if (mx == g) *h = ((b - r) / c + 2) * 60;
  else *h = ((r - g) / c + 4) * 60;
}
void hsv_to_rgb(double h, double s, double v, int *r, int *g, int *b)
{
  double c = v * s, hp = fmod(h, 360) / 60, x = c * (1 - fabs(fmod(hp, 2) - 1)), m = v - c, rr = 0, gg = 0, bb = 0;
  if (hp < 0) hp += 6;
  if (hp < 1) { rr = c; gg = x; } else if (hp < 2) { rr = x; gg = c; } else if (hp < 3) { gg = c; bb = x; }
  else if (hp < 4) { gg = x; bb = c; } else if (hp < 5) { rr = x; bb = c; } else { rr = c; bb = x; }
  *r = clamp255((rr + m) * 255); *g = clamp255((gg + m) * 255); *b = clamp255((bb + m) * 255);
}
