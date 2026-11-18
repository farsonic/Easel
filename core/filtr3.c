/*
 * filtr3.c - round 3's filters (see filtr3.h). Easel round 3.
 *
 * Oil Paint is ported from PhotoCraft (https://github.com/storytold/photocraft), Copyright (c) 2026
 * ArtCraft Team and the PhotoCraft contributors, used under the MIT License (see Licenses.txt):
 * crates/algo/src/oil.rs. The rest is Easel's own.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "util.h"
#include "filtr3.h"
#include "layers.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int clamp255(double v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5); }
static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
/* a value 0-1 for a canvas position (the same whatever the bands) */
static double hash01(i32 x, i32 y, u32 salt)
{
  u32 h = (u32)x * 0x8DA6B343u ^ (u32)y * 0xD8163841u ^ salt * 0xCB1AB31Fu;
  h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
  return (h & 0xFFFFFF) / 16777216.0;
}

/* ------------------------------------------------------------------ the area and its memory */
static int area_of(Doc *d, Layer **lp, Box *bp)
{
  Layer *l;
  Box b;
  if (d->active < 0 || d->active >= d->nlay) return 0;
  l = d->lay[d->active];
  if (!l->px || l->locked || l->lock_all || !l->visible) return 0;
  b = d->sel ? box_clip(d->selbox, doc_box(d)) : doc_box(d);
  if (box_empty(b)) return 0;
  *lp = l; *bp = b;
  return 1;
}
#define BAND_MIN 32
long r3_memory(Doc *d, int kind, int reach)
{
  Box b = d->sel ? box_clip(d->selbox, doc_box(d)) : doc_box(d);
  long bw = b.x1 - b.x0, bh = b.y1 - b.y0, band, in;
  if (box_empty(b)) return 0;
  if (kind == R3M_WHOLE) return (long)d->w * d->h * 4 + bw * bh * 4;
  band = reach > BAND_MIN ? reach : BAND_MIN;
  if (band > bh) band = bh;
  in = (bw + 2L * reach) * (band + 2L * reach);
  /* the input, two output bands, about six float planes of the input, the undo copy */
  return in * 4 + bw * band * 8 + in * 4 * 6 + bw * bh * 4;
}

/* ------------------------------------------------------------------ the band engine */
/* fn works out the output for box ob (out: RGBA, its own width) from in: the original pixels of
   box ib (RGBA, ib's width), which is ob grown by the reach and clipped to the canvas. 0 if out of
   memory */
typedef int (*BandFn)(void *ctx, Doc *d, const u8 *in, Box ib, u8 *out, Box ob);
static void band_put(Doc *d, Layer *l, const u8 *out, Box ob)
{
  i32 x, y, bw = ob.x1 - ob.x0;
  for (y = ob.y0; y < ob.y1; y++)
    for (x = ob.x0; x < ob.x1; x++) {
      long k = (long)y * d->w + x;
      int s = d->sel ? d->sel[k] : 255, c;
      u8 *p = l->px + k * 4;
      const u8 *q = out + ((long)(y - ob.y0) * bw + (x - ob.x0)) * 4;
      u8 a = p[3];
      if (!s) continue;
      if (s == 255) memcpy(p, q, 4);
      else for (c = 0; c < 4; c++) p[c] = (u8)(p[c] + ((q[c] - p[c]) * s + (q[c] >= p[c] ? 127 : -127)) / 255);
      if (l->alpha_lock) p[3] = a;
    }
}
static int band_run(Doc *d, const char *name, int reach, BandFn fn, void *ctx)
{
  Layer *l;
  Box b, pend = box_make(0, 0, 0, 0);
  i32 band, y, bw;
  u8 *in = NULL, *out = NULL, *keep = NULL, *t;
  int ok = 1;
  long saved;
  if (!area_of(d, &l, &b)) return 0;
  if (reach < 1) reach = 1;
  band = reach > BAND_MIN ? reach : BAND_MIN;
  if (band > b.y1 - b.y0) band = b.y1 - b.y0;
  bw = b.x1 - b.x0;
  in = (u8 *)malloc((size_t)(bw + 2 * reach) * (band + 2 * reach) * 4);
  out = (u8 *)malloc((size_t)bw * band * 4); keep = (u8 *)malloc((size_t)bw * band * 4);
  if (!in || !out || !keep) { free(in); free(out); free(keep); return -1; }
  doc_begin(d, name);
  doc_note_layer(d, d->active, b);
  for (y = b.y0; ok && y < b.y1 && !progress_aborted; y += band) {
    Box ob = box_make(b.x0, y, b.x1, y + band < b.y1 ? y + band : b.y1);
    Box ib = box_clip(box_make(ob.x0 - reach, ob.y0 - reach, ob.x1 + reach, ob.y1 + reach), doc_box(d));
    i32 r, iw = ib.x1 - ib.x0;
    /* the original rows: the band before this one isn't written yet (its rows are within the reach) */
    for (r = ib.y0; r < ib.y1; r++) memcpy(in + (long)(r - ib.y0) * iw * 4, l->px + ((long)r * d->w + ib.x0) * 4, (size_t)iw * 4);
    saved = progress_push((long)(y - b.y0) * 1000 / (b.y1 - b.y0), (long)(ob.y1 - b.y0) * 1000 / (b.y1 - b.y0));
    ok = fn(ctx, d, in, ib, out, ob);
    progress_pop(saved);
    if (!box_empty(pend)) band_put(d, l, keep, pend);
    t = keep; keep = out; out = t; pend = ob;
    if (progress(y + band - b.y0, b.y1 - b.y0)) break;
  }
  if (ok && !progress_aborted && !box_empty(pend)) band_put(d, l, keep, pend);
  free(in); free(out); free(keep);
  doc_end(d);
  return ok ? 1 : -1;
}

/* ------------------------------------------------------------------ blurs on float planes */
/* box mean of radius r along rows (step 1) or columns, edges repeated, n values a line */
static void box_line(float *p, long n, long step, int r, float *tmp)
{
  long i;
  double acc = 0, inv = 1.0 / (2 * r + 1);
  if (r < 1 || n < 1) return;
  for (i = 0; i < n; i++) tmp[i] = p[i * step];
  for (i = -r; i <= r; i++) acc += tmp[i < 0 ? 0 : i >= n ? n - 1 : i];
  for (i = 0; i < n; i++) {
    long a = i + r + 1, b = i - r;
    p[i * step] = (float)(acc * inv);
    acc += tmp[a >= n ? n - 1 : a] - tmp[b < 0 ? 0 : b];
  }
}
static int box_plane(float *p, i32 w, i32 h, int r)
{
  float *tmp = (float *)malloc((size_t)(w > h ? w : h) * sizeof(float));
  i32 i;
  if (!tmp) return 0;
  for (i = 0; i < h; i++) box_line(p + (long)i * w, w, 1, r, tmp);
  for (i = 0; i < w; i++) box_line(p + i, h, w, r, tmp);
  free(tmp);
  return 1;
}
/* three box passes near a Gaussian of sigma s (reach about 3 s) */
static void gauss_radii(double s, int *r)
{
  double wi = sqrt(12.0 * s * s / 3 + 1);
  int wl = (int)floor(wi), m, k;
  if (!(wl & 1)) wl--;
  m = (int)floor((12.0 * s * s - 3.0 * wl * wl - 12.0 * wl - 9) / (-4.0 * wl - 4) + 0.5);
  for (k = 0; k < 3; k++) r[k] = ((k < m ? wl : wl + 2) - 1) / 2;
}
static int gauss_reach(double s) { int r[3]; if (s < 0.3) return 0; gauss_radii(s, r); return r[0] + r[1] + r[2] + 1; }
static int gauss_plane(float *p, i32 w, i32 h, double s)
{
  int r[3], k;
  if (s < 0.3) return 1;
  gauss_radii(s, r);
  for (k = 0; k < 3; k++) if (!box_plane(p, w, h, r[k])) return 0;
  return 1;
}

/* ------------------------------------------------------------------ Field Blur */
double field_blur_radius(const BlurPin *pins, int n, double x, double y)
{
  double ws = 0, acc = 0;
  int i;
  if (n < 1) return 0;
  for (i = 0; i < n; i++) {
    double dx = x - pins[i].x, dy = y - pins[i].y, d2 = dx * dx + dy * dy, w;
    if (d2 < 1e-6) return pins[i].blur;
    w = 1 / d2;
    ws += w; acc += w * pins[i].blur;
  }
  return acc / ws;
}
typedef struct { const BlurPin *pins; int n; double rmax; } FieldCtx;
#define FB_LEVELS 4
static int field_band(void *vc, Doc *d, const u8 *in, Box ib, u8 *out, Box ob)
{
  FieldCtx *fc = (FieldCtx *)vc;
  i32 iw = ib.x1 - ib.x0, ih = ib.y1 - ib.y0, ow = ob.x1 - ob.x0, x, y;
  long n = (long)iw * ih, k;
  float *pl = (float *)malloc((size_t)n * sizeof(float));
  u8 *lv = (u8 *)malloc((size_t)ow * (ob.y1 - ob.y0) * 4 * FB_LEVELS);
  int c, L;
  (void)d;
  if (!pl || !lv) { free(pl); free(lv); return 0; }
  /* each level: the band blurred at rmax * L / (levels - 1), premultiplied, kept for the output box */
  for (L = 0; L < FB_LEVELS; L++)
    for (c = 0; c < 4; c++) {
      double s = fc->rmax * L / (FB_LEVELS - 1) / 2.0;            /* (radius ~ two sigmas) */
      for (k = 0; k < n; k++) pl[k] = c == 3 ? in[k * 4 + 3] : in[k * 4 + c] * in[k * 4 + 3] / 255.0f;
      if (!gauss_plane(pl, iw, ih, s)) { free(pl); free(lv); return 0; }
      for (y = ob.y0; y < ob.y1; y++)
        for (x = ob.x0; x < ob.x1; x++)
          lv[(((long)L * (ob.y1 - ob.y0) + (y - ob.y0)) * ow + (x - ob.x0)) * 4 + c] = (u8)clamp255(pl[(long)(y - ib.y0) * iw + (x - ib.x0)]);
      if (progress(L * 4 + c, FB_LEVELS * 4 + 1)) { free(pl); free(lv); return 1; }
    }
  for (y = ob.y0; y < ob.y1; y++)
    for (x = ob.x0; x < ob.x1; x++) {
      double r = field_blur_radius(fc->pins, fc->n, x + 0.5, y + 0.5), f = fc->rmax > 0 ? r / fc->rmax * (FB_LEVELS - 1) : 0, acc[4];
      int lo = (int)floor(f);
      u8 *o = out + ((long)(y - ob.y0) * ow + (x - ob.x0)) * 4;
      if (lo >= FB_LEVELS - 1) { lo = FB_LEVELS - 2; f = FB_LEVELS - 1; }
      if (lo < 0) { lo = 0; f = 0; }
      f -= lo;
      for (c = 0; c < 4; c++) {
        double a = lv[(((long)lo * (ob.y1 - ob.y0) + (y - ob.y0)) * ow + (x - ob.x0)) * 4 + c];
        double b = lv[(((long)(lo + 1) * (ob.y1 - ob.y0) + (y - ob.y0)) * ow + (x - ob.x0)) * 4 + c];
        acc[c] = a + (b - a) * f;
      }
      o[3] = (u8)clamp255(acc[3]);
      for (c = 0; c < 3; c++) o[c] = acc[3] > 0.5 ? (u8)clamp255(acc[c] * 255 / acc[3]) : 0;
      if (lo == 0 && f == 0) memcpy(o, in + ((long)(y - ib.y0) * iw + (x - ib.x0)) * 4, 4);      /* (no blur: exactly the pixel) */
    }
  free(pl); free(lv);
  return 1;
}
int flt_field_blur(Doc *d, const BlurPin *pins, int n)
{
  FieldCtx fc;
  int i;
  if (n < 1 || n > FB_PINS) return 0;
  fc.pins = pins; fc.n = n; fc.rmax = 0;
  for (i = 0; i < n; i++) if (pins[i].blur > fc.rmax) fc.rmax = pins[i].blur;
  if (fc.rmax > 200) fc.rmax = 200;
  if (fc.rmax < 0.5) return 0;
  return band_run(d, "Field Blur", gauss_reach(fc.rmax / 2.0) + 1, field_band, &fc);
}

/* ------------------------------------------------------------------ Lens Blur with a depth map */
void lens_blur_defaults(LensBlurParams *p)
{
  memset(p, 0, sizeof *p);
  p->radius = 15; p->blades = 6; p->depth = LBD_NONE; p->focal = 0; p->brightness = 0; p->threshold = 255;
}
void lens_iris_runs(int r, int blades, int rotation, int *lo, int *hi)
{
  int dy, dx, k;
  double rot = rotation * M_PI / 180;
  for (dy = -r; dy <= r; dy++) {
    int a = 1, b = 0;
    for (dx = -r; dx <= r; dx++) {
      int in;
      if (!blades) in = dx * dx + dy * dy <= r * r + r / 2;
      else {
        /* inside every edge of the regular polygon (its corners on the circle) */
        double ang = 2 * M_PI / blades, apo = r * cos(ang / 2) + 0.5;
        in = 1;
        for (k = 0; k < blades && in; k++) {
          double t = rot + ang * (k + 0.5);
          if (dx * cos(t) + dy * sin(t) > apo) in = 0;
        }
      }
      if (in) { if (a > b) a = dx; b = dx; }
    }
    if (a > b) { lo[dy + r] = 1; hi[dy + r] = 0; } else { lo[dy + r] = a; hi[dy + r] = b; }
  }
}
#define LB_LEVELS 4
typedef struct { const LensBlurParams *p; const u8 *depth; int rmax; } LbCtx;
static int lb_band(void *vc, Doc *d, const u8 *in, Box ib, u8 *out, Box ob)
{
  LbCtx *lc = (LbCtx *)vc;
  const LensBlurParams *p = lc->p;
  i32 iw = ib.x1 - ib.x0, ow = ob.x1 - ob.x0, oh = ob.y1 - ob.y0, x, y;
  long npx = (long)ow * oh;
  double *pre = (double *)malloc((size_t)(iw + 1) * 4 * sizeof(double));
  float *lv = (float *)malloc((size_t)npx * 4 * LB_LEVELS * sizeof(float));
  int *lo = (int *)malloc((size_t)(2 * lc->rmax + 1) * 2 * sizeof(int)), *hi, L, c;
  if (!pre || !lv || !lo) { free(pre); free(lv); free(lo); return 0; }
  hi = lo + 2 * lc->rmax + 1;
  /* the levels: the iris at rmax * L / (levels - 1), premultiplied, highlights boosted */
  for (L = 1; L < LB_LEVELS; L++) {
    int r = (int)floor(lc->rmax * L / (double)(LB_LEVELS - 1) + 0.5), dy;
    double cnt = 0;
    if (r < 1) r = 1;
    lens_iris_runs(r, p->blades, p->rotation, lo, hi);
    for (dy = 0; dy <= 2 * r; dy++) if (hi[dy] >= lo[dy]) cnt += hi[dy] - lo[dy] + 1;
    for (y = ob.y0; y < ob.y1; y++) {
      float *o = lv + (((long)(L - 1) * oh + (y - ob.y0)) * ow) * 4;
      memset(o, 0, (size_t)ow * 4 * sizeof(float));
      for (dy = -r; dy <= r; dy++) {
        i32 ry = y + dy;
        const u8 *row;
        if (hi[dy + r] < lo[dy + r]) continue;
        if (ry < ib.y0) ry = ib.y0;
        if (ry >= ib.y1) ry = ib.y1 - 1;
        row = in + (long)(ry - ib.y0) * iw * 4;
        for (c = 0; c < 4; c++) pre[c] = 0;
        for (x = 0; x < iw; x++) {
          const u8 *q = row + x * 4;
          double boost = p->brightness > 0 && (q[0] * 77 + q[1] * 150 + q[2] * 29) / 256 >= p->threshold ? 1 + p->brightness / 100.0 : 1;
          for (c = 0; c < 3; c++) pre[(x + 1) * 4 + c] = pre[x * 4 + c] + q[c] * q[3] / 255.0 * boost;
          pre[(x + 1) * 4 + 3] = pre[x * 4 + 3] + q[3];
        }
        for (x = ob.x0; x < ob.x1; x++) {
          i32 a = x - ib.x0 + lo[dy + r], b = x - ib.x0 + hi[dy + r] + 1;
          float *t = o + (x - ob.x0) * 4;
          /* (past the band's ends the edge pixels repeat) */
          int extra_l = a < 0 ? -a : 0, extra_r = b > iw ? b - iw : 0;
          if (a < 0) a = 0;
          if (b > iw) b = iw;
          for (c = 0; c < 4; c++) {
            double v = b > a ? pre[b * 4 + c] - pre[a * 4 + c] : 0;
            if (extra_l) v += extra_l * (pre[4 + c] - pre[c]);
            if (extra_r) v += extra_r * (pre[iw * 4 + c] - pre[(iw - 1) * 4 + c]);
            t[c] += (float)v;
          }
        }
      }
      for (x = 0; x < ow * 4; x++) o[x] = (float)(o[x] / cnt);
    }
    if (progress(L, LB_LEVELS)) break;
  }
  /* each pixel: between the levels either side of its depth's blur */
  for (y = ob.y0; y < ob.y1; y++)
    for (x = ob.x0; x < ob.x1; x++) {
      const u8 *q = in + ((long)(y - ib.y0) * iw + (x - ib.x0)) * 4;
      u8 *o = out + ((long)(y - ob.y0) * ow + (x - ob.x0)) * 4;
      double dep = lc->depth ? lc->depth[(long)y * d->w + x] : 255, f, a[4], b[4];
      int lo_l;
      if (p->invert) dep = 255 - dep;
      f = lc->depth ? fabs(dep - p->focal) / 255.0 * (LB_LEVELS - 1) : LB_LEVELS - 1;
      lo_l = (int)floor(f);
      if (lo_l >= LB_LEVELS - 1) { lo_l = LB_LEVELS - 2; f = LB_LEVELS - 1; }
      f -= lo_l;
      for (c = 0; c < 4; c++) {
        if (lo_l == 0) a[c] = c < 3 ? q[c] * q[3] / 255.0 : q[3];
        else a[c] = lv[(((long)(lo_l - 1) * oh + (y - ob.y0)) * ow + (x - ob.x0)) * 4 + c];
        b[c] = lv[(((long)lo_l * oh + (y - ob.y0)) * ow + (x - ob.x0)) * 4 + c];
      }
      {
        double al = a[3] + (b[3] - a[3]) * f;
        o[3] = (u8)clamp255(al);
        for (c = 0; c < 3; c++) o[c] = al > 0.5 ? (u8)clamp255((a[c] + (b[c] - a[c]) * f) * 255 / al) : 0;
      }
      if (lo_l == 0 && f == 0) memcpy(o, q, 4);
    }
  free(pre); free(lv); free(lo);
  return 1;
}
int flt_lens_blur_ex(Doc *d, const LensBlurParams *pin)
{
  LensBlurParams p = *pin;
  LbCtx lc;
  Layer *l;
  u8 *alpha = NULL;
  int r;
  p.radius = clampi(p.radius, 1, 100); p.blades = p.blades ? clampi(p.blades, 3, 8) : 0;
  p.focal = clampi(p.focal, 0, 255); p.brightness = clampi(p.brightness, 0, 100); p.threshold = clampi(p.threshold, 0, 255);
  if (d->active < 0 || d->active >= d->nlay) return 0;
  l = d->lay[d->active];
  lc.p = &p; lc.rmax = p.radius; lc.depth = NULL;
  if (p.depth == LBD_MASK) lc.depth = l->mask;
  else if (p.depth == LBD_CHANNEL && p.channel >= 0 && p.channel < d->nchan) lc.depth = d->chan[p.channel];
  else if (p.depth == LBD_TRANSPARENCY && l->px) {
    long k, n = (long)d->w * d->h;
    if (!(alpha = (u8 *)malloc((size_t)n))) return -1;
    for (k = 0; k < n; k++) alpha[k] = l->px[k * 4 + 3];
    lc.depth = alpha;
  }
  if (p.depth != LBD_NONE && !lc.depth) { free(alpha); return 0; }
  r = band_run(d, "Lens Blur", p.radius + 1, lb_band, &lc);
  free(alpha);
  return r;
}

/* ------------------------------------------------------------------ Path Blur */
#define PB_CELL 4
typedef struct { i32 gw, gh; float *dir; int speed, taper, centered; } PbCtx;
static int pb_band(void *vc, Doc *d, const u8 *in, Box ib, u8 *out, Box ob)
{
  PbCtx *pc = (PbCtx *)vc;
  i32 iw = ib.x1 - ib.x0, ih = ib.y1 - ib.y0, ow = ob.x1 - ob.x0, x, y;
  int n = pc->speed, k, c;
  (void)d;
  for (y = ob.y0; y < ob.y1; y++) {
    if (progress(y - ob.y0, ob.y1 - ob.y0)) return 1;
    for (x = ob.x0; x < ob.x1; x++) {
      long gi = (long)(y / PB_CELL < pc->gh ? y / PB_CELL : pc->gh - 1) * pc->gw + (x / PB_CELL < pc->gw ? x / PB_CELL : pc->gw - 1);
      double tx = pc->dir[gi * 2], ty = pc->dir[gi * 2 + 1], acc[4] = { 0, 0, 0, 0 }, ws = 0;
      u8 *o = out + ((long)(y - ob.y0) * ow + (x - ob.x0)) * 4;
      for (k = 0; k <= n; k++) {
        /* the samples along the direction: centred on the pixel, or behind it */
        double t = pc->centered ? k - n / 2.0 : -(double)k, w = 1 - pc->taper / 100.0 * fabs(t) / (n > 0 ? (pc->centered ? n / 2.0 : n) : 1);
        double sx = x - ib.x0 + tx * t, sy = y - ib.y0 + ty * t, fx, fy;
        i32 x0, y0;
        if (w <= 0) continue;
        sx = clampd(sx, 0, iw - 1); sy = clampd(sy, 0, ih - 1);
        x0 = (i32)sx; y0 = (i32)sy; fx = sx - x0; fy = sy - y0;
        {
          i32 x1 = x0 + 1 < iw ? x0 + 1 : iw - 1, y1 = y0 + 1 < ih ? y0 + 1 : ih - 1;
          const u8 *q00 = in + ((long)y0 * iw + x0) * 4, *q10 = in + ((long)y0 * iw + x1) * 4, *q01 = in + ((long)y1 * iw + x0) * 4, *q11 = in + ((long)y1 * iw + x1) * 4;
          double w00 = (1 - fx) * (1 - fy) * w, w10 = fx * (1 - fy) * w, w01 = (1 - fx) * fy * w, w11 = fx * fy * w;
          for (c = 0; c < 3; c++) acc[c] += (q00[c] * q00[3] * w00 + q10[c] * q10[3] * w10 + q01[c] * q01[3] * w01 + q11[c] * q11[3] * w11) / 255.0;
          acc[3] += q00[3] * w00 + q10[3] * w10 + q01[3] * w01 + q11[3] * w11;
        }
        ws += w;
      }
      if (ws <= 0) { memcpy(o, in + ((long)(y - ib.y0) * iw + (x - ib.x0)) * 4, 4); continue; }
      o[3] = (u8)clamp255(acc[3] / ws);
      for (c = 0; c < 3; c++) o[c] = acc[3] > 0.5 ? (u8)clamp255(acc[c] * 255 / acc[3]) : 0;
    }
  }
  return 1;
}
int flt_path_blur(Doc *d, const Path *path, int speed, int taper, int centered)
{
  PbCtx pc;
  Poly pl;
  i32 gx, gy;
  int r;
  if (!path || path->na < 2 || speed < 1) return 0;
  pc.speed = clampi(speed, 1, 500); pc.taper = clampi(taper, 0, 100); pc.centered = centered != 0;
  pc.gw = (d->w + PB_CELL - 1) / PB_CELL; pc.gh = (d->h + PB_CELL - 1) / PB_CELL;
  poly_init(&pl);
  if (!path_flatten(path, 0.5, &pl) || pl.n < 2) { poly_free(&pl); return 0; }
  if (!(pc.dir = (float *)malloc((size_t)pc.gw * pc.gh * 2 * sizeof(float)))) { poly_free(&pl); return -1; }
  /* the direction field: each cell takes the tangent of the path's nearest segment */
  for (gy = 0; gy < pc.gh; gy++) {
    if (progress(gy, pc.gh * 4L)) break;
    for (gx = 0; gx < pc.gw; gx++) {
      double x = (gx + 0.5) * PB_CELL, y = (gy + 0.5) * PB_CELL, best = 1e300, tx = 1, ty = 0;
      i32 ci, s0 = 0, i;
      for (ci = 0; ci < pl.nc; s0 = pl.end[ci], ci++) {
        i32 m = pl.end[ci] - s0, segs = pl.closed[ci] ? m : m - 1;
        for (i = 0; i < segs; i++) {
          const double *a = pl.xy + 2 * (s0 + i), *b = pl.xy + 2 * (s0 + (i + 1) % m);
          double vx = b[0] - a[0], vy = b[1] - a[1], L2 = vx * vx + vy * vy, t, px, py, dd;
          if (L2 <= 0) continue;
          t = clampd(((x - a[0]) * vx + (y - a[1]) * vy) / L2, 0, 1);
          px = a[0] + vx * t - x; py = a[1] + vy * t - y; dd = px * px + py * py;
          if (dd < best) { best = dd; tx = vx / sqrt(L2); ty = vy / sqrt(L2); }
        }
      }
      pc.dir[((long)gy * pc.gw + gx) * 2] = (float)tx; pc.dir[((long)gy * pc.gw + gx) * 2 + 1] = (float)ty;
    }
  }
  poly_free(&pl);
  if (progress_aborted) { free(pc.dir); return 0; }
  r = band_run(d, "Path Blur", pc.speed + 2, pb_band, &pc);
  free(pc.dir);
  return r;
}

/* ------------------------------------------------------------------ HDR Toning */
void hdr_defaults(HdrParams *p)
{
  memset(p, 0, sizeof *p);
  p->radius = 15; p->strength = 5; p->gamma = 100; p->exposure = 0; p->detail = 30;
  p->vibrance = 0; p->saturation = 20;
}
static double s2lin(double v) { return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
static double lin2s(double v) { return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1 / 2.4) - 0.055; }
static int hdr_band(void *vc, Doc *d, const u8 *in, Box ib, u8 *out, Box ob)
{
  const HdrParams *p = (const HdrParams *)vc;
  i32 iw = ib.x1 - ib.x0, ih = ib.y1 - ib.y0, ow = ob.x1 - ob.x0, x, y;
  long n = (long)iw * ih, k;
  float *I = (float *)malloc((size_t)n * sizeof(float) * 4), *mI, *mII, *A;
  double eps = 0.01 / (p->strength > 0 ? p->strength / 10.0 : 0.5), g = p->gamma / 100.0, ex = p->exposure / 100.0, det = 1 + p->detail / 100.0;
  int r = clampi(p->radius, 1, 500), c;
  float lut[256];
  (void)d;
  if (!I) return 0;
  mI = I + n; mII = mI + n; A = mII + n;
  for (c = 0; c < 256; c++) lut[c] = (float)s2lin(c / 255.0);
  /* the log luminance, and the guided filter's local means (He, Sun and Tang 2010: the picture
     its own guide) as the edge-aware base */
  for (k = 0; k < n; k++) {
    const u8 *q = in + k * 4;
    double L = 0.2126 * lut[q[0]] + 0.7152 * lut[q[1]] + 0.0722 * lut[q[2]];
    I[k] = (float)(log(L + 1e-4) / log(2.0) / 14.0);            /* (about 0-1 over the stops 8 bits hold) */
    mI[k] = I[k]; mII[k] = I[k] * I[k];
  }
  if (!box_plane(mI, iw, ih, r) || !box_plane(mII, iw, ih, r)) { free(I); return 0; }
  if (progress(1, 4)) { free(I); return 1; }
  for (k = 0; k < n; k++) {
    double var = mII[k] - mI[k] * mI[k], a = var / (var + eps);
    A[k] = (float)a; mII[k] = (float)(mI[k] - a * mI[k]);       /* (mII now b) */
  }
  if (!box_plane(A, iw, ih, r) || !box_plane(mII, iw, ih, r)) { free(I); return 0; }
  if (progress(2, 4)) { free(I); return 1; }
  for (y = ob.y0; y < ob.y1; y++)
    for (x = ob.x0; x < ob.x1; x++) {
      long ki = (long)(y - ib.y0) * iw + (x - ib.x0);
      const u8 *q = in + ki * 4;
      u8 *o = out + ((long)(y - ob.y0) * ow + (x - ob.x0)) * 4;
      double base = A[ki] * I[ki] + mII[ki], detail = I[ki] - base, mid = -6.0 / 14.0, l2, L, L2, ratio, rgb[3], lum, s, sat;
      /* the base's contrast by Gamma about the middle grey, the detail by Detail, then Exposure */
      l2 = (mid + (base - mid) / g + detail * det) * 14.0 + ex;
      L = 0.2126 * lut[q[0]] + 0.7152 * lut[q[1]] + 0.0722 * lut[q[2]];
      L2 = pow(2.0, l2) - 1e-4;
      if (L2 < 0) L2 = 0;
      /* Shadow and Highlight lift or lower the ends */
      s = lin2s(L2 > 1 ? 1 : L2);
      s += p->shadow / 100.0 * 0.5 * (1 - s) * (1 - s) * s * 4 * 0.5;
      s += p->highlight / 100.0 * 0.5 * s * s * (1 - s) * 4 * 0.5;
      s = clampd(s, 0, 1);
      L2 = s2lin(s);
      ratio = L > 1e-5 ? L2 / L : 0;
      for (c = 0; c < 3; c++) rgb[c] = L > 1e-5 ? lin2s(clampd(lut[q[c]] * ratio, 0, 1)) : s;
      /* Vibrance (more where the colour is dull) and Saturation */
      lum = 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2];
      {
        double mx = rgb[0] > rgb[1] ? (rgb[0] > rgb[2] ? rgb[0] : rgb[2]) : (rgb[1] > rgb[2] ? rgb[1] : rgb[2]);
        double mn = rgb[0] < rgb[1] ? (rgb[0] < rgb[2] ? rgb[0] : rgb[2]) : (rgb[1] < rgb[2] ? rgb[1] : rgb[2]);
        double chroma = mx - mn;
        sat = 1 + p->saturation / 100.0 + p->vibrance / 100.0 * (1 - chroma);
      }
      for (c = 0; c < 3; c++) o[c] = (u8)clamp255((lum + (rgb[c] - lum) * sat) * 255);
      o[3] = q[3];
    }
  free(I);
  return 1;
}
int flt_hdr_toning(Doc *d, const HdrParams *p)
{
  HdrParams q = *p;
  q.radius = clampi(q.radius, 1, 500); q.strength = clampi(q.strength, 1, 40); q.gamma = clampi(q.gamma, 10, 200);
  q.exposure = clampi(q.exposure, -500, 500); q.detail = clampi(q.detail, -100, 300);
  q.shadow = clampi(q.shadow, -100, 100); q.highlight = clampi(q.highlight, -100, 100);
  q.vibrance = clampi(q.vibrance, -100, 100); q.saturation = clampi(q.saturation, -100, 100);
  return band_run(d, "HDR Toning", 2 * q.radius + 2, hdr_band, &q);
}

/* ------------------------------------------------------------------ Oil Paint (PhotoCraft's oil.rs) */
void oil_defaults(OilParams *p)
{
  p->stylization = 23; p->cleanliness = 67; p->scale = 10; p->bristle = 30;
  p->lighting = 1; p->angle = -60; p->shine = 10;
}
static double oil_len(const OilParams *p) { return 1.5 + clampd(p->stylization / 10.0, 0.1, 10) * 2.0; }
static double oil_tsigma(const OilParams *p) { return 0.8 + clampd(p->scale / 10.0, 0.1, 10) * 0.6; }
static int oil_reach(const OilParams *p) { return (int)ceil(oil_len(p) + 3.0 * oil_tsigma(p) + 3.0 * 2.5 + 4.0); }
static int oil_band(void *vc, Doc *d, const u8 *in, Box ib, u8 *out, Box ob)
{
  const OilParams *sp = (const OilParams *)vc;
  i32 ww = ib.x1 - ib.x0, wh = ib.y1 - ib.y0, ow = ob.x1 - ob.x0, x, y;
  long n = (long)ww * wh, k;
  float *pm = (float *)malloc((size_t)n * 4 * sizeof(float)), *lum = (float *)malloc((size_t)n * sizeof(float));
  float *efg = (float *)calloc((size_t)n * 3, sizeof(float)), *tx = (float *)malloc((size_t)n * 2 * sizeof(float));
  float *pl = (float *)malloc((size_t)n * sizeof(float));
  Box r1 = box_clip(box_make(ob.x0 - 1, ob.y0 - 1, ob.x1 + 1, ob.y1 + 1), ib);
  i32 rw = r1.x1 - r1.x0, rh = r1.y1 - r1.y0;
  float *col = (float *)malloc((size_t)rw * rh * 4 * sizeof(float)), *bri = (float *)malloc((size_t)rw * rh * sizeof(float));
  double len = oil_len(sp), hstep = len / 10.0 < 1 ? 1 : len / 10.0, amp = clampd(sp->bristle / 10.0, 0, 10) / 10.0, shine = clampd(sp->shine / 10.0, 0, 10) / 10.0;
  int steps = (int)ceil(len / hstep), c, ok = 0;
  (void)d;
  if (!pm || !lum || !efg || !tx || !pl || !col || !bri) goto done;
  /* 1. premultiplied colour, cleaned by Cleanliness */
  for (k = 0; k < n; k++) {
    float a = in[k * 4 + 3] / 255.0f;
    for (c = 0; c < 3; c++) pm[k * 4 + c] = in[k * 4 + c] / 255.0f * a;
    pm[k * 4 + 3] = a;
  }
  for (c = 0; c < 4; c++) {
    for (k = 0; k < n; k++) pl[k] = pm[k * 4 + c];
    if (!gauss_plane(pl, ww, wh, clampd(sp->cleanliness / 10.0, 0, 10) * 0.25)) goto done;
    for (k = 0; k < n; k++) pm[k * 4 + c] = pl[k];
  }
  /* 2. the intensity and 3. its smoothed structure tensor */
  for (k = 0; k < n; k++) lum[k] = (pm[k * 4] + pm[k * 4 + 1] + pm[k * 4 + 2]) / 3;
  for (y = 1; y < wh - 1; y++)
    for (x = 1; x < ww - 1; x++) {
#define L(dx, dy) lum[(long)(y + (dy)) * ww + x + (dx)]
      float gx = (L(1, -1) + 2 * L(1, 0) + L(1, 1)) - (L(-1, -1) + 2 * L(-1, 0) + L(-1, 1));
      float gy = (L(-1, 1) + 2 * L(0, 1) + L(1, 1)) - (L(-1, -1) + 2 * L(0, -1) + L(1, -1));
#undef L
      long o = ((long)y * ww + x) * 3;
      efg[o] = gx * gx; efg[o + 1] = gx * gy; efg[o + 2] = gy * gy;
    }
  for (c = 0; c < 3; c++) {
    for (k = 0; k < n; k++) pl[k] = efg[k * 3 + c];
    if (!gauss_plane(pl, ww, wh, oil_tsigma(sp))) goto done;
    for (k = 0; k < n; k++) efg[k * 3 + c] = pl[k];
  }
  if (progress(1, 3)) { ok = 1; goto done; }
  /* 4. the stroke direction: the minor eigenvector (along the edges) */
  for (k = 0; k < n; k++) {
    double e = efg[k * 3], f = efg[k * 3 + 1], g = efg[k * 3 + 2];
    double l1 = 0.5 * (e + g + sqrt((e - g) * (e - g) + 4 * f * f)), vx = l1 - e, vy = -f, m = sqrt(vx * vx + vy * vy);
    if (m < 1e-6 || l1 - (e + g - l1) < 1e-6) { tx[k * 2] = tx[k * 2 + 1] = (float)(1 / sqrt(2.0)); }
    else { tx[k * 2] = (float)(vx / m); tx[k * 2 + 1] = (float)(vy / m); }
  }
  /* 5. the line integral convolution over the output box grown by one (for the relief) */
  for (y = 0; y < rh; y++) {
    if (progress(rh + y, 3L * rh)) { ok = 1; goto done; }
    for (x = 0; x < rw; x++) {
      double dx = r1.x0 - ib.x0 + x, dy = r1.y0 - ib.y0 + y, acc[4] = { 0, 0, 0, 0 }, wsum = 0, nsum = 0;
      int dir, s;
#define SAMPLE(sx, sy, wk) do { \
        double qx_ = clampd(sx, 0, ww - 1), qy_ = clampd(sy, 0, wh - 1); \
        i32 x0_ = (i32)qx_, y0_ = (i32)qy_, x1_ = x0_ + 1 < ww ? x0_ + 1 : ww - 1, y1_ = y0_ + 1 < wh ? y0_ + 1 : wh - 1; \
        double ax_ = qx_ - x0_, ay_ = qy_ - y0_; \
        double w00 = (1 - ax_) * (1 - ay_) * (wk), w10 = ax_ * (1 - ay_) * (wk), w01 = (1 - ax_) * ay_ * (wk), w11 = ax_ * ay_ * (wk); \
        int c_; \
        for (c_ = 0; c_ < 4; c_++) acc[c_] += pm[((long)y0_ * ww + x0_) * 4 + c_] * w00 + pm[((long)y0_ * ww + x1_) * 4 + c_] * w10 + \
                                             pm[((long)y1_ * ww + x0_) * 4 + c_] * w01 + pm[((long)y1_ * ww + x1_) * 4 + c_] * w11; \
      } while (0)
      SAMPLE(dx, dy, 1.0);
      wsum += 1; nsum += hash01(ib.x0 + (i32)dx, ib.y0 + (i32)dy, 71);
      for (dir = 0; dir < 2; dir++) {
        double qx = dx, qy = dy, px = 0, py = 0;
        for (s = 1; s <= steps; s++) {
          long ti = (long)clampi((int)floor(qy + 0.5), 0, wh - 1) * ww + clampi((int)floor(qx + 0.5), 0, ww - 1);
          double ux = tx[ti * 2], uy = tx[ti * 2 + 1], wk;
          if (s == 1) { if (dir) { ux = -ux; uy = -uy; } }
          else if (ux * px + uy * py < 0) { ux = -ux; uy = -uy; }
          px = ux; py = uy;
          qx += ux * hstep; qy += uy * hstep;
          wk = 1.0 - (double)s / (steps + 1);
          SAMPLE(qx, qy, wk);
          wsum += wk;
          nsum += wk * hash01(ib.x0 + (i32)floor(qx + 0.5), ib.y0 + (i32)floor(qy + 0.5), 71);
        }
      }
#undef SAMPLE
      for (c = 0; c < 4; c++) col[((long)y * rw + x) * 4 + c] = (float)(acc[c] / wsum);
      bri[(long)y * rw + x] = (float)(nsum / wsum - 0.5);
    }
  }
  /* 6. the relief, lit */
  {
    double ls = sin(sp->angle * M_PI / 180), lc = cos(sp->angle * M_PI / 180), elev = 45 * M_PI / 180;
    double light[3], half[3], m, flat;
    light[0] = lc * cos(elev); light[1] = -ls * cos(elev); light[2] = sin(elev);
    half[0] = light[0]; half[1] = light[1]; half[2] = light[2] + 1;
    m = sqrt(half[0] * half[0] + half[1] * half[1] + half[2] * half[2]);
    half[0] /= m; half[1] /= m; half[2] /= m;
    flat = pow(half[2], 40);
#define HT(xx, yy) ((col[((long)(yy) * rw + (xx)) * 4] + col[((long)(yy) * rw + (xx)) * 4 + 1] + col[((long)(yy) * rw + (xx)) * 4 + 2]) / 3 * 2.0 + bri[(long)(yy) * rw + (xx)] * amp * 6.0)
    for (y = ob.y0; y < ob.y1; y++)
      for (x = ob.x0; x < ob.x1; x++) {
        i32 rx = x - r1.x0, ry = y - r1.y0;
        const float *q = col + ((long)ry * rw + rx) * 4;
        double v[4];
        u8 *o = out + ((long)(y - ob.y0) * ow + (x - ob.x0)) * 4;
        for (c = 0; c < 3; c++) v[c] = q[3] > 1e-6 ? q[c] / q[3] : 0;
        if (sp->lighting) {
          double hx = HT(rx + 1 < rw ? rx + 1 : rw - 1, ry) - HT(rx > 0 ? rx - 1 : 0, ry);
          double hy = HT(rx, ry + 1 < rh ? ry + 1 : rh - 1) - HT(rx, ry > 0 ? ry - 1 : 0);
          double nx = -hx * 0.5, ny = -hy * 0.5, nm = sqrt(nx * nx + ny * ny + 1);
          double ndl = (nx * light[0] + ny * light[1] + light[2]) / nm, ndh = (nx * half[0] + ny * half[1] + half[2]) / nm, shade, spv;
          if (ndh < 0) ndh = 0;
          shade = 1 + (ndl - light[2]) * 1.2;
          spv = shine * (pow(ndh, 40) - flat > 0 ? pow(ndh, 40) - flat : 0) * 0.8;
          for (c = 0; c < 3; c++) v[c] = v[c] * shade + spv;
        }
        for (c = 0; c < 3; c++) o[c] = (u8)clamp255(v[c] * 255);
        o[3] = in[((long)(y - ib.y0) * ww + (x - ib.x0)) * 4 + 3];       /* (the layer's own alpha) */
      }
#undef HT
  }
  ok = 1;
done:
  free(pm); free(lum); free(efg); free(tx); free(pl); free(col); free(bri);
  return ok;
}
int flt_oil_paint_ex(Doc *d, const OilParams *p)
{
  OilParams q = *p;
  q.stylization = clampi(q.stylization, 1, 100); q.cleanliness = clampi(q.cleanliness, 0, 100); q.scale = clampi(q.scale, 1, 100);
  q.bristle = clampi(q.bristle, 0, 100); q.shine = clampi(q.shine, 0, 100);
  return band_run(d, "Oil Paint", oil_reach(&q), oil_band, &q);
}

/* ------------------------------------------------------------------ the renderers' primitives */
/* a tapered capsule from a to b (radii ra, rb; colour c0 to c1 along it), or an ellipse at a
   (radii ra, rb, turned by angle; c0 at its middle, c1 at its edge); straight RGBA 0-1, blended
   normally or added (light) */
enum { PR_CAPSULE, PR_ELLIPSE };
typedef struct { u8 kind, add; float ax, ay, bx, by, ra, rb, angle; float c0[4], c1[4]; } Prim;
typedef struct { Prim *p; long n, cap; int oom; } PrimList;
static Prim *prim_new(PrimList *l)
{
  if (l->n >= l->cap) {
    long nc = l->cap ? l->cap * 2 : 256;
    Prim *np;
    if (nc > 400000 || !(np = (Prim *)realloc(l->p, (size_t)nc * sizeof(Prim)))) { l->oom = 1; return NULL; }
    l->p = np; l->cap = nc;
  }
  memset(&l->p[l->n], 0, sizeof(Prim));
  return &l->p[l->n++];
}
static u32 shade(u32 c, double f);
static void rgbaf(u32 c, double a, float *o) { o[0] = ((c >> 16) & 255) / 255.0f; o[1] = ((c >> 8) & 255) / 255.0f; o[2] = (c & 255) / 255.0f; o[3] = (float)a; }
static void capsule(PrimList *l, double ax, double ay, double bx, double by, double ra, double rb, const float *c0, const float *c1, int add)
{
  Prim *p = prim_new(l);
  if (!p) return;
  p->kind = PR_CAPSULE; p->add = (u8)add;
  p->ax = (float)ax; p->ay = (float)ay; p->bx = (float)bx; p->by = (float)by; p->ra = (float)ra; p->rb = (float)rb;
  memcpy(p->c0, c0, sizeof p->c0); memcpy(p->c1, c1 ? c1 : c0, sizeof p->c1);
}
static void ellipse(PrimList *l, double cx, double cy, double rx, double ry, double angle, const float *c0, const float *c1, int add)
{
  Prim *p = prim_new(l);
  if (!p) return;
  p->kind = PR_ELLIPSE; p->add = (u8)add;
  p->ax = (float)cx; p->ay = (float)cy; p->ra = (float)(rx > 0.3 ? rx : 0.3); p->rb = (float)(ry > 0.3 ? ry : 0.3); p->angle = (float)angle;
  memcpy(p->c0, c0, sizeof p->c0); memcpy(p->c1, c1 ? c1 : c0, sizeof p->c1);
}
static Box prim_box(const Prim *p)
{
  double r = (p->ra > p->rb ? p->ra : p->rb) + 1;
  if (p->kind == PR_ELLIPSE) return box_make((i32)floor(p->ax - r), (i32)floor(p->ay - r), (i32)ceil(p->ax + r) + 1, (i32)ceil(p->ay + r) + 1);
  return box_make((i32)floor((p->ax < p->bx ? p->ax : p->bx) - r), (i32)floor((p->ay < p->by ? p->ay : p->by) - r),
                  (i32)ceil((p->ax > p->bx ? p->ax : p->bx) + r) + 1, (i32)ceil((p->ay > p->by ? p->ay : p->by) + r) + 1);
}
/* the primitive's coverage (0-1) and colour at pixel centre x, y */
static double prim_at(const Prim *p, double x, double y, float *col)
{
  double t, cov;
  int c;
  if (p->kind == PR_CAPSULE) {
    double vx = p->bx - p->ax, vy = p->by - p->ay, L2 = vx * vx + vy * vy, dx, dy, r;
    t = L2 > 1e-9 ? ((x - p->ax) * vx + (y - p->ay) * vy) / L2 : 0;
    t = clampd(t, 0, 1);
    dx = x - (p->ax + vx * t); dy = y - (p->ay + vy * t);
    r = p->ra + (p->rb - p->ra) * t;
    cov = clampd(r - sqrt(dx * dx + dy * dy) + 0.5, 0, 1);
    if (r < 0.5) cov *= r * 2;                                /* (thin strokes fade rather than alias) */
  } else {
    double ca = cos(p->angle), sa = sin(p->angle), u = (x - p->ax) * ca + (y - p->ay) * sa, v = -(x - p->ax) * sa + (y - p->ay) * ca;
    double rho = sqrt(u * u / ((double)p->ra * p->ra) + v * v / ((double)p->rb * p->rb)), mr = p->ra < p->rb ? p->ra : p->rb;
    cov = clampd((1 - rho) * mr + 0.5, 0, 1);
    t = clampd(rho, 0, 1);
  }
  if (cov <= 0) return 0;
  for (c = 0; c < 4; c++) col[c] = (float)(p->c0[c] + (p->c1[c] - p->c0[c]) * t);
  return cov * col[3];
}
/* the list drawn onto the active layer through the selection, band by band; one step. The
   primitives are drawn into a transparent layer of their own (premultiplied), which is then laid
   over the picture at opacity 0-1 (so overlapping parts of one stroke don't show their joins) */
static int prims_draw(Doc *d, const char *name, PrimList *pl, double opacity)
{
  Layer *l;
  Box b, all = box_make(0, 0, 0, 0);
  i32 y0, band = 32, bw;
  long i;
  u8 *buf;
  float *acc, *pic;
  if (pl->oom) return -1;
  if (!area_of(d, &l, &b)) return 0;
  for (i = 0; i < pl->n; i++) all = box_union(all, prim_box(&pl->p[i]));
  b = box_clip(b, all);
  if (box_empty(b)) return 1;
  bw = b.x1 - b.x0;
  buf = (u8 *)malloc((size_t)bw * band * 4); acc = (float *)malloc((size_t)bw * band * 4 * sizeof(float));
  pic = (float *)malloc((size_t)bw * band * 4 * sizeof(float));
  if (!buf || !acc || !pic) { free(buf); free(acc); free(pic); return -1; }
  doc_begin(d, name);
  doc_note_layer(d, d->active, b);
  for (y0 = b.y0; y0 < b.y1 && !progress(y0 - b.y0, b.y1 - b.y0); y0 += band) {
    Box ob = box_make(b.x0, y0, b.x1, y0 + band < b.y1 ? y0 + band : b.y1);
    i32 x, y;
    long k, nn = (long)bw * (ob.y1 - ob.y0);
    memset(acc, 0, (size_t)nn * 4 * sizeof(float));
    /* (the picture's band as straight 0-1 in pic: light is added to it directly) */
    for (k = 0; k < nn; k++) {
      const u8 *src = l->px + ((long)(ob.y0 + k / bw) * d->w + ob.x0 + k % bw) * 4;
      int c;
      for (c = 0; c < 4; c++) pic[k * 4 + c] = src[c] / 255.0f;
    }
    for (i = 0; i < pl->n; i++) {
      const Prim *p = &pl->p[i];
      Box pb = box_clip(prim_box(p), ob);
      for (y = pb.y0; y < pb.y1; y++)
        for (x = pb.x0; x < pb.x1; x++) {
          float col[4], *a = acc + ((long)(y - ob.y0) * bw + (x - ob.x0)) * 4;
          double cov = prim_at(p, x + 0.5, y + 0.5, col);
          int c;
          if (cov <= 0) continue;
          if (p->add) {
            /* light added to the picture, so overlapping strands glow brighter */
            float *q = pic + ((long)(y - ob.y0) * bw + (x - ob.x0)) * 4;
            double sc = cov * opacity, na = q[3] + sc - q[3] * sc;
            for (c = 0; c < 3; c++) q[c] = (float)(na > 0 ? clampd((q[c] * q[3] + col[c] * sc) / na, 0, 1) : 0);
            q[3] = (float)na;
          } else {
            for (c = 0; c < 3; c++) a[c] = (float)(col[c] * cov + a[c] * (1 - cov));
            a[3] = (float)(cov + a[3] * (1 - cov));
          }
        }
    }
    /* that layer over the picture's band */
    for (k = 0; k < nn; k++) {
      i32 xx = ob.x0 + (i32)(k % bw), yy = ob.y0 + (i32)(k / bw);
      const float *src = pic + k * 4;
      const float *a = acc + k * 4;
      u8 *o = buf + k * 4;
      double sa = a[3] * opacity, da = src[3], na = sa + da * (1 - sa);
      int c;
      (void)xx; (void)yy;
      for (c = 0; c < 3; c++) o[c] = (u8)clamp255(na > 0 ? (a[c] * opacity + src[c] * da * (1 - sa)) * 255 / na : 0);
      o[3] = (u8)clamp255(na * 255);
    }
    band_put(d, l, buf, ob);
  }
  free(buf); free(acc); free(pic);
  doc_end(d);
  return 1;
}

/* a small random number generator (the renderers': the same seed, the same picture) */
typedef struct { u32 s; } Rng;
static double rnd(Rng *r) { r->s = r->s * 1664525u + 1013904223u; return ((r->s >> 8) & 0xFFFFFF) / 16777216.0; }
static double rsym(Rng *r) { return rnd(r) * 2 - 1; }

/* ------------------------------------------------------------------ Flame */
void flame_defaults(FlameParams *p)
{
  memset(p, 0, sizeof *p);
  p->type = FLAME_MANY_UP; p->length = 120; p->width = 30; p->interval = 40; p->angle = 0;
  p->turbulence = 40; p->jag = 20; p->lines = 12; p->style = FLS_NORMAL; p->opacity = 80; p->seed = 1;
}
/* one flame from x, y going along ux, uy (unit), length len, width w */
static void flame_one(PrimList *pl, const FlameParams *p, Rng *r, double x, double y, double ux, double uy, double len, double w)
{
  double nx = -uy, ny = ux, turb = p->turbulence / 100.0 * (p->style == FLS_VIOLENT ? 1.8 : 1), jag = p->jag / 100.0;
  double spread = p->style == FLS_FLAT ? 0.35 : 0.5, op = 1;   /* (prims_draw applies the opacity) */
  int lines = clampi(p->lines, 1, 30), k, s, segs = 14;
  float glow0[4], glow1[4];
  /* the glow at its foot */
  if (p->custom_color) { rgbaf(p->color, 0.25 * op, glow0); rgbaf(p->color, 0, glow1); }
  else { rgbaf(0xFF8020, 0.25 * op, glow0); rgbaf(0xFF2000, 0, glow1); }
  ellipse(pl, x + ux * len * 0.15, y + uy * len * 0.15, w * 0.8, w * 0.5, atan2(ny, nx), glow0, glow1, 1);
  for (k = 0; k < lines; k++) {
    double off = lines > 1 ? ((double)k / (lines - 1) - 0.5) * 2 * spread * w : 0, L = len * (0.55 + 0.45 * rnd(r)) * (p->style == FLS_FLAT ? 0.7 : 1);
    double lift = rnd(r) * w * 0.25;                          /* (the strands start at slightly different heights) */
    double phase = rnd(r) * 2 * M_PI, freq = 1.5 + rnd(r) * 2, px = x + nx * off + ux * lift, py = y + ny * off + uy * lift, rad = w / lines * 1.0 + 1;
    if (p->style == FLS_VIOLENT) L *= 0.7 + 0.6 * rnd(r);
    for (s = 1; s <= segs; s++) {
      double t = (double)s / segs, sway = turb * w * 0.6 * t * sin(phase + freq * t * 2 * M_PI) + jag * w * 0.25 * rsym(r) * t;
      double converge = -off * t * 0.8;                       /* (the strands come together at the tip) */
      double qx = x + ux * (lift + L * t) + nx * (off + converge + sway), qy = y + uy * (lift + L * t) + ny * (off + converge + sway);
      double r0 = rad * sin(M_PI * (0.15 + 0.85 * (t - 1.0 / segs))), r1 = rad * sin(M_PI * (0.15 + 0.85 * t));   /* (swelling, then tapering to the tip) */
      float c0[4], c1[4];
      double a0 = op * (1 - (t - 1.0 / segs)) * 0.5, a1 = op * (1 - t) * 0.5;
      if (p->custom_color) { rgbaf(p->color, a0, c0); rgbaf(p->color, a1, c1); }
      else {
        /* white-yellow at the core, orange, then red at the tips */
        double tt = t - 1.0 / segs;
        u32 ca = tt < 0.15 ? 0xFFE080 : tt < 0.45 ? 0xFFB030 : tt < 0.75 ? 0xFF6810 : 0xD02000;
        u32 cb = t < 0.15 ? 0xFFE080 : t < 0.45 ? 0xFFB030 : t < 0.75 ? 0xFF6810 : 0xD02000;
        rgbaf(ca, a0, c0); rgbaf(cb, a1, c1);
      }
      capsule(pl, px, py, qx, qy, r0, r1, c0, c1, 1);
      px = qx; py = qy;
    }
  }
}
int flt_flame(Doc *d, const Path *path, const FlameParams *pin)
{
  FlameParams p = *pin;
  PrimList pl;
  Poly poly;
  Rng r;
  i32 c, s = 0;
  int ok;
  memset(&pl, 0, sizeof pl);
  p.length = clampi(p.length, 4, 4000); p.width = clampi(p.width, 2, 1000); p.interval = clampi(p.interval, 2, 2000);
  p.turbulence = clampi(p.turbulence, 0, 100); p.jag = clampi(p.jag, 0, 100); p.opacity = clampi(p.opacity, 1, 100);
  r.s = p.seed * 2654435761u + 7;
  poly_init(&poly);
  if (path && path->na >= 2) { if (!path_flatten(path, 0.5, &poly)) { poly_free(&poly); return -1; } }
  else {
    poly_add(&poly, d->w * 0.15, d->h * 0.85); poly_add(&poly, d->w * 0.85, d->h * 0.85); poly_end(&poly, 0);
  }
  for (c = 0; c < poly.nc; s = poly.end[c], c++) {
    i32 n = poly.end[c] - s, i;
    const double *xy = poly.xy + 2 * s;
    double total = 0, next = 0, along = 0;
    if (n < 2) continue;
    for (i = 1; i < n; i++) total += hypot(xy[2 * i] - xy[2 * i - 2], xy[2 * i + 1] - xy[2 * i - 1]);
    if (total < 1) continue;
    if (p.type == FLAME_ONE_ALONG) {
      /* one flame whose spine is the path: strands follow it, widest at its start */
      int k, lines = clampi(p.lines, 1, 30);
      for (k = 0; k < lines; k++) {
        double off = lines > 1 ? ((double)k / (lines - 1) - 0.5) * p.width : 0, ph = rnd(&r) * 6.28, prevx = 0, prevy = 0, t;
        int first = 1;
        along = 0;
        for (i = 1; i < n; i++) {
          double ax = xy[2 * i - 2], ay = xy[2 * i - 1], bx = xy[2 * i], by = xy[2 * i + 1], sl = hypot(bx - ax, by - ay), q;
          if (sl < 1e-6) continue;
          for (q = 0; q < sl; q += 4) {
            double tx = (bx - ax) / sl, ty = (by - ay) / sl, X, Y, rad, a;
            float c0[4];
            t = (along + q) / total;
            X = ax + tx * q - ty * (off * (1 - t * 0.8) + p.turbulence / 100.0 * p.width * 0.3 * sin(ph + t * 20));
            Y = ay + ty * q + tx * (off * (1 - t * 0.8) + p.turbulence / 100.0 * p.width * 0.3 * sin(ph + t * 20));
            rad = (p.width / (double)lines * 1.2 + 1) * (1 - t * 0.85);
            a = 1;
            if (p.custom_color) rgbaf(shade(p.color, 1.2 - fabs(off) / (p.width + 1.0)), a, c0);
            else {
              /* hot in the middle and at the start, red at the sides and the end */
              double heat = (1 - fabs(off) * 2 / (p.width + 1.0)) * (1 - t * 0.8);
              rgbaf(heat > 0.6 ? 0xFFF0A0 : heat > 0.4 ? 0xFFC040 : heat > 0.2 ? 0xFF7818 : 0xD83010, a, c0);
            }
            if (!first) capsule(&pl, prevx, prevy, X, Y, rad, rad, c0, c0, 0);
            prevx = X; prevy = Y; first = 0;
          }
          along += sl;
        }
      }
      continue;
    }
    /* flames every interval along the path */
    along = 0; next = p.interval / 2.0;
    for (i = 1; i < n; i++) {
      double ax = xy[2 * i - 2], ay = xy[2 * i - 1], bx = xy[2 * i], by = xy[2 * i + 1], sl = hypot(bx - ax, by - ay);
      if (sl < 1e-6) continue;
      while (next <= along + sl) {
        double t = (next - along) / sl, X = ax + (bx - ax) * t, Y = ay + (by - ay) * t, ux, uy;
        if (p.type == FLAME_MANY_ALONG) { ux = (by - ay) / sl; uy = -(bx - ax) / sl; }          /* (to the path's left) */
        else { double a = p.angle * M_PI / 180; ux = sin(a); uy = -cos(a); }
        flame_one(&pl, &p, &r, X, Y, ux, uy, p.length, p.width);
        next += p.interval;
      }
      along += sl;
    }
  }
  poly_free(&poly);
  ok = prims_draw(d, "Flame", &pl, p.opacity / 100.0);
  free(pl.p);
  return ok;
}

/* ------------------------------------------------------------------ Tree */
const char *const tree_names[TREE_N] = { "Oak", "Pine", "Maple", "Birch", "Willow", "Palm", "Bush", "Cherry Blossom", "Bare Tree", "Cypress" };
typedef struct { int depth, nb; double spread, ratio, trunk, droop, thick; int leaf; u32 leafc, barkc; double leafn, leafs; } TreeType;
static const TreeType tree_types[TREE_N] = {
  /* depth branches spread ratio trunk droop thick leaf leafcolour bark   leaves size */
  { 6, 3, 34, 0.72, 0.34, 0.00, 0.045, 0, 0x3C7A28, 0x5A3C22, 6, 1.0 },   /* oak */
  { 0, 0, 0, 0, 0, 0, 0.030, 1, 0x1F4A28, 0x4A3020, 0, 1.0 },             /* pine (its own) */
  { 6, 2, 30, 0.78, 0.30, 0.00, 0.040, 0, 0xC84A1E, 0x5A3A26, 7, 1.1 },   /* maple, in autumn */
  { 6, 2, 22, 0.76, 0.44, 0.00, 0.028, 0, 0x8FBF40, 0xE2DED2, 5, 0.7 },   /* birch: white bark */
  { 5, 3, 40, 0.70, 0.34, 0.80, 0.045, 2, 0x7FA840, 0x5C4630, 9, 1.0 },   /* willow: hanging */
  { 0, 0, 0, 0, 0, 0, 0.035, 3, 0x3E8A30, 0x7A6040, 0, 1.0 },             /* palm (its own) */
  { 5, 3, 55, 0.70, 0.06, 0.00, 0.030, 0, 0x2E6B2E, 0x4A3A28, 7, 1.0 },   /* bush */
  { 6, 3, 40, 0.70, 0.30, 0.00, 0.040, 0, 0xF2B6C8, 0x4A3030, 8, 0.8 },   /* cherry blossom */
  { 6, 2, 40, 0.72, 0.34, 0.00, 0.040, -1, 0x000000, 0x4A3A30, 0, 0.0 },  /* bare */
  { 5, 2, 12, 0.86, 0.12, 0.00, 0.030, 0, 0x24502A, 0x4A3A28, 6, 0.9 },   /* cypress */
};
void tree_defaults(TreeParams *p)
{
  memset(p, 0, sizeof *p);
  p->type = TREE_OAK; p->base_x = 50; p->size = 80; p->branch_height = 100; p->thickness = 100; p->leaves = 100; p->leaf_size = 100;
  p->light = -45; p->seed = 1;
  p->leaf_color = 0x3C7A28; p->branch_color = 0x5A3C22;
}
static u32 shade(u32 c, double f)
{
  int r = clamp255(((c >> 16) & 255) * f), g = clamp255(((c >> 8) & 255) * f), b = clamp255((c & 255) * f);
  return (u32)r << 16 | (u32)g << 8 | (u32)b;
}
typedef struct { PrimList *pl; const TreeParams *p; const TreeType *t; Rng *r; double size, lx; u32 leafc, barkc; } TreeCtx;
static void leaf_cluster(TreeCtx *tc, double x, double y, double ang)
{
  int n = (int)(tc->t->leafn * tc->p->leaves / 100.0 + 0.5), k;
  double ls = tc->size * 0.018 * tc->t->leafs * tc->p->leaf_size / 100.0;
  for (k = 0; k < n; k++) {
    double ox = rsym(tc->r) * ls * 2.2, oy = rsym(tc->r) * ls * 2.2, f = 0.85 + 0.3 * rnd(tc->r) + tc->lx * ox / (ls * 6 + 1e-6) * 0.25;
    float c0[4], c1[4];
    rgbaf(shade(tc->leafc, f * 1.1), 1, c0); rgbaf(shade(tc->leafc, f * 0.8), 1, c1);
    if (tc->t->leaf == 2) {
      /* hanging strands (willow) */
      double L = ls * (3 + 4 * rnd(tc->r));
      capsule(tc->pl, x + ox, y + oy, x + ox + rsym(tc->r) * ls * 0.5, y + oy + L, ls * 0.25, ls * 0.12, c0, c1, 0);
    } else ellipse(tc->pl, x + ox, y + oy, ls * (0.8 + 0.5 * rnd(tc->r)), ls * (0.5 + 0.3 * rnd(tc->r)), ang + rsym(tc->r) * 1.5, c0, c1, 0);
  }
}
static void branch(TreeCtx *tc, double x, double y, double ang, double len, double th, int depth)
{
  const TreeType *t = tc->t;
  double ex, ey;
  float c0[4], c1[4];
  int k;
  if (tc->pl->oom) return;
  if (t->droop > 0 && depth < t->depth - 1) ang += (ang > 0 ? 1 : -1) * t->droop * 0.35;   /* (willow: the twigs bend outwards and down) */
  ex = x + sin(ang) * len; ey = y - cos(ang) * len;
  rgbaf(shade(tc->barkc, 1 + 0.15 * tc->lx * sin(ang)), 1, c0); rgbaf(shade(tc->barkc, 0.85 + 0.15 * tc->lx * sin(ang)), 1, c1);
  capsule(tc->pl, x, y, ex, ey, th, th * 0.72, c0, c1, 0);
  if (depth <= 0 || len < 1.5) { if (t->leaf >= 0) leaf_cluster(tc, ex, ey, ang); return; }
  for (k = 0; k < t->nb; k++) {
    double a = ang + (t->nb > 1 ? (k - (t->nb - 1) / 2.0) / ((t->nb - 1) / 2.0) : 0) * t->spread * M_PI / 180 + rsym(tc->r) * t->spread * 0.35 * M_PI / 180;
    branch(tc, ex, ey, a, len * t->ratio * (0.8 + 0.35 * rnd(tc->r)), th * 0.68, depth - 1);
  }
  if (depth < t->depth - 1 && t->leaf >= 0 && rnd(tc->r) < 0.4) leaf_cluster(tc, ex, ey, ang);
}
int flt_tree(Doc *d, const TreeParams *pin)
{
  TreeParams p = *pin;
  PrimList pl;
  Rng r;
  TreeCtx tc;
  const TreeType *t;
  double bx, by, size, th;
  float c0[4], c1[4];
  int ok, k;
  memset(&pl, 0, sizeof pl);
  p.type = clampi(p.type, 0, TREE_N - 1); p.size = clampi(p.size, 5, 100); p.base_x = clampi(p.base_x, 0, 100);
  p.branch_height = clampi(p.branch_height, 10, 300); p.thickness = clampi(p.thickness, 10, 300);
  p.leaves = clampi(p.leaves, 0, 300); p.leaf_size = clampi(p.leaf_size, 10, 300);
  t = &tree_types[p.type];
  r.s = p.seed * 2654435761u + 17;
  size = d->h * p.size / 100.0; bx = d->w * p.base_x / 100.0; by = d->h;
  th = size * t->thick * p.thickness / 100.0 / 2;
  tc.pl = &pl; tc.p = &p; tc.t = t; tc.r = &r; tc.size = size; tc.lx = sin(p.light * M_PI / 180);
  tc.leafc = p.custom_colors ? p.leaf_color & 0xFFFFFF : t->leafc; tc.barkc = p.custom_colors ? p.branch_color & 0xFFFFFF : t->barkc;
  if (p.type == TREE_PINE) {
    /* a straight trunk with tiers of drooping branches, shorter towards the top, covered in needles */
    double top = by - size, start = by - size * 0.18 * p.branch_height / 100.0;
    int tiers = 14, n;
    rgbaf(tc.barkc, 1, c0); rgbaf(shade(tc.barkc, 0.8), 1, c1);
    capsule(&pl, bx, by, bx, top, th, th * 0.15, c0, c1, 0);
    for (k = 0; k < tiers; k++) {
      double f = (double)k / tiers, y = start + (top - start) * f, L = size * 0.32 * (1 - f) + size * 0.03;
      int side;
      for (side = -1; side <= 1; side += 2) {
        double ex = bx + side * L, ey = y + L * 0.25;
        rgbaf(tc.barkc, 1, c0);
        capsule(&pl, bx, y, ex, ey, th * 0.25 * (1 - f) + 0.6, 0.5, c0, c0, 0);
        for (n = 0; n < (int)(10 * p.leaves / 100.0); n++) {
          double u = rnd(&r), px = bx + side * L * u, py = y + L * 0.25 * u, nl = size * 0.03 * p.leaf_size / 100.0 * (1.2 - u * 0.5);
          rgbaf(shade(tc.leafc, 0.85 + 0.3 * rnd(&r) + 0.2 * tc.lx * side), 1, c0);
          capsule(&pl, px, py, px + side * nl * 0.4 + rsym(&r) * nl * 0.3, py + nl * (0.6 + 0.4 * rnd(&r)), nl * 0.22, nl * 0.08, c0, c0, 0);
        }
      }
    }
  } else if (p.type == TREE_PALM) {
    /* a leaning, curving trunk and a crown of drooping fronds */
    double x = bx, y = by, ang = 0.12 * (rnd(&r) < 0.5 ? -1 : 1), seg = size * 0.75 / 12;
    for (k = 0; k < 12; k++) {
      double nx = x + sin(ang) * seg, ny = y - cos(ang) * seg;
      rgbaf(shade(tc.barkc, k & 1 ? 0.9 : 1.05), 1, c0);
      capsule(&pl, x, y, nx, ny, th * (1 - k * 0.04), th * (1 - (k + 1) * 0.04), c0, c0, 0);
      x = nx; y = ny; ang *= 1.08;
    }
    for (k = 0; k < 9; k++) {
      double a = -M_PI * 0.9 + k * M_PI * 1.8 / 8 + rsym(&r) * 0.15, L = size * 0.3 * (0.8 + 0.3 * rnd(&r)) * p.leaf_size / 100.0, px = x, py = y, aa = a;
      int s;
      for (s = 0; s < 10; s++) {
        double qx = px + sin(aa) * L / 10, qy = py - cos(aa) * L / 10, w = size * 0.012 * (1 - s / 12.0);
        int q;
        rgbaf(shade(tc.leafc, 0.9 + 0.2 * tc.lx * sin(aa)), 1, c0);
        capsule(&pl, px, py, qx, qy, w * 0.4, w * 0.4, c0, c0, 0);
        for (q = -1; q <= 1; q += 2) if (p.leaves > 0) capsule(&pl, qx, qy, qx + cos(aa) * q * L * 0.12 * (1 - s / 11.0), qy + sin(aa) * q * L * 0.12 * (1 - s / 11.0) + L * 0.04, w * 0.35, w * 0.1, c0, c0, 0);
        px = qx; py = qy; aa += (aa > 0 ? 1 : -1) * 0.12;      /* (drooping) */
      }
    }
  } else {
    double trunk = size * t->trunk * p.branch_height / 100.0, rest = (size - trunk) * (p.type == TREE_BUSH ? 0.55 : 1);
    rgbaf(tc.barkc, 1, c0); rgbaf(shade(tc.barkc, 0.85), 1, c1);
    capsule(&pl, bx, by, bx, by - trunk, th, th * 0.85, c0, c1, 0);
    /* the crown: the branches' lengths add up to the rest of the height */
    {
      double sum = 0, f = 1;
      for (k = 0; k < t->depth; k++) { sum += f; f *= t->ratio; }
      for (k = 0; k < t->nb; k++) {
        double a = (t->nb > 1 ? (k - (t->nb - 1) / 2.0) / ((t->nb - 1) / 2.0) : 0) * t->spread * 0.7 * M_PI / 180 + rsym(&r) * 0.1;
        branch(&tc, bx, by - trunk, a, rest / sum * (0.9 + 0.2 * rnd(&r)), th * 0.8, t->depth - 1);
      }
    }
  }
  ok = prims_draw(d, "Tree", &pl, 1);
  free(pl.p);
  return ok;
}

/* ------------------------------------------------------------------ Picture Frame */
const char *const frame_names[PF_N] = { "Vine", "Flowers", "Leaves", "Dots", "Waves", "Stars", "Simple Border" };
void frame_defaults(FrameParams *p, i32 w, i32 h)
{
  i32 m = w < h ? w : h;
  memset(p, 0, sizeof *p);
  p->kind = PF_VINE; p->margin = m / 12 > 4 ? m / 12 : 4; p->size = m / 20 > 3 ? m / 20 : 3; p->count = 12; p->arrangement = 30;
  p->vine_color_on = 1; p->vine_color = 0x3A6A2A; p->flower_color = 0xD04070; p->leaf_color = 0x4E9A3A; p->seed = 1;
}
/* the point at distance s along the frame's rectangle (clockwise from the top left), and its direction */
static void frame_pt(double x0, double y0, double x1, double y1, double s, double *x, double *y, double *ux, double *uy)
{
  double w = x1 - x0, h = y1 - y0, per = 2 * (w + h);
  s = fmod(s, per); if (s < 0) s += per;
  if (s < w) { *x = x0 + s; *y = y0; *ux = 1; *uy = 0; }
  else if (s < w + h) { *x = x1; *y = y0 + s - w; *ux = 0; *uy = 1; }
  else if (s < 2 * w + h) { *x = x1 - (s - w - h); *y = y1; *ux = -1; *uy = 0; }
  else { *x = x0; *y = y1 - (s - 2 * w - h); *ux = 0; *uy = -1; }
}
static void flower(PrimList *pl, double x, double y, double r, double rot, u32 petal, u32 middle)
{
  float c0[4], c1[4];
  int k;
  rgbaf(shade(petal, 1.15), 1, c0); rgbaf(shade(petal, 0.8), 1, c1);
  for (k = 0; k < 5; k++) {
    double a = rot + k * 2 * M_PI / 5;
    ellipse(pl, x + cos(a) * r * 0.55, y + sin(a) * r * 0.55, r * 0.5, r * 0.3, a, c0, c1, 0);
  }
  rgbaf(middle, 1, c0); rgbaf(shade(middle, 0.7), 1, c1);
  ellipse(pl, x, y, r * 0.28, r * 0.28, 0, c0, c1, 0);
}
static void star(PrimList *pl, double x, double y, double r, double rot, u32 col)
{
  float c0[4];
  int k;
  rgbaf(col, 1, c0);
  for (k = 0; k < 5; k++) {
    double a = rot + k * 2 * M_PI / 5 - M_PI / 2;
    capsule(pl, x, y, x + cos(a) * r, y + sin(a) * r, r * 0.3, 0.3, c0, c0, 0);
  }
}
int flt_picture_frame(Doc *d, const FrameParams *pin)
{
  FrameParams p = *pin;
  PrimList pl;
  Rng r;
  double x0, y0, x1, y1, per, step, s, sz, var;
  float vc[4], lc[4], lc2[4];
  int ok, k, n;
  memset(&pl, 0, sizeof pl);
  p.kind = clampi(p.kind, 0, PF_N - 1); p.margin = clampi(p.margin, 0, (d->w < d->h ? d->w : d->h) / 2); p.size = clampi(p.size, 1, 1000);
  p.count = clampi(p.count, 1, 200); p.arrangement = clampi(p.arrangement, 0, 100);
  r.s = p.seed * 2654435761u + 29;
  x0 = p.margin; y0 = p.margin; x1 = d->w - p.margin; y1 = d->h - p.margin;
  if (x1 - x0 < 2 || y1 - y0 < 2) return 0;
  per = 2 * (x1 - x0 + y1 - y0);
  step = ((x1 - x0) > (y1 - y0) ? x1 - x0 : y1 - y0) / p.count;
  n = (int)(per / step + 0.5);
  if (n < 4) n = 4;
  step = per / n;
  sz = p.size; var = p.arrangement / 100.0;
  rgbaf(p.vine_color, 1, vc); rgbaf(shade(p.leaf_color, 1.1), 1, lc); rgbaf(shade(p.leaf_color, 0.75), 1, lc2);
  /* the line the ornaments sit on */
  if (p.vine_color_on || p.kind == PF_VINE || p.kind == PF_WAVES || p.kind == PF_BORDER) {
    int waves = p.kind == PF_WAVES ? 2 : 1, wv;
    for (wv = 0; wv < waves; wv++) {
      double px = 0, py = 0, sx, sy, ux, uy;
      int first = 1;
      for (s = 0; s <= per + 0.5; s += 2) {
        double off = p.kind == PF_VINE ? sz * 0.3 * sin(s / step * 2 * M_PI) : p.kind == PF_WAVES ? sz * 0.5 * sin(s / step * 2 * M_PI + wv * M_PI) : 0;
        frame_pt(x0, y0, x1, y1, s, &sx, &sy, &ux, &uy);
        sx += -uy * off; sy += ux * off;
        if (!first) capsule(&pl, px, py, sx, sy, sz * (p.kind == PF_BORDER ? 0.12 : 0.07) + 0.5, sz * (p.kind == PF_BORDER ? 0.12 : 0.07) + 0.5, vc, vc, 0);
        px = sx; py = sy; first = 0;
      }
    }
    if (p.kind == PF_BORDER) {
      /* a second, finer line inside, and squares at the corners */
      double m2 = sz * 0.6, cx[4], cy[4];
      double px = 0, py = 0, sx, sy, ux, uy;
      int first = 1;
      for (s = 0; s <= 2 * (x1 - x0 - 2 * m2 + y1 - y0 - 2 * m2) + 0.5; s += 2) {
        frame_pt(x0 + m2, y0 + m2, x1 - m2, y1 - m2, s, &sx, &sy, &ux, &uy);
        if (!first) capsule(&pl, px, py, sx, sy, sz * 0.05 + 0.4, sz * 0.05 + 0.4, vc, vc, 0);
        px = sx; py = sy; first = 0;
      }
      cx[0] = x0; cy[0] = y0; cx[1] = x1; cy[1] = y0; cx[2] = x1; cy[2] = y1; cx[3] = x0; cy[3] = y1;
      for (k = 0; k < 4; k++) {
        double h = sz * 0.4;
        capsule(&pl, cx[k] - h, cy[k] - h, cx[k] + h, cy[k] - h, sz * 0.1, sz * 0.1, vc, vc, 0);
        capsule(&pl, cx[k] + h, cy[k] - h, cx[k] + h, cy[k] + h, sz * 0.1, sz * 0.1, vc, vc, 0);
        capsule(&pl, cx[k] + h, cy[k] + h, cx[k] - h, cy[k] + h, sz * 0.1, sz * 0.1, vc, vc, 0);
        capsule(&pl, cx[k] - h, cy[k] + h, cx[k] - h, cy[k] - h, sz * 0.1, sz * 0.1, vc, vc, 0);
      }
    }
  }
  /* the ornaments */
  for (k = 0; k < n && p.kind != PF_BORDER && p.kind != PF_WAVES; k++) {
    double sx, sy, ux, uy, f = 1 + rsym(&r) * var * 0.4, rot = rsym(&r) * var * M_PI, side = k & 1 ? 1 : -1, ang;
    frame_pt(x0, y0, x1, y1, k * step + rsym(&r) * var * step * 0.2, &sx, &sy, &ux, &uy);
    ang = atan2(uy, ux);
    switch (p.kind) {
    case PF_VINE: {
      double lx = sx - uy * side * sz * 0.45, ly = sy + ux * side * sz * 0.45;
      ellipse(&pl, lx, ly, sz * 0.45 * f, sz * 0.2 * f, ang + side * 0.6 + rot * 0.3, lc, lc2, 0);
      if (k % 3 == 0) flower(&pl, sx, sy, sz * 0.4 * f, rot, p.flower_color, 0xF0D040);
      break; }
    case PF_FLOWERS: flower(&pl, sx, sy, sz * 0.5 * f, rot, p.flower_color, 0xF0D040);
      if (p.vine_color_on) ellipse(&pl, sx - uy * side * sz * 0.55 + ux * sz * 0.4, sy + ux * side * sz * 0.55 + uy * sz * 0.4, sz * 0.35, sz * 0.15, ang + side * 0.5, lc, lc2, 0);
      break;
    case PF_LEAVES: {
      int q;
      for (q = -1; q <= 1; q += 2)
        ellipse(&pl, sx - uy * q * sz * 0.35, sy + ux * q * sz * 0.35, sz * 0.45 * f, sz * 0.18 * f, ang + q * 0.7 + rot * 0.2, lc, lc2, 0);
      break; }
    case PF_DOTS: {
      float c0[4], c1[4];
      rgbaf(shade(p.flower_color, 1.2), 1, c0); rgbaf(shade(p.flower_color, 0.75), 1, c1);
      ellipse(&pl, sx, sy, sz * 0.3 * f * (k & 1 ? 0.6 : 1), sz * 0.3 * f * (k & 1 ? 0.6 : 1), 0, c0, c1, 0);
      break; }
    case PF_STARS: star(&pl, sx, sy, sz * 0.5 * f, rot, p.flower_color); break;
    default: break;
    }
  }
  ok = prims_draw(d, "Picture Frame", &pl, 1);
  free(pl.p);
  return ok;
}

/* ------------------------------------------------------------------ resampling from a whole copy */
/* a bilinear sample of orig (w x h, straight RGBA, premultiplied in the sum) at sx, sy (pixel
   centres at whole numbers); outside the canvas is transparent */
static void sample_clear(const u8 *orig, i32 w, i32 h, double sx, double sy, u8 *out)
{
  i32 ix = (i32)floor(sx), iy = (i32)floor(sy);
  double fx = sx - ix, fy = sy - iy, acc[4] = { 0, 0, 0, 0 };
  int k, c;
  if (!(sx > -2 && sy > -2 && sx < w + 1 && sy < h + 1)) { memset(out, 0, 4); return; }
  for (k = 0; k < 4; k++) {
    i32 xx = ix + (k & 1), yy = iy + (k >> 1);
    double wt = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
    const u8 *q;
    if (wt <= 0 || xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
    q = orig + ((long)yy * w + xx) * 4;
    for (c = 0; c < 3; c++) acc[c] += q[c] * q[3] * wt;
    acc[3] += q[3] * wt;
  }
  out[3] = (u8)clamp255(acc[3]);
  for (c = 0; c < 3; c++) out[c] = acc[3] > 0 ? (u8)clamp255(acc[c] / acc[3]) : 0;
}
/* a whole-layer filter: orig is a copy, out written through the selection at the end */
typedef struct { Doc *d; Layer *l; Box b; u8 *orig; } Whole;
static int whole_begin(Whole *o, Doc *d, const char *name)
{
  if (!area_of(d, &o->l, &o->b)) return 0;
  o->d = d;
  if (!(o->orig = (u8 *)malloc((size_t)d->w * d->h * 4))) return -1;
  memcpy(o->orig, o->l->px, (size_t)d->w * d->h * 4);
  doc_begin(d, name);
  doc_note_layer(d, d->active, o->b);
  return 1;
}
static void whole_end(Whole *o)
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
}

/* ------------------------------------------------------------------ Lens Correction */
void lens_defaults(LensParams *p) { memset(p, 0, sizeof *p); p->vig_mid = 50; p->scale = 100; }
void lens_map(const LensParams *p, i32 w, i32 h, double scale, double x, double y, double *sx, double *sy)
{
  double cx = w / 2.0, cy = h / 2.0, R = sqrt((double)w * w + (double)h * h) / 2, u, v, a = p->angle * M_PI / 180, ca = cos(a), sa = sin(a), t, den;
  double hp = clampi(p->hperspective, -100, 100) / 100.0 * 0.6, vp = clampi(p->vperspective, -100, 100) / 100.0 * 0.6;
  if (R < 1) R = 1;
  if (scale < 0.05) scale = 0.05;
  u = (x - cx) / R / scale; v = (y - cy) / R / scale;
  /* the picture turned clockwise by angle: the source is turned back */
  t = u * ca + v * sa; v = -u * sa + v * ca; u = t;
  /* perspective: the vertical tilts the top towards or away from the viewer, the horizontal the left side */
  den = 1 + hp * u + vp * v;
  if (den < 0.05) den = 0.05;
  *sx = cx + u / den * R; *sy = cy + v / den * R;
}
/* the source of result pixel (x, y): the transform, then the distortion */
static void lens_full(const LensParams *p, i32 w, i32 h, double scale, double x, double y, double *sx, double *sy)
{
  double cx = w / 2.0, cy = h / 2.0, R = sqrt((double)w * w + (double)h * h) / 2, k = -clampi(p->distortion, -100, 100) / 100.0 * 0.5, dx, dy, rn, f;
  lens_map(p, w, h, scale, x, y, sx, sy);
  if (R < 1) R = 1;
  dx = *sx - cx; dy = *sy - cy; rn = sqrt(dx * dx + dy * dy) / R; f = 1 + k * rn * rn;
  *sx = cx + dx * f; *sy = cy + dy * f;
}
double lens_auto_scale(const LensParams *p, i32 w, i32 h)
{
  double lo = 0.2, hi = 8;
  int it;
  /* the smallest scale with every point of the border taken from inside the picture */
  for (it = 0; it < 40; it++) {
    double s = (lo + hi) / 2;
    int k, inside = 1;
    for (k = 0; k < 64 && inside; k++) {
      double t = (k % 16) / 16.0, x, y, sx, sy;
      switch (k / 16) {
      case 0: x = t * w; y = 0; break;
      case 1: x = w; y = t * h; break;
      case 2: x = w - t * w; y = h; break;
      default: x = 0; y = h - t * h; break;
      }
      lens_full(p, w, h, s, x, y, &sx, &sy);
      if (sx < -0.01 || sy < -0.01 || sx > w + 0.01 || sy > h + 0.01) inside = 0;
    }
    if (inside) hi = s; else lo = s;
  }
  return hi;
}
int flt_lens_correction_ex(Doc *d, const LensParams *p)
{
  Whole o;
  i32 x, y, w = d->w, h = d->h;
  double cx = w / 2.0, cy = h / 2.0, R = sqrt((double)w * w + (double)h * h) / 2;
  double sr = 1 + clampi(p->fringe_rc, -100, 100) / 100.0 * 0.01, sb = 1 + clampi(p->fringe_by, -100, 100) / 100.0 * 0.01;
  double vg = clampi(p->vig_amount, -100, 100) / 100.0, mid = clampi(p->vig_mid, 0, 100) / 100.0;
  double sc = p->auto_scale ? lens_auto_scale(p, w, h) : clampi(p->scale, 25, 400) / 100.0;
  int r = whole_begin(&o, d, "Lens Correction");
  if (r <= 0) return r;
  if (R < 1) R = 1;
  for (y = o.b.y0; y < o.b.y1 && !progress(y - o.b.y0, o.b.y1 - o.b.y0); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      double sx, sy, rn;
      u8 s[4], *q = o.l->px + ((long)y * w + x) * 4;
      int c;
      lens_full(p, w, h, sc, x + 0.5, y + 0.5, &sx, &sy);
      sample_clear(o.orig, w, h, sx - 0.5, sy - 0.5, q);
      if (sr != 1) { sample_clear(o.orig, w, h, cx + (sx - cx) * sr - 0.5, cy + (sy - cy) * sr - 0.5, s); q[0] = s[0]; }
      if (sb != 1) { sample_clear(o.orig, w, h, cx + (sx - cx) * sb - 0.5, cy + (sy - cy) * sb - 0.5, s); q[2] = s[2]; }
      rn = sqrt((x + 0.5 - cx) * (x + 0.5 - cx) + (y + 0.5 - cy) * (y + 0.5 - cy)) / R;
      if (vg != 0) {
        double t = (rn - mid * 0.7) / (1.0 - mid * 0.7);
        if (t > 0) {
          if (t > 1) t = 1;
          t = t * t * (3 - 2 * t);
          for (c = 0; c < 3; c++) q[c] = (u8)clamp255(vg < 0 ? q[c] * (1 + vg * t) : q[c] + (255 - q[c]) * vg * t);
        }
      }
    }
  whole_end(&o);
  return 1;
}

/* ------------------------------------------------------------------ Liquify */
const char *const liquify_tool_names[LQ_N] = { "Forward Warp", "Reconstruct", "Smooth", "Twirl Clockwise", "Twirl Counterclockwise", "Pucker", "Bloat", "Push Left" };
LiquifyMesh *liquify_new(i32 w, i32 h)
{
  LiquifyMesh *m;
  i32 cell = 4;
  if (w < 1 || h < 1) return NULL;
  while ((long)(w / cell + 2) * (h / cell + 2) > 300000L) cell *= 2;
  if (!(m = (LiquifyMesh *)calloc(1, sizeof *m))) return NULL;
  m->w = w; m->h = h; m->cell = cell; m->mw = w / cell + 2; m->mh = h / cell + 2;
  m->dx = (float *)calloc((size_t)m->mw * m->mh, sizeof(float)); m->dy = (float *)calloc((size_t)m->mw * m->mh, sizeof(float));
  if (!m->dx || !m->dy) { liquify_free(m); return NULL; }
  return m;
}
void liquify_free(LiquifyMesh *m) { if (m) { free(m->dx); free(m->dy); free(m); } }
void liquify_reset(LiquifyMesh *m) { memset(m->dx, 0, (size_t)m->mw * m->mh * sizeof(float)); memset(m->dy, 0, (size_t)m->mw * m->mh * sizeof(float)); }
void liquify_at(const LiquifyMesh *m, double x, double y, double *sx, double *sy)
{
  double gx = clampd(x / m->cell, 0, m->mw - 1.001), gy = clampd(y / m->cell, 0, m->mh - 1.001), fx, fy;
  i32 ix = (i32)gx, iy = (i32)gy;
  long k = (long)iy * m->mw + ix;
  fx = gx - ix; fy = gy - iy;
  *sx = x + (m->dx[k] * (1 - fx) + m->dx[k + 1] * fx) * (1 - fy) + (m->dx[k + m->mw] * (1 - fx) + m->dx[k + m->mw + 1] * fx) * fy;
  *sy = y + (m->dy[k] * (1 - fx) + m->dy[k + 1] * fx) * (1 - fy) + (m->dy[k + m->mw] * (1 - fx) + m->dy[k + m->mw + 1] * fx) * fy;
}
/* the brush's weight at distance t (0 at its middle, 1 at its edge): density the part that is full */
static double lq_falloff(double t, int density)
{
  double core = clampi(density, 0, 100) / 100.0 * 0.8;
  if (t >= 1) return 0;
  if (t <= core) return 1;
  t = (t - core) / (1 - core);
  return 0.5 + 0.5 * cos(t * M_PI);
}
static void lq_dab(LiquifyMesh *m, int tool, double cx, double cy, double mvx, double mvy, double r, int density, double pr, double rate)
{
  i32 gx0 = (i32)floor((cx - r) / m->cell), gx1 = (i32)ceil((cx + r) / m->cell), gy0 = (i32)floor((cy - r) / m->cell), gy1 = (i32)ceil((cy + r) / m->cell), gx, gy;
  float *ox = NULL, *oy = NULL;
  if (gx0 < 0) gx0 = 0;
  if (gy0 < 0) gy0 = 0;
  if (gx1 > m->mw - 1) gx1 = m->mw - 1;
  if (gy1 > m->mh - 1) gy1 = m->mh - 1;
  if (gx1 < gx0 || gy1 < gy0) return;
  if (tool == LQ_SMOOTH) {
    /* the neighbours' average, from a copy */
    long n = (long)(gx1 - gx0 + 1) * (gy1 - gy0 + 1);
    ox = (float *)malloc((size_t)n * sizeof(float)); oy = (float *)malloc((size_t)n * sizeof(float));
    if (!ox || !oy) { free(ox); free(oy); return; }
    for (gy = gy0; gy <= gy1; gy++) for (gx = gx0; gx <= gx1; gx++) {
      long j = (long)(gy - gy0) * (gx1 - gx0 + 1) + (gx - gx0);
      ox[j] = m->dx[(long)gy * m->mw + gx]; oy[j] = m->dy[(long)gy * m->mw + gx];
    }
  }
  for (gy = gy0; gy <= gy1; gy++)
    for (gx = gx0; gx <= gx1; gx++) {
      double px = gx * m->cell, py = gy * m->cell, vx = px - cx, vy = py - cy, dist = sqrt(vx * vx + vy * vy), f = lq_falloff(dist / r, density) * pr;
      long k = (long)gy * m->mw + gx;
      if (f <= 0) continue;
      switch (tool) {
      case LQ_WARP: m->dx[k] -= (float)(mvx * f); m->dy[k] -= (float)(mvy * f); break;
      case LQ_PUSH_LEFT: m->dx[k] -= (float)(mvy * f); m->dy[k] += (float)(mvx * f); break;   /* (the picture moves to the stroke's left) */
      case LQ_RECONSTRUCT: m->dx[k] *= (float)(1 - f * rate); m->dy[k] *= (float)(1 - f * rate); break;
      case LQ_TWIRL_CW: case LQ_TWIRL_CCW: {
        /* the point turned round the middle: its source turned back */
        double a = (tool == LQ_TWIRL_CW ? -1 : 1) * f * rate * 0.35, ca = cos(a), sa = sin(a);
        double sx = px + m->dx[k] - cx, sy = py + m->dy[k] - cy;
        m->dx[k] = (float)(cx + sx * ca - sy * sa - px); m->dy[k] = (float)(cy + sx * sa + sy * ca - py);
        break; }
      case LQ_PUCKER: case LQ_BLOAT: {
        /* towards the middle (pucker) or away (bloat): the source taken further out or nearer in */
        double s = (tool == LQ_PUCKER ? 1 : -1) * f * rate * 0.12;
        double sx = px + m->dx[k] - cx, sy = py + m->dy[k] - cy;
        m->dx[k] = (float)(cx + sx * (1 + s) - px); m->dy[k] = (float)(cy + sy * (1 + s) - py);
        break; }
      case LQ_SMOOTH: {
        double ax = 0, ay = 0;
        int cnt = 0, ddx, ddy;
        for (ddy = -1; ddy <= 1; ddy++) for (ddx = -1; ddx <= 1; ddx++) {
          i32 xx = gx + ddx, yy = gy + ddy;
          if (xx < gx0 || yy < gy0 || xx > gx1 || yy > gy1) continue;
          ax += ox[(long)(yy - gy0) * (gx1 - gx0 + 1) + (xx - gx0)]; ay += oy[(long)(yy - gy0) * (gx1 - gx0 + 1) + (xx - gx0)]; cnt++;
        }
        m->dx[k] += (float)((ax / cnt - m->dx[k]) * f * rate); m->dy[k] += (float)((ay / cnt - m->dy[k]) * f * rate);
        break; }
      default: break;
      }
      /* (no point taken from further than the canvas allows) */
      m->dx[k] = (float)clampd(m->dx[k], -px - m->w, 2.0 * m->w - px); m->dy[k] = (float)clampd(m->dy[k], -py - m->h, 2.0 * m->h - py);
    }
  free(ox); free(oy);
}
void liquify_stroke(LiquifyMesh *m, int tool, double x0, double y0, double x1, double y1, double size, int density, int pressure, int rate)
{
  double r = clampd(size, 2, 15000) / 2, pr = clampi(pressure, 1, 100) / 100.0, rt = clampi(rate, 1, 100) / 100.0;
  double L = hypot(x1 - x0, y1 - y0), sp = r / 4 > 1 ? r / 4 : 1;
  int n, i;
  if (!m || tool < 0 || tool >= LQ_N || !(x0 == x0 && y0 == y0 && x1 == x1 && y1 == y1)) return;
  if (fabs(x0) > 1e6 || fabs(y0) > 1e6 || fabs(x1) > 1e6 || fabs(y1) > 1e6) return;
  n = (int)ceil(L / sp);
  if (n < 1) n = 1;
  if (n > 4000) n = 4000;
  for (i = 1; i <= n; i++) {
    double t = (double)i / n, cx = x0 + (x1 - x0) * t, cy = y0 + (y1 - y0) * t;
    lq_dab(m, tool, cx, cy, (x1 - x0) / n, (y1 - y0) / n, r, density, pr, rt);
  }
}
Box liquify_stroke_box(const LiquifyMesh *m, double x0, double y0, double x1, double y1, double size)
{
  double r = clampd(size, 2, 15000) / 2 + 2 * m->cell + 2;
  if (!(x0 == x0 && y0 == y0 && x1 == x1 && y1 == y1)) return box_make(0, 0, 0, 0);
  return box_clip(box_make((i32)floor(clampd((x0 < x1 ? x0 : x1) - r, -1, m->w + 1)), (i32)floor(clampd((y0 < y1 ? y0 : y1) - r, -1, m->h + 1)),
                           (i32)ceil(clampd((x0 > x1 ? x0 : x1) + r, -1, m->w + 1)), (i32)ceil(clampd((y0 > y1 ? y0 : y1) + r, -1, m->h + 1))), box_make(0, 0, m->w, m->h));
}
void liquify_apply_box(const LiquifyMesh *m, const u8 *src, u8 *out, i32 w, i32 h, Box b)
{
  i32 x, y;
  b = box_clip(b, box_make(0, 0, w, h));
  for (y = b.y0; y < b.y1; y++)
    for (x = b.x0; x < b.x1; x++) {
      double sx, sy;
      liquify_at(m, x + 0.5, y + 0.5, &sx, &sy);
      sample_clear(src, w, h, sx - 0.5, sy - 0.5, out + ((long)y * w + x) * 4);
    }
}
void liquify_apply_rgba(const LiquifyMesh *m, const u8 *src, u8 *out, i32 w, i32 h)
{
  i32 x, y;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      double sx, sy;
      liquify_at(m, x + 0.5, y + 0.5, &sx, &sy);
      sample_clear(src, w, h, sx - 0.5, sy - 0.5, out + ((long)y * w + x) * 4);
    }
}
int flt_liquify(Doc *d, const LiquifyMesh *m)
{
  Whole o;
  i32 x, y;
  int r;
  if (!m || m->w != d->w || m->h != d->h) return 0;
  if ((r = whole_begin(&o, d, "Liquify")) <= 0) return r;
  for (y = o.b.y0; y < o.b.y1 && !progress(y - o.b.y0, o.b.y1 - o.b.y0); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      double sx, sy;
      liquify_at(m, x + 0.5, y + 0.5, &sx, &sy);
      sample_clear(o.orig, d->w, d->h, sx - 0.5, sy - 0.5, o.l->px + ((long)y * d->w + x) * 4);
    }
  whole_end(&o);
  return 1;
}
