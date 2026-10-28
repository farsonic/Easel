/*
 * adjust10.c - Easel 1.0's adjustments (see adjust10.h): Hue/Saturation's six ranges, Black & White's
 * six sliders and tint, Replace Color, Shadows/Highlights, Match Color, per-channel Curves, the
 * Levels and Curves eyedroppers, and presets.
 *
 * The pixel functions are what adjustment layers run (through adjust.c's colour cube, or pixel by
 * pixel for Replace Color, whose edge is too sharp for the cube); the destructive forms hand the
 * same settings to adj_apply, so a layer and Image > Adjustments give the same pixels.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "adjust10.h"
#include "adjust.h"
#include "util.h"

static int clamp255(double v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5); }

/* ------------------------------------------------------------------ HSL (as adjust.c's) */
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
/* hue, saturation and lightness moved (non-colorize Hue/Saturation, as adjust.c's fn_hsl) */
static void hsl_shift(u8 *p, double dh, double ds, double dl)
{
  double h, s, l;
  rgb_hsl(p, &h, &s, &l);
  h += dh;
  s = ds >= 0 ? s + (1 - s) * ds / 100.0 * (s > 0 ? 1 : 0) : s * (1 + ds / 100.0);
  if (s < 0) s = 0;
  if (s > 1) s = 1;
  l = dl >= 0 ? l + (1 - l) * dl / 100.0 : l * (1 + dl / 100.0);
  hsl_rgb(h, s, l, p);
}

/* ------------------------------------------------------------------ Hue/Saturation with ranges */
/* a[0..3] master (hue, saturation, lightness, colorize); a[4 + 5 r ..] range r */
static void fn_hs10(u8 *p, const double *a)
{
  double dh = a[0], ds = a[1], dl = a[2], h, s, l, chroma;
  int r, mx, mn;
  if (a[3]) {
    /* colorize: as Easel 0.9 did */
    rgb_hsl(p, &h, &s, &l);
    h = a[0] < 0 ? a[0] + 360 : a[0]; s = (a[1] + 100) / 200.0;
    if (s < 0.25) s = 0.25 * (1 + a[1] / 100.0);
    if (s < 0) s = 0;
    if (s > 1) s = 1;
    l = a[2] >= 0 ? l + (1 - l) * a[2] / 100.0 : l * (1 + a[2] / 100.0);
    hsl_rgb(h, s, l, p);
    return;
  }
  mx = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
  mn = p[0] < p[1] ? (p[0] < p[2] ? p[0] : p[2]) : (p[1] < p[2] ? p[1] : p[2]);
  chroma = (mx - mn) / 255.0;
  if (chroma > 0) {
    rgb_hsl(p, &h, &s, &l);
    for (r = 0; r < 6; r++) {
      const double *g = a + 4 + 5 * r;
      double dist, w;
      if (!g[0] && !g[1] && !g[2]) continue;
      dist = fabs(fmod(h - r * 60 + 540, 360) - 180);       /* 0-180 degrees from the range's centre */
      if (dist <= g[3]) w = 1;
      else if (dist >= g[3] + g[4]) w = 0;
      else w = 1 - (dist - g[3]) / g[4];
      /* nearly grey pixels have little hue to go by */
      if (chroma < 0.08) w *= chroma / 0.08;
      dh += w * g[0]; ds += w * g[1]; dl += w * g[2];
    }
  }
  if (dh == 0 && ds == 0 && dl == 0) return;
  if (ds < -100) ds = -100;
  if (ds > 100) ds = 100;
  if (dl < -100) dl = -100;
  if (dl > 100) dl = 100;
  hsl_shift(p, dh, ds, dl);
}

/* ------------------------------------------------------------------ Black & White, six colours */
/* a[0..5] reds, yellows, greens, cyans, blues, magentas (per cent); a[6] tint, a[7] hue, a[8] sat */
static void fn_bw6(u8 *p, const double *a)
{
  int r = p[0], g = p[1], b = p[2], mx, md, mn, imax, isec;
  double grey;
  /* the largest channel's primary, and the secondary of the largest two */
  if (r >= g && r >= b) { imax = 0; mx = r; if (g >= b) { md = g; mn = b; isec = 1; } else { md = b; mn = g; isec = 5; } }
  else if (g >= b) { imax = 2; mx = g; if (r >= b) { md = r; mn = b; isec = 1; } else { md = b; mn = r; isec = 3; } }
  else { imax = 4; mx = b; if (g >= r) { md = g; mn = r; isec = 3; } else { md = r; mn = g; isec = 5; } }
  grey = mn + (md - mn) * a[isec] / 100.0 + (mx - md) * a[imax] / 100.0;
  if (grey < 0) grey = 0;
  if (grey > 255) grey = 255;
  if (a[6]) { hsl_rgb(a[7], a[8] / 100.0, grey / 255.0, p); return; }
  p[0] = p[1] = p[2] = (u8)clamp255(grey);
}

/* ------------------------------------------------------------------ Replace Color */
/* a[0..2] the colour, a[3] fuzziness, a[4..6] hue, saturation, lightness */
static double near_color(const u8 *p, const double *c, double fuzz)
{
  double dr = p[0] - c[0], dg = p[1] - c[1], db = p[2] - c[2], dist = sqrt(dr * dr + dg * dg + db * db);
  if (fuzz < 1) return dist < 0.5;
  return dist >= fuzz ? 0 : 1 - dist / fuzz;
}
static void fn_replace(u8 *p, const double *a)
{
  double w = near_color(p, a, a[3]);
  u8 q[3];
  int c;
  if (w <= 0 || (!a[4] && !a[5] && !a[6])) return;
  q[0] = p[0]; q[1] = p[1]; q[2] = p[2];
  hsl_shift(q, a[4], a[5], a[6]);
  for (c = 0; c < 3; c++) p[c] = (u8)clamp255(p[c] + (q[c] - p[c]) * w);
}

int adj10_pixfn(const AdjParams *p, Adj10Fn *fn, double *a, int *exact)
{
  int i;
  *exact = 0;
  switch (p->kind) {
  case AK_HUESAT:
    if (p->npts != 15) return 0;
    for (i = 0; i < 4; i++) a[i] = p->v[i];
    for (i = 0; i < 30; i++) a[4 + i] = p->pts[i];
    for (i = 0; i < 6; i++) {
      if (a[4 + 5 * i + 3] < 0) a[4 + 5 * i + 3] = 0;
      if (a[4 + 5 * i + 4] < 1) a[4 + 5 * i + 4] = 1;
    }
    *fn = fn_hs10;
    {
      /* nothing to do: the identity (tables, so it is exact) */
      int any = p->v[0] || p->v[1] || p->v[2] || p->v[3];
      for (i = 0; i < 6 && !any; i++) any = p->pts[5 * i] || p->pts[5 * i + 1] || p->pts[5 * i + 2];
      if (!any) return 2;
    }
    return 1;
  case AK_BLACKWHITE:
    if (p->v[9] != 1) return 0;
    for (i = 0; i < 9; i++) a[i] = p->v[i];
    *fn = fn_bw6;
    return 1;
  case AK_REPLACECOLOR:
    a[0] = ((u32)p->v[0] >> 16) & 255; a[1] = ((u32)p->v[0] >> 8) & 255; a[2] = (u32)p->v[0] & 255;
    a[3] = p->v[1] < 0 ? 0 : p->v[1] > 200 ? 200 : p->v[1];
    a[4] = p->v[2]; a[5] = p->v[3]; a[6] = p->v[4];
    *fn = fn_replace; *exact = 1;
    return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------ per-channel Curves */
void adj10_curves_lut(const AdjParams *p, u8 *lut)
{
  u8 t[4][256];
  int c, i;
  for (c = 0; c < 4; c++) {
    int n = p->v[2 + c];
    if (n > CURVE10_MAX) n = CURVE10_MAX;
    if (n < 2) { for (i = 0; i < 256; i++) t[c][i] = (u8)i; }
    else curve_lut(t[c], p->pts + 16 * c, n);
  }
  for (c = 0; c < 3; c++) for (i = 0; i < 256; i++) lut[256 * c + i] = t[0][t[c + 1][i]];
}
void curves10_convert(AdjParams *p)
{
  int old[64], n = p->npts, ch = p->v[0], c, k;
  if (p->kind != AK_CURVES || p->v[1] == 1) return;
  if (n > 32) n = 32;
  memcpy(old, p->pts, sizeof old);
  memset(p->pts, 0, sizeof p->pts);
  for (c = 0; c < 4; c++) { p->v[2 + c] = 2; p->pts[16 * c] = 0; p->pts[16 * c + 1] = 0; p->pts[16 * c + 2] = 255; p->pts[16 * c + 3] = 255; }
  if (ch < 0 || ch > 3) ch = 0;
  if (n >= 2) {
    /* more than eight points: every so many kept, with both ends */
    int m = n > CURVE10_MAX ? CURVE10_MAX : n;
    for (k = 0; k < m; k++) {
      int src = m == n ? k : (int)((long)k * (n - 1) / (m - 1));
      p->pts[16 * ch + 2 * k] = old[2 * src]; p->pts[16 * ch + 2 * k + 1] = old[2 * src + 1];
    }
    p->v[2 + ch] = m;
  }
  p->v[1] = 1; p->v[0] = ch; p->npts = 32;
}
void adj10_defaults(int kind, AdjParams *p)
{
  int r;
  switch (kind) {
  case AK_HUESAT:
    p->npts = 15;
    for (r = 0; r < 6; r++) { p->pts[5 * r + 3] = 15; p->pts[5 * r + 4] = 30; }
    break;
  case AK_BLACKWHITE:
    p->v[0] = 40; p->v[1] = 60; p->v[2] = 40; p->v[3] = 60; p->v[4] = 20; p->v[5] = 80;
    p->v[6] = 0; p->v[7] = 42; p->v[8] = 20; p->v[9] = 1;
    break;
  case AK_REPLACECOLOR: p->v[0] = 0xFF0000; p->v[1] = 40; break;
  default: break;
  }
}

int adj_hue_saturation_ranges(Doc *d, const int *master4, const int *ranges30)
{
  AdjParams p;
  memset(&p, 0, sizeof p);
  p.kind = AK_HUESAT; p.npts = 15;
  memcpy(p.v, master4, 4 * sizeof(int));
  memcpy(p.pts, ranges30, 30 * sizeof(int));
  return adj_apply(d, &p, "Hue/Saturation");
}
int adj_black_white6(Doc *d, const int *w6, int tint, int tint_hue, int tint_sat)
{
  AdjParams p;
  memset(&p, 0, sizeof p);
  p.kind = AK_BLACKWHITE;
  memcpy(p.v, w6, 6 * sizeof(int));
  p.v[6] = tint; p.v[7] = tint_hue; p.v[8] = tint_sat; p.v[9] = 1;
  return adj_apply(d, &p, "Black & White");
}
int adj_replace_color(Doc *d, u32 color, int fuzziness, int hue, int sat, int light)
{
  AdjParams p;
  memset(&p, 0, sizeof p);
  p.kind = AK_REPLACECOLOR;
  p.v[0] = (int)(color & 0xFFFFFF); p.v[1] = fuzziness; p.v[2] = hue; p.v[3] = sat; p.v[4] = light;
  return adj_apply(d, &p, "Replace Color");
}

/* ------------------------------------------------------------------ the frame for the rest */
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
  if (d->sel)
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

/* a plane of w x h floats blurred in place by three box passes nearest a Gaussian of sigma */
static int blur_plane(float *pl, i32 w, i32 h, double sigma)
{
  int rad[3], p;
  float *line;
  i32 x, y;
  double wi;
  int wl, wu, m;
  if (sigma < 0.5) return 1;
  wi = sqrt(12 * sigma * sigma / 3 + 1);
  wl = (int)floor(wi);
  if (wl % 2 == 0) wl--;
  wu = wl + 2;
  m = (int)floor((12 * sigma * sigma - 3 * wl * wl - 12 * wl - 9) / (-4.0 * wl - 4) + 0.5);
  for (p = 0; p < 3; p++) rad[p] = ((p < m ? wl : wu) - 1) / 2;
  if (!(line = (float *)malloc((size_t)(w > h ? w : h) * sizeof(float)))) return 0;
  for (p = 0; p < 3; p++) {
    int r = rad[p];
    double div = 2 * r + 1;
    if (r < 1) continue;
    for (y = 0; y < h; y++) {          /* 1.0: progress */
      float *row = pl + (long)y * w;
      double sum = 0;
      memcpy(line, row, (size_t)w * sizeof(float));
      for (x = -r; x <= r; x++) sum += line[x < 0 ? 0 : x >= w ? w - 1 : x];
      for (x = 0; x < w; x++) {
        row[x] = (float)(sum / div);
        sum += line[x + r + 1 >= w ? w - 1 : x + r + 1] - line[x - r < 0 ? 0 : x - r];
      }
    }
    for (x = 0; x < w; x++) {
      double sum = 0;
      for (y = 0; y < h; y++) line[y] = pl[(long)y * w + x];
      for (y = -r; y <= r; y++) sum += line[y < 0 ? 0 : y >= h ? h - 1 : y];
      for (y = 0; y < h; y++) {
        pl[(long)y * w + x] = (float)(sum / div);
        sum += line[y + r + 1 >= h ? h - 1 : y + r + 1] - line[y - r < 0 ? 0 : y - r];
      }
    }
  }
  free(line);
  return 1;
}

/* ------------------------------------------------------------------ Shadows/Highlights */
int adj_shadows_highlights(Doc *d, int sha, int sht, int shr, int hla, int hlt, int hlr, int color, int midtone)
{
  Op o;
  float *ms = NULL, *mh = NULL;
  i32 bw, bh, x, y, mx0, my0, mw, mh_, margin;
  double as = sha / 100.0, ah = hla / 100.0, ts = (sht < 1 ? 1 : sht > 100 ? 100 : sht) * 2.55, th = (hlt < 1 ? 1 : hlt > 100 ? 100 : hlt) * 2.55;
  double cc = (color < -100 ? -100 : color > 100 ? 100 : color) / 100.0, mc = (midtone < -100 ? -100 : midtone > 100 ? 100 : midtone) / 100.0;
  if (sha <= 0 && hla <= 0 && midtone == 0) return 0;
  if (!op_begin(&o, d, "Shadows/Highlights")) return 0;
  bw = o.b.x1 - o.b.x0; bh = o.b.y1 - o.b.y0;
  /* the local brightness around each pixel: the box with a margin for the blur, blurred */
  margin = (shr > hlr ? shr : hlr) * 3;
  mx0 = o.b.x0 - margin < 0 ? 0 : o.b.x0 - margin; my0 = o.b.y0 - margin < 0 ? 0 : o.b.y0 - margin;
  mw = (o.b.x1 + margin > d->w ? d->w : o.b.x1 + margin) - mx0; mh_ = (o.b.y1 + margin > d->h ? d->h : o.b.y1 + margin) - my0;
  ms = (float *)malloc((size_t)mw * mh_ * sizeof(float));
  mh = (float *)malloc((size_t)mw * mh_ * sizeof(float));
  if (!ms || !mh) { free(ms); free(mh); op_end(&o); return 0; }
  for (y = 0; y < mh_; y++)
    for (x = 0; x < mw; x++) {
      const u8 *q = o.orig + ((long)(y + my0) * d->w + x + mx0) * 4;
      ms[(long)y * mw + x] = mh[(long)y * mw + x] = (float)(0.299 * q[0] + 0.587 * q[1] + 0.114 * q[2]);
    }
  if (!blur_plane(ms, mw, mh_, shr) || !blur_plane(mh, mw, mh_, hlr)) { free(ms); free(mh); op_end(&o); return 0; }
  (void)bw; (void)bh;
  for (y = o.b.y0; y < o.b.y1; y++) {          /* 1.0: progress */
    for (x = o.b.x0; x < o.b.x1; x++) {
      u8 *p = o.l->px + ((long)y * d->w + x) * 4;
      long mk = (long)(y - my0) * mw + (x - mx0);
      double L = 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2], L2 = L, M, w, k, ratio;
      int c;
      if (!p[3]) continue;
      /* shadows lifted where the surroundings are dark, by a gamma (black stays black) */
      M = ms[mk];
      if (as > 0 && M < ts) { w = 1 - M / ts; w *= w; L2 = 255 * pow(L2 / 255, 1 / (1 + 2 * as * w)); }
      /* highlights brought down where the surroundings are bright */
      M = mh[mk];
      if (ah > 0 && M > 255 - th) { w = 1 - (255 - M) / th; w *= w; L2 = 255 - 255 * pow((255 - L2) / 255, 1 / (1 + 2 * ah * w)); }
      if (mc != 0) { w = 1 - fabs(L2 - 127.5) / 127.5; L2 += (L2 - 127.5) * mc * w; }
      if (L2 < 0) L2 = 0;
      if (L2 > 255) L2 = 255;
      /* the colours follow: offsets from the grey kept, or (color correction) scaled with it */
      ratio = (L2 + 1) / (L + 1);
      k = 1 + (ratio - 1) * (0.5 + cc / 2);
      for (c = 0; c < 3; c++) p[c] = (u8)clamp255(L2 + (p[c] - L) * k);
    }
  }
  free(ms); free(mh);
  return op_end(&o);
}

/* ------------------------------------------------------------------ Match Color */
static void ycc(const u8 *p, double *o)
{
  o[0] = 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
  o[1] = -0.168736 * p[0] - 0.331264 * p[1] + 0.5 * p[2];
  o[2] = 0.5 * p[0] - 0.418688 * p[1] - 0.081312 * p[2];
}
/* mean and spread of each of Y, Cb, Cr, weighted (sel, alpha) */
static void stats(const u8 *px, i32 w, i32 h, Box b, const u8 *sel, double *mean, double *sd)
{
  double s[3] = { 0, 0, 0 }, s2[3] = { 0, 0, 0 }, n = 0, v[3];
  i32 x, y;
  int c;
  for (y = b.y0; y < b.y1; y++)
    for (x = b.x0; x < b.x1; x++) {
      long k = (long)y * w + x;
      const u8 *p = px + k * 4;
      double wt = p[3] / 255.0 * (sel ? sel[k] / 255.0 : 1);
      if (wt <= 0) continue;
      ycc(p, v);
      for (c = 0; c < 3; c++) { s[c] += v[c] * wt; s2[c] += v[c] * v[c] * wt; }
      n += wt;
    }
  for (c = 0; c < 3; c++) {
    double m = n > 0 ? s[c] / n : 0, var = n > 0 ? s2[c] / n - m * m : 0;
    mean[c] = m; sd[c] = var > 1e-6 ? sqrt(var) : 0;
  }
  (void)h;
}
int adj_match_color(Doc *d, const u8 *src, i32 sw, i32 sh, int luminance, int intensity, int fade, int neutralize)
{
  Op o;
  double mt[3], st[3], ms[3] = { 0, 0, 0 }, ss[3] = { 1, 1, 1 }, gain[3], to[3];
  double lf = (luminance < 1 ? 1 : luminance > 200 ? 200 : luminance) / 100.0, cf = (intensity < 0 ? 0 : intensity > 200 ? 200 : intensity) / 100.0;
  double fd = (fade < 0 ? 0 : fade > 100 ? 100 : fade) / 100.0;
  i32 x, y;
  int c;
  if (!src && !neutralize && luminance == 100 && intensity == 100) return 0;
  if (!op_begin(&o, d, "Match Color")) return 0;
  stats(o.orig, d->w, d->h, o.b, d->sel, mt, st);
  if (src) stats(src, sw, sh, box_make(0, 0, sw, sh), NULL, ms, ss);
  for (c = 0; c < 3; c++) {
    gain[c] = src && st[c] > 0.5 && ss[c] > 0 ? ss[c] / st[c] : 1;
    to[c] = src ? ms[c] : mt[c];
  }
  if (neutralize) to[1] = to[2] = 0;
  for (y = o.b.y0; y < o.b.y1; y++)          /* 1.0: progress */
    for (x = o.b.x0; x < o.b.x1; x++) {
      u8 *p = o.l->px + ((long)y * d->w + x) * 4;
      double v[3], Y, Cb, Cr, r, g, b;
      if (!p[3]) continue;
      ycc(p, v);
      Y = ((v[0] - mt[0]) * gain[0] + to[0]) * lf;
      Cb = ((v[1] - mt[1]) * gain[1] + to[1]) * cf;
      Cr = ((v[2] - mt[2]) * gain[2] + to[2]) * cf;
      r = Y + 1.402 * Cr; g = Y - 0.344136 * Cb - 0.714136 * Cr; b = Y + 1.772 * Cb;
      p[0] = (u8)clamp255(r + (p[0] - r) * fd);
      p[1] = (u8)clamp255(g + (p[1] - g) * fd);
      p[2] = (u8)clamp255(b + (p[2] - b) * fd);
    }
  return op_end(&o);
}

/* ------------------------------------------------------------------ eyedroppers */
static void levels_rows(AdjParams *p)
{
  int k;
  if (p->npts >= 15) return;
  for (k = 0; k < 3; k++) { p->pts[k * 5] = 0; p->pts[k * 5 + 1] = 255; p->pts[k * 5 + 2] = 100; p->pts[k * 5 + 3] = 0; p->pts[k * 5 + 4] = 255; }
  p->npts = 15;
}
void adj_levels_pick(AdjParams *p, int which, u32 col)
{
  int ch, v[3];
  double x[3], t = 0;
  v[0] = (int)((col >> 16) & 255); v[1] = (int)((col >> 8) & 255); v[2] = (int)(col & 255);
  levels_rows(p);
  for (ch = 0; ch < 3; ch++) {
    int *row = p->pts + ch * 5;
    if (row[1] <= 0) row[1] = 255;
    if (which == 0) row[0] = v[ch] > row[1] - 2 ? row[1] - 2 : v[ch];
    else if (which == 2) row[1] = v[ch] < row[0] + 2 ? row[0] + 2 : v[ch];
    if (row[0] < 0) row[0] = 0;
    if (row[1] > 255) row[1] = 255;
    x[ch] = (v[ch] - row[0]) / (double)(row[1] - row[0]);
    if (x[ch] < 0.02) x[ch] = 0.02;
    if (x[ch] > 0.98) x[ch] = 0.98;
    t += x[ch] / 3;
  }
  if (which == 1)
    for (ch = 0; ch < 3; ch++) {
      /* the gamma that takes this channel to the grey the three average: x^(1/g) = t */
      double g = log(x[ch]) / log(t);
      int gi = (int)(g * 100 + 0.5);
      p->pts[ch * 5 + 2] = gi < 10 ? 10 : gi > 999 ? 999 : gi;
    }
}
/* a point put in curve c (at pts + 16 c, n points): black takes away those left of it, white those
   right of it; grey replaces one close to it */
static void curve_put(AdjParams *p, int c, int which, int x, int y)
{
  int *q = p->pts + 16 * c, n = p->v[2 + c], k, m, out[2 * CURVE10_MAX];
  if (n < 2) { n = 2; q[0] = 0; q[1] = 0; q[2] = 255; q[3] = 255; }
  if (x < 1) x = 1;
  if (x > 254) x = 254;
  m = 0;
  if (which == 0) {
    out[m++] = x; out[m++] = y;
    for (k = 0; k < n; k++) if (q[2 * k] > x && m < 2 * CURVE10_MAX) { out[m++] = q[2 * k]; out[m++] = q[2 * k + 1]; }
    if (m < 4) { out[m++] = 255; out[m++] = 255; }
  } else if (which == 2) {
    for (k = 0; k < n; k++) if (q[2 * k] < x && m < 2 * CURVE10_MAX - 2) { out[m++] = q[2 * k]; out[m++] = q[2 * k + 1]; }
    if (m < 2) { out[m++] = 0; out[m++] = 0; }
    out[m++] = x; out[m++] = y;
  } else {
    int placed = 0, drop = -1, best = 6;
    /* an interior point close to it goes; when there is none and the curve is full, nothing changes */
    for (k = 1; k < n - 1; k++) { int dd = abs(q[2 * k] - x); if (dd < best) { best = dd; drop = k; } }
    if (drop < 0 && n >= CURVE10_MAX) return;
    for (k = 0; k < n; k++) {
      if (k == drop) continue;
      if (!placed && q[2 * k] > x) { out[m++] = x; out[m++] = y; placed = 1; }
      if (q[2 * k] == x) continue;
      out[m++] = q[2 * k]; out[m++] = q[2 * k + 1];
    }
    if (!placed) { out[m++] = x; out[m++] = y; }
  }
  memcpy(q, out, (size_t)m * sizeof(int));
  p->v[2 + c] = m / 2;
}
void adj_curves_pick(AdjParams *p, int which, u32 col)
{
  int v[3], c, t = 0;
  v[0] = (int)((col >> 16) & 255); v[1] = (int)((col >> 8) & 255); v[2] = (int)(col & 255);
  if (p->kind != AK_CURVES) return;
  curves10_convert(p);
  if (which == 1) {
    /* grey: each channel through its curve to the average of where they are now */
    u8 lut[768];
    adj10_curves_lut(p, lut);
    for (c = 0; c < 3; c++) t += lut[256 * c + v[c]];
    t /= 3;
  }
  for (c = 0; c < 3; c++) curve_put(p, c + 1, which, v[c], which == 0 ? 0 : which == 2 ? 255 : t);
}

/* ------------------------------------------------------------------ presets */
typedef struct { const char *name; int v[10]; int pts[30]; } HsPreset;
static const char *const lev_names[] = { "Darker", "Increase Contrast 1", "Increase Contrast 2", "Increase Contrast 3", "Lighten Shadows", "Lighter", "Midtones Brighter", "Midtones Darker" };
static const int lev_vals[][5] = { { 15, 255, 75, 0, 255 }, { 10, 245, 100, 0, 255 }, { 20, 235, 100, 0, 255 }, { 30, 225, 100, 0, 255 },
                                   { 0, 255, 160, 0, 255 }, { 0, 230, 125, 0, 255 }, { 0, 255, 125, 0, 255 }, { 0, 255, 80, 0, 255 } };
static const char *const cur_names[] = { "Color Negative", "Cross Process", "Darker", "Increase Contrast", "Lighter", "Linear Contrast", "Medium Contrast", "Negative", "Strong Contrast" };
/* each: four curves as (count, x y ...) */
static const int cur_vals[][4][17] = {
  { { 2, 0, 0, 255, 255 }, { 2, 40, 255, 220, 0 }, { 2, 25, 255, 185, 0 }, { 2, 15, 255, 145, 0 } },
  { { 2, 0, 0, 255, 255 }, { 4, 0, 0, 64, 46, 192, 222, 255, 255 }, { 4, 0, 0, 64, 40, 192, 215, 255, 255 }, { 2, 0, 40, 255, 215 } },
  { { 3, 0, 0, 130, 100, 255, 255 }, { 0 }, { 0 }, { 0 } },
  { { 4, 0, 0, 64, 52, 191, 203, 255, 255 }, { 0 }, { 0 }, { 0 } },
  { { 3, 0, 0, 110, 140, 255, 255 }, { 0 }, { 0 }, { 0 } },
  { { 4, 0, 0, 64, 48, 191, 207, 255, 255 }, { 0 }, { 0 }, { 0 } },
  { { 4, 0, 0, 64, 57, 191, 198, 255, 255 }, { 0 }, { 0 }, { 0 } },
  { { 2, 0, 255, 255, 0 }, { 0 }, { 0 }, { 0 } },
  { { 4, 0, 0, 64, 37, 191, 218, 255, 255 }, { 0 }, { 0 }, { 0 } }
};
/* Hue/Saturation: master hue, sat, light, colorize; then the ranges' (hue, sat, light) changes */
static const HsPreset hs_presets[] = {
  { "Cyanotype", { 200, -50, 0, 1 }, { 0 } },
  { "Increase Saturation", { 0, 15, 0, 0 }, { 0 } },
  { "Increase Saturation More", { 0, 30, 0, 0 }, { 0 } },
  { "Old Style", { 0, -60, 5, 0 }, { 0 } },
  { "Red Boost", { 0, 0, 0, 0 }, { 0, 35, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { "Sepia", { 35, -50, 0, 1 }, { 0 } },
  { "Strong Saturation", { 0, 50, 0, 0 }, { 0 } },
  { "Yellow Boost", { 0, 0, 0, 0 }, { 0, 0, 0, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } }
};
static const char *const bw_names[] = { "Default", "Blue Filter", "Green Filter", "High Contrast Blue Filter", "High Contrast Red Filter", "Infrared",
                                        "Maximum Black", "Maximum White", "Neutral Density", "Red Filter", "Yellow Filter" };
static const int bw_vals[][6] = { { 40, 60, 40, 60, 20, 80 }, { 0, 0, 0, 110, 110, 110 }, { 40, 40, 160, 40, 20, 80 }, { -50, -50, -50, 150, 150, 150 },
                                  { 120, 120, -10, -50, -50, 120 }, { -40, 235, 144, -68, -3, -107 }, { 0, 0, 0, 0, 0, 0 }, { 100, 100, 100, 100, 100, 100 },
                                  { 128, 128, 100, 100, 128, 100 }, { 120, 110, -10, -50, -50, 120 }, { 120, 110, 40, -30, 0, 70 } };
#define NOF(a) (int)(sizeof a / sizeof a[0])
int adj_preset_count(int kind)
{
  switch (kind) {
  case AK_LEVELS: return NOF(lev_names);
  case AK_CURVES: return NOF(cur_names);
  case AK_HUESAT: return NOF(hs_presets);
  case AK_BLACKWHITE: return NOF(bw_names);
  }
  return 0;
}
const char *adj_preset_name(int kind, int i)
{
  if (i < 0 || i >= adj_preset_count(kind)) return "";
  switch (kind) {
  case AK_LEVELS: return lev_names[i];
  case AK_CURVES: return cur_names[i];
  case AK_HUESAT: return hs_presets[i].name;
  case AK_BLACKWHITE: return bw_names[i];
  }
  return "";
}
void adj_preset_get(int kind, int i, AdjParams *p)
{
  int k, c;
  adj_defaults(kind, p);
  if (i < 0 || i >= adj_preset_count(kind)) return;
  switch (kind) {
  case AK_LEVELS: for (k = 0; k < 5; k++) p->v[k] = lev_vals[i][k]; break;
  case AK_CURVES:
    curves10_convert(p);
    for (c = 0; c < 4; c++) {
      int n = cur_vals[i][c][0];
      if (n < 2) continue;
      p->v[2 + c] = n;
      for (k = 0; k < 2 * n; k++) p->pts[16 * c + k] = cur_vals[i][c][1 + k];
    }
    break;
  case AK_HUESAT:
    for (k = 0; k < 4; k++) p->v[k] = hs_presets[i].v[k];
    for (c = 0; c < 6; c++) for (k = 0; k < 3; k++) p->pts[5 * c + k] = hs_presets[i].pts[3 * c + k];
    break;
  case AK_BLACKWHITE: for (k = 0; k < 6; k++) p->v[k] = bw_vals[i][k]; break;
  }
}
int adj_preset_format(const AdjParams *p, char *out, int cap)
{
  Buf b;
  int k, n = p->npts * 2;
  buf_init(&b);
  buf_printf(&b, "EaselAdjust 1\nkind %s\nv", p->kind < AK_N ? adj_names[p->kind] : "?");
  for (k = 0; k < 16; k++) buf_printf(&b, " %d", p->v[k]);
  if (p->kind == AK_SELECTIVECOLOR && n < 36) n = 36;
  if (n > 64) n = 64;
  buf_printf(&b, "\nnpts %d\npts", p->npts);
  for (k = 0; k < n; k++) buf_printf(&b, " %d", p->pts[k]);
  buf_adds(&b, "\n");
  if (b.oom || (int)b.len + 1 > cap) { buf_free(&b); return 0; }
  memcpy(out, b.data, b.len + 1);
  k = (int)b.len;
  buf_free(&b);
  return k;
}
/* the numbers after a word at the start of a line */
static const char *line_after(const char *text, const char *word)
{
  size_t n = strlen(word);
  const char *s = text;
  while (s && *s) {
    if (!strncmp(s, word, n) && (s[n] == ' ' || s[n] == '\t')) return s + n;
    s = strchr(s, '\n');
    if (s) s++;
  }
  return NULL;
}
static int read_nums(const char *s, int *out, int max)
{
  int n = 0;
  char *e;
  while (s && n < max) {
    long v;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\n' || !*s) break;
    v = strtol(s, &e, 10);
    if (e == s) break;
    out[n++] = (int)v;
    s = e;
  }
  return n;
}
int adj_preset_parse(const char *text, AdjParams *p)
{
  const char *s;
  int k, kind = -1, n;
  char name[40];
  if (!text || strncmp(text, "EaselAdjust 1", 13)) return 0;
  if (!(s = line_after(text, "kind"))) return 0;
  while (*s == ' ') s++;
  for (k = 0; k < 39 && s[k] && s[k] != '\n' && s[k] != '\r'; k++) name[k] = s[k];
  name[k] = 0;
  for (k = 0; k < AK_N; k++) if (!strcmp(name, adj_names[k])) kind = k;
  if (kind < 0) return 0;
  adj_defaults(kind, p);
  memset(p->v, 0, sizeof p->v);
  memset(p->pts, 0, sizeof p->pts);
  if ((s = line_after(text, "v"))) read_nums(s, p->v, 16);
  p->npts = 0;
  if ((s = line_after(text, "npts"))) { int np = 0; if (read_nums(s, &np, 1) == 1) p->npts = np < 0 ? 0 : np > 32 ? 32 : np; }
  if ((s = line_after(text, "pts"))) { n = read_nums(s, p->pts, 64); if (p->npts * 2 > n && p->kind != AK_SELECTIVECOLOR) p->npts = n / 2; }
  if (kind == AK_CURVES && p->v[1] == 1) for (k = 0; k < 4; k++) if (p->v[2 + k] > CURVE10_MAX) p->v[2 + k] = CURVE10_MAX;
  return 1;
}
int adj_preset_save(const AdjParams *p, const char *path)
{
  char buf[1024];
  int n = adj_preset_format(p, buf, sizeof buf);
  return n > 0 && file_write(path, buf, (size_t)n);
}
int adj_preset_load(const char *path, AdjParams *p)
{
  size_t len;
  u8 *data = file_read(path, &len);
  int ok;
  if (!data) return 0;
  ok = adj_preset_parse((const char *)data, p);
  free(data);
  return ok;
}

/* ------------------------------------------------------------------ Photo Filter */
const char *const photo_filter_names[PHOTO_FILTERS] = {
  "Warming Filter (85)", "Warming Filter (LBA)", "Warming Filter (81)", "Cooling Filter (80)", "Cooling Filter (LBB)", "Cooling Filter (82)",
  "Red", "Orange", "Yellow", "Green", "Cyan", "Blue", "Violet", "Magenta", "Sepia", "Deep Red", "Deep Blue", "Deep Emerald", "Deep Yellow", "Underwater"
};
const u32 photo_filter_colors[PHOTO_FILTERS] = {
  0xEC8A00, 0xFA9600, 0xEBB113, 0x006DFF, 0x005DFF, 0x00B5FF, 0xEA1A1A, 0xF38417, 0xF9E31C, 0x19C900, 0x1DCBEA, 0x1D35EA, 0x9B1DEA,
  0xE318E3, 0xAC7A33, 0xFF0000, 0x0022CD, 0x008C00, 0xFFD500, 0x00C2B1
};
