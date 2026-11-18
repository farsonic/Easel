/*
 * fx.c - layer styles (Easel 1.1); see fx.h.
 *
 * Every effect starts from the layer's shape A (its alpha times its mask, 0-255). Spread, choke and
 * Stroke need to know how far each pixel is from the shape's edge: a signed distance (negative
 * inside), made from two exact Euclidean distance transforms (Felzenszwalb and Huttenlocher's) and
 * refined at the edge from the anti-aliased alpha. Soft edges are three box blurs (close to a
 * Gaussian). Bevel & Emboss lights a height made by blurring A. All of it is worked out once into
 * one byte a pixel per effect over the box the effects reach, and kept with the layer until its
 * pixels, mask or style change (doc.c clears fx_ok then).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "fx.h"
#include "live.h"
#include "paint.h"

const char *const fx_names[FX_N] = { "Drop Shadow", "Inner Shadow", "Outer Glow", "Inner Glow", "Bevel & Emboss",
  "Satin", "Color Overlay", "Gradient Overlay", "Stroke" };
static const char *const fx_keys[FX_N] = { "dropshadow", "innershadow", "outerglow", "innerglow", "bevel", "satin",
  "coloroverlay", "gradientoverlay", "stroke" };

typedef struct {
  i32 w, h;                           /* the canvas when they were worked out */
  Box b; i32 bw, bh;                  /* the box the planes cover */
  Box cb;                             /* the shape's own extent (Gradient Overlay spans it) */
  LayerFx made; int la, lalt;         /* the settings and global light they were made with */
  u8 *a;                              /* the shape */
  u8 *pl[FX_N];                       /* each effect's coverage (Color and Gradient Overlay use a) */
  u8 *bsh;                            /* Bevel's shadow (pl[FX_BEVEL] is its highlight) */
} FxCache;

static int mul255(int a, int b) { int t = a * b + 128; return (t + (t >> 8)) >> 8; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ------------------------------------------------------------------ settings */
void fx_defaults(FxOne *e, int k)
{
  memset(e, 0, sizeof *e);
  e->blend = BM_NORMAL; e->opacity = 191; e->angle = 120; e->distance = 5; e->size = 5; e->altitude = 30;
  e->depth = 100; e->flags = FXF_GLOBAL;
  switch (k) {
  case FX_DROPSHADOW: e->blend = BM_MULTIPLY; e->color = 0x000000; e->flags |= FXF_KNOCKOUT; break;
  case FX_INNERSHADOW: e->blend = BM_MULTIPLY; e->color = 0x000000; break;
  case FX_OUTERGLOW: case FX_INNERGLOW: e->blend = BM_SCREEN; e->color = 0xFFFFBE; e->flags = 0; break;
  case FX_BEVEL: e->blend = BM_SCREEN; e->color = 0xFFFFFF; e->blend2 = BM_MULTIPLY; e->color2 = 0x000000; e->opacity2 = 191; break;
  case FX_SATIN: e->blend = BM_MULTIPLY; e->opacity = 128; e->angle = 19; e->distance = 11; e->size = 14; e->flags = FXF_INVERT; break;
  case FX_COLOR: e->opacity = 255; e->color = 0xFF0000; e->flags = 0; break;
  case FX_GRADIENT: e->opacity = 255; e->color = 0x000000; e->color2 = 0xFFFFFF; e->angle = 90; e->style = GR_LINEAR; e->flags = 0; break;
  case FX_STROKE: e->opacity = 255; e->color = 0xFF0000; e->size = 3; e->style = FXS_OUTSIDE; e->flags = 0; break;
  }
}
void fx_defaults_all(LayerFx *fx)
{
  int k;
  memset(fx, 0, sizeof *fx);
  for (k = 0; k < FX_N; k++) fx_defaults(&fx->e[k], k);
}
int fx_has(const Layer *l)
{
  int k;
  for (k = 0; k < FX_N; k++) if (l->fx.e[k].on) return 1;
  return 0;
}
int fx_any(const Layer *l)
{
  if (l->fx.hidden || (l->kind != LK_PIXEL && l->kind != LK_FILL && l->kind != LK_TEXT && l->kind != LK_SHAPE)) return 0;
  return fx_has(l);
}
void fx_light(const Doc *d, int *angle, int *alt)
{
  *angle = d->light_set ? d->light_angle : 120;
  *alt = d->light_set ? d->light_alt : 30;
}
static void cache_free(FxCache *c)
{
  int k;
  if (!c) return;
  free(c->a); free(c->bsh);
  for (k = 0; k < FX_N; k++) free(c->pl[k]);
  free(c);
}
void fx_free(Layer *l) { cache_free((FxCache *)l->fxc); l->fxc = NULL; l->fx_ok = 0; }

/* ------------------------------------------------------------------ the pieces */
/* the layer's shape at canvas pixel x, y: alpha times mask (the mask's density counted) */
static int shape_at(Doc *d, Layer *l, const u8 *mask, i32 x, i32 y)
{
  int a;
  if (l->kind == LK_FILL) a = 255;
  else if (l->kind == LK_PIXEL) a = l->px ? l->px[((long)y * d->w + x) * 4 + 3] : 0;
  else {
    Box b = l->livebox;
    if (!l->livepx || x < b.x0 || x >= b.x1 || y < b.y0 || y >= b.y1) return 0;
    a = l->livepx[((long)(y - b.y0) * (b.x1 - b.x0) + (x - b.x0)) * 4 + 3];
  }
  if (mask && a) a = mul255(a, 255 - mul255(255 - mask[(long)y * d->w + x], 255 - l->mask_fade));
  return a;
}
/* how far the effects reach beyond the shape */
static int fx_reach(const LayerFx *fx)
{
  int r = 0, k, v;
  for (k = 0; k < FX_N; k++) {
    const FxOne *e = &fx->e[k];
    if (!e->on) continue;
    v = 0;
    if (k == FX_DROPSHADOW) v = e->distance + e->size + 2;
    else if (k == FX_OUTERGLOW) v = e->size + 2;
    else if (k == FX_BEVEL && e->style != FXB_INNER) v = 2 * e->size + e->spread + 2;
    else if (k == FX_STROKE && e->style != FXS_INSIDE) v = e->size + 2;
    if (v > r) r = v;
  }
  return r;
}
/* one dimension of the squared distance transform: f (n values) into d; v and z are work space */
static void edt1(const double *f, int n, double *d, int *v, double *z)
{
  int k = 0, q;
  v[0] = 0; z[0] = -1e300; z[1] = 1e300;
  for (q = 1; q < n; q++) {
    double s = ((f[q] + (double)q * q) - (f[v[k]] + (double)v[k] * v[k])) / (2.0 * (q - v[k]));
    while (s <= z[k]) { k--; s = ((f[q] + (double)q * q) - (f[v[k]] + (double)v[k] * v[k])) / (2.0 * (q - v[k])); }
    k++; v[k] = q; z[k] = s; z[k + 1] = 1e300;
  }
  k = 0;
  for (q = 0; q < n; q++) {
    double t;
    while (z[k + 1] < q) k++;
    t = (double)(q - v[k]);
    d[q] = t * t + f[v[k]];
  }
}
/* the squared distance from each of the w x h cells of g to the nearest cell that is 0 (the others
   1e20), in place; 0 if out of memory */
static int edt2(float *g, i32 w, i32 h)
{
  i32 n = w > h ? w : h, x, y;
  double *f = (double *)malloc(sizeof(double) * n), *d = (double *)malloc(sizeof(double) * n), *z = (double *)malloc(sizeof(double) * (n + 1));
  int *v = (int *)malloc(sizeof(int) * n);
  if (!f || !d || !z || !v) { free(f); free(d); free(z); free(v); return 0; }
  for (x = 0; x < w; x++) {
    for (y = 0; y < h; y++) f[y] = g[(long)y * w + x];
    edt1(f, h, d, v, z);
    for (y = 0; y < h; y++) g[(long)y * w + x] = (float)d[y];
  }
  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++) f[x] = g[(long)y * w + x];
    edt1(f, w, d, v, z);
    for (x = 0; x < w; x++) g[(long)y * w + x] = (float)(d[x] > 1e20 ? 1e20 : d[x]);
  }
  free(f); free(d); free(z); free(v);
  return 1;
}
/* the signed distance from the shape's edge (negative inside) for each pixel of the box, in pixels;
   outside the box counts as outside the shape. NULL if out of memory */
static float *signed_dist(const u8 *a, i32 bw, i32 bh)
{
  i32 pw = bw + 2, ph = bh + 2, x, y;
  float *go = (float *)malloc(sizeof(float) * pw * ph), *gi = (float *)malloc(sizeof(float) * pw * ph), *sd;
  if (!go || !gi) { free(go); free(gi); return NULL; }
  for (y = 0; y < ph; y++)
    for (x = 0; x < pw; x++) {
      int in = x > 0 && y > 0 && x <= bw && y <= bh && a[(long)(y - 1) * bw + x - 1] >= 128;
      go[(long)y * pw + x] = in ? 0 : 1e20f;            /* distance to the inside */
      gi[(long)y * pw + x] = in ? 1e20f : 0;            /* distance to the outside */
    }
  if (!edt2(go, pw, ph) || !edt2(gi, pw, ph) || !(sd = (float *)malloc(sizeof(float) * bw * bh))) { free(go); free(gi); return NULL; }
  for (y = 0; y < bh; y++)
    for (x = 0; x < bw; x++) {
      int av = a[(long)y * bw + x];
      long q = (long)(y + 1) * pw + x + 1;
      float v;
      if (av > 0 && av < 255) v = 0.5f - av / 255.0f;  /* the anti-aliased edge says it best */
      else if (av >= 128) v = 0.5f - (float)sqrt(gi[q]);
      else v = (float)sqrt(go[q]) - 0.5f;
      sd[(long)y * bw + x] = v;
    }
  free(go); free(gi);
  return sd;
}
static int cov01(double v) { return v <= 0 ? 0 : v >= 1 ? 255 : (int)(v * 255 + 0.5); }
/* three box blurs of radius r over the w x h plane p; ov is what lies outside it */
static int blur3(u8 *p, i32 w, i32 h, int r, int ov)
{
  i32 n = w > h ? w : h, x, y;
  int pass;
  u8 *t;
  if (r < 1) return 1;
  if (!(t = (u8 *)malloc((size_t)n))) return 0;
  for (pass = 0; pass < 3; pass++) {
    for (y = 0; y < h; y++) {
      u8 *row = p + (long)y * w;
      long sum = 0;
      for (x = -r; x <= r; x++) sum += x < 0 || x >= w ? ov : row[x];
      for (x = 0; x < w; x++) {
        i32 a = x - r, b = x + r + 1;
        t[x] = (u8)((sum + r) / (2 * r + 1));
        sum += (b >= w ? ov : row[b]) - (a < 0 ? ov : row[a]);
      }
      memcpy(row, t, (size_t)w);
    }
    for (x = 0; x < w; x++) {
      long sum = 0;
      for (y = -r; y <= r; y++) sum += y < 0 || y >= h ? ov : p[(long)y * w + x];
      for (y = 0; y < h; y++) {
        i32 a = y - r, b = y + r + 1;
        t[y] = (u8)((sum + r) / (2 * r + 1));
        sum += (b >= h ? ov : p[(long)b * w + x]) - (a < 0 ? ov : p[(long)a * w + x]);
      }
      for (y = 0; y < h; y++) p[(long)y * w + x] = t[y];
    }
  }
  free(t);
  return 1;
}
/* src moved by dx, dy into a new plane (ov where nothing comes from) */
static u8 *shifted(const u8 *src, i32 w, i32 h, i32 dx, i32 dy, int ov)
{
  u8 *o = (u8 *)malloc((size_t)w * h);
  i32 x, y;
  if (!o) return NULL;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      i32 sx = x - dx, sy = y - dy;
      o[(long)y * w + x] = sx < 0 || sy < 0 || sx >= w || sy >= h ? (u8)ov : src[(long)sy * w + sx];
    }
  return o;
}
static void offset_of(int angle, int dist, i32 *dx, i32 *dy)
{
  double a = angle * 3.14159265358979 / 180;
  *dx = (i32)floor(-cos(a) * dist + 0.5);
  *dy = (i32)floor(sin(a) * dist + 0.5);
}
/* a soft plane: the shape (or what is outside it, inside), grown by grow pixels, blurred to size */
static u8 *soft(const u8 *a, const float *sd, i32 bw, i32 bh, int inner, int grow, int size)
{
  u8 *p = (u8 *)malloc((size_t)bw * bh);
  long i, n = (long)bw * bh;
  int r = (size - grow + 2) / 3;
  if (!p) return NULL;
  for (i = 0; i < n; i++) {
    if (!grow || !sd) p[i] = (u8)(inner ? 255 - a[i] : a[i]);
    else p[i] = (u8)(inner ? cov01(sd[i] + grow + 0.5) : cov01(grow - sd[i] + 0.5));
  }
  if (size > grow && !blur3(p, bw, bh, r, inner ? 255 : 0)) { free(p); return NULL; }
  return p;
}
/* the effects' planes made for layer l */
static FxCache *build(Doc *d, Layer *l)
{
  FxCache *c = (FxCache *)calloc(1, sizeof *c);
  const u8 *mask = layer_mask_used(d, l);
  const LayerFx *fx = &l->fx;
  Box cb = box_make(d->w, d->h, 0, 0), scan = doc_box(d);
  i32 x, y, bw, bh;
  long i, n;
  int k, reach, la, lalt, need_sd = 0;
  float *sd = NULL;
  if (!c) return NULL;
  c->w = d->w; c->h = d->h; c->made = *fx;
  fx_light(d, &la, &lalt);
  c->la = la; c->lalt = lalt;
  if (l->kind == LK_TEXT || l->kind == LK_SHAPE) scan = box_clip(l->livebox, scan);
  for (y = scan.y0; y < scan.y1; y++)
    for (x = scan.x0; x < scan.x1; x++)
      if (shape_at(d, l, mask, x, y)) {
        if (x < cb.x0) cb.x0 = x;
        if (x >= cb.x1) cb.x1 = x + 1;
        if (y < cb.y0) cb.y0 = y;
        if (y >= cb.y1) cb.y1 = y + 1;
      }
  c->cb = cb;
  if (box_empty(cb)) { c->b = box_make(0, 0, 0, 0); return c; }
  reach = fx_reach(fx);
  c->b = box_clip(box_make(cb.x0 - reach, cb.y0 - reach, cb.x1 + reach, cb.y1 + reach), doc_box(d));
  bw = c->bw = c->b.x1 - c->b.x0; bh = c->bh = c->b.y1 - c->b.y0;
  n = (long)bw * bh;
  if (!(c->a = (u8 *)malloc((size_t)n))) goto fail;
  for (y = 0; y < bh; y++)
    for (x = 0; x < bw; x++) c->a[(long)y * bw + x] = (u8)shape_at(d, l, mask, c->b.x0 + x, c->b.y0 + y);
  /* the signed distance, when something needs it */
  if (fx->e[FX_STROKE].on || fx->e[FX_BEVEL].on) need_sd = 1;
  for (k = FX_DROPSHADOW; k <= FX_INNERGLOW; k++) if (fx->e[k].on && fx->e[k].spread) need_sd = 1;
  if (need_sd && !(sd = signed_dist(c->a, bw, bh))) goto fail;
  for (k = 0; k < FX_N; k++) {
    const FxOne *e = &fx->e[k];
    int ang = e->flags & FXF_GLOBAL ? la : e->angle, size = e->size > FX_MAXSIZE ? FX_MAXSIZE : e->size;
    int grow = size * e->spread / 100;
    i32 dx, dy;
    u8 *p = NULL;
    if (!e->on) continue;
    switch (k) {
    case FX_DROPSHADOW: case FX_INNERSHADOW: {
      u8 *s = soft(c->a, sd, bw, bh, k == FX_INNERSHADOW, grow, size);
      if (!s) goto fail;
      offset_of(ang, e->distance > FX_MAXDIST ? FX_MAXDIST : e->distance, &dx, &dy);
      p = shifted(s, bw, bh, dx, dy, k == FX_INNERSHADOW ? 255 : 0);
      free(s);
      if (!p) goto fail;
      if (k == FX_INNERSHADOW) for (i = 0; i < n; i++) p[i] = (u8)mul255(p[i], c->a[i]);
      else if (e->flags & FXF_KNOCKOUT) for (i = 0; i < n; i++) p[i] = (u8)mul255(p[i], 255 - c->a[i]);
      break;
    }
    case FX_OUTERGLOW:
      if (!(p = soft(c->a, sd, bw, bh, 0, grow, size))) goto fail;
      break;
    case FX_INNERGLOW:
      if (!(p = soft(c->a, sd, bw, bh, 1, grow, size))) goto fail;
      for (i = 0; i < n; i++) p[i] = (u8)mul255(e->style == FXG_CENTER ? 255 - p[i] : p[i], c->a[i]);
      break;
    case FX_SATIN: {
      u8 *f = (u8 *)malloc((size_t)n);
      if (!f) goto fail;
      memcpy(f, c->a, (size_t)n);
      if (!blur3(f, bw, bh, (size + 2) / 3, 0) || !(p = (u8 *)malloc((size_t)n))) { free(f); goto fail; }
      offset_of(e->angle, e->distance > FX_MAXDIST ? FX_MAXDIST : e->distance, &dx, &dy);
      for (y = 0; y < bh; y++)
        for (x = 0; x < bw; x++) {
          i32 x1 = x + dx, y1 = y + dy, x2 = x - dx, y2 = y - dy;
          int v1 = x1 < 0 || y1 < 0 || x1 >= bw || y1 >= bh ? 0 : f[(long)y1 * bw + x1];
          int v2 = x2 < 0 || y2 < 0 || x2 >= bw || y2 >= bh ? 0 : f[(long)y2 * bw + x2];
          int v = v1 > v2 ? v1 - v2 : v2 - v1;
          if (e->flags & FXF_INVERT) v = 255 - v;
          p[(long)y * bw + x] = (u8)mul255(v, c->a[(long)y * bw + x]);
        }
      free(f);
      break;
    }
    case FX_STROKE: {
      double s = size, lo, hi;
      if (!(p = (u8 *)malloc((size_t)n))) goto fail;
      lo = e->style == FXS_OUTSIDE ? 0 : e->style == FXS_INSIDE ? -s : -s / 2;
      hi = lo + s;
      for (i = 0; i < n; i++) {
        int v = mul255(cov01(hi - sd[i] + 0.5), cov01(sd[i] - lo + 0.5));
        p[i] = (u8)v;
      }
      break;
    }
    case FX_BEVEL: {
      /* a height rising over size pixels from the edge (inside, outside or across it), smoothed, lit
         from the angle and altitude */
      u8 *hgt = (u8 *)malloc((size_t)n);
      double a = ang * 3.14159265358979 / 180, alt = (e->flags & FXF_GLOBAL ? lalt : e->altitude) * 3.14159265358979 / 180;
      double lx = cos(alt) * cos(a), ly = -cos(alt) * sin(a), lz = sin(alt), flat = lz;
      double sz = size > 0 ? size : 1, k2 = (e->depth ? e->depth : 100) / 100.0 * sz / 255.0;
      if (!hgt) goto fail;
      for (i = 0; i < n; i++) {
        double t, v = sd[i];
        switch (e->style) {
        case FXB_OUTER: t = 1 - v / sz; break;
        case FXB_EMBOSS: t = 0.5 - v / sz; break;
        case FXB_PILLOW: t = v < 0 ? -v / sz : 1 - v / sz; break;
        default: t = -v / sz; break;
        }
        hgt[i] = (u8)cov01(t);
      }
      if (!blur3(hgt, bw, bh, 1 + e->spread / 3, 0) || !(p = (u8 *)malloc((size_t)n)) || !(c->bsh = (u8 *)malloc((size_t)n))) { free(hgt); free(p); goto fail; }
      if (e->flags & FXF_INVERT) k2 = -k2;
      for (y = 0; y < bh; y++)
        for (x = 0; x < bw; x++) {
          long q = (long)y * bw + x;
          int hl = hgt[(long)y * bw + (x + 1 < bw ? x + 1 : x)], hr = hgt[(long)y * bw + (x > 0 ? x - 1 : x)];
          int hd = hgt[(long)(y + 1 < bh ? y + 1 : y) * bw + x], hu = hgt[(long)(y > 0 ? y - 1 : y) * bw + x];
          double gx = (hl - hr) / 2.0 * k2, gy = (hd - hu) / 2.0 * k2, len, sh, hi, lo;
          int region;
          if (e->style == FXB_PILLOW && sd[q] >= 0) { gx = -gx; gy = -gy; }
          len = sqrt(gx * gx + gy * gy + 1);
          sh = (-gx * lx - gy * ly + lz) / len;
          hi = sh > flat ? (sh - flat) / (1 - flat > 0.01 ? 1 - flat : 0.01) : 0;
          lo = sh < flat ? (flat - sh) / (flat > 0.01 ? flat : 0.01) : 0;
          region = e->style == FXB_INNER ? c->a[q] : e->style == FXB_OUTER ? 255 - c->a[q] : 255;
          p[q] = (u8)mul255(cov01(2 * hi), region);       /* (doubled: Photoshop's gloss is as bright) */
          c->bsh[q] = (u8)mul255(cov01(2 * lo), region);
        }
      free(hgt);
      break;
    }
    default: break;                   /* the overlays use the shape itself */
    }
    c->pl[k] = p;
  }
  free(sd);
  return c;
fail:
  free(sd);
  cache_free(c);
  return NULL;
}
static FxCache *ensure(Doc *d, Layer *l)
{
  FxCache *c = (FxCache *)l->fxc;
  int la, lalt;
  if ((l->kind == LK_TEXT || l->kind == LK_SHAPE) && !l->live_ok) { live_render(d, l); l->fx_ok = 0; }
  fx_light(d, &la, &lalt);
  if (l->fx_ok && c && c->w == d->w && c->h == d->h && c->la == la && c->lalt == lalt && !memcmp(&c->made, &l->fx, sizeof l->fx)) return c;
  if (l->fx_ok && !c) return NULL;                         /* (out of memory last time: tried again once something changes) */
  cache_free(c);
  l->fxc = c = build(d, l);
  l->fx_ok = 1;
  return c;
}
int fx_render(Doc *d, Layer *l, Box *reach)
{
  FxCache *c = ensure(d, l);
  if (reach) *reach = c ? c->b : box_make(0, 0, 0, 0);
  return c != NULL;
}
int fx_coverage(Doc *d, Layer *l, int k, i32 x, i32 y)
{
  FxCache *c = ensure(d, l);
  long q;
  if (!c || k < 0 || k >= FX_N || !l->fx.e[k].on || x < c->b.x0 || y < c->b.y0 || x >= c->b.x1 || y >= c->b.y1) return 0;
  q = (long)(y - c->b.y0) * c->bw + (x - c->b.x0);
  if (k == FX_COLOR || k == FX_GRADIENT) return c->a[q];
  return c->pl[k] ? c->pl[k][q] : 0;
}

/* ------------------------------------------------------------------ drawing */
/* the Gradient Overlay's colour at canvas x, y, across the shape's extent */
static u32 grad_at(const FxCache *c, const FxOne *e, i32 x, i32 y)
{
  double cx = (c->cb.x0 + c->cb.x1) / 2.0 + e->dx, cy = (c->cb.y0 + c->cb.y1) / 2.0 + e->dy;
  double hw = (c->cb.x1 - c->cb.x0) / 2.0, hh = (c->cb.y1 - c->cb.y0) / 2.0, sc = (e->depth >= 10 ? e->depth : 100) / 100.0;
  double a = e->angle * 3.14159265358979 / 180, ca = cos(a), sa = -sin(a), px = x + 0.5 - cx, py = y + 0.5 - cy, t, len;
  int k, ti;
  u32 o = 0;
  if (hw < 0.5) hw = 0.5;
  if (hh < 0.5) hh = 0.5;
  len = (fabs(hw * ca) + fabs(hh * sa)) * sc;
  if (len < 0.5) len = 0.5;
  switch (e->style) {
  case GR_RADIAL: t = sqrt(px * px + py * py) / (sqrt(hw * hw + hh * hh) * sc); break;
  case GR_ANGLE: t = fmod(atan2(py, px) - a + 4 * 3.14159265358979, 2 * 3.14159265358979) / (2 * 3.14159265358979); break;
  case GR_REFLECTED: t = fabs(px * ca + py * sa) / len; break;
  case GR_DIAMOND: t = (fabs(px) / hw > fabs(py) / hh ? fabs(px) / hw : fabs(py) / hh) / sc; break;
  default: t = (px * ca + py * sa) / (2 * len) + 0.5; break;
  }
  if (e->flags & FXF_INVERT) t = 1 - t;
  ti = t <= 0 ? 0 : t >= 1 ? 256 : (int)(t * 256);
  for (k = 0; k < 3; k++) {
    int c0 = (int)((e->color >> (16 - 8 * k)) & 255), c1 = (int)((e->color2 >> (16 - 8 * k)) & 255);
    o |= (u32)(c0 + ((c1 - c0) * ti) / 256) << (16 - 8 * k);
  }
  return o;
}
static void put(u8 *p, u32 col, int cov, int op, int mode, i32 x, i32 y, int keep)
{
  u8 s[4], a = p[3];
  if (!cov || !op || (keep && !a)) return;
  s[0] = (u8)(col >> 16); s[1] = (u8)(col >> 8); s[2] = (u8)col; s[3] = (u8)cov;
  blend_pixel_at(p, s, op, mode, x, y);
  if (keep) p[3] = a;
}
/* effects k listed (in drawing order) for one row */
static void draw_row(Doc *d, Layer *l, const int *ks, int nk, i32 y, i32 x0, i32 n, u8 *acc, int keep)
{
  FxCache *c = ensure(d, l);
  i32 xa, xb, x;
  int j;
  if (!c || box_empty(c->b) || y < c->b.y0 || y >= c->b.y1) return;
  xa = x0 > c->b.x0 ? x0 : c->b.x0; xb = x0 + n < c->b.x1 ? x0 + n : c->b.x1;
  if (xa >= xb) return;
  for (j = 0; j < nk; j++) {
    int k = ks[j];
    const FxOne *e = &l->fx.e[k];
    const u8 *pl = k == FX_COLOR || k == FX_GRADIENT ? c->a : c->pl[k];
    long q0 = (long)(y - c->b.y0) * c->bw - c->b.x0;
    int op = mul255(e->opacity, l->opacity), mode = e->blend < BM_N ? e->blend : BM_NORMAL;
    if (!e->on || !pl) continue;
    for (x = xa; x < xb; x++) {
      int v = pl[q0 + x];
      if (!v) continue;
      put(acc + (x - x0) * 4, k == FX_GRADIENT ? grad_at(c, e, x, y) : e->color, v, op, mode, x, y, keep);
    }
    if (k == FX_BEVEL && c->bsh) {
      int op2 = mul255(e->opacity2, l->opacity), m2 = e->blend2 < BM_N ? e->blend2 : BM_NORMAL;
      for (x = xa; x < xb; x++) if (c->bsh[q0 + x]) put(acc + (x - x0) * 4, e->color2, c->bsh[q0 + x], op2, m2, x, y, keep);
    }
  }
}
void fx_under(Doc *d, Layer *l, i32 y, i32 x0, i32 n, u8 *acc, int keep)
{
  static const int ks[] = { FX_DROPSHADOW, FX_OUTERGLOW };
  draw_row(d, l, ks, 2, y, x0, n, acc, keep);
}
void fx_over(Doc *d, Layer *l, i32 y, i32 x0, i32 n, u8 *acc, int keep)
{
  static const int ks[] = { FX_GRADIENT, FX_COLOR, FX_SATIN, FX_INNERGLOW, FX_INNERSHADOW, FX_STROKE, FX_BEVEL };
  draw_row(d, l, ks, 7, y, x0, n, acc, keep);
}

/* ------------------------------------------------------------------ the menu's operations */
static int fx_layer_ok(const Doc *d, int i) { return i >= 0 && i < d->nlay && d->lay[i]->kind != LK_GROUP && d->lay[i]->kind != LK_ADJUST; }
static void sanitize(LayerFx *fx)
{
  int k;
  for (k = 0; k < FX_N; k++) {
    FxOne *e = &fx->e[k];
    e->on = e->on != 0;
    if (e->blend >= BM_N) e->blend = BM_NORMAL;
    if (e->blend2 >= BM_N) e->blend2 = BM_MULTIPLY;
    if (e->size > FX_MAXSIZE) e->size = FX_MAXSIZE;
    if (e->distance > FX_MAXDIST) e->distance = FX_MAXDIST;
    if (e->spread > (k == FX_BEVEL ? 16 : 100)) e->spread = k == FX_BEVEL ? 16 : 100;
    if (e->altitude > 90) e->altitude = 90;
    if (k == FX_GRADIENT) { if (e->depth < 10 || e->depth > 150) e->depth = 100; if (e->style > GR_DIAMOND) e->style = GR_LINEAR; }
    else if (e->depth < 1 || e->depth > 1000) e->depth = 100;
    if (k == FX_BEVEL && e->style >= FXB_N) e->style = FXB_INNER;
    if (k == FX_STROKE && e->style > FXS_CENTER) e->style = FXS_OUTSIDE;
    if (k == FX_INNERGLOW && e->style > FXG_CENTER) e->style = FXG_EDGE;
    e->angle = (i16)(((e->angle % 360) + 540) % 360 - 180);
    if (e->dx < -MAXDIM) e->dx = -MAXDIM;
    if (e->dx > MAXDIM) e->dx = MAXDIM;
    if (e->dy < -MAXDIM) e->dy = -MAXDIM;
    if (e->dy > MAXDIM) e->dy = MAXDIM;
    e->pad = 0;
  }
  fx->hidden = fx->hidden != 0;
  memset(fx->pad, 0, sizeof fx->pad);
}
int doc_set_fx(Doc *d, int i, const LayerFx *fx)
{
  LayerFx f = *fx;
  if (!fx_layer_ok(d, i)) return 0;
  sanitize(&f);
  doc_begin(d, "Layer Style");
  d->lay[i]->fx = f;
  doc_end(d);
  return 1;
}
int doc_clear_fx(Doc *d, int i)
{
  LayerFx f;
  if (i < 0 || i >= d->nlay) return 0;
  f = d->lay[i]->fx;
  {
    int k;
    for (k = 0; k < FX_N; k++) f.e[k].on = 0;
  }
  f.hidden = 0;
  doc_begin(d, "Clear Layer Style");
  d->lay[i]->fx = f;
  doc_end(d);
  fx_free(d->lay[i]);
  return 1;
}
int doc_paste_fx(Doc *d, const LayerFx *fx)
{
  LayerFx f = *fx;
  int i, n = 0;
  sanitize(&f);
  doc_begin(d, "Paste Layer Style");
  for (i = 0; i < d->nlay; i++)
    if ((i == d->active || d->lay[i]->picked) && fx_layer_ok(d, i)) { d->lay[i]->fx = f; n++; }
  doc_end(d);
  return n;
}
int doc_hide_all_fx(Doc *d, int hide)
{
  int i, n = 0;
  doc_begin(d, hide ? "Hide All Effects" : "Show All Effects");
  for (i = 0; i < d->nlay; i++) if (fx_has(d->lay[i])) { d->lay[i]->fx.hidden = (u8)(hide != 0); n++; }
  doc_end(d);
  return n;
}
int doc_scale_fx(Doc *d, int i, int pc)
{
  LayerFx f;
  int k;
  if (!fx_layer_ok(d, i) || pc < 1 || pc > 1000) return 0;
  f = d->lay[i]->fx;
  for (k = 0; k < FX_N; k++) {
    FxOne *e = &f.e[k];
    long s = ((long)e->size * pc + 50) / 100, ds = ((long)e->distance * pc + 50) / 100;
    e->size = (u16)(s > FX_MAXSIZE ? FX_MAXSIZE : s);
    e->distance = (u16)(ds > FX_MAXDIST ? FX_MAXDIST : ds);
    if (k == FX_BEVEL) { long so = ((long)e->spread * pc + 50) / 100; e->spread = (u8)(so > 16 ? 16 : so); }
    if (k == FX_STROKE && e->size < 1 && e->on) e->size = 1;
    if (k == FX_GRADIENT) { e->dx = (i16)((long)e->dx * pc / 100); e->dy = (i16)((long)e->dy * pc / 100); }
  }
  doc_begin(d, "Scale Effects");
  d->lay[i]->fx = f;
  doc_end(d);
  return 1;
}
int doc_global_light(Doc *d, int angle, int alt)
{
  int i;
  d->light_angle = (i16)(((angle % 360) + 540) % 360 - 180);
  d->light_alt = (u8)clampi(alt, 0, 90);
  d->light_set = 1;
  d->dirty = 1;
  for (i = 0; i < d->nlay; i++) d->lay[i]->fx_ok = 0;
  return 1;
}
/* Create Layers: Drop Shadow and Outer Glow go below the layer, the rest above it, each a pixel layer
   with the effect's mode and opacity; the style is cleared. The picture looks the same as long as the
   layer's opacity is 100 % */
int doc_fx_to_layers(Doc *d, int i)
{
  static const int order[] = { FX_DROPSHADOW, FX_OUTERGLOW, -1, FX_GRADIENT, FX_COLOR, FX_SATIN, FX_INNERGLOW, FX_INNERSHADOW, FX_STROKE, FX_BEVEL, FX_BEVEL + 100 };
  Layer *l, *nl[FX_N + 1];
  int nb = 0, na = 0, j, m, total;
  FxCache *c;
  if (!fx_layer_ok(d, i) || !fx_any(d->lay[i])) return 0;
  l = d->lay[i];
  if (!(c = ensure(d, l))) return 0;
  for (total = 0, j = 0; j < (int)(sizeof order / sizeof order[0]); j++) {
    int k = order[j] >= 100 ? order[j] - 100 : order[j];
    if (k >= 0 && l->fx.e[k].on) total++;
  }
  if (d->nlay + total > MAXLAYERS) return 0;
  doc_begin(d, "Create Layers");
  doc_note_all(d);
  for (j = 0; j < (int)(sizeof order / sizeof order[0]); j++) {
    int k, second = order[j] >= 100;
    const FxOne *e;
    const u8 *pl;
    Layer *n;
    char nm[64];
    i32 x, y;
    if (order[j] < 0) continue;
    k = second ? order[j] - 100 : order[j];
    e = &l->fx.e[k];
    pl = k == FX_COLOR || k == FX_GRADIENT ? c->a : second ? c->bsh : c->pl[k];
    if (!e->on || !pl) continue;
    snprintf(nm, sizeof nm, "%.30s's %s", l->name, k == FX_BEVEL ? (second ? "Bevel Shadow" : "Bevel Highlight") : fx_names[k]);
    if (!(n = layer_new(d, nm))) break;
    for (y = 0; y < c->bh; y++)
      for (x = 0; x < c->bw; x++) {
        int v = pl[(long)y * c->bw + x];
        u32 col = k == FX_GRADIENT ? grad_at(c, e, c->b.x0 + x, c->b.y0 + y) : second ? e->color2 : e->color;
        u8 *p = n->px + ((long)(c->b.y0 + y) * d->w + c->b.x0 + x) * 4;
        if (!v) continue;
        p[0] = (u8)(col >> 16); p[1] = (u8)(col >> 8); p[2] = (u8)col; p[3] = (u8)v;
      }
    n->blend = second ? e->blend2 : e->blend;
    n->opacity = (u8)mul255(second ? e->opacity2 : e->opacity, l->opacity);
    n->depth = l->depth;
    if (k == FX_DROPSHADOW || k == FX_OUTERGLOW) nl[nb++] = n; else nl[nb + na++] = n;
  }
  /* the new layers into the stack: the lower ones under layer i, the upper ones over it */
  m = nb + na;
  memmove(&d->lay[i + 1 + m], &d->lay[i + 1], sizeof(Layer *) * (size_t)(d->nlay - i - 1));
  d->lay[i + nb] = l;
  for (j = 0; j < nb; j++) d->lay[i + j] = nl[j];
  for (j = 0; j < na; j++) d->lay[i + nb + 1 + j] = nl[nb + j];
  d->nlay += m;
  if (d->active >= i) d->active += nb;
  for (j = 0; j < FX_N; j++) l->fx.e[j].on = 0;
  fx_free(l);
  doc_end(d);
  return m;
}

/* ------------------------------------------------------------------ as text */
char *fx_save(const LayerFx *fx)
{
  Buf b;
  int k, any = 0;
  buf_init(&b);
  for (k = 0; k < FX_N; k++) if (fx->e[k].on) any = 1;
  if (fx->hidden && any) buf_adds(&b, "hidden;");
  for (k = 0; k < FX_N; k++) {
    const FxOne *e = &fx->e[k];
    if (!e->on) continue;
    buf_printf(&b, "%s %d %d %d %06lX %06lX %d %d %d %d %d %d %d %d %d %d %d;", fx_keys[k], e->blend, e->opacity, e->style,
               (unsigned long)e->color, (unsigned long)e->color2, e->angle, e->distance, e->size, e->spread, e->altitude,
               e->depth, e->flags, e->blend2, e->opacity2, e->dx, e->dy);
  }
  if (b.oom) { buf_free(&b); return NULL; }
  return buf_take(&b);
}
int fx_load(LayerFx *fx, const char *s)
{
  int any = 0;
  fx_defaults_all(fx);
  while (s && *s) {
    char key[24];
    long v[16];
    int k, j = 0, used = 0;
    const char *e = strchr(s, ';');
    size_t len = e ? (size_t)(e - s) : strlen(s);
    char item[256];
    if (len >= sizeof item) len = sizeof item - 1;
    memcpy(item, s, len); item[len] = 0;
    s = e ? e + 1 : s + strlen(s);
    if (!strcmp(item, "hidden")) { fx->hidden = 1; continue; }
    if (sscanf(item, "%23s%n", key, &used) != 1) continue;
    for (k = 0; k < FX_N && strcmp(key, fx_keys[k]); k++) continue;
    if (k == FX_N) continue;
    {
      const char *p = item + used;
      char *q;
      for (j = 0; j < 16; j++) {
        v[j] = strtol(p, &q, j == 3 || j == 4 ? 16 : 10);
        if (q == p) break;
        p = q;
      }
    }
    if (j < 16) continue;
    {
      FxOne *o = &fx->e[k];
      o->on = 1;
      o->blend = (u8)clampi((int)v[0], 0, BM_N - 1); o->opacity = (u8)clampi((int)v[1], 0, 255); o->style = (u8)clampi((int)v[2], 0, 255);
      o->color = (u32)v[3] & 0xFFFFFF; o->color2 = (u32)v[4] & 0xFFFFFF;
      o->angle = (i16)clampi((int)v[5], -360, 360); o->distance = (u16)clampi((int)v[6], 0, FX_MAXDIST);
      o->size = (u16)clampi((int)v[7], 0, FX_MAXSIZE); o->spread = (u8)clampi((int)v[8], 0, 100);
      o->altitude = (u8)clampi((int)v[9], 0, 90); o->depth = (u16)clampi((int)v[10], 1, 1000); o->flags = (u8)clampi((int)v[11], 0, 7);
      o->blend2 = (u8)clampi((int)v[12], 0, BM_N - 1); o->opacity2 = (u8)clampi((int)v[13], 0, 255);
      o->dx = (i16)clampi((int)v[14], -MAXDIM, MAXDIM); o->dy = (i16)clampi((int)v[15], -MAXDIM, MAXDIM);
      any = 1;
    }
  }
  sanitize(fx);
  return any;
}
