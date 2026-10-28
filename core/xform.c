/*
 * xform.c - projective transforms of layers and the selection (see xform.h).
 *
 * The map from the unit square to a quadrilateral is Heckbert's ("Fundamentals of Texture Mapping
 * and Image Warping", 1989); a box to a quad is that after the box is scaled to the unit square.
 * The pixels taken off the layer float in a buffer padded with two transparent pixels each side, so
 * the bilinear and bicubic samplers fade the edges out (anti-aliasing) instead of repeating them.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "xform.h"
#include "paint.h"
#include "layers.h"
#include "channels.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int mul255(int a, int b) { int t = a * b + 128; return (t + (t >> 8)) >> 8; }

/* ------------------------------------------------------------------ 3 x 3 maps */
void hom_mul(const double *a, const double *b, double *out)
{
  double r[9];
  int i, j;
  for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
  memcpy(out, r, sizeof r);
}
int hom_invert(const double *m, double *inv)
{
  double r[9], det;
  int k;
  r[0] = m[4] * m[8] - m[5] * m[7]; r[1] = m[2] * m[7] - m[1] * m[8]; r[2] = m[1] * m[5] - m[2] * m[4];
  r[3] = m[5] * m[6] - m[3] * m[8]; r[4] = m[0] * m[8] - m[2] * m[6]; r[5] = m[2] * m[3] - m[0] * m[5];
  r[6] = m[3] * m[7] - m[4] * m[6]; r[7] = m[1] * m[6] - m[0] * m[7]; r[8] = m[0] * m[4] - m[1] * m[3];
  det = m[0] * r[0] + m[1] * r[3] + m[2] * r[6];
  if (fabs(det) < 1e-12) return 0;
  for (k = 0; k < 9; k++) r[k] /= det;
  if (fabs(r[8]) > 1e-12) { double s = r[8]; for (k = 0; k < 9; k++) r[k] /= s; }
  memcpy(inv, r, sizeof r);
  return 1;
}
void xform_point(const double *h, double x, double y, double *ox, double *oy)
{
  double w = h[6] * x + h[7] * y + h[8];
  if (fabs(w) < 1e-12) w = w < 0 ? -1e-12 : 1e-12;
  *ox = (h[0] * x + h[1] * y + h[2]) / w;
  *oy = (h[3] * x + h[4] * y + h[5]) / w;
}
int hom_square_to_quad(const double *q, double *h)
{
  double x0 = q[0], y0 = q[1], x1 = q[2], y1 = q[3], x2 = q[4], y2 = q[5], x3 = q[6], y3 = q[7];
  double dx3 = x0 - x1 + x2 - x3, dy3 = y0 - y1 + y2 - y3;
  if (fabs(dx3) < 1e-12 && fabs(dy3) < 1e-12) {
    h[0] = x1 - x0; h[1] = x3 - x0; h[2] = x0;
    h[3] = y1 - y0; h[4] = y3 - y0; h[5] = y0;
    h[6] = 0; h[7] = 0; h[8] = 1;
  } else {
    double dx1 = x1 - x2, dx2 = x3 - x2, dy1 = y1 - y2, dy2 = y3 - y2, den = dx1 * dy2 - dx2 * dy1, g, hh;
    if (fabs(den) < 1e-12) return 0;
    g = (dx3 * dy2 - dx2 * dy3) / den;
    hh = (dx1 * dy3 - dx3 * dy1) / den;
    h[0] = x1 - x0 + g * x1; h[1] = x3 - x0 + hh * x3; h[2] = x0;
    h[3] = y1 - y0 + g * y1; h[4] = y3 - y0 + hh * y3; h[5] = y0;
    h[6] = g; h[7] = hh; h[8] = 1;
  }
  return 1;
}
int hom_quad_to_quad(const double *from, const double *to, double *h)
{
  double a[9], b[9], ai[9];
  if (!hom_square_to_quad(from, a) || !hom_square_to_quad(to, b) || !hom_invert(a, ai)) return 0;
  hom_mul(b, ai, h);
  return 1;
}
static void box_quad(Box b, double *q)
{
  q[0] = b.x0; q[1] = b.y0; q[2] = b.x1; q[3] = b.y0; q[4] = b.x1; q[5] = b.y1; q[6] = b.x0; q[7] = b.y1;
}
int xform_matrix(const XForm *t, double *h)
{
  double sq[9], s[9];
  double bw = t->src.x1 - t->src.x0, bh = t->src.y1 - t->src.y0;
  if (bw <= 0 || bh <= 0 || !hom_square_to_quad(t->q, sq)) return 0;
  /* the box onto the unit square */
  s[0] = 1 / bw; s[1] = 0; s[2] = -t->src.x0 / bw;
  s[3] = 0; s[4] = 1 / bh; s[5] = -t->src.y0 / bh;
  s[6] = 0; s[7] = 0; s[8] = 1;
  hom_mul(sq, s, h);
  if (fabs(h[8]) > 1e-12) { int k; double v = h[8]; for (k = 0; k < 9; k++) h[k] /= v; }
  return 1;
}
int xform_inverse(const XForm *t, double *h)
{
  double f[9];
  return xform_matrix(t, f) && hom_invert(f, h);
}
Box xform_bounds(const XForm *t)
{
  double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
  int c;
  for (c = 0; c < 4; c++) {
    double x = t->q[2 * c], y = t->q[2 * c + 1];
    if (x < x0) x0 = x;
    if (x > x1) x1 = x;
    if (y < y0) y0 = y;
    if (y > y1) y1 = y;
  }
  if (!(x0 > -4.0 * MAXDIM && x1 < 4.0 * MAXDIM && y0 > -4.0 * MAXDIM && y1 < 4.0 * MAXDIM)) return box_make(0, 0, 0, 0);
  return box_make((i32)floor(x0 + 1e-9), (i32)floor(y0 + 1e-9), (i32)ceil(x1 - 1e-9), (i32)ceil(y1 - 1e-9));
}
int xform_valid(const XForm *t)
{
  int c, pos = 0, neg = 0;
  double h[9];
  if (box_empty(t->src)) return 0;
  for (c = 0; c < 4; c++) {
    const double *a = t->q + 2 * c, *b = t->q + 2 * ((c + 1) % 4), *e = t->q + 2 * ((c + 2) % 4);
    double cr = (b[0] - a[0]) * (e[1] - b[1]) - (b[1] - a[1]) * (e[0] - b[0]);
    if (!(cr == cr)) return 0;                              /* NaN */
    if (cr > 1e-9) pos++; else if (cr < -1e-9) neg++; else return 0;
  }
  return (pos == 4 || neg == 4) && xform_inverse(t, h);
}

/* ------------------------------------------------------------------ building */
void xform_init(XForm *t, Box src) { t->src = src; box_quad(src, t->q); }
void xform_compose(XForm *t, const double *h)
{
  int c;
  for (c = 0; c < 4; c++) xform_point(h, t->q[2 * c], t->q[2 * c + 1], &t->q[2 * c], &t->q[2 * c + 1]);
}
void xform_translate(XForm *t, double dx, double dy)
{
  int c;
  for (c = 0; c < 4; c++) { t->q[2 * c] += dx; t->q[2 * c + 1] += dy; }
}
void xform_scale_about(XForm *t, double sx, double sy, double cx, double cy)
{
  int c;
  for (c = 0; c < 4; c++) { t->q[2 * c] = cx + (t->q[2 * c] - cx) * sx; t->q[2 * c + 1] = cy + (t->q[2 * c + 1] - cy) * sy; }
}
void xform_rotate_about(XForm *t, double angle, double cx, double cy)
{
  double a = angle * M_PI / 180, co = cos(a), si = sin(a);
  int c;
  for (c = 0; c < 4; c++) {
    double x = t->q[2 * c] - cx, y = t->q[2 * c + 1] - cy;
    t->q[2 * c] = cx + co * x - si * y; t->q[2 * c + 1] = cy + si * x + co * y;
  }
}
void xform_set_affine(XForm *t, Box src, double rx, double ry, double sx, double sy, double angle, double skx, double sky, double dx, double dy)
{
  double a = angle * M_PI / 180, co = cos(a), si = sin(a), tx = tan(skx * M_PI / 180), ty = tan(sky * M_PI / 180);
  int c;
  xform_init(t, src);
  for (c = 0; c < 4; c++) {
    double x = (t->q[2 * c] - rx) * sx, y = (t->q[2 * c + 1] - ry) * sy, kx = x + tx * y, ky = y + ty * x;
    t->q[2 * c] = rx + dx + co * kx - si * ky;
    t->q[2 * c + 1] = ry + dy + si * kx + co * ky;
  }
}
void xform_distort(XForm *t, int c, double x, double y)
{
  if (c < 0 || c > 3) return;
  t->q[2 * c] = x; t->q[2 * c + 1] = y;
}
void xform_perspective(XForm *t, int c, double x, double y)
{
  double dx, dy;
  int o;
  if (c < 0 || c > 3) return;
  dx = x - t->q[2 * c]; dy = y - t->q[2 * c + 1];
  if (fabs(dx) >= fabs(dy)) {
    /* along the top or bottom edge: the other corner of that edge the other way */
    o = c == XC_TL ? XC_TR : c == XC_TR ? XC_TL : c == XC_BR ? XC_BL : XC_BR;
    t->q[2 * c] += dx; t->q[2 * o] -= dx;
  } else {
    o = c == XC_TL ? XC_BL : c == XC_BL ? XC_TL : c == XC_TR ? XC_BR : XC_TR;
    t->q[2 * c + 1] += dy; t->q[2 * o + 1] -= dy;
  }
}
void xform_skew(XForm *t, int e, double dx, double dy)
{
  int a, b;
  double ex, ey, len, s;
  if (e < 0 || e > 3) return;
  a = e; b = (e + 1) % 4;
  ex = t->q[2 * b] - t->q[2 * a]; ey = t->q[2 * b + 1] - t->q[2 * a + 1];
  len = ex * ex + ey * ey;
  if (len < 1e-12) return;
  s = (dx * ex + dy * ey) / len;                           /* the move's part along the edge */
  t->q[2 * a] += s * ex; t->q[2 * a + 1] += s * ey;
  t->q[2 * b] += s * ex; t->q[2 * b + 1] += s * ey;
}
Box xform_source_box(Doc *d, int layer)
{
  Box b;
  if (d->sel) { if (sel_bounds(d, &b)) return b; return box_make(0, 0, 0, 0); }
  if (layer >= 0 && layer < d->nlay && d->lay[layer]->px && layer_content_box(d, layer, &b)) return b;
  return box_make(0, 0, 0, 0);
}

/* ------------------------------------------------------------------ resampling */
/* a map from destination to source that moves whole pixels only (mirrors, quarter turns, whole
   steps): then nearest is exact */
static int whole_pixels(const double *inv)
{
  int k;
  double sx, sy;
  if (fabs(inv[6]) > 1e-12 || fabs(inv[7]) > 1e-12 || fabs(inv[8] - 1) > 1e-12) return 0;
  for (k = 0; k < 6; k++) {
    if (k == 2 || k == 5) continue;
    if (fabs(inv[k]) > 1e-9 && fabs(fabs(inv[k]) - 1) > 1e-9) return 0;
  }
  if (fabs(fabs(inv[0] * inv[4] - inv[1] * inv[3]) - 1) > 1e-9) return 0;
  xform_point(inv, 0.5, 0.5, &sx, &sy);
  return fabs(sx - 0.5 - floor(sx - 0.5 + 0.5)) < 1e-6 && fabs(sy - 0.5 - floor(sy - 0.5 + 0.5)) < 1e-6;
}
/* how many samples a side a destination pixel needs: the most source pixels one spans, 1-4 */
static int supersample(const XForm *t, const double *inv)
{
  double cx = 0, cy = 0, m = 0;
  int c, k;
  for (c = 0; c < 4; c++) { cx += t->q[2 * c] / 4; cy += t->q[2 * c + 1] / 4; }
  for (k = 0; k < 5; k++) {
    double x = k < 4 ? cx + (t->q[2 * k] - cx) * 0.9 : cx, y = k < 4 ? cy + (t->q[2 * k + 1] - cy) * 0.9 : cy;
    double x0, y0, x1, y1, x2, y2, a, b;
    xform_point(inv, x, y, &x0, &y0);
    xform_point(inv, x + 1, y, &x1, &y1);
    xform_point(inv, x, y + 1, &x2, &y2);
    a = sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    b = sqrt((x2 - x0) * (x2 - x0) + (y2 - y0) * (y2 - y0));
    if (a > m) m = a;
    if (b > m) m = b;
  }
  if (!(m < 1.5)) { int n = (int)ceil(m - 0.25); return n > 4 ? 4 : n < 1 ? 1 : n; }
  return 1;
}
#define PAD 2
/* a floating buffer: fw x fh RGBA (bpp 4) or grey (bpp 1), whose pixel (PAD, PAD) is canvas (ox, oy) */
typedef struct { u8 *px; i32 fw, fh, ox, oy; int bpp; } Float;

/* one RGBA sample from the float at canvas position sx, sy */
static void float_sample(const Float *f, double sx, double sy, int interp, u8 *out)
{
  double x = sx - f->ox + PAD, y = sy - f->oy + PAD;
  if (!(x > 0 && y > 0 && x < f->fw && y < f->fh)) { out[0] = out[1] = out[2] = out[3] = 0; return; }
  if (interp == RS_NEAREST) { memcpy(out, f->px + ((long)floor(y) * f->fw + (long)floor(x)) * 4, 4); return; }
  if (interp == RS_BICUBIC) sample_bicubic(f->px, f->fw, f->fh, x, y, out);
  else sample_bilinear(f->px, f->fw, f->fh, x, y, out);
}
static double float_sample1(const Float *f, double sx, double sy, int interp)
{
  double x = sx - f->ox + PAD - 0.5, y = sy - f->oy + PAD - 0.5, fx, fy;
  i32 x0, y0;
  const u8 *p;
  if (!(x >= 0 && y >= 0 && x < f->fw - 1 && y < f->fh - 1)) return 0;
  x0 = (i32)x; y0 = (i32)y;
  if (interp == RS_NEAREST) return f->px[(long)floor(y + 0.5) * f->fw + (long)floor(x + 0.5)];
  fx = x - x0; fy = y - y0;
  p = f->px + (long)y0 * f->fw + x0;
  return (p[0] * (1 - fx) + p[1] * fx) * (1 - fy) + (p[f->fw] * (1 - fx) + p[f->fw + 1] * fx) * fy;
}
/* the pixel at destination X, Y (its centre X + .5) sampled through inv with n x n samples */
static void sample_px(const Float *f, const double *inv, i32 X, i32 Y, int n, int interp, u8 *out)
{
  double acc[4] = { 0, 0, 0, 0 };
  int i, j, k;
  if (n == 1) {
    double sx, sy;
    xform_point(inv, X + 0.5, Y + 0.5, &sx, &sy);
    if (f->bpp == 1) { out[0] = (u8)(float_sample1(f, sx, sy, interp) + 0.5); return; }
    float_sample(f, sx, sy, interp, out);
    return;
  }
  for (j = 0; j < n; j++)
    for (i = 0; i < n; i++) {
      double sx, sy;
      u8 s[4];
      xform_point(inv, X + (i + 0.5) / n, Y + (j + 0.5) / n, &sx, &sy);
      if (f->bpp == 1) { acc[0] += float_sample1(f, sx, sy, interp); continue; }
      float_sample(f, sx, sy, interp, s);
      for (k = 0; k < 3; k++) acc[k] += (double)s[k] * s[3];
      acc[3] += s[3];
    }
  if (f->bpp == 1) { out[0] = (u8)(acc[0] / (n * n) + 0.5); return; }
  if (acc[3] <= 0) { out[0] = out[1] = out[2] = out[3] = 0; return; }
  for (k = 0; k < 3; k++) out[k] = (u8)(acc[k] / acc[3] + 0.5);
  out[3] = (u8)(acc[3] / (n * n) + 0.5);
}
/* a float for box b of plane src (bpp 4 or 1, canvas size) times mask m (or NULL: all of it); the
   part taken is cut out of src when cut */
static int float_take(Doc *d, Float *f, u8 *src, int bpp, Box b, const u8 *m, int cut)
{
  i32 x, y;
  f->fw = b.x1 - b.x0 + 2 * PAD; f->fh = b.y1 - b.y0 + 2 * PAD; f->ox = b.x0; f->oy = b.y0; f->bpp = bpp;
  if (!(f->px = (u8 *)calloc((size_t)f->fw * f->fh, (size_t)bpp))) return 0;
  for (y = b.y0; y < b.y1; y++)
    for (x = b.x0; x < b.x1; x++) {
      long k = (long)y * d->w + x;
      int s = m ? m[k] : 255;
      u8 *o = f->px + ((long)(y - b.y0 + PAD) * f->fw + x - b.x0 + PAD) * bpp, *p = src + k * bpp;
      if (bpp == 1) {
        o[0] = (u8)mul255(p[0], s);
        if (cut) p[0] = (u8)mul255(p[0], 255 - s);
        continue;
      }
      o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; o[3] = (u8)mul255(p[3], s);
      if (!o[3]) o[0] = o[1] = o[2] = 0;
      if (cut) { p[3] = (u8)mul255(p[3], 255 - s); if (!p[3]) p[0] = p[1] = p[2] = 0; }
    }
  return 1;
}
/* the float put through inv into the destination box db of plane dst: blended over (RGBA) or
   added (grey) */
static void float_drop(Doc *d, const Float *f, const double *inv, Box db, u8 *dst, int n, int interp)
{
  i32 x, y;
  for (y = db.y0; y < db.y1 && !progress(y - db.y0, db.y1 - db.y0); y++)   /* (0.8: progress and Abort) */
    for (x = db.x0; x < db.x1; x++) {
      u8 s[4], *p;
      sample_px(f, inv, x, y, n, interp, s);
      if (f->bpp == 1) {
        p = dst + (long)y * d->w + x;
        if (s[0]) *p = (u8)(*p + s[0] - mul255(*p, s[0]));
      } else if (s[3]) {
        p = dst + ((long)y * d->w + x) * 4;
        blend_pixel(p, s, 255, BM_NORMAL);
      }
    }
}
/* the selection's box worked out again; an empty mask is no selection */
static void sel_rebox(Doc *d)
{
  i32 x, y;
  Box b = box_make(d->w, d->h, 0, 0);
  if (!d->sel) { d->selbox = doc_box(d); return; }
  for (y = 0; y < d->h; y++)
    for (x = 0; x < d->w; x++)
      if (d->sel[(long)y * d->w + x]) {
        if (x < b.x0) b.x0 = x;
        if (x >= b.x1) b.x1 = x + 1;
        if (y < b.y0) b.y0 = y;
        b.y1 = y + 1;
      }
  if (box_empty(b)) { free(d->sel); d->sel = NULL; d->selbox = doc_box(d); return; }
  d->selbox = b;
}
static int can_change(const Doc *d, int li)
{
  const Layer *l;
  if (li < 0 || li >= d->nlay) return 0;
  l = d->lay[li];
  return l->px && l->kind == LK_PIXEL && !l->locked && !l->lock_all && !l->lock_pos;
}
/* the transform kept, about its box's centre, for Transform Again */
static void remember(Doc *d, const XForm *t)
{
  double h[9], a[9], b[9], cx = (t->src.x0 + t->src.x1) / 2.0, cy = (t->src.y0 + t->src.y1) / 2.0;
  if (!xform_matrix(t, h)) return;
  a[0] = 1; a[1] = 0; a[2] = -cx; a[3] = 0; a[4] = 1; a[5] = -cy; a[6] = 0; a[7] = 0; a[8] = 1;
  b[0] = 1; b[1] = 0; b[2] = cx; b[3] = 0; b[4] = 1; b[5] = cy; b[6] = 0; b[7] = 0; b[8] = 1;
  hom_mul(a, h, h);
  hom_mul(h, b, d->xf_again);
  d->xf_again_ok = 1;
}
static Box xform_named(Doc *d, int li, const XForm *t, int interp, const char *name)
{
  Layer *l;
  double inv[9];
  Box sb, db, all;
  Float f;
  int n;
  if (!can_change(d, li) || !xform_valid(t)) return box_make(0, 0, 0, 0);
  l = d->lay[li];
  sb = box_clip(t->src, doc_box(d));
  db = box_clip(xform_bounds(t), doc_box(d));
  if (box_empty(sb) || !xform_inverse(t, inv)) return box_make(0, 0, 0, 0);
  if (whole_pixels(inv)) { interp = RS_NEAREST; n = 1; } else n = supersample(t, inv);
  all = box_union(sb, db);
  doc_begin(d, name);
  doc_note_layer(d, li, all);
  if (d->sel) doc_note_sel(d);
  if (!float_take(d, &f, l->px, 4, sb, d->sel, 1)) { doc_end(d); return box_make(0, 0, 0, 0); }
  float_drop(d, &f, inv, db, l->px, n, interp);
  free(f.px);
  if (d->sel) {
    /* the selection goes with the pixels */
    if (float_take(d, &f, d->sel, 1, sb, NULL, 1)) {
      float_drop(d, &f, inv, db, d->sel, n, interp == RS_NEAREST ? RS_NEAREST : RS_BILINEAR);
      free(f.px);
    }
    sel_rebox(d);
  }
  doc_end(d);
  remember(d, t);
  return all;
}
Box xform_layer(Doc *d, int li, const XForm *t, int interp) { return xform_named(d, li, t, interp, "Free Transform"); }
int xform_selection(Doc *d, const XForm *t)
{
  double inv[9];
  Box sb, db;
  Float f;
  u8 *m;
  int n;
  if (!d->sel || !xform_valid(t) || !xform_inverse(t, inv)) return 0;
  sb = box_clip(t->src, doc_box(d));
  db = box_clip(xform_bounds(t), doc_box(d));
  if (box_empty(sb) || !(m = (u8 *)malloc((size_t)d->w * d->h))) return 0;
  memcpy(m, d->sel, (size_t)d->w * d->h);
  n = whole_pixels(inv) ? 1 : supersample(t, inv);
  if (!float_take(d, &f, m, 1, sb, NULL, 1)) { free(m); return 0; }
  float_drop(d, &f, inv, db, m, n, whole_pixels(inv) ? RS_NEAREST : RS_BILINEAR);
  free(f.px);
  sel_apply_mask(d, m, SEL_REPLACE, "Transform Selection");
  return 1;
}
int xform_again_get(Doc *d, Box src, XForm *t)
{
  double a[9], b[9], h[9], cx = (src.x0 + src.x1) / 2.0, cy = (src.y0 + src.y1) / 2.0;
  if (!d->xf_again_ok || box_empty(src)) return 0;
  a[0] = 1; a[1] = 0; a[2] = cx; a[3] = 0; a[4] = 1; a[5] = cy; a[6] = 0; a[7] = 0; a[8] = 1;
  b[0] = 1; b[1] = 0; b[2] = -cx; b[3] = 0; b[4] = 1; b[5] = -cy; b[6] = 0; b[7] = 0; b[8] = 1;
  hom_mul(a, d->xf_again, h);
  hom_mul(h, b, h);
  xform_init(t, src);
  xform_compose(t, h);
  return 1;
}
Box xform_again(Doc *d, int li, int interp)
{
  XForm t;
  if (!xform_again_get(d, xform_source_box(d, li), &t)) return box_make(0, 0, 0, 0);
  return xform_named(d, li, &t, interp, "Transform Again");
}
/* the quad of box b flipped, or turned q quarter turns clockwise about its centre (kept on whole pixels) */
static void exact_quad(Box b, int flip, int vertical, int q, XForm *t)
{
  i32 w = b.x1 - b.x0, h = b.y1 - b.y0;
  xform_init(t, b);
  if (flip) {
    double tmp[8];
    memcpy(tmp, t->q, sizeof tmp);
    /* corners swapped: horizontally TL<->TR, BL<->BR; vertically TL<->BL, TR<->BR */
    if (vertical) { t->q[0] = tmp[6]; t->q[1] = tmp[7]; t->q[2] = tmp[4]; t->q[3] = tmp[5]; t->q[4] = tmp[2]; t->q[5] = tmp[3]; t->q[6] = tmp[0]; t->q[7] = tmp[1]; }
    else { t->q[0] = tmp[2]; t->q[1] = tmp[3]; t->q[2] = tmp[0]; t->q[3] = tmp[1]; t->q[4] = tmp[6]; t->q[5] = tmp[7]; t->q[6] = tmp[4]; t->q[7] = tmp[5]; }
    return;
  }
  q = ((q % 4) + 4) % 4;
  if (q) {
    i32 nw = q == 2 ? w : h, nh = q == 2 ? h : w;
    i32 nx = b.x0 + (w - nw) / 2, ny = b.y0 + (h - nh) / 2;
    double nq[8];
    int c;
    Box nb = box_make(nx, ny, nx + nw, ny + nh);
    box_quad(nb, nq);
    /* source corner c lands on new corner c + q (clockwise) */
    for (c = 0; c < 4; c++) { t->q[2 * c] = nq[2 * ((c + q) % 4)]; t->q[2 * c + 1] = nq[2 * ((c + q) % 4) + 1]; }
  }
}
Box xform_flip(Doc *d, int li, int vertical)
{
  XForm t;
  Box b = d->sel ? d->selbox : doc_box(d);
  if (box_empty(b)) return b;
  exact_quad(b, 1, vertical, 0, &t);
  return xform_named(d, li, &t, RS_NEAREST, vertical ? "Flip Vertical" : "Flip Horizontal");
}
Box xform_rotate90(Doc *d, int li, int q)
{
  static const char *const names[] = { "", "Rotate 90 Clockwise", "Rotate 180", "Rotate 90 Counter Clockwise" };
  XForm t;
  Box b = d->sel ? d->selbox : doc_box(d);
  q = ((q % 4) + 4) % 4;
  if (box_empty(b) || !q) return box_make(0, 0, 0, 0);
  exact_quad(b, 0, 0, q, &t);
  return xform_named(d, li, &t, RS_NEAREST, names[q]);
}
int sel_flip(Doc *d, int vertical)
{
  XForm t;
  if (!d->sel) return 0;
  exact_quad(d->selbox, 1, vertical, 0, &t);
  return xform_selection(d, &t);
}
int sel_rotate90(Doc *d, int q)
{
  XForm t;
  if (!d->sel || !(((q % 4) + 4) % 4)) return 0;
  exact_quad(d->selbox, 0, 0, q, &t);
  return xform_selection(d, &t);
}

/* ------------------------------------------------------------------ the Move tool's Alt-drag */
Box move_selection_copy(Doc *d, i32 dx, i32 dy)
{
  Layer *l;
  Float f;
  Box sb, db, all;
  double inv[9];
  if (d->active < 0 || d->active >= d->nlay) return box_make(0, 0, 0, 0);
  l = d->lay[d->active];
  if (!d->sel) {
    /* the layer duplicated and the copy moved, as one step */
    doc_begin(d, "Duplicate");
    if (!doc_duplicate_layer(d, d->active)) { doc_end(d); return box_make(0, 0, 0, 0); }
    move_layer(d, dx, dy);                                 /* (the copy is the active layer now) */
    doc_end(d);
    return doc_box(d);
  }
  if (!l->px || l->kind != LK_PIXEL || l->locked || l->lock_all || (!dx && !dy)) return box_make(0, 0, 0, 0);
  sb = d->selbox;
  db = box_clip(box_make(sb.x0 + dx, sb.y0 + dy, sb.x1 + dx, sb.y1 + dy), doc_box(d));
  all = box_union(sb, db);
  inv[0] = 1; inv[1] = 0; inv[2] = -dx; inv[3] = 0; inv[4] = 1; inv[5] = -dy; inv[6] = 0; inv[7] = 0; inv[8] = 1;
  doc_begin(d, "Move");
  doc_note_layer(d, d->active, all);
  doc_note_sel(d);
  if (float_take(d, &f, l->px, 4, sb, d->sel, 0)) { float_drop(d, &f, inv, db, l->px, 1, RS_NEAREST); free(f.px); }
  /* the selection moves to the copy */
  if (float_take(d, &f, d->sel, 1, sb, NULL, 1)) { float_drop(d, &f, inv, db, d->sel, 1, RS_NEAREST); free(f.px); }
  sel_rebox(d);
  doc_end(d);
  return all;
}
