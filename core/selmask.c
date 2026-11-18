/*
 * selmask.c - Focus Area and Select and Mask (see selmask.h). Masks are worked on as floats 0..1;
 * blurs are box passes with running sums (their cost doesn't grow with the radius), and the local
 * colours inside and outside the edge come from block sums, so little more than the mask itself
 * is held in memory.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "util.h"
#include "paint.h"
#include "channels.h"
#include "selmask.h"

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static double smoothstep(double e0, double e1, double x) { double t = clamp01((x - e0) / (e1 - e0)); return t * t * (3 - 2 * t); }

/* a box blur of radius r in place (edges repeated); 0 if out of memory */
static int blurf(float *p, i32 w, i32 h, int r)
{
  i32 x, y, len = w > h ? w : h;
  float *line;
  double norm = 1.0 / (2 * r + 1);
  if (r < 1) return 1;
  if (!(line = (float *)malloc((size_t)len * sizeof(float)))) return 0;
  for (y = 0; y < h; y++) {
    float *row = p + (long)y * w;
    double acc = 0;
    int k;
    memcpy(line, row, (size_t)w * sizeof(float));
    for (k = -r; k <= r; k++) acc += line[clampi(k, 0, w - 1)];
    for (x = 0; x < w; x++) { row[x] = (float)(acc * norm); acc += line[clampi(x + r + 1, 0, w - 1)] - line[clampi(x - r, 0, w - 1)]; }
  }
  for (x = 0; x < w; x++) {
    double acc = 0;
    int k;
    for (y = 0; y < h; y++) line[y] = p[(long)y * w + x];
    for (k = -r; k <= r; k++) acc += line[clampi(k, 0, h - 1)];
    for (y = 0; y < h; y++) { p[(long)y * w + x] = (float)(acc * norm); acc += line[clampi(y + r + 1, 0, h - 1)] - line[clampi(y - r, 0, h - 1)]; }
  }
  free(line);
  return 1;
}
/* three box passes near a Gaussian of sigma */
static int gaussf(float *p, i32 w, i32 h, double sigma)
{
  double wi;
  int wl, wu, m, k;
  if (sigma < 0.3) return 1;
  wi = sqrt(12 * sigma * sigma / 3 + 1);
  wl = (int)floor(wi);
  if (wl % 2 == 0) wl--;
  if (wl < 1) wl = 1;
  wu = wl + 2;
  m = (int)floor((12 * sigma * sigma - 3 * wl * wl - 12 * wl - 9) / (-4.0 * wl - 4) + 0.5);
  for (k = 0; k < 3; k++) if (!blurf(p, w, h, ((k < m ? wl : wu) - 1) / 2)) return 0;
  return 1;
}
/* the largest (grow) or smallest value within r pixels (a square), in place */
static int minmaxf(float *p, i32 w, i32 h, int r, int grow)
{
  i32 x, y, len = w > h ? w : h;
  float *line;
  int k;
  if (r < 1) return 1;
  if (!(line = (float *)malloc((size_t)len * sizeof(float)))) return 0;
  for (y = 0; y < h; y++) {
    float *row = p + (long)y * w;
    memcpy(line, row, (size_t)w * sizeof(float));
    for (x = 0; x < w; x++) {
      float v = line[x];
      for (k = -r; k <= r; k++) { float q = line[clampi(x + k, 0, w - 1)]; if (grow ? q > v : q < v) v = q; }
      row[x] = v;
    }
  }
  for (x = 0; x < w; x++) {
    for (y = 0; y < h; y++) line[y] = p[(long)y * w + x];
    for (y = 0; y < h; y++) {
      float v = line[y];
      for (k = -r; k <= r; k++) { float q = line[clampi(y + k, 0, h - 1)]; if (grow ? q > v : q < v) v = q; }
      p[(long)y * w + x] = v;
    }
  }
  free(line);
  return 1;
}
static u8 *to_mask(const float *f, long n)
{
  u8 *m = (u8 *)malloc((size_t)n);
  long k;
  if (m) for (k = 0; k < n; k++) m[k] = (u8)(int)(clamp01(f[k]) * 255 + 0.5);
  return m;
}

/* ------------------------------------------------------------------ Focus Area */
u8 *focus_area_mask(Doc *d, int range, int noise, int soften, int all_layers)
{
  i32 w = d->w, h = d->h, x, y;
  long n = (long)w * h, k, hist[1024];
  float *L = (float *)malloc((size_t)n * sizeof(float)), *E = (float *)malloc((size_t)n * sizeof(float));
  u8 *row = (u8 *)malloc((size_t)w * 4), *m = NULL;
  Layer *l = d->active >= 0 && d->active < d->nlay ? d->lay[d->active] : NULL;
  int R = (w < h ? w : h) / 60, i;
  double top = 0, t, acc;
  if (!L || !E || !row || (!all_layers && (!l || !l->px))) goto done;
  if (R < 2) R = 2;
  range = clampi(range, 0, 100); noise = clampi(noise, 0, 100);
  for (y = 0; y < h; y++) {
    const u8 *p;
    if (all_layers) { doc_composite(d, box_make(0, y, w, y + 1), row, (long)w * 4); p = row; }
    else p = l->px + (long)y * w * 4;
    for (x = 0; x < w; x++, p += 4) L[(long)y * w + x] = (float)((0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]) * p[3] / 255.0);
  }
  /* detail: the Laplacian's size, less the noise floor, its energy gathered over a neighbourhood */
  for (y = 0; y < h; y++) {
    if (progress(y, 2L * h)) goto done;
    for (x = 0; x < w; x++) {
      double c = L[(long)y * w + x], lap = 4 * c - L[(long)y * w + clampi(x - 1, 0, w - 1)] - L[(long)y * w + clampi(x + 1, 0, w - 1)] -
                 L[(long)clampi(y - 1, 0, h - 1) * w + x] - L[(long)clampi(y + 1, 0, h - 1) * w + x];
      double e = fabs(lap) - noise * 0.4;
      E[(long)y * w + x] = (float)(e > 0 ? e * e : 0);
    }
  }
  if (!blurf(E, w, h, R) || !blurf(E, w, h, R)) goto done;
  /* scaled by the 99th percentile of the detail */
  for (k = 0; k < n; k++) { E[k] = (float)sqrt(E[k]); if (E[k] > top) top = E[k]; }
  if (top <= 0) top = 1;
  memset(hist, 0, sizeof hist);
  for (k = 0; k < n; k++) hist[clampi((int)(E[k] / top * 1023), 0, 1023)]++;
  for (acc = 0, i = 1023; i > 0; i--) { acc += hist[i]; if (acc > n / 100) break; }
  top = top * (i + 1) / 1024.0;
  t = 0.6 * (1 - range / 100.0) + 0.03;
  for (k = 0; k < n; k++) E[k] = (float)smoothstep(t * 0.6, t * 1.4, E[k] / top);
  progress(h, 2L * h);
  /* parts made whole: the mask gathered and made firm again */
  if (!blurf(E, w, h, R)) goto done;
  for (k = 0; k < n; k++) E[k] = (float)smoothstep(0.35, 0.65, E[k]);
  if (soften && !gaussf(E, w, h, 2.0)) goto done;
  m = to_mask(E, n);
done:
  free(L); free(E); free(row);
  return m;
}
int sel_focus_area(Doc *d, int range, int noise, int soften, int all_layers, int op)
{
  u8 *m = focus_area_mask(d, range, noise, soften, all_layers);
  if (!m) return 0;
  sel_apply_mask(d, m, op, "Focus Area");
  return 1;
}

/* ------------------------------------------------------------------ Select and Mask */
u8 *refine_mask(Doc *d, const RefineParams *rp)
{
  i32 w = d->w, h = d->h, x, y;
  long n = (long)w * h, k;
  float *m = NULL, *t = NULL;
  double *blk = NULL;
  u8 *out = NULL;
  Layer *l = d->active >= 0 && d->active < d->nlay ? d->lay[d->active] : NULL;
  RefineParams r = *rp;
  if (!d->sel) return NULL;
  r.radius = clampi(r.radius, 0, 250); r.smooth = clampi(r.smooth, 0, 100); r.feather10 = clampi(r.feather10, 0, 2500);
  r.contrast = clampi(r.contrast, 0, 100); r.shift = clampi(r.shift, -100, 100);
  if (!(m = (float *)malloc((size_t)n * sizeof(float)))) goto done;
  for (k = 0; k < n; k++) m[k] = (float)(d->sel[k] / 255.0);
  /* edge detection: in the band within radius of the edge, each pixel's place between the
     colours just inside and just outside */
  if (r.radius > 0 && l && l->px) {
    int S = r.radius < 8 ? 8 : r.radius, bw = (w + S - 1) / S, bh = (h + S - 1) / S, bx, by, c;
    double gsum[8];
    if (!(t = (float *)malloc((size_t)n * sizeof(float)))) goto done;
    for (k = 0; k < n; k++) t[k] = d->sel[k] >= 128 ? 1.0f : 0.0f;
    if (!blurf(t, w, h, r.radius)) goto done;
    /* block sums of the colours surely inside (0..3) and surely outside (4..7: r, g, b, count) */
    if (!(blk = (double *)calloc((size_t)bw * bh * 8, sizeof(double)))) goto done;
    memset(gsum, 0, sizeof gsum);
    for (y = 0; y < h; y++)
      for (x = 0; x < w; x++) {
        long i = (long)y * w + x;
        const u8 *p = l->px + i * 4;
        int o;
        if (t[i] > 0.0001f && t[i] < 0.9999f) continue;   /* in the band */
        o = t[i] >= 0.5f ? 0 : 4;
        for (c = 0; c < 3; c++) { blk[((long)(y / S) * bw + x / S) * 8 + o + c] += p[c]; gsum[o + c] += p[c]; }
        blk[((long)(y / S) * bw + x / S) * 8 + o + 3] += 1; gsum[o + 3] += 1;
      }
    for (y = 0; y < h; y++) {
      if (progress(y, h)) goto done;
      for (x = 0; x < w; x++) {
        long i = (long)y * w + x;
        double s[8], fb2 = 0, dot = 0;
        const u8 *p = l->px + i * 4;
        if (!(t[i] > 0.0001f && t[i] < 0.9999f)) continue;
        memset(s, 0, sizeof s);
        for (by = y / S - 1; by <= y / S + 1; by++)
          for (bx = x / S - 1; bx <= x / S + 1; bx++) {
            if (bx < 0 || by < 0 || bx >= bw || by >= bh) continue;
            for (c = 0; c < 8; c++) s[c] += blk[((long)by * bw + bx) * 8 + c];
          }
        for (c = 0; c < 8; c += 4) if (s[c + 3] < 1) { s[c] = gsum[c]; s[c + 1] = gsum[c + 1]; s[c + 2] = gsum[c + 2]; s[c + 3] = gsum[c + 3]; }
        if (s[3] < 1 || s[7] < 1) continue;
        for (c = 0; c < 3; c++) {
          double f = s[c] / s[3], b = s[4 + c] / s[7];
          fb2 += (f - b) * (f - b); dot += (p[c] - b) * (f - b);
        }
        if (fb2 < 100) continue;           /* inside and outside too alike to tell apart */
        m[i] = (float)clamp01(dot / fb2);
      }
    }
  }
  /* Smooth: corners rounded, the edge made firm again */
  if (r.smooth > 0) {
    if (!blurf(m, w, h, (r.smooth + 9) / 10)) goto done;
    for (k = 0; k < n; k++) m[k] = (float)clamp01((m[k] - 0.5) * 3 + 0.5);
  }
  if (r.feather10 > 0 && !gaussf(m, w, h, r.feather10 / 10.0)) goto done;
  if (r.contrast > 0) {
    double s = 1 / (1 - r.contrast * 0.0099);
    for (k = 0; k < n; k++) m[k] = (float)clamp01((m[k] - 0.5) * s + 0.5);
  }
  /* Shift Edge: up to 10 pixels out (or in) */
  if (r.shift && !minmaxf(m, w, h, (abs(r.shift) + 5) / 10, r.shift > 0)) goto done;
  out = to_mask(m, n);
done:
  free(m); free(t); free(blk);
  return out;
}
int sel_refine(Doc *d, const RefineParams *r)
{
  u8 *m = refine_mask(d, r);
  if (!m) return 0;
  sel_apply_mask(d, m, SEL_REPLACE, "Select and Mask");
  return 1;
}
