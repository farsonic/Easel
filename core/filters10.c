/*
 * filters10.c - Easel 1.0's filters (see filters10.h), in the manner of adjust.c and filters2.c: new
 * pixels for the active layer in the selection's box are worked out from a copy of the layer as it
 * was; a partial selection blends them with the old in proportion. Blurs and resampling work on
 * premultiplied colour, so transparent pixels don't darken what is next to them.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "filters10.h"
#include "adjust.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int clamp255(double v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5); }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static double lumd(const u8 *p) { return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]; }

/* ------------------------------------------------------------------ the frame */
typedef struct { Doc *d; Layer *l; Box b; u8 *orig; } Op;
static int op_begin(Op *o, Doc *d, const char *name)
{
  Layer *l;
  o->d = d; o->orig = NULL;
  if (d->active < 0 || d->active >= d->nlay) return 0;
  l = d->lay[d->active];
  if (!l->px || l->locked || l->lock_all || !l->visible) return 0;
  o->l = l;
  o->b = d->sel ? box_clip(d->selbox, doc_box(d)) : doc_box(d);
  if (box_empty(o->b)) return 0;
  if (!(o->orig = (u8 *)malloc((size_t)d->w * d->h * 4))) return 0;
  memcpy(o->orig, l->px, (size_t)d->w * d->h * 4);
  doc_begin(d, name);
  doc_note_layer(d, d->active, o->b);
  return 1;
}
static int op_end(Op *o)
{
  Doc *d = o->d;
  i32 x, y;
  if (d->sel || o->l->alpha_lock)
    for (y = o->b.y0; y < o->b.y1; y++)
      for (x = o->b.x0; x < o->b.x1; x++) {
        long k = (long)y * d->w + x;
        int s = d->sel ? d->sel[k] : 255, c;
        u8 *p = o->l->px + k * 4;
        const u8 *q = o->orig + k * 4;
        if (o->l->alpha_lock) p[3] = q[3];
        if (s == 255) continue;
        for (c = 0; c < 4; c++) p[c] = (u8)(q[c] + ((p[c] - q[c]) * s + (p[c] >= q[c] ? 127 : -127)) / 255);
      }
  free(o->orig);
  doc_end(d);
  return 1;
}
#define ORIG(o, x, y) ((o)->orig + ((long)(y) * (o)->d->w + (x)) * 4)
#define DEST(o, x, y) ((o)->l->px + ((long)(y) * (o)->d->w + (x)) * 4)
static const u8 *orig_at(const Op *o, i32 x, i32 y)
{
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= o->d->w) x = o->d->w - 1;
  if (y >= o->d->h) y = o->d->h - 1;
  return ORIG(o, x, y);
}
/* the edge rules for sampling outside box c */
enum { EDGE_CLAMP, EDGE_WRAP, EDGE_CLEAR };
static int edge_coord(i32 *v, i32 lo, i32 hi, int mode)
{
  i32 n = hi - lo;
  if (*v >= lo && *v < hi) return 1;
  if (mode == EDGE_WRAP) { *v = lo + ((*v - lo) % n + n) % n; return 1; }
  if (mode == EDGE_CLEAR) return 0;
  *v = *v < lo ? lo : hi - 1;
  return 1;
}
/* a bilinear sample of the original (premultiplied) at sx, sy (pixel centres at whole numbers) */
static void sample(const Op *o, Box c, int mode, double sx, double sy, u8 *out)
{
  i32 ix = (i32)floor(sx), iy = (i32)floor(sy);
  double fx = sx - ix, fy = sy - iy, acc[4] = { 0, 0, 0, 0 };
  int k, ch;
  for (k = 0; k < 4; k++) {
    i32 xx = ix + (k & 1), yy = iy + (k >> 1);
    double w = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
    const u8 *q;
    if (w <= 0) continue;
    if (!edge_coord(&xx, c.x0, c.x1, mode) || !edge_coord(&yy, c.y0, c.y1, mode)) continue;
    q = ORIG(o, xx, yy);
    for (ch = 0; ch < 3; ch++) acc[ch] += q[ch] * q[3] * w;
    acc[3] += q[3] * w;
  }
  out[3] = (u8)clamp255(acc[3]);
  for (ch = 0; ch < 3; ch++) out[ch] = acc[3] > 0 ? (u8)clamp255(acc[ch] / acc[3]) : 0;
}

/* ------------------------------------------------------------------ a Gaussian blur into a buffer */
/* box r of src (canvas w x h) blurred into dst (canvas sized) by three box passes nearest sigma,
   premultiplied with eight extra bits; reads past r by the radii */
static int gblur(const u8 *src, u8 *dst, i32 w, i32 h, Box r, double sigma)
{
  int rad[3], margin, p, c, wl, wu, m;
  double wi;
  Box e;
  i32 ew, eh, x, y;
  u16 *buf, *line;
  if (sigma < 0.3) {
    for (y = r.y0; y < r.y1; y++) memcpy(dst + ((long)y * w + r.x0) * 4, src + ((long)y * w + r.x0) * 4, (size_t)(r.x1 - r.x0) * 4);
    return 1;
  }
  wi = sqrt(12 * sigma * sigma / 3 + 1);
  wl = (int)floor(wi);
  if (wl % 2 == 0) wl--;
  wu = wl + 2;
  m = (int)floor((12 * sigma * sigma - 3 * wl * wl - 12 * wl - 9) / (-4.0 * wl - 4) + 0.5);
  for (p = 0; p < 3; p++) rad[p] = ((p < m ? wl : wu) - 1) / 2;
  margin = rad[0] + rad[1] + rad[2];
  e = box_clip(box_make(r.x0 - margin, r.y0 - margin, r.x1 + margin, r.y1 + margin), box_make(0, 0, w, h));
  ew = e.x1 - e.x0; eh = e.y1 - e.y0;
  buf = (u16 *)malloc((size_t)ew * eh * 4 * sizeof(u16));
  line = (u16 *)malloc((size_t)(ew > eh ? ew : eh) * 4 * sizeof(u16));
  if (!buf || !line) { free(buf); free(line); return 0; }
  for (y = 0; y < eh; y++)
    for (x = 0; x < ew; x++) {
      const u8 *s = src + ((long)(y + e.y0) * w + x + e.x0) * 4;
      u16 *q = buf + ((long)y * ew + x) * 4;
      q[3] = (u16)(s[3] << 8);
      for (c = 0; c < 3; c++) q[c] = (u16)(s[c] * s[3]);
    }
  for (p = 0; p < 3; p++) {
    int rr = rad[p];
    long div = 2 * rr + 1;
    if (rr < 1) continue;
    for (y = 0; y < eh; y++) {          /* 1.0: progress */
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
  return 1;
}
static u8 *canvas_buf(Doc *d) { return (u8 *)malloc((size_t)d->w * d->h * 4); }

/* ------------------------------------------------------------------ blur */
int flt_average(Doc *d)
{
  Op o;
  double acc[4] = { 0, 0, 0, 0 }, ws = 0;
  i32 x, y;
  u8 avg[4];
  int c;
  if (!op_begin(&o, d, "Average")) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      const u8 *q = ORIG(&o, x, y);
      double s = d->sel ? d->sel[(long)y * d->w + x] / 255.0 : 1;
      for (c = 0; c < 3; c++) acc[c] += q[c] * q[3] * s;
      acc[3] += q[3] * s; ws += s;
    }
  avg[3] = (u8)clamp255(ws > 0 ? acc[3] / ws : 0);
  for (c = 0; c < 3; c++) avg[c] = (u8)(acc[3] > 0 ? clamp255(acc[c] / acc[3]) : 0);
  for (y = o.b.y0; y < o.b.y1; y++) for (x = o.b.x0; x < o.b.x1; x++) memcpy(DEST(&o, x, y), avg, 4);
  return op_end(&o);
}
int flt_blur_more(Doc *d)
{
  Op o;
  if (!op_begin(&o, d, "Blur More")) return 0;
  gblur(o.orig, o.l->px, d->w, d->h, o.b, 1.4);
  return op_end(&o);
}
int flt_radial_blur_ex(Doc *d, int amount, int zoom, i32 cx, i32 cy, int quality)
{
  Op o;
  i32 x, y;
  int n = quality <= 0 ? 8 : quality == 1 ? 16 : 32;
  double a = clampi(amount, 1, 100);
  if (!op_begin(&o, d, zoom ? "Radial Blur (Zoom)" : "Radial Blur")) return 0;
  /* more samples for a long blur: about one every two pixels at the far corner */
  if (quality >= 1) {
    double far = sqrt((double)d->w * d->w + (double)d->h * d->h), len = zoom ? far * a / 200.0 : far * a * M_PI / 360.0;
    int want = (int)(len / (quality == 1 ? 3 : 1.5));
    if (want > n) n = want > 128 ? 128 : want;
  }
  for (y = o.b.y0; y < o.b.y1; y++) {          /* 1.0: progress */
    for (x = o.b.x0; x < o.b.x1; x++) {
      double dx = x + 0.5 - cx, dy = y + 0.5 - cy, acc[4] = { 0, 0, 0, 0 };
      int i, c;
      u8 *p = DEST(&o, x, y);
      for (i = 0; i < n; i++) {
        double t = n > 1 ? i / (double)(n - 1) - 0.5 : 0, sx, sy;
        const u8 *q;
        if (zoom) { double f = 1 + t * a / 100.0; sx = cx + dx * f; sy = cy + dy * f; }
        else { double an = t * a * M_PI / 180, cs = cos(an), sn = sin(an); sx = cx + dx * cs - dy * sn; sy = cy + dx * sn + dy * cs; }
        q = orig_at(&o, (i32)floor(sx), (i32)floor(sy));
        for (c = 0; c < 3; c++) acc[c] += q[c] * q[3];
        acc[3] += q[3];
      }
      p[3] = (u8)clamp255(acc[3] / n);
      for (c = 0; c < 3; c++) p[c] = acc[3] > 0 ? (u8)clamp255(acc[c] / acc[3]) : 0;
    }
  }
  return op_end(&o);
}
/* a blur that varies by place: the original and three blurs (radius/3, 2/3, all) mixed by a weight
   0-1 for each pixel */
typedef double (*WeightFn)(const double *a, double x, double y);
static int varblur(Doc *d, const char *name, int radius, WeightFn wf, const double *a)
{
  Op o;
  u8 *lv[3];
  i32 x, y;
  int k, c;
  if (radius < 1) return 0;
  if (!op_begin(&o, d, name)) return 0;
  for (k = 0; k < 3; k++) lv[k] = canvas_buf(d);
  if (!lv[0] || !lv[1] || !lv[2]) { for (k = 0; k < 3; k++) free(lv[k]); op_end(&o); return 0; }
  for (k = 0; k < 3; k++) gblur(o.orig, lv[k], d->w, d->h, o.b, radius * (k + 1) / 3.0);
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      double w = wf(a, x + 0.5, y + 0.5), f;
      long off = ((long)y * d->w + x) * 4;
      const u8 *lo, *hi;
      u8 *p = o.l->px + off;
      if (w <= 0) continue;
      if (w > 1) w = 1;
      /* between the two levels either side of w * 3 */
      f = w * 3;
      k = (int)floor(f);
      if (k >= 3) { k = 2; f = 3; }
      lo = k == 0 ? o.orig + off : lv[k - 1] + off; hi = lv[k] + off;
      f -= k;
      for (c = 0; c < 4; c++) p[c] = (u8)clamp255(lo[c] + (hi[c] - lo[c]) * f);
    }
  for (k = 0; k < 3; k++) free(lv[k]);
  return op_end(&o);
}
static double w_tilt(const double *a, double x, double y)
{
  /* the distance across the band (a: a point on its middle line, the normal, half the focus, the transition) */
  double dist = fabs((x - a[0]) * a[2] + (y - a[1]) * a[3]) - a[4];
  if (dist <= 0) return 0;
  return a[5] <= 0 ? 1 : dist / a[5];
}
int flt_tilt_shift(Doc *d, int radius, i32 cy, int angle, int focus, int transition)
{
  double a[6];
  a[0] = d->w / 2.0; a[1] = cy; a[2] = sin(angle * M_PI / 180); a[3] = cos(angle * M_PI / 180); a[4] = focus / 2.0; a[5] = transition;
  return varblur(d, "Tilt-Shift", radius, w_tilt, a);
}
static double w_iris(const double *a, double x, double y)
{
  double nx = (x - a[0]) / a[2], ny = (y - a[1]) / a[3], r = sqrt(nx * nx + ny * ny);
  if (r <= 1) return 0;
  return a[4] <= 0 ? 1 : (r - 1) / a[4];
}
int flt_iris_blur(Doc *d, int radius, i32 cx, i32 cy, i32 rx, i32 ry, int transition)
{
  double a[5];
  a[0] = cx; a[1] = cy; a[2] = rx < 1 ? 1 : rx; a[3] = ry < 1 ? 1 : ry; a[4] = transition / 100.0;
  return varblur(d, "Iris Blur", radius, w_iris, a);
}

/* ------------------------------------------------------------------ sharpen */
/* a disc of radius r averaged (premultiplied) into dst over box b: row by row, each row's prefix sums */
static int disc_blur(const u8 *src, u8 *dst, i32 w, i32 h, Box b, int r)
{
  double *pre, *acc;
  i32 x, y, bw = b.x1 - b.x0;
  int dy, c;
  pre = (double *)malloc((size_t)(w + 1) * 4 * sizeof(double));
  acc = (double *)malloc((size_t)bw * 5 * sizeof(double));
  if (!pre || !acc) { free(pre); free(acc); return 0; }
  for (y = b.y0; y < b.y1; y++) {          /* 1.0: progress */
    memset(acc, 0, (size_t)bw * 5 * sizeof(double));
    for (dy = -r; dy <= r; dy++) {
      i32 yy = y + dy < 0 ? 0 : y + dy >= h ? h - 1 : y + dy;
      int hw = (int)floor(sqrt((double)r * r - (double)dy * dy) + 1e-6);
      const u8 *row = src + (long)yy * w * 4;
      for (c = 0; c < 4; c++) pre[c] = 0;
      for (x = 0; x < w; x++) {
        const u8 *q = row + x * 4;
        for (c = 0; c < 3; c++) pre[(x + 1) * 4 + c] = pre[x * 4 + c] + q[c] * q[3];
        pre[(x + 1) * 4 + 3] = pre[x * 4 + 3] + q[3];
      }
      for (x = b.x0; x < b.x1; x++) {
        i32 x0 = x - hw, x1 = x + hw + 1, clampL = 0, clampR = 0;
        double *ac = acc + (long)(x - b.x0) * 5;
        /* past the edges the edge pixels repeat */
        if (x0 < 0) { clampL = -x0; x0 = 0; }
        if (x1 > w) { clampR = x1 - w; x1 = w; }
        for (c = 0; c < 4; c++) ac[c] += pre[x1 * 4 + c] - pre[x0 * 4 + c];
        if (clampL) { const u8 *q = row; for (c = 0; c < 3; c++) ac[c] += (double)clampL * q[c] * q[3]; ac[3] += (double)clampL * q[3]; }
        if (clampR) { const u8 *q = row + (w - 1) * 4; for (c = 0; c < 3; c++) ac[c] += (double)clampR * q[c] * q[3]; ac[3] += (double)clampR * q[3]; }
        ac[4] += 2 * hw + 1;
      }
    }
    for (x = b.x0; x < b.x1; x++) {
      const double *ac = acc + (long)(x - b.x0) * 5;
      u8 *o = dst + ((long)y * w + x) * 4;
      o[3] = (u8)clamp255(ac[3] / ac[4]);
      for (c = 0; c < 3; c++) o[c] = ac[3] > 0 ? (u8)clamp255(ac[c] / ac[3]) : 0;
    }
  }
  free(pre); free(acc);
  return 1;
}
/* a line of length len at angle averaged (premultiplied) into dst over box b */
static void line_blur(const Op *o, u8 *dst, double len, double angle)
{
  Doc *d = o->d;
  int n = (int)len + 1, i, c;
  double ux = cos(angle * M_PI / 180), uy = -sin(angle * M_PI / 180);
  i32 x, y;
  if (n < 2) n = 2;
  for (y = o->b.y0; y < o->b.y1; y++)
    for (x = o->b.x0; x < o->b.x1; x++) {
      double acc[4] = { 0, 0, 0, 0 };
      u8 *p = dst + ((long)y * d->w + x) * 4, s[4];
      for (i = 0; i < n; i++) {
        double t = (i / (double)(n - 1) - 0.5) * len;
        sample(o, doc_box(d), EDGE_CLAMP, x + ux * t, y + uy * t, s);
        for (c = 0; c < 3; c++) acc[c] += s[c] * s[3];
        acc[3] += s[3];
      }
      p[3] = (u8)clamp255(acc[3] / n);
      for (c = 0; c < 3; c++) p[c] = acc[3] > 0 ? (u8)clamp255(acc[c] / acc[3]) : 0;
    }
}
int flt_smart_sharpen(Doc *d, int amount, int radius10, int noise, int remove, int angle)
{
  Op o;
  u8 *bl;
  i32 x, y;
  double r = clampi(radius10, 1, 640) / 10.0, amt = clampi(amount, 1, 500) / 100.0, t = clampi(noise, 0, 100) * 0.25;
  int ok = 1;
  if (!op_begin(&o, d, "Smart Sharpen")) return 0;
  if (!(bl = canvas_buf(d))) { op_end(&o); return 0; }
  if (remove == 1) ok = disc_blur(o.orig, bl, d->w, d->h, o.b, (int)(r + 0.5) < 1 ? 1 : (int)(r + 0.5));
  else if (remove == 2) line_blur(&o, bl, 2 * r, angle);
  else ok = gblur(o.orig, bl, d->w, d->h, o.b, r);
  if (ok)
    for (y = o.b.y0; y < o.b.y1; y++)
      for (x = o.b.x0; x < o.b.x1; x++) {
        long k = ((long)y * d->w + x) * 4;
        int c;
        for (c = 0; c < 3; c++) {
          double diff = o.orig[k + c] - bl[k + c];
          /* noise reduction: small differences let go, the rest lessened by as much */
          if (diff > t) diff -= t; else if (diff < -t) diff += t; else diff = 0;
          o.l->px[k + c] = (u8)clamp255(o.orig[k + c] + diff * amt);
        }
      }
  free(bl);
  return op_end(&o);
}
int flt_sharpen_edges(Doc *d)
{
  Op o;
  i32 x, y;
  if (!op_begin(&o, d, "Sharpen Edges")) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      double l[9], gx, gy, mag, w;
      int i, j, c;
      u8 *p = DEST(&o, x, y);
      const u8 *q = ORIG(&o, x, y);
      for (j = -1; j <= 1; j++) for (i = -1; i <= 1; i++) l[(j + 1) * 3 + i + 1] = lumd(orig_at(&o, x + i, y + j));
      gx = l[2] + 2 * l[5] + l[8] - l[0] - 2 * l[3] - l[6];
      gy = l[6] + 2 * l[7] + l[8] - l[0] - 2 * l[1] - l[2];
      mag = sqrt(gx * gx + gy * gy);
      w = (mag - 40) / 80;
      if (w <= 0) continue;
      if (w > 1) w = 1;
      for (c = 0; c < 3; c++) {
        double s = 8.0 * q[c] - orig_at(&o, x - 1, y)[c] - orig_at(&o, x + 1, y)[c] - orig_at(&o, x, y - 1)[c] - orig_at(&o, x, y + 1)[c];
        p[c] = (u8)clamp255(q[c] + (s / 4 - q[c]) * w);
      }
    }
  return op_end(&o);
}

/* ------------------------------------------------------------------ distort */
int flt_displace(Doc *d, const u8 *map, i32 mw, i32 mh, int hscale, int vscale, int stretch, int wrap)
{
  Op o;
  i32 x, y, bw, bh;
  if (!map || mw < 1 || mh < 1) return 0;
  if (!op_begin(&o, d, "Displace")) return 0;
  bw = o.b.x1 - o.b.x0; bh = o.b.y1 - o.b.y0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      i32 mx = stretch ? (i32)((long)(x - o.b.x0) * mw / bw) : (x - o.b.x0) % mw, my = stretch ? (i32)((long)(y - o.b.y0) * mh / bh) : (y - o.b.y0) % mh;
      const u8 *m = map + ((long)my * mw + mx) * 4;
      double dx = (m[0] - 128) / 128.0 * hscale / 100.0 * 128, dy = (m[1] - 128) / 128.0 * vscale / 100.0 * 128;
      sample(&o, o.b, wrap ? EDGE_WRAP : EDGE_CLAMP, x - dx, y - dy, DEST(&o, x, y));
    }
  return op_end(&o);
}
int flt_spherize_ex(Doc *d, int amount, int mode)
{
  Op o;
  i32 x, y;
  double cx, cy, rx, ry, a = clampi(amount, -100, 100) / 100.0;
  if (mode <= 0 || mode > 2) return flt_spherize(d, amount);
  if (!op_begin(&o, d, "Spherize")) return 0;
  cx = (o.b.x0 + o.b.x1) / 2.0; cy = (o.b.y0 + o.b.y1) / 2.0; rx = (o.b.x1 - o.b.x0) / 2.0; ry = (o.b.y1 - o.b.y0) / 2.0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      double dx = x + 0.5 - cx, dy = y + 0.5 - cy, n = mode == 1 ? dx / rx : dy / ry, r = fabs(n), f;
      if (r >= 1) continue;
      /* as the 2-D spherize, along one axis: a cylinder */
      f = r > 0 ? asin(r) / (M_PI / 2) / r : 1;
      f = 1 + (f - 1) * a;
      if (mode == 1) dx *= f; else dy *= f;
      sample(&o, doc_box(d), EDGE_CLAMP, cx + dx - 0.5, cy + dy - 0.5, DEST(&o, x, y));
    }
  return op_end(&o);
}
static double wave_shape(int type, double t)
{
  double f = t - floor(t);
  if (type == 1) return f < 0.5 ? 4 * f - 1 : 3 - 4 * f;
  if (type == 2) return f < 0.5 ? 1 : -1;
  return sin(2 * M_PI * f);
}
static double rnd01(unsigned *s) { *s = *s * 1103515245u + 12345u; return ((*s >> 8) & 0xFFFFFF) / (double)0x1000000; }
int flt_wave_ex(Doc *d, int gens, int wl_min, int wl_max, int amp_min, int amp_max, int hscale, int vscale, int type, int wrap, unsigned seed)
{
  Op o;
  double wl[20][2], amp[20][2], ph[20][2];
  i32 x, y;
  int g, k;
  gens = clampi(gens, 1, 20);
  if (wl_min < 1) wl_min = 1;
  if (wl_max < wl_min) wl_max = wl_min;
  if (amp_min < 0) amp_min = 0;
  if (amp_max < amp_min) amp_max = amp_min;
  for (g = 0; g < gens; g++)
    for (k = 0; k < 2; k++) {
      wl[g][k] = wl_min + (wl_max - wl_min) * rnd01(&seed);
      amp[g][k] = amp_min + (amp_max - amp_min) * rnd01(&seed);
      ph[g][k] = rnd01(&seed);
    }
  if (!op_begin(&o, d, "Wave")) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      double dx = 0, dy = 0;
      for (g = 0; g < gens; g++) {
        dx += amp[g][0] * wave_shape(type, (y - o.b.y0) / wl[g][0] + ph[g][0]);
        dy += amp[g][1] * wave_shape(type, (x - o.b.x0) / wl[g][1] + ph[g][1]);
      }
      dx = dx / gens * clampi(hscale, 0, 100) / 100.0; dy = dy / gens * clampi(vscale, 0, 100) / 100.0;
      sample(&o, o.b, wrap ? EDGE_WRAP : EDGE_CLAMP, x + dx, y + dy, DEST(&o, x, y));
    }
  return op_end(&o);
}
int flt_lens_correction(Doc *d, int distortion, int ca_rc, int ca_by, int vignette, int midpoint, int scale)
{
  Op o;
  i32 x, y;
  double cx, cy, R, k = -clampi(distortion, -100, 100) / 100.0 * 0.5, sr = 1 + clampi(ca_rc, -100, 100) / 100.0 * 0.01, sb = 1 + clampi(ca_by, -100, 100) / 100.0 * 0.01;
  double vg = clampi(vignette, -100, 100) / 100.0, mid = clampi(midpoint, 0, 100) / 100.0, sc = clampi(scale, 50, 150) / 100.0;
  if (!op_begin(&o, d, "Lens Correction")) return 0;
  cx = (o.b.x0 + o.b.x1) / 2.0; cy = (o.b.y0 + o.b.y1) / 2.0;
  R = sqrt((o.b.x1 - o.b.x0) * (o.b.x1 - o.b.x0) + (o.b.y1 - o.b.y0) * (o.b.y1 - o.b.y0)) / 2.0;
  if (R < 1) R = 1;
  for (y = o.b.y0; y < o.b.y1; y++) {          /* 1.0: progress */
    for (x = o.b.x0; x < o.b.x1; x++) {
      double dx = (x + 0.5 - cx) / sc, dy = (y + 0.5 - cy) / sc, rn = sqrt(dx * dx + dy * dy) / R, f = 1 + k * rn * rn, gain;
      u8 s[4], *p = DEST(&o, x, y);
      int c;
      sample(&o, doc_box(d), EDGE_CLEAR, cx + dx * f - 0.5, cy + dy * f - 0.5, p);
      if (sr != 1) { sample(&o, doc_box(d), EDGE_CLEAR, cx + dx * f * sr - 0.5, cy + dy * f * sr - 0.5, s); p[0] = s[0]; }
      if (sb != 1) { sample(&o, doc_box(d), EDGE_CLEAR, cx + dx * f * sb - 0.5, cy + dy * f * sb - 0.5, s); p[2] = s[2]; }
      if (vg != 0) {
        /* darker (or lighter) towards the corners, starting at the midpoint */
        double t = (rn * sc - mid * 0.7) / (1.0 - mid * 0.7);
        if (t > 0) {
          if (t > 1) t = 1;
          t = t * t * (3 - 2 * t);
          gain = 1 + vg * t;
          for (c = 0; c < 3; c++) p[c] = (u8)clamp255(vg < 0 ? p[c] * gain : p[c] + (255 - p[c]) * vg * t);
        }
      }
    }
  }
  return op_end(&o);
}

/* ------------------------------------------------------------------ stylize */
/* a cell's average colour (premultiplied) */
static void cell_avg(const Op *o, i32 x0, i32 y0, i32 x1, i32 y1, u8 *out)
{
  double acc[4] = { 0, 0, 0, 0 };
  long n = 0;
  i32 x, y;
  int c;
  for (y = y0; y < y1; y++) for (x = x0; x < x1; x++) {
    const u8 *q = ORIG(o, x, y);
    for (c = 0; c < 3; c++) acc[c] += q[c] * q[3];
    acc[3] += q[3]; n++;
  }
  out[3] = (u8)(n ? clamp255(acc[3] / n) : 0);
  for (c = 0; c < 3; c++) out[c] = (u8)(acc[3] > 0 ? clamp255(acc[c] / acc[3]) : 0);
}
typedef struct { i32 x0, y0, x1, y1; double h; } Cell;
static int cell_cmp(const void *a, const void *b)
{
  double d = ((const Cell *)a)->h - ((const Cell *)b)->h;
  if (d != 0) return d < 0 ? -1 : 1;
  /* (the same height: by place, so the order never depends on qsort) */
  if (((const Cell *)a)->y0 != ((const Cell *)b)->y0) return ((const Cell *)a)->y0 < ((const Cell *)b)->y0 ? -1 : 1;
  return ((const Cell *)a)->x0 < ((const Cell *)b)->x0 ? -1 : ((const Cell *)a)->x0 > ((const Cell *)b)->x0;
}
int flt_extrude(Doc *d, int type, int size, int depth, int random, int solid, int mask_incomplete, unsigned seed)
{
  Op o;
  Cell *cells;
  long nc = 0, k;
  i32 x, y, gx, gy;
  double icx, icy, half;
  size = clampi(size, 2, 255); depth = clampi(depth, 1, 255);
  if (!op_begin(&o, d, "Extrude")) return 0;
  gx = (o.b.x1 - o.b.x0 + size - 1) / size; gy = (o.b.y1 - o.b.y0 + size - 1) / size;
  if (!(cells = (Cell *)malloc((size_t)gx * gy * sizeof(Cell)))) { op_end(&o); return 0; }
  icx = (o.b.x0 + o.b.x1) / 2.0; icy = (o.b.y0 + o.b.y1) / 2.0;
  half = (o.b.x1 - o.b.x0 > o.b.y1 - o.b.y0 ? o.b.x1 - o.b.x0 : o.b.y1 - o.b.y0) / 2.0;
  for (y = 0; y < gy; y++)
    for (x = 0; x < gx; x++) {
      Cell c;
      u8 avg[4];
      double r = rnd01(&seed);
      c.x0 = o.b.x0 + x * size; c.y0 = o.b.y0 + y * size;
      c.x1 = c.x0 + size > o.b.x1 ? o.b.x1 : c.x0 + size; c.y1 = c.y0 + size > o.b.y1 ? o.b.y1 : c.y0 + size;
      if (mask_incomplete && (c.x1 - c.x0 < size || c.y1 - c.y0 < size)) continue;
      cell_avg(&o, c.x0, c.y0, c.x1, c.y1, avg);
      c.h = random ? r * depth : lumd(avg) / 255.0 * depth;
      cells[nc++] = c;
    }
  if (type == 1) {
    /* pyramids: four faces lit from the top left, as steep as the height */
    for (k = 0; k < nc; k++) {
      Cell *c = &cells[k];
      double hc = (c->x1 - c->x0) / 2.0, vc = (c->y1 - c->y0) / 2.0, sh = c->h / (double)size;
      u8 avg[4];
      if (sh > 1) sh = 1;
      cell_avg(&o, c->x0, c->y0, c->x1, c->y1, avg);
      for (y = c->y0; y < c->y1; y++)
        for (x = c->x0; x < c->x1; x++) {
          double dx = (x + 0.5 - c->x0 - hc) / hc, dy = (y + 0.5 - c->y0 - vc) / vc, f;
          const u8 *src = solid ? avg : ORIG(&o, x, y);
          u8 *p = DEST(&o, x, y);
          int ch;
          if (fabs(dy) >= fabs(dx)) f = dy < 0 ? 0.6 : -0.6; else f = dx < 0 ? 0.3 : -0.3;
          f *= sh;
          for (ch = 0; ch < 3; ch++) p[ch] = (u8)clamp255(f > 0 ? src[ch] + (255 - src[ch]) * f : src[ch] * (1 + f));
          p[3] = src[3];
        }
    }
  } else {
    /* blocks: the lowest first; each a square pushed out from the middle by its height, its sides
       the square swept along the way in a darker shade */
    qsort(cells, (size_t)nc, sizeof(Cell), cell_cmp);
    for (k = 0; k < nc; k++) {
      Cell *c = &cells[k];
      double ox = ((c->x0 + c->x1) / 2.0 - icx) / half * c->h, oy = ((c->y0 + c->y1) / 2.0 - icy) / half * c->h;
      int steps = (int)(fabs(ox) > fabs(oy) ? fabs(ox) : fabs(oy)) + 1, s, ch;
      u8 avg[4], side[4];
      cell_avg(&o, c->x0, c->y0, c->x1, c->y1, avg);
      for (ch = 0; ch < 3; ch++) side[ch] = (u8)(avg[ch] * 6 / 10);
      side[3] = avg[3];
      for (s = 0; s <= steps; s++) {
        i32 sx = (i32)floor(ox * s / steps + 0.5), sy = (i32)floor(oy * s / steps + 0.5);
        for (y = c->y0; y < c->y1; y++)
          for (x = c->x0; x < c->x1; x++) {
            i32 tx = x + sx, ty = y + sy;
            if (tx < o.b.x0 || ty < o.b.y0 || tx >= o.b.x1 || ty >= o.b.y1) continue;
            memcpy(DEST(&o, tx, ty), s < steps ? side : solid ? avg : ORIG(&o, x, y), 4);
          }
      }
    }
  }
  free(cells);
  return op_end(&o);
}
int flt_glowing_edges(Doc *d, int width, int brightness, int smoothness)
{
  Op o;
  u8 *sm, *ed;
  i32 x, y;
  int r = clampi(width, 1, 14) / 2;
  double gain = clampi(brightness, 0, 20) / 6.0;
  if (!op_begin(&o, d, "Glowing Edges")) return 0;
  sm = canvas_buf(d); ed = canvas_buf(d);
  if (!sm || !ed) { free(sm); free(ed); op_end(&o); return 0; }
  {
    Box e = box_clip(box_make(o.b.x0 - r - 1, o.b.y0 - r - 1, o.b.x1 + r + 1, o.b.y1 + r + 1), doc_box(d));
    gblur(o.orig, sm, d->w, d->h, e, clampi(smoothness, 1, 15) / 3.0);
    for (y = e.y0; y < e.y1; y++)
      for (x = e.x0; x < e.x1; x++) {
        int c;
        u8 *q = ed + ((long)y * d->w + x) * 4;
        for (c = 0; c < 3; c++) {
          int v[9], i, j;
          double gx, gy;
          for (j = -1; j <= 1; j++) for (i = -1; i <= 1; i++) {
            i32 xx = clampi(x + i, e.x0, e.x1 - 1), yy = clampi(y + j, e.y0, e.y1 - 1);
            v[(j + 1) * 3 + i + 1] = sm[((long)yy * d->w + xx) * 4 + c];
          }
          gx = v[2] + 2 * v[5] + v[8] - v[0] - 2 * v[3] - v[6];
          gy = v[6] + 2 * v[7] + v[8] - v[0] - 2 * v[1] - v[2];
          q[c] = (u8)clamp255(sqrt(gx * gx + gy * gy) * gain);
        }
      }
    /* widened: the brightest within the edge width */
    for (y = o.b.y0; y < o.b.y1; y++)
      for (x = o.b.x0; x < o.b.x1; x++) {
        u8 *p = DEST(&o, x, y);
        int c, i, j, m[3] = { 0, 0, 0 };
        for (j = -r; j <= r; j++) for (i = -r; i <= r; i++) {
          i32 xx = clampi(x + i, e.x0, e.x1 - 1), yy = clampi(y + j, e.y0, e.y1 - 1);
          const u8 *q = ed + ((long)yy * d->w + xx) * 4;
          for (c = 0; c < 3; c++) if (q[c] > m[c]) m[c] = q[c];
        }
        for (c = 0; c < 3; c++) p[c] = (u8)m[c];
      }
  }
  free(sm); free(ed);
  return op_end(&o);
}
int flt_plastic_wrap(Doc *d, int strength, int detail, int smoothness)
{
  Op o;
  float *h;
  u8 *bl;
  i32 x, y, w = d->w;
  int off = 1 + clampi(16 - detail, 1, 15) / 4;
  double st = clampi(strength, 0, 20) / 20.0, scale = 6.0 + clampi(detail, 1, 15);
  if (!op_begin(&o, d, "Plastic Wrap")) return 0;
  bl = canvas_buf(d);
  h = (float *)malloc((size_t)d->w * d->h * sizeof(float));
  if (!bl || !h) { free(bl); free(h); op_end(&o); return 0; }
  {
    Box e = box_clip(box_make(o.b.x0 - off, o.b.y0 - off, o.b.x1 + off, o.b.y1 + off), doc_box(d));
    gblur(o.orig, bl, d->w, d->h, e, clampi(smoothness, 1, 15) * 0.6);
    for (y = e.y0; y < e.y1; y++) for (x = e.x0; x < e.x1; x++) h[(long)y * w + x] = (float)lumd(bl + ((long)y * w + x) * 4);
    for (y = o.b.y0; y < o.b.y1; y++)
      for (x = o.b.x0; x < o.b.x1; x++) {
        /* the slope towards a light at the top left: bright sheen where the surface faces it */
        i32 xa = clampi(x - off, e.x0, e.x1 - 1), ya = clampi(y - off, e.y0, e.y1 - 1), xb = clampi(x + off, e.x0, e.x1 - 1), yb = clampi(y + off, e.y0, e.y1 - 1);
        double rel = (h[(long)yb * w + xb] - h[(long)ya * w + xa]) / scale, hl;
        u8 *p = DEST(&o, x, y);
        int c;
        if (rel <= 0) continue;
        hl = rel > 1 ? 1 : rel;
        hl = hl * hl * st * 1.5;
        if (hl > 1) hl = 1;
        for (c = 0; c < 3; c++) p[c] = (u8)clamp255(p[c] + (255 - p[c]) * hl);
      }
  }
  free(bl); free(h);
  return op_end(&o);
}
int flt_edge_detect(Doc *d, int method, int invert)
{
  Op o;
  i32 x, y;
  if (method == 0 && invert) return flt_find_edges(d);
  if (!op_begin(&o, d, "Find Edges")) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      int c, i, j;
      u8 *p = DEST(&o, x, y);
      for (c = 0; c < 3; c++) {
        int v[9];
        double m;
        for (j = -1; j <= 1; j++) for (i = -1; i <= 1; i++) v[(j + 1) * 3 + i + 1] = orig_at(&o, x + i, y + j)[c];
        if (method == 1) {
          double gx = v[2] + v[5] + v[8] - v[0] - v[3] - v[6], gy = v[6] + v[7] + v[8] - v[0] - v[1] - v[2];
          m = sqrt(gx * gx + gy * gy) / 1.5;
        } else if (method == 2) m = fabs(4.0 * v[4] - v[1] - v[3] - v[5] - v[7]);
        else if (method == 3) { double a = v[4] - v[8], b = v[5] - v[7]; m = sqrt(a * a + b * b) * 1.4; }
        else { double gx = v[2] + 2 * v[5] + v[8] - v[0] - 2 * v[3] - v[6], gy = v[6] + 2 * v[7] + v[8] - v[0] - 2 * v[1] - v[2]; m = sqrt(gx * gx + gy * gy) / 2; }
        p[c] = (u8)clamp255(invert ? 255 - m : m);
      }
    }
  return op_end(&o);
}

/* ------------------------------------------------------------------ other */
/* the most (or least) of n values (stride apart) within r either side, edges repeated: van Herk's
   method, three comparisons a value whatever the radius. tmp: 3 (n + 2r) bytes */
static void run_extreme(const u8 *in, int stride, i32 n, int r, int mx, u8 *out, int ostride, u8 *tmp)
{
  i32 m = n + 2 * r, k, wdt = 2 * r + 1;
  u8 *ext = tmp, *g = tmp + m, *h = tmp + 2 * m;
  for (k = 0; k < m; k++) { i32 s = k - r; ext[k] = in[(long)(s < 0 ? 0 : s >= n ? n - 1 : s) * stride]; }
  for (k = 0; k < m; k++) {
    if (k % wdt == 0) g[k] = ext[k];
    else g[k] = mx ? (ext[k] > g[k - 1] ? ext[k] : g[k - 1]) : (ext[k] < g[k - 1] ? ext[k] : g[k - 1]);
  }
  for (k = m - 1; k >= 0; k--) {
    if (k == m - 1 || (k + 1) % wdt == 0) h[k] = ext[k];
    else h[k] = mx ? (ext[k] > h[k + 1] ? ext[k] : h[k + 1]) : (ext[k] < h[k + 1] ? ext[k] : h[k + 1]);
  }
  for (k = 0; k < n; k++) {
    u8 a = h[k], b = g[k + 2 * r];
    out[(long)k * ostride] = mx ? (a > b ? a : b) : (a < b ? a : b);
  }
}
int flt_minmax(Doc *d, int radius, int maximum, int round)
{
  Op o;
  u8 *tmp, *rowo, *plane;
  i32 x, y, w = d->w, h = d->h;
  int r = clampi(radius, 1, 100), c;
  if (!op_begin(&o, d, maximum ? "Maximum" : "Minimum")) return 0;
  tmp = (u8 *)malloc((size_t)3 * ((w > h ? w : h) + 2 * r));
  rowo = (u8 *)malloc((size_t)(w > h ? w : h));
  plane = (u8 *)malloc((size_t)w * h);
  if (!tmp || !rowo || !plane) { free(tmp); free(rowo); free(plane); op_end(&o); return 0; }
  for (c = 0; c < 3; c++) {
    if (!round) {
      /* a square: along the rows, then down the columns */
      for (y = 0; y < h; y++) run_extreme(o.orig + (long)y * w * 4 + c, 4, w, r, maximum, plane + (long)y * w, 1, tmp);
      for (x = o.b.x0; x < o.b.x1; x++) {
        run_extreme(plane + x, w, h, r, maximum, rowo, 1, tmp);
        for (y = o.b.y0; y < o.b.y1; y++) DEST(&o, x, y)[c] = rowo[(long)y];
      }
    } else {
      /* a disc: each row of it a run as wide as the disc there */
      for (y = o.b.y0; y < o.b.y1; y++) {          /* 1.0: progress */
        int dy;
        for (x = o.b.x0; x < o.b.x1; x++) DEST(&o, x, y)[c] = maximum ? 0 : 255;
        for (dy = -r; dy <= r; dy++) {
          i32 yy = clampi(y + dy, 0, h - 1);
          int hw = (int)floor(sqrt((double)r * r - (double)dy * dy) + 1e-6);
          run_extreme(o.orig + (long)yy * w * 4 + c, 4, w, hw, maximum, rowo, 1, tmp);
          for (x = o.b.x0; x < o.b.x1; x++) {
            u8 *p = DEST(&o, x, y) + c;
            if (maximum ? rowo[x] > *p : rowo[x] < *p) *p = rowo[x];
          }
        }
      }
    }
  }
  free(tmp); free(rowo); free(plane);
  return op_end(&o);
}
int flt_offset_ex(Doc *d, i32 dx, i32 dy, int mode, u32 bg)
{
  Op o;
  i32 x, y;
  if (!op_begin(&o, d, "Offset")) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      i32 sx = x - dx, sy = y - dy;
      u8 *p = DEST(&o, x, y);
      int in = edge_coord(&sx, o.b.x0, o.b.x1, mode == 3 ? EDGE_WRAP : mode == 2 ? EDGE_CLAMP : EDGE_CLEAR) &&
               edge_coord(&sy, o.b.y0, o.b.y1, mode == 3 ? EDGE_WRAP : mode == 2 ? EDGE_CLAMP : EDGE_CLEAR);
      if (in) memcpy(p, ORIG(&o, sx, sy), 4);
      else if (mode == 1) { p[0] = (u8)(bg >> 16); p[1] = (u8)(bg >> 8); p[2] = (u8)bg; p[3] = 255; }
      else memset(p, 0, 4);
    }
  return op_end(&o);
}
int flt_color_to_alpha(Doc *d, u32 color)
{
  Op o;
  i32 x, y;
  int cc[3];
  cc[0] = (int)((color >> 16) & 255); cc[1] = (int)((color >> 8) & 255); cc[2] = (int)(color & 255);
  if (!op_begin(&o, d, "Color to Alpha")) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      u8 *p = DEST(&o, x, y);
      double a = 0, t;
      int c;
      if (!p[3]) continue;
      /* the least opacity that, laid over the colour, still gives this pixel */
      for (c = 0; c < 3; c++) {
        if (p[c] > cc[c]) t = cc[c] < 255 ? (p[c] - cc[c]) / (255.0 - cc[c]) : 0;
        else if (p[c] < cc[c]) t = cc[c] > 0 ? (cc[c] - p[c]) / (double)cc[c] : 0;
        else t = 0;
        if (t > a) a = t;
      }
      if (a <= 0) { p[0] = p[1] = p[2] = p[3] = 0; continue; }
      for (c = 0; c < 3; c++) p[c] = (u8)clamp255((p[c] - cc[c]) / a + cc[c]);
      p[3] = (u8)clamp255(p[3] * a);
    }
  return op_end(&o);
}
int flt_hsb_hsl(Doc *d, int mode)
{
  Op o;
  i32 x, y;
  if (!op_begin(&o, d, "HSB/HSL")) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      u8 *p = DEST(&o, x, y);
      double r, g, b, mx, mn, c, h = 0, s, v;
      if (mode == 0 || mode == 2) {
        r = p[0] / 255.0; g = p[1] / 255.0; b = p[2] / 255.0;
        mx = r > g ? (r > b ? r : b) : (g > b ? g : b); mn = r < g ? (r < b ? r : b) : (g < b ? g : b); c = mx - mn;
        if (c > 0) {
          if (mx == r) h = fmod((g - b) / c + 6, 6); else if (mx == g) h = (b - r) / c + 2; else h = (r - g) / c + 4;
        }
        if (mode == 0) { v = (mx + mn) / 2; s = c == 0 ? 0 : c / (1 - fabs(2 * v - 1)); }
        else { v = mx; s = mx > 0 ? c / mx : 0; }
        p[0] = (u8)clamp255(h / 6 * 255); p[1] = (u8)clamp255(s * 255); p[2] = (u8)clamp255(v * 255);
      } else {
        double hp = p[0] / 255.0 * 6, xx, m;
        s = p[1] / 255.0; v = p[2] / 255.0;
        c = mode == 1 ? (1 - fabs(2 * v - 1)) * s : v * s;
        m = mode == 1 ? v - c / 2 : v - c;
        if (hp >= 6) hp -= 6;
        xx = c * (1 - fabs(fmod(hp, 2) - 1));
        r = g = b = 0;
        if (hp < 1) { r = c; g = xx; } else if (hp < 2) { r = xx; g = c; } else if (hp < 3) { g = c; b = xx; }
        else if (hp < 4) { g = xx; b = c; } else if (hp < 5) { r = xx; b = c; } else { r = c; b = xx; }
        p[0] = (u8)clamp255((r + m) * 255); p[1] = (u8)clamp255((g + m) * 255); p[2] = (u8)clamp255((b + m) * 255);
      }
    }
  return op_end(&o);
}

/* ------------------------------------------------------------------ render */
int flt_lighting_ex(Doc *d, int type, i32 lx, i32 ly, int radius, int angle, int cone, int intensity, u32 color, int ambient, int texture, int gloss)
{
  Op o;
  i32 x, y;
  double lc[3], amb = (clampi(ambient, -100, 100) + 100) / 200.0 * 0.6, in = clampi(intensity, -100, 100) / 100.0 * 1.6;
  double ax = cos(angle * M_PI / 180), ay = -sin(angle * M_PI / 180), cosc = cos(clampi(cone, 1, 90) * M_PI / 180);
  double bump = clampi(texture, 0, 100) / 100.0 * 4, gl = clampi(gloss, 0, 100) / 100.0, lz;
  if (radius < 1) radius = 1;
  lz = radius * 0.5;
  lc[0] = ((color >> 16) & 255) / 255.0; lc[1] = ((color >> 8) & 255) / 255.0; lc[2] = (color & 255) / 255.0;
  if (!op_begin(&o, d, "Lighting Effects")) return 0;
  for (y = o.b.y0; y < o.b.y1; y++) {          /* 1.0: progress */
    for (x = o.b.x0; x < o.b.x1; x++) {
      double nx = 0, ny = 0, nz = 1, nl, Lx, Ly, Lz, ll, diff, att = 1, spec = 0, hz;
      u8 *p = DEST(&o, x, y);
      int c;
      if (bump > 0) {
        /* the surface's slope from the brightness either side */
        nx = -(lumd(orig_at(&o, x + 1, y)) - lumd(orig_at(&o, x - 1, y))) / 255.0 * bump;
        ny = -(lumd(orig_at(&o, x, y + 1)) - lumd(orig_at(&o, x, y - 1))) / 255.0 * bump;
        nl = sqrt(nx * nx + ny * ny + 1); nx /= nl; ny /= nl; nz = 1 / nl;
      }
      if (type == 2) {
        /* infinite: from a direction, the same everywhere, raised 45 degrees */
        Lx = -ax; Ly = -ay; Lz = 1;
      } else {
        Lx = lx - (x + 0.5); Ly = ly - (y + 0.5); Lz = lz;
        ll = sqrt(Lx * Lx + Ly * Ly);
        att = 1 - (ll * ll) / ((double)radius * radius);
        if (att < 0) att = 0;
        att *= att;
        if (type == 1) {
          /* spot: only inside the cone round where it points, softening to its edge */
          double cs = ll > 0 ? (-(Lx * ax + Ly * ay)) / ll : 1, edge;
          if (cs < cosc) att = 0;
          else { edge = (cs - cosc) / (1 - cosc + 1e-9); att *= edge < 0.25 ? edge * 4 : 1; }
        }
      }
      ll = sqrt(Lx * Lx + Ly * Ly + Lz * Lz);
      Lx /= ll; Ly /= ll; Lz /= ll;
      diff = nx * Lx + ny * Ly + nz * Lz;
      if (diff < 0) diff = 0;
      if (gl > 0 && diff > 0) {
        /* a highlight where the surface turns the light towards the viewer */
        hz = 2 * diff * nz - Lz;
        if (hz > 0) spec = pow(hz, 4 + gl * 40) * gl;
      }
      for (c = 0; c < 3; c++) {
        double g = amb + in * att * diff * lc[c] * 1.2;
        p[c] = (u8)clamp255(p[c] * (g < 0 ? 0 : g) + 255 * spec * att * lc[c] * (in > 0 ? 1 : 0));
      }
    }
  }
  return op_end(&o);
}

/* ------------------------------------------------------------------ gallery effects (1.0) */
/* a hash of a pixel and a seed, 0 to 1 (the same pixel, the same grain) */
static double hash01(i32 x, i32 y, unsigned seed)
{
  unsigned long n = (unsigned long)x * 374761393UL + (unsigned long)y * 668265263UL + seed * 2246822519UL;
  n = (n ^ (n >> 13)) * 1274126177UL;
  return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0;
}
int flt_film_grain(Doc *d, int grain, int highlight, int intensity, unsigned seed)
{
  Op o;
  i32 x, y;
  double g = clampi(grain, 0, 20) * 4.0, ha = clampi(highlight, 0, 20) / 20.0, in = clampi(intensity, 0, 10) / 10.0;
  if (!op_begin(&o, d, "Film Grain")) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      u8 *p = DEST(&o, x, y);
      double l = lumd(p) / 255.0, n = (hash01(x, y, seed) + hash01(x, y, seed + 1) - 1) * g, lift = 0;
      int c;
      /* grain most in the shadows and midtones; the brightest parts lifted into a glow */
      n *= 1 - l * 0.7;
      if (ha > 0 && l > 1 - ha) lift = (l - (1 - ha)) / ha * in * 120;
      for (c = 0; c < 3; c++) p[c] = (u8)clamp255(p[c] + n + lift);
    }
  return op_end(&o);
}
int flt_cutout(Doc *d, int levels, int simplicity, int fidelity)
{
  Op o;
  u8 *bl;
  i32 x, y;
  int lv = clampi(levels, 2, 8);
  if (!op_begin(&o, d, "Cutout")) return 0;
  if (!(bl = canvas_buf(d))) { op_end(&o); return 0; }
  /* simplified (blurred more for more simplicity, less for more fidelity), then flat colours */
  gblur(o.orig, bl, d->w, d->h, o.b, clampi(simplicity, 0, 10) * 0.8 / clampi(fidelity, 1, 3) + 0.3);
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      const u8 *q = bl + ((long)y * d->w + x) * 4;
      u8 *p = DEST(&o, x, y);
      int c;
      for (c = 0; c < 3; c++) { int k = q[c] * lv / 256; p[c] = (u8)(k * 255 / (lv - 1)); }
    }
  free(bl);
  return op_end(&o);
}
int flt_poster_edges(Doc *d, int thickness, int intensity, int posterize)
{
  Op o;
  i32 x, y;
  int lv = clampi(posterize, 0, 6) + 2;
  double th = 40 + (10 - clampi(thickness, 0, 10)) * 12, in = clampi(intensity, 0, 10) / 10.0;
  if (!op_begin(&o, d, "Poster Edges")) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      double l[9], gx, gy, mag, k;
      int i, j, c;
      u8 *p = DEST(&o, x, y);
      for (j = -1; j <= 1; j++) for (i = -1; i <= 1; i++) l[(j + 1) * 3 + i + 1] = lumd(orig_at(&o, x + i, y + j));
      gx = l[2] + 2 * l[5] + l[8] - l[0] - 2 * l[3] - l[6];
      gy = l[6] + 2 * l[7] + l[8] - l[0] - 2 * l[1] - l[2];
      mag = sqrt(gx * gx + gy * gy);
      k = mag > th ? 1 - in * (mag - th) / (mag + 1) * 2 : 1;
      if (k < 0) k = 0;
      for (c = 0; c < 3; c++) { int q = p[c] * lv / 256; p[c] = (u8)clamp255(q * 255 / (lv - 1) * k); }
    }
  return op_end(&o);
}
int flt_diffuse_glow(Doc *d, int graininess, int glow, int clear, u32 bg, unsigned seed)
{
  Op o;
  i32 x, y;
  double gr = clampi(graininess, 0, 10) * 6.0, ga = clampi(glow, 0, 20) / 20.0, cl = clampi(clear, 0, 20) / 20.0;
  u8 b[3];
  b[0] = (u8)(bg >> 16); b[1] = (u8)(bg >> 8); b[2] = (u8)bg;
  if (!op_begin(&o, d, "Diffuse Glow")) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      u8 *p = DEST(&o, x, y);
      double l = lumd(p) / 255.0, t = (l - cl * 0.8) / (1 - cl * 0.8 + 1e-6), n;
      int c;
      /* the background colour glowing out of the light parts, with grain in it */
      if (t < 0) t = 0;
      t = t * t * ga;
      n = (hash01(x, y, seed) - 0.5) * gr * t;
      for (c = 0; c < 3; c++) p[c] = (u8)clamp255(p[c] + (b[c] - p[c]) * t + n);
    }
  return op_end(&o);
}
/* Stained Glass: cells about a jittered grid, each its average colour, leaded in fg */
int flt_stained_glass(Doc *d, int cell, int border, int light, u32 fg, unsigned seed)
{
  Op o;
  i32 x, y, gw, gh;
  int cs = clampi(cell, 2, 50);
  double bw = clampi(border, 1, 20) * 0.5, li = clampi(light, 0, 10) / 10.0;
  double *acc;
  u8 lead[3];
  long ncell;
  lead[0] = (u8)(fg >> 16); lead[1] = (u8)(fg >> 8); lead[2] = (u8)fg;
  if (!op_begin(&o, d, "Stained Glass")) return 0;
  gw = (o.b.x1 - o.b.x0) / cs + 3; gh = (o.b.y1 - o.b.y0) / cs + 3; ncell = (long)gw * gh;
  if (!(acc = (double *)calloc((size_t)ncell * 5, sizeof(double)))) { op_end(&o); return 0; }
  /* two passes: the cells' averages, then the colours and leading */
  {
    int pass;
    for (pass = 0; pass < 2; pass++)
      for (y = o.b.y0; y < o.b.y1; y++)
        for (x = o.b.x0; x < o.b.x1; x++) {
          i32 cx = (x - o.b.x0) / cs + 1, cy = (y - o.b.y0) / cs + 1, i, j, best = -1;
          double d1 = 1e30, d2 = 1e30, bx = 0, by = 0;
          for (j = cy - 1; j <= cy + 1; j++) for (i = cx - 1; i <= cx + 1; i++) {
            double px = o.b.x0 + (i - 1 + 0.15 + 0.7 * hash01(i, j, seed)) * cs, py = o.b.y0 + (j - 1 + 0.15 + 0.7 * hash01(i, j, seed + 7)) * cs;
            double dd = (x + 0.5 - px) * (x + 0.5 - px) + (y + 0.5 - py) * (y + 0.5 - py);
            if (dd < d1) { d2 = d1; d1 = dd; best = j * gw + i; bx = px; by = py; }
            else if (dd < d2) d2 = dd;
          }
          if (pass == 0) {
            const u8 *q = ORIG(&o, x, y);
            double *a = acc + (long)best * 5;
            int c;
            for (c = 0; c < 3; c++) a[c] += q[c] * q[3];
            a[3] += q[3]; a[4] += 1;
          } else {
            u8 *p = DEST(&o, x, y);
            const double *a = acc + (long)best * 5;
            int c;
            /* near the line between two cells: the lead */
            if (sqrt(d2) - sqrt(d1) < bw) { p[0] = lead[0]; p[1] = lead[1]; p[2] = lead[2]; continue; }
            {
              double r = sqrt(d1) / cs, f = 1 + li * 0.6 * (0.5 - r);
              (void)bx; (void)by;
              for (c = 0; c < 3; c++) p[c] = (u8)clamp255((a[3] > 0 ? a[c] / a[3] : 0) * f);
            }
          }
        }
  }
  free(acc);
  return op_end(&o);
}
int flt_halftone_pattern(Doc *d, int size, int contrast, int type, u32 fg, u32 bg)
{
  Op o;
  i32 x, y;
  int s = clampi(size, 1, 12) * 2 + 2;
  double ct = 1 + clampi(contrast, 0, 50) / 10.0;
  if (!op_begin(&o, d, "Halftone Pattern")) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      u8 *p = DEST(&o, x, y);
      double l = lumd(p) / 255.0, t, v;
      int c;
      if (type == 1) { double dx = (x % s) - s / 2.0 + 0.5, dy = (y % s) - s / 2.0 + 0.5; t = fmod(sqrt(dx * dx + dy * dy) * 2 / s + 1, 1.0); }
      else if (type == 2) t = (y % s + 0.5) / s;
      else { double dx = (x % s) - s / 2.0 + 0.5, dy = (y % s) - s / 2.0 + 0.5; t = sqrt(dx * dx + dy * dy) / (s * 0.7071); }
      /* ink (the foreground) where the picture is darker than the pattern, sharpened by contrast */
      v = (l - t) * ct * 4 + 0.5;
      if (v < 0) v = 0;
      if (v > 1) v = 1;
      for (c = 0; c < 3; c++) { int a = (int)((fg >> (16 - 8 * c)) & 255), b = (int)((bg >> (16 - 8 * c)) & 255); p[c] = (u8)clamp255(a + (b - a) * v); }
    }
  return op_end(&o);
}
int flt_photocopy(Doc *d, int detail, int darkness, u32 fg, u32 bg)
{
  Op o;
  u8 *bl;
  i32 x, y;
  double dk = clampi(darkness, 1, 50) / 10.0;
  if (!op_begin(&o, d, "Photocopy")) return 0;
  if (!(bl = canvas_buf(d))) { op_end(&o); return 0; }
  gblur(o.orig, bl, d->w, d->h, o.b, clampi(detail, 1, 24) * 0.5 + 0.5);
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      u8 *p = DEST(&o, x, y);
      double diff = lumd(bl + ((long)y * d->w + x) * 4) - lumd(ORIG(&o, x, y)), v = 1 - diff * dk / 40;
      int c;
      /* toner where it is darker than round it */
      if (v < 0) v = 0;
      if (v > 1) v = 1;
      for (c = 0; c < 3; c++) { int a = (int)((fg >> (16 - 8 * c)) & 255), b = (int)((bg >> (16 - 8 * c)) & 255); p[c] = (u8)clamp255(a + (b - a) * v); }
    }
  free(bl);
  return op_end(&o);
}
/* Texturizer: a relief (0 brick, 1 burlap, 2 canvas, 3 sandstone) lit from angle degrees */
static double tex_height(int type, double x, double y, unsigned seed)
{
  switch (type) {
  case 0: {
    /* bricks 32 x 16 with every other row offset, mortar lines lower */
    double row = floor(y / 16), bx = fmod(x + (fmod(row, 2) ? 16 : 0), 32), by = fmod(y, 16);
    double e = bx < by ? bx : by, e2 = 32 - bx < 16 - by ? 32 - bx : 16 - by;
    if (e2 < e) e = e2;
    return (e < 2 ? e / 2 : 1) * (0.85 + 0.15 * hash01((i32)floor(x / 3), (i32)floor(y / 3), seed));
  }
  case 1: return 0.5 + 0.25 * sin(x * 1.2) * (fmod(floor(y / 3), 2) ? 1 : -1) + 0.25 * sin(y * 1.2) * (fmod(floor(x / 3), 2) ? 1 : -1);
  case 2: return 0.5 + 0.25 * sin(x * 2.1) + 0.25 * sin(y * 2.1) + 0.1 * (hash01((i32)x, (i32)y, seed) - 0.5);
  default: return hash01((i32)x, (i32)y, seed) * 0.5 + hash01((i32)(x / 2), (i32)(y / 2), seed + 3) * 0.5;
  }
}
int flt_texturizer(Doc *d, int type, int scaling, int relief, int angle, unsigned seed)
{
  Op o;
  i32 x, y;
  double sc = clampi(scaling, 50, 200) / 100.0, rl = clampi(relief, 0, 50) / 10.0, lx = cos(angle * M_PI / 180), ly = -sin(angle * M_PI / 180);
  if (!op_begin(&o, d, "Texturizer")) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      u8 *p = DEST(&o, x, y);
      double h0 = tex_height(type, x / sc, y / sc, seed), hx = tex_height(type, (x + 1) / sc, y / sc, seed), hy = tex_height(type, x / sc, (y + 1) / sc, seed);
      double shade = ((hx - h0) * -lx + (hy - h0) * -ly) * rl;
      int c;
      for (c = 0; c < 3; c++) p[c] = (u8)clamp255(shade > 0 ? p[c] + (255 - p[c]) * (shade > 1 ? 1 : shade) : p[c] * (1 + (shade < -1 ? -1 : shade)));
    }
  return op_end(&o);
}
int flt_mosaic_tiles(Doc *d, int tile, int grout, int lighten)
{
  Op o;
  i32 x, y;
  int ts = clampi(tile, 2, 100), gw = clampi(grout, 1, 15), lt = clampi(lighten, 0, 10);
  if (!op_begin(&o, d, "Mosaic Tiles")) return 0;
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      u8 *p = DEST(&o, x, y);
      int tx = (x - o.b.x0) % ts, ty = (y - o.b.y0) % ts, c;
      /* grout round each tile; the tiles' edges catch the light from the top left */
      if (tx < gw || ty < gw) { for (c = 0; c < 3; c++) p[c] = (u8)clamp255(p[c] * 0.35 + lt * 18); continue; }
      if (tx == gw || ty == gw) for (c = 0; c < 3; c++) p[c] = (u8)clamp255(p[c] + (255 - p[c]) * 0.3);
      else if (tx == ts - 1 || ty == ts - 1) for (c = 0; c < 3; c++) p[c] = (u8)(p[c] * 7 / 10);
    }
  return op_end(&o);
}
int flt_patchwork(Doc *d, int square, int relief)
{
  Op o;
  i32 x, y;
  int s = clampi(square, 2, 50);
  double rl = clampi(relief, 0, 25) / 25.0;
  if (!op_begin(&o, d, "Patchwork")) return 0;
  for (y = o.b.y0; y < o.b.y1; y += s)
    for (x = o.b.x0; x < o.b.x1; x += s) {
      i32 x1 = x + s > o.b.x1 ? o.b.x1 : x + s, y1 = y + s > o.b.y1 ? o.b.y1 : y + s, i, j;
      u8 avg[4];
      cell_avg(&o, x, y, x1, y1, avg);
      for (j = y; j < y1; j++)
        for (i = x; i < x1; i++) {
          u8 *p = DEST(&o, i, j);
          double f = 0;
          int c;
          if (i == x || j == y) f = 0.5 * rl;
          else if (i == x1 - 1 || j == y1 - 1) f = -0.5 * rl;
          for (c = 0; c < 3; c++) p[c] = (u8)clamp255(f > 0 ? avg[c] + (255 - avg[c]) * f : avg[c] * (1 + f));
          p[3] = avg[3];
        }
    }
  return op_end(&o);
}
int flt_spatter(Doc *d, int radius, int smoothness, unsigned seed)
{
  Op o;
  u8 *tmp;
  i32 x, y;
  int r = clampi(radius, 0, 25);
  if (!op_begin(&o, d, "Spatter")) return 0;
  if (!(tmp = canvas_buf(d))) { op_end(&o); return 0; }
  memcpy(tmp, o.orig, (size_t)d->w * d->h * 4);
  /* each pixel from one up to radius away, the offsets smoothed into blobs */
  for (y = o.b.y0; y < o.b.y1; y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      int sm = clampi(smoothness, 1, 15);
      i32 qx = x / sm, qy = y / sm;
      double ox = (hash01(qx, qy, seed) - 0.5) * 2 * r, oy = (hash01(qx, qy, seed + 5) - 0.5) * 2 * r;
      ox += (hash01(x, y, seed + 9) - 0.5) * r * 0.6; oy += (hash01(x, y, seed + 11) - 0.5) * r * 0.6;
      memcpy(tmp + ((long)y * d->w + x) * 4, orig_at(&o, (i32)floor(x + ox + 0.5), (i32)floor(y + oy + 0.5)), 4);
    }
  for (y = o.b.y0; y < o.b.y1; y++) memcpy(DEST(&o, o.b.x0, y), tmp + ((long)y * d->w + o.b.x0) * 4, (size_t)(o.b.x1 - o.b.x0) * 4);
  free(tmp);
  return op_end(&o);
}
