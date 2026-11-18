/*
 * selr3.c - Select > Sky and Subject, Decontaminate Colors (see selr3.h). Easel round 3.
 *
 * Select Sky's colour prior and region growing follow PhotoCraft (https://github.com/storytold/photocraft),
 * Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors, used under the MIT License (see
 * Licenses.txt): crates/algo/src/segment/sky.rs.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "util.h"
#include "selr3.h"
#include "channels.h"
#include "selsmart.h"
#include "paint.h"

#define WORK_PX 160000L

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
static double smooth(double e0, double e1, double x) { double t = clampd((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t); }

/* ------------------------------------------------------------------ the picture, smaller */
typedef struct { i32 w, h, f; float *px; } Work;     /* RGB 0-1, f full pixels a side per work pixel */
static int work_make(Doc *d, Work *k, long budget)
{
  i32 x, y, f = 1, yy;
  u8 *row;
  double *acc;
  while ((long)((d->w + f - 1) / f) * ((d->h + f - 1) / f) > budget) f++;
  k->f = f; k->w = (d->w + f - 1) / f; k->h = (d->h + f - 1) / f;
  k->px = (float *)malloc((size_t)k->w * k->h * 3 * sizeof(float));
  row = (u8 *)malloc((size_t)d->w * 4); acc = (double *)malloc((size_t)k->w * 4 * sizeof(double));
  if (!k->px || !row || !acc) { free(k->px); free(row); free(acc); k->px = NULL; return 0; }
  for (y = 0; y < k->h; y++) {
    if (progress(y, k->h * 3L)) break;
    memset(acc, 0, (size_t)k->w * 4 * sizeof(double));
    for (yy = y * f; yy < (y + 1) * f && yy < d->h; yy++) {
      doc_composite(d, box_make(0, yy, d->w, yy + 1), row, (long)d->w * 4);
      for (x = 0; x < d->w; x++) {
        double *a = acc + (x / f) * 4, al = row[x * 4 + 3] / 255.0;
        /* (transparency counts as white, as the picture is shown on paper) */
        a[0] += row[x * 4] / 255.0 * al + (1 - al); a[1] += row[x * 4 + 1] / 255.0 * al + (1 - al); a[2] += row[x * 4 + 2] / 255.0 * al + (1 - al); a[3] += 1;
      }
    }
    for (x = 0; x < k->w; x++) {
      double *a = acc + x * 4;
      int c;
      for (c = 0; c < 3; c++) k->px[((long)y * k->w + x) * 3 + c] = (float)(a[3] > 0 ? a[c] / a[3] : 1);
    }
  }
  free(row); free(acc);
  if (progress_aborted) { free(k->px); k->px = NULL; return 0; }
  return 1;
}
static double luma(const float *p) { return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]; }
/* box mean of radius r (edges repeated) of a w x h plane, in place */
static int box_mean(float *p, i32 w, i32 h, int r)
{
  float *t = (float *)malloc((size_t)(w > h ? w : h) * sizeof(float));
  i32 i, j;
  if (!t) return 0;
  for (j = 0; j < h; j++) {
    float *q = p + (long)j * w;
    double acc = 0;
    memcpy(t, q, (size_t)w * sizeof(float));
    for (i = -r; i <= r; i++) acc += t[i < 0 ? 0 : i >= w ? w - 1 : i];
    for (i = 0; i < w; i++) { i32 a = i + r + 1, b = i - r; q[i] = (float)(acc / (2 * r + 1)); acc += t[a >= w ? w - 1 : a] - t[b < 0 ? 0 : b]; }
  }
  for (i = 0; i < w; i++) {
    double acc = 0;
    for (j = 0; j < h; j++) t[j] = p[(long)j * w + i];
    for (j = -r; j <= r; j++) acc += t[j < 0 ? 0 : j >= h ? h - 1 : j];
    for (j = 0; j < h; j++) { i32 a = j + r + 1, b = j - r; p[(long)j * w + i] = (float)(acc / (2 * r + 1)); acc += t[a >= h ? h - 1 : a] - t[b < 0 ? 0 : b]; }
  }
  free(t);
  return 1;
}

/* ------------------------------------------------------------------ cleaning a mask */
/* components of value v smaller than minpx turned to !v (4-connected) */
static int drop_small(u8 *m, i32 w, i32 h, int v, long minpx)
{
  long n = (long)w * h, k, *stack = (long *)malloc((size_t)n * sizeof(long)), top, cnt, s;
  u8 *seen = (u8 *)calloc((size_t)n, 1);
  if (!stack || !seen) { free(stack); free(seen); return 0; }
  for (s = 0; s < n; s++) {
    if (seen[s] || (m[s] != 0) != (v != 0)) continue;
    /* one component: found, then counted */
    top = 0; stack[top++] = s; seen[s] = 1; cnt = 0;
    while (top > cnt) {
      long i = stack[cnt++], x = i % w, nb[4];
      int q;
      nb[0] = x > 0 ? i - 1 : -1; nb[1] = x + 1 < w ? i + 1 : -1; nb[2] = i >= w ? i - w : -1; nb[3] = i + w < n ? i + w : -1;
      for (q = 0; q < 4; q++) if (nb[q] >= 0 && !seen[nb[q]] && (m[nb[q]] != 0) == (v != 0)) { seen[nb[q]] = 1; stack[top++] = nb[q]; }
    }
    if (cnt < minpx) for (k = 0; k < cnt; k++) m[stack[k]] = (u8)(v ? 0 : 1);
  }
  free(stack); free(seen);
  return 1;
}

/* ------------------------------------------------------------------ Sky */
static double colour_prior(const float *p)
{
  double r = p[0], g = p[1], b = p[2], y = luma(p), mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
  double sat = mx > 1e-4 ? (mx - mn) / mx : 0, blue, white, warm, best;
  blue = smooth(0.02, 0.12, b - r) * smooth(-0.12, 0.0, b - g) * smooth(0.18, 0.4, y);
  white = smooth(0.5, 0.75, y) * (1 - smooth(0.12, 0.3, sat));
  warm = 0.6 * smooth(0.55, 0.8, y) * smooth(0.0, 0.1, r - b) * (1 - smooth(0.05, 0.25, g - b - 0.2));
  best = blue > white ? blue : white;
  return best > warm ? best : warm;
}
static double rgb_dist(const float *a, const float *b) { return sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])); }
u8 *sky_mask(Doc *d, int threshold, int softness)
{
  Work k;
  i32 w, h, x, y;
  long n, i, top, sp = 0, *stack = NULL, cnt;
  float *like = NULL, *lum = NULL, *grad = NULL, *I = NULL, *P = NULL, *mI, *mP, *mII, *mIP;
  u8 *m = NULL, *out = NULL;
  double t = 0.12 + 0.6 * clampd(threshold, 0, 100) / 100.0, mean[3] = { 0, 0, 0 }, var = 0;
  int r, ok = 0;
  if (!work_make(d, &k, WORK_PX)) return NULL;
  w = k.w; h = k.h; n = (long)w * h;
  if (w < 2 || h < 2) { free(k.px); return NULL; }
  like = (float *)malloc((size_t)n * sizeof(float)); lum = (float *)malloc((size_t)n * sizeof(float)); grad = (float *)malloc((size_t)n * sizeof(float));
  m = (u8 *)calloc((size_t)n, 1); stack = (long *)malloc((size_t)n * sizeof(long));
  if (!like || !lum || !grad || !m || !stack) goto done;
  /* 1. the likelihood: the colour prior, where the picture is smooth */
  for (i = 0; i < n; i++) lum[i] = (float)luma(k.px + i * 3);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
#define L(xx, yy) lum[(long)((yy) < 0 ? 0 : (yy) >= h ? h - 1 : (yy)) * w + ((xx) < 0 ? 0 : (xx) >= w ? w - 1 : (xx))]
      double gx = (L(x + 1, y - 1) + 2 * L(x + 1, y) + L(x + 1, y + 1)) - (L(x - 1, y - 1) + 2 * L(x - 1, y) + L(x - 1, y + 1));
      double gy = (L(x - 1, y + 1) + 2 * L(x, y + 1) + L(x + 1, y + 1)) - (L(x - 1, y - 1) + 2 * L(x, y - 1) + L(x + 1, y - 1));
#undef L
      grad[(long)y * w + x] = (float)(sqrt(gx * gx + gy * gy) / 8);
    }
  if (!box_mean(grad, w, h, 2)) goto done;
  for (i = 0; i < n; i++) like[i] = (float)(colour_prior(k.px + i * 3) * (1 - smooth(0.015, 0.07, grad[i])));
  if (progress(1, 3)) goto done;
  /* 2. seeds: likely sky along the top (else the largest certain patch in the upper half) */
  top = h / 40 > 1 ? h / 40 : 1;
  for (i = 0; i < top * w; i++) if (like[i] > t) { m[i] = 1; stack[sp++] = i; }
  if (sp * 20 < top * w) {
    long best = 0, bstart = -1, upper = (long)w * ((h + 1) / 2);
    u8 *seen = (u8 *)calloc((size_t)n, 1);
    if (!seen) goto done;
    memset(m, 0, (size_t)n); sp = 0;
    for (i = 0; i < upper; i++) {
      long c0, c1;
      if (seen[i] || like[i] <= t + 0.1) continue;
      c0 = c1 = 0; stack[c1++] = i; seen[i] = 1;
      while (c0 < c1) {
        long j = stack[c0++], jx = j % w, nb[4];
        int q;
        nb[0] = jx > 0 ? j - 1 : -1; nb[1] = jx + 1 < w ? j + 1 : -1; nb[2] = j >= w ? j - w : -1; nb[3] = j + w < upper ? j + w : -1;
        for (q = 0; q < 4; q++) if (nb[q] >= 0 && !seen[nb[q]] && like[nb[q]] > t + 0.1 && rgb_dist(k.px + j * 3, k.px + nb[q] * 3) < 0.12) { seen[nb[q]] = 1; stack[c1++] = nb[q]; }
      }
      if (c1 > best) { best = c1; bstart = i; }
    }
    free(seen);
    if (bstart < 0 || best < (n + 199) / 200 || best < 16) goto done;
    m[bstart] = 1; stack[sp++] = bstart;
  }
  /* 3. growing: to neighbours that stay likely and change colour gradually */
  while (sp > 0) {
    long j = stack[--sp], jx = j % w, nb[4];
    int q;
    nb[0] = jx > 0 ? j - 1 : -1; nb[1] = jx + 1 < w ? j + 1 : -1; nb[2] = j >= w ? j - w : -1; nb[3] = j + w < n ? j + w : -1;
    for (q = 0; q < 4; q++) if (nb[q] >= 0 && !m[nb[q]] && like[nb[q]] > t && rgb_dist(k.px + j * 3, k.px + nb[q] * 3) < 0.12) { m[nb[q]] = 1; stack[sp++] = nb[q]; }
  }
  /* 4. sky through gaps: likely pixels near the grown sky's colour (its mean and spread) */
  for (i = 0, cnt = 0; i < n; i++) if (m[i]) { mean[0] += k.px[i * 3]; mean[1] += k.px[i * 3 + 1]; mean[2] += k.px[i * 3 + 2]; cnt++; }
  if (cnt < 16) goto done;
  mean[0] /= cnt; mean[1] /= cnt; mean[2] /= cnt;
  for (i = 0; i < n; i++) if (m[i]) { float mm[3]; mm[0] = (float)mean[0]; mm[1] = (float)mean[1]; mm[2] = (float)mean[2]; var += rgb_dist(k.px + i * 3, mm) * rgb_dist(k.px + i * 3, mm); }
  var = sqrt(var / cnt);
  for (i = 0; i < n; i++)
    if (!m[i] && like[i] > t + 0.1) {
      float mm[3];
      mm[0] = (float)mean[0]; mm[1] = (float)mean[1]; mm[2] = (float)mean[2];
      if (rgb_dist(k.px + i * 3, mm) <= 2 * var + 0.03) m[i] = 1;
    }
  /* 5. specks and small holes */
  if (!drop_small(m, w, h, 1, n / 500 > 4 ? n / 500 : 4) || !drop_small(m, w, h, 0, n / 100 > 4 ? n / 100 : 4)) goto done;
  if (progress(2, 3)) goto done;
  /* 6. the soft edge: a guided filter (He, Sun and Tang 2010) of the mask on the luminance */
  r = 1 + (int)(clampd(softness, 0, 100) / 100.0 * 6);
  I = (float *)malloc((size_t)n * 6 * sizeof(float));
  if (!I) goto done;
  P = I + n; mI = P + n; mP = mI + n; mII = mP + n; mIP = mII + n;
  for (i = 0; i < n; i++) { I[i] = lum[i]; P[i] = m[i] ? 1.0f : 0.0f; mI[i] = I[i]; mP[i] = P[i]; mII[i] = I[i] * I[i]; mIP[i] = I[i] * P[i]; }
  if (!box_mean(mI, w, h, r) || !box_mean(mP, w, h, r) || !box_mean(mII, w, h, r) || !box_mean(mIP, w, h, r)) goto done;
  for (i = 0; i < n; i++) {
    double vI = mII[i] - mI[i] * mI[i], cIP = mIP[i] - mI[i] * mP[i], a = cIP / (vI + 1e-3), b = mP[i] - a * mI[i];
    mII[i] = (float)a; mIP[i] = (float)b;
  }
  if (!box_mean(mII, w, h, r) || !box_mean(mIP, w, h, r)) goto done;
  for (i = 0; i < n; i++) P[i] = (float)clampd(mII[i] * I[i] + mIP[i], 0, 1);
  /* 7. at full size: bilinear */
  if (!(out = (u8 *)malloc((size_t)d->w * d->h))) goto done;
  for (y = 0; y < d->h; y++) {
    double gy = (y + 0.5) / k.f - 0.5, fy;
    i32 y0 = (i32)floor(gy), y1;
    fy = gy - y0;
    if (y0 < 0) { y0 = 0; fy = 0; }
    y1 = y0 + 1 < h ? y0 + 1 : h - 1;
    for (x = 0; x < d->w; x++) {
      double gx = (x + 0.5) / k.f - 0.5, fx, v;
      i32 x0 = (i32)floor(gx), x1;
      fx = gx - x0;
      if (x0 < 0) { x0 = 0; fx = 0; }
      x1 = x0 + 1 < w ? x0 + 1 : w - 1;
      v = (P[(long)y0 * w + x0] * (1 - fx) + P[(long)y0 * w + x1] * fx) * (1 - fy) + (P[(long)y1 * w + x0] * (1 - fx) + P[(long)y1 * w + x1] * fx) * fy;
      out[(long)y * d->w + x] = (u8)(clampd(v, 0, 1) * 255 + 0.5);
    }
  }
  ok = 1;
done:
  free(k.px); free(like); free(lum); free(grad); free(m); free(stack); free(I);
  if (!ok) { free(out); return NULL; }
  return out;
}
int sel_sky(Doc *d, int op, int threshold, int softness)
{
  u8 *m = sky_mask(d, threshold, softness);
  long i, any = 0;
  if (!m) return 0;
  for (i = 0; i < (long)d->w * d->h && !any; i++) any = m[i] >= 128;
  if (!any) { free(m); return 0; }
  sel_apply_mask(d, m, op, "Sky");
  return 1;
}

/* ------------------------------------------------------------------ Subject */
static double lab_f(double t) { return t > 0.008856 ? pow(t, 1.0 / 3) : 7.787 * t + 16.0 / 116; }
static double lin(double v) { return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
int subject_box(Doc *d, Box *bx)
{
  Work k;
  i32 w, h, x, y;
  long n, i, best = -1, *stack = NULL;
  float *lab = NULL, *sal;
  u8 *on = NULL, *seen = NULL;
  double mean[3] = { 0, 0, 0 }, smean = 0, bestscore = 0;
  int c, ok = 0;
  i32 bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
  if (!work_make(d, &k, 40000)) return 0;
  w = k.w; h = k.h; n = (long)w * h;
  if (w < 4 || h < 4) { free(k.px); return 0; }
  lab = (float *)malloc((size_t)n * 4 * sizeof(float)); on = (u8 *)calloc((size_t)n, 1); seen = (u8 *)calloc((size_t)n, 1);
  stack = (long *)malloc((size_t)n * sizeof(long));
  if (!lab || !on || !seen || !stack) goto done;
  sal = lab + n * 3;
  for (i = 0; i < n; i++) {
    double r = lin(k.px[i * 3]), g = lin(k.px[i * 3 + 1]), b = lin(k.px[i * 3 + 2]);
    double X = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047, Y = 0.2126 * r + 0.7152 * g + 0.0722 * b, Z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883;
    double fx = lab_f(X), fy = lab_f(Y), fz = lab_f(Z);
    lab[i] = (float)(116 * fy - 16); lab[n + i] = (float)(500 * (fx - fy)); lab[2 * n + i] = (float)(200 * (fy - fz));
  }
  for (c = 0; c < 3; c++) {
    for (i = 0; i < n; i++) mean[c] += lab[c * n + i];
    mean[c] /= n;
    if (!box_mean(lab + c * n, w, h, 1)) goto done;
  }
  /* saliency, leaning towards the middle */
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      long j = (long)y * w + x;
      double dx = (x + 0.5) / w - 0.5, dy = (y + 0.5) / h - 0.5, s = 0;
      for (c = 0; c < 3; c++) s += (lab[c * n + j] - mean[c]) * (lab[c * n + j] - mean[c]);
      sal[j] = (float)(sqrt(s) * (0.4 + 0.6 * exp(-(dx * dx + dy * dy) / (2 * 0.25 * 0.25))));
      smean += sal[j];
    }
  smean /= n;
  if (smean < 1e-3) goto done;
  for (i = 0; i < n; i++) on[i] = sal[i] > 2 * smean;
  if (!drop_small(on, w, h, 0, n / 100 > 4 ? n / 100 : 4)) goto done;
  /* the region with the most saliency in it */
  for (i = 0; i < n; i++) {
    long c0 = 0, c1 = 0;
    double score = 0;
    i32 ax0 = w, ay0 = h, ax1 = 0, ay1 = 0;
    if (!on[i] || seen[i]) continue;
    stack[c1++] = i; seen[i] = 1;
    while (c0 < c1) {
      long j = stack[c0++], jx = j % w, jy = j / w, nb[4];
      int q;
      score += sal[j];
      if (jx < ax0) ax0 = (i32)jx;
      if (jx + 1 > ax1) ax1 = (i32)jx + 1;
      if (jy < ay0) ay0 = (i32)jy;
      if (jy + 1 > ay1) ay1 = (i32)jy + 1;
      nb[0] = jx > 0 ? j - 1 : -1; nb[1] = jx + 1 < w ? j + 1 : -1; nb[2] = j >= w ? j - w : -1; nb[3] = j + w < n ? j + w : -1;
      for (q = 0; q < 4; q++) if (nb[q] >= 0 && on[nb[q]] && !seen[nb[q]]) { seen[nb[q]] = 1; stack[c1++] = nb[q]; }
    }
    if (score > bestscore) { bestscore = score; best = i; bx0 = ax0; by0 = ay0; bx1 = ax1; by1 = ay1; }
  }
  if (best < 0) goto done;
  {
    /* to full size, grown by a twelfth each way */
    i32 gw = (bx1 - bx0) * k.f / 12 + 2, gh = (by1 - by0) * k.f / 12 + 2;
    *bx = box_clip(box_make(bx0 * k.f - gw, by0 * k.f - gh, bx1 * k.f + gw, by1 * k.f + gh), doc_box(d));
  }
  ok = !box_empty(*bx);
done:
  free(k.px); free(lab); free(on); free(seen); free(stack);
  return ok;
}
int sel_subject(Doc *d, int op)
{
  Box b;
  if (!subject_box(d, &b)) return 0;
  return object_select(d, b, op, 1, 0);
}

/* ------------------------------------------------------------------ Decontaminate Colors */
int doc_decontaminate(Doc *d, int li, int amount)
{
  Layer *l;
  Box b = box_make(0, 0, 0, 0), g;
  i32 x, y, gw, gh;
  long k, n;
  float *acc = NULL;
  u8 *done = NULL;
  int radius, c;
  if (li < 0 || li >= d->nlay || !(l = d->lay[li])->px || !l->mask || amount <= 0) return 0;
  if (amount > 100) amount = 100;
  /* the partly shown pixels' box */
  for (y = 0; y < d->h; y++)
    for (x = 0; x < d->w; x++) {
      u8 m = l->mask[(long)y * d->w + x];
      if (m > 0 && m < 255) b = box_union(b, box_make(x, y, x + 1, y + 1));
    }
  if (box_empty(b)) return 1;
  g = box_clip(box_make(b.x0 - 64, b.y0 - 64, b.x1 + 64, b.y1 + 64), doc_box(d));
  gw = g.x1 - g.x0; gh = g.y1 - g.y0; n = (long)gw * gh;
  acc = (float *)malloc((size_t)n * 4 * sizeof(float)); done = (u8 *)calloc((size_t)(b.x1 - b.x0) * (b.y1 - b.y0), 1);
  if (!acc || !done) { free(acc); free(done); return 0; }
  doc_begin(d, "Decontaminate Colors");
  doc_note_layer(d, li, b);
  /* the colours of the fully shown pixels spread outwards, wider each round, until every partly
     shown pixel has some (each takes the nearest round that reaches it) */
  for (radius = 2; radius <= 64; radius *= 2) {
    int pending = 0;
    for (y = g.y0; y < g.y1; y++)
      for (x = g.x0; x < g.x1; x++) {
        long q = ((long)(y - g.y0) * gw + (x - g.x0)) * 4, s = (long)y * d->w + x;
        int fg = l->mask[s] == 255 && l->px[s * 4 + 3] > 0;
        for (c = 0; c < 3; c++) acc[q + c] = fg ? l->px[s * 4 + c] : 0;
        acc[q + 3] = (float)fg;
      }
    for (c = 0; c < 4; c++) {
      /* (one plane at a time through a strided copy) */
      float *pl = (float *)malloc((size_t)n * sizeof(float));
      if (!pl) { free(acc); free(done); doc_end(d); return 0; }
      for (k = 0; k < n; k++) pl[k] = acc[k * 4 + c];
      if (!box_mean(pl, gw, gh, radius)) { free(pl); free(acc); free(done); doc_end(d); return 0; }
      for (k = 0; k < n; k++) acc[k * 4 + c] = pl[k];
      free(pl);
    }
    for (y = b.y0; y < b.y1; y++)
      for (x = b.x0; x < b.x1; x++) {
        long s = (long)y * d->w + x, q = ((long)(y - g.y0) * gw + (x - g.x0)) * 4, di = (long)(y - b.y0) * (b.x1 - b.x0) + (x - b.x0);
        u8 m = l->mask[s];
        double t;
        if (m == 0 || m == 255 || done[di]) continue;
        if (acc[q + 3] < 1e-4) { pending = 1; continue; }
        /* less shown, more replaced */
        t = amount / 100.0 * clampd(2 * (1 - m / 255.0), 0, 1);
        for (c = 0; c < 3; c++) {
          double f = acc[q + c] / acc[q + 3];
          l->px[s * 4 + c] = (u8)clampd(l->px[s * 4 + c] + (f - l->px[s * 4 + c]) * t + 0.5, 0, 255);
        }
        done[di] = 1;
      }
    if (!pending || progress(radius, 64)) break;
  }
  free(acc); free(done);
  doc_end(d);
  return 1;
}
