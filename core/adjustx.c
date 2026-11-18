/*
 * adjustx.c - Color Lookup, Equalize's areas and Black & White Auto (see adjustx.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "util.h"
#include "adjust.h"
#include "adjust10.h"
#include "adjustx.h"

static int clamp255(double v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5); }

/* ------------------------------------------------------------------ the frame: pixels changed in
   place, then blended with the selection from the undo-free copy of each row */
typedef struct { Doc *d; Layer *l; Box b; u8 *row; } Run;
static int run_begin(Run *r, Doc *d, const char *name, int whole)
{
  Layer *l;
  r->d = d; r->row = NULL;
  if (d->active < 0 || d->active >= d->nlay) return 0;
  l = d->lay[d->active];
  if (!l->px || l->locked || l->lock_all || !l->visible) return 0;
  r->l = l;
  r->b = d->sel && !whole ? box_clip(d->selbox, doc_box(d)) : doc_box(d);
  if (box_empty(r->b)) return 0;
  if (!(r->row = (u8 *)malloc((size_t)(r->b.x1 - r->b.x0) * 4))) return 0;
  doc_begin(d, name);
  doc_note_layer(d, d->active, r->b);
  return 1;
}
/* row y's pixels kept before they change (call first), then blended back after */
static u8 *run_row(Run *r, i32 y)
{
  u8 *p = r->l->px + ((long)y * r->d->w + r->b.x0) * 4;
  memcpy(r->row, p, (size_t)(r->b.x1 - r->b.x0) * 4);
  return p;
}
static void run_blend(Run *r, i32 y, int whole)
{
  i32 x;
  Doc *d = r->d;
  if (!d->sel || whole) return;
  for (x = r->b.x0; x < r->b.x1; x++) {
    int s = d->sel[(long)y * d->w + x], c;
    u8 *p = r->l->px + ((long)y * d->w + x) * 4;
    const u8 *q = r->row + (x - r->b.x0) * 4;
    if (s == 255) continue;
    for (c = 0; c < 3; c++) p[c] = (u8)(q[c] + ((p[c] - q[c]) * s + (p[c] >= q[c] ? 127 : -127)) / 255);
  }
}
static int run_end(Run *r) { free(r->row); doc_end(r->d); return 1; }

/* ------------------------------------------------------------------ 3D LUTs */
void lut_free(Lut3D *l) { if (l) { free(l->t); free(l); } }
static Lut3D *lut_new(int n)
{
  Lut3D *l;
  if (n < 2 || n > LUT_MAXN || !(l = (Lut3D *)calloc(1, sizeof *l))) return NULL;
  if (!(l->t = (float *)malloc((size_t)n * n * n * 3 * sizeof(float)))) { free(l); return NULL; }
  l->n = n;
  l->dmin[0] = l->dmin[1] = l->dmin[2] = 0; l->dmax[0] = l->dmax[1] = l->dmax[2] = 1;
  return l;
}
Lut3D *lut_identity(int n)
{
  Lut3D *l = lut_new(n);
  int r, g, b;
  if (!l) return NULL;
  for (b = 0; b < n; b++) for (g = 0; g < n; g++) for (r = 0; r < n; r++) {
    float *e = l->t + (((long)b * n + g) * n + r) * 3;
    e[0] = (float)r / (n - 1); e[1] = (float)g / (n - 1); e[2] = (float)b / (n - 1);
  }
  return l;
}
static void seterr(char *err, int cap, const char *m) { if (err && cap > 0) { strncpy(err, m, cap - 1); err[cap - 1] = 0; } }
Lut3D *lut_parse_cube(const char *text, long len, char *err, int errcap)
{
  char line[256], title[64];
  long pos = 0, have = 0, need = 0;
  int n3 = 0, n1 = 0, k;
  float dmin[3] = { 0, 0, 0 }, dmax[3] = { 1, 1, 1 }, *one = NULL;
  Lut3D *l = NULL;
  title[0] = 0;
  while (pos < len) {
    int i = 0;
    char *s;
    while (pos < len && text[pos] != '\n' && text[pos] != '\r') { if (i < (int)sizeof line - 1) line[i++] = text[pos]; pos++; }
    while (pos < len && (text[pos] == '\n' || text[pos] == '\r')) pos++;
    line[i] = 0;
    for (s = line; *s == ' ' || *s == '\t'; s++) continue;
    if (!*s || *s == '#') continue;
    if (!strncmp(s, "TITLE", 5)) {
      char *q = strchr(s, '"'), *e2;
      if (q && (e2 = strchr(q + 1, '"'))) { *e2 = 0; strncpy(title, q + 1, sizeof title - 1); title[sizeof title - 1] = 0; }
      continue;
    }
    if (!strncmp(s, "LUT_3D_SIZE", 11)) { n3 = atoi(s + 11); continue; }
    if (!strncmp(s, "LUT_1D_SIZE", 11)) { n1 = atoi(s + 11); continue; }
    if (!strncmp(s, "DOMAIN_MIN", 10)) { sscanf(s + 10, "%f %f %f", &dmin[0], &dmin[1], &dmin[2]); continue; }
    if (!strncmp(s, "DOMAIN_MAX", 10)) { sscanf(s + 10, "%f %f %f", &dmax[0], &dmax[1], &dmax[2]); continue; }
    if (!strncmp(s, "LUT_3D_INPUT_RANGE", 18) || !strncmp(s, "LUT_1D_INPUT_RANGE", 18)) {
      float a = 0, b = 1;
      sscanf(s + 18, "%f %f", &a, &b);
      for (k = 0; k < 3; k++) { dmin[k] = a; dmax[k] = b; }
      continue;
    }
    if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) continue;     /* a keyword we don't use */
    /* a row of numbers */
    if (!l && !one) {
      if (n3) {
        if (n3 < 2 || n3 > LUT_MAXN) { seterr(err, errcap, "The LUT's size isn't one Easel can use (2 to 65)."); return NULL; }
        if (!(l = lut_new(n3))) { seterr(err, errcap, "There isn't enough memory for the LUT."); return NULL; }
        need = (long)n3 * n3 * n3;
      } else if (n1) {
        if (n1 < 2 || n1 > 65536) { seterr(err, errcap, "The LUT's size isn't one Easel can use."); return NULL; }
        if (!(one = (float *)malloc((size_t)n1 * 3 * sizeof(float)))) { seterr(err, errcap, "There isn't enough memory for the LUT."); return NULL; }
        need = n1;
      } else { seterr(err, errcap, "This isn't a .cube LUT (no LUT_3D_SIZE)."); return NULL; }
    }
    if (have < need) {
      float v[3];
      if (sscanf(s, "%f %f %f", &v[0], &v[1], &v[2]) != 3) { lut_free(l); free(one); seterr(err, errcap, "The LUT has a line Easel can't read."); return NULL; }
      memcpy((l ? l->t : one) + have * 3, v, sizeof v);
      have++;
    }
  }
  if (!need || have < need) { lut_free(l); free(one); seterr(err, errcap, need ? "The LUT ends early." : "This isn't a .cube LUT (no LUT_3D_SIZE)."); return NULL; }
  if (one) {
    /* a 1D table: each channel through its own curve, made a 33-point 3D table */
    int n = 33, r, g, b, c;
    if (!(l = lut_new(n))) { free(one); seterr(err, errcap, "There isn't enough memory for the LUT."); return NULL; }
    for (b = 0; b < n; b++) for (g = 0; g < n; g++) for (r = 0; r < n; r++) {
      int idx[3];
      idx[0] = r; idx[1] = g; idx[2] = b;
      for (c = 0; c < 3; c++) {
        double f = (double)idx[c] / (n - 1) * (n1 - 1);
        int i0 = (int)f, i1 = i0 + 1 < n1 ? i0 + 1 : i0;
        l->t[(((long)b * n + g) * n + r) * 3 + c] = (float)(one[i0 * 3 + c] + (one[i1 * 3 + c] - one[i0 * 3 + c]) * (f - i0));
      }
    }
    free(one);
    /* the domain applies to the input; the 3D table built covers it */
  }
  memcpy(l->dmin, dmin, sizeof dmin); memcpy(l->dmax, dmax, sizeof dmax);
  for (k = 0; k < 3; k++) if (l->dmax[k] <= l->dmin[k]) { l->dmin[k] = 0; l->dmax[k] = 1; }
  strcpy(l->title, title);
  return l;
}
Lut3D *lut_load_cube(const char *path, char *err, int errcap)
{
  size_t len;
  u8 *data = file_read(path, &len);
  Lut3D *l;
  if (!data) { seterr(err, errcap, "The LUT file couldn't be read."); return NULL; }
  l = lut_parse_cube((const char *)data, (long)len, err, errcap);
  free(data);
  return l;
}
char *lut_format_cube(const Lut3D *l, long *len)
{
  long n3 = (long)l->n * l->n * l->n, k, cap = n3 * 40 + 300, at;
  char *s = (char *)malloc((size_t)cap);
  if (!s) return NULL;
  at = sprintf(s, "# Made by Easel\nTITLE \"%s\"\nLUT_3D_SIZE %d\nDOMAIN_MIN %g %g %g\nDOMAIN_MAX %g %g %g\n", l->title[0] ? l->title : "Easel",
               l->n, l->dmin[0], l->dmin[1], l->dmin[2], l->dmax[0], l->dmax[1], l->dmax[2]);
  for (k = 0; k < n3; k++) at += sprintf(s + at, "%.6f %.6f %.6f\n", l->t[k * 3], l->t[k * 3 + 1], l->t[k * 3 + 2]);
  if (len) *len = at;
  return s;
}
void lut_lookup(const Lut3D *l, const u8 *rgb, int tetrahedral, double *out)
{
  int n = l->n, i0[3], i1[3], c;
  double f[3];
  const float *t = l->t;
  for (c = 0; c < 3; c++) {
    double v = (rgb[c] / 255.0 - l->dmin[c]) / (l->dmax[c] - l->dmin[c]) * (n - 1);
    if (v < 0) v = 0;
    if (v > n - 1) v = n - 1;
    i0[c] = (int)v; if (i0[c] > n - 2) i0[c] = n - 2;
    f[c] = v - i0[c]; i1[c] = i0[c] + 1;
  }
#define E(r, g, b) (t + (((long)(b) * n + (g)) * n + (r)) * 3)
  if (!tetrahedral) {
    for (c = 0; c < 3; c++) {
      double c00 = E(i0[0], i0[1], i0[2])[c] * (1 - f[0]) + E(i1[0], i0[1], i0[2])[c] * f[0];
      double c10 = E(i0[0], i1[1], i0[2])[c] * (1 - f[0]) + E(i1[0], i1[1], i0[2])[c] * f[0];
      double c01 = E(i0[0], i0[1], i1[2])[c] * (1 - f[0]) + E(i1[0], i0[1], i1[2])[c] * f[0];
      double c11 = E(i0[0], i1[1], i1[2])[c] * (1 - f[0]) + E(i1[0], i1[1], i1[2])[c] * f[0];
      double c0 = c00 * (1 - f[1]) + c10 * f[1], c1 = c01 * (1 - f[1]) + c11 * f[1];
      out[c] = c0 * (1 - f[2]) + c1 * f[2];
    }
  } else {
    /* the cube split into six tetrahedra by the order of the fractions */
    double fr = f[0], fg = f[1], fb = f[2];
    const float *c000 = E(i0[0], i0[1], i0[2]), *c111 = E(i1[0], i1[1], i1[2]), *a, *b;
    double w0, w1, w2, w3;
    if (fr > fg) {
      if (fg > fb) { a = E(i1[0], i0[1], i0[2]); b = E(i1[0], i1[1], i0[2]); w0 = 1 - fr; w1 = fr - fg; w2 = fg - fb; w3 = fb; }
      else if (fr > fb) { a = E(i1[0], i0[1], i0[2]); b = E(i1[0], i0[1], i1[2]); w0 = 1 - fr; w1 = fr - fb; w2 = fb - fg; w3 = fg; }
      else { a = E(i0[0], i0[1], i1[2]); b = E(i1[0], i0[1], i1[2]); w0 = 1 - fb; w1 = fb - fr; w2 = fr - fg; w3 = fg; }
    } else {
      if (fb > fg) { a = E(i0[0], i0[1], i1[2]); b = E(i0[0], i1[1], i1[2]); w0 = 1 - fb; w1 = fb - fg; w2 = fg - fr; w3 = fr; }
      else if (fb > fr) { a = E(i0[0], i1[1], i0[2]); b = E(i0[0], i1[1], i1[2]); w0 = 1 - fg; w1 = fg - fb; w2 = fb - fr; w3 = fr; }
      else { a = E(i0[0], i1[1], i0[2]); b = E(i1[0], i1[1], i0[2]); w0 = 1 - fg; w1 = fg - fr; w2 = fr - fb; w3 = fb; }
    }
    for (c = 0; c < 3; c++) out[c] = w0 * c000[c] + w1 * a[c] + w2 * b[c] + w3 * c111[c];
  }
#undef E
}
int adj_color_lookup(Doc *d, const Lut3D *l, int tetrahedral, int dither, int opacity)
{
  static const int bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
  Run r;
  i32 x, y;
  if (!l || opacity <= 0) return 0;
  if (opacity > 100) opacity = 100;
  if (!run_begin(&r, d, "Color Lookup", 0)) return 0;
  for (y = r.b.y0; y < r.b.y1 && !progress(y - r.b.y0, r.b.y1 - r.b.y0); y++) {
    u8 *p = run_row(&r, y);
    for (x = r.b.x0; x < r.b.x1; x++, p += 4) {
      double o[3], th = dither ? (bayer[(y & 3) * 4 + (x & 3)] + 0.5) / 16.0 - 0.5 : 0;
      int c;
      if (!p[3]) continue;
      lut_lookup(l, p, tetrahedral, o);
      for (c = 0; c < 3; c++) {
        double v = o[c] * 255;
        v = p[c] + (v - p[c]) * opacity / 100.0;
        p[c] = (u8)clamp255(v + th);
      }
    }
    run_blend(&r, y, 0);
  }
  return run_end(&r);
}

/* ------------------------------------------------------------------ Equalize */
int adj_equalize_ex(Doc *d, int mode)
{
  u32 h[1024];
  u8 lut[256];
  double total = 0, acc = 0;
  int i, whole = mode == EQ_ENTIRE;
  Run r;
  i32 x, y;
  histogram(d, 0, h);            /* the selected pixels' (or all of them) */
  for (i = 0; i < 256; i++) total += h[i];
  if (total <= 0) return 0;
  for (i = 0; i < 256; i++) { acc += h[i]; lut[i] = (u8)clamp255(acc / total * 255); }
  if (!run_begin(&r, d, "Equalize", whole)) return 0;
  for (y = r.b.y0; y < r.b.y1 && !progress(y - r.b.y0, r.b.y1 - r.b.y0); y++) {
    u8 *p = run_row(&r, y);
    for (x = r.b.x0; x < r.b.x1; x++, p += 4) { p[0] = lut[p[0]]; p[1] = lut[p[1]]; p[2] = lut[p[2]]; }
    run_blend(&r, y, whole);
  }
  return run_end(&r);
}

/* ------------------------------------------------------------------ Black & White Auto */
#define BW_SAMPLES 4096
/* how good a set of weights is: the greys' spread, less a heavy cost for each pixel pushed to
   black or white */
static double bw_score(const u8 *s, int n, const int *w6)
{
  AdjParams p;
  Adj10Fn fn;
  double a[48], sum = 0, sum2 = 0;
  int exact, i, clip = 0;
  memset(&p, 0, sizeof p);
  p.kind = AK_BLACKWHITE;
  memcpy(p.v, w6, 6 * sizeof(int));
  p.v[9] = 1;
  if (!adj10_pixfn(&p, &fn, a, &exact) || !fn) return -1e30;
  for (i = 0; i < n; i++) {
    u8 q[4];
    memcpy(q, s + i * 4, 4);
    fn(q, a);
    sum += q[0]; sum2 += (double)q[0] * q[0];
    if ((q[0] <= 1 && (s[i * 4] > 8 || s[i * 4 + 1] > 8 || s[i * 4 + 2] > 8)) ||
        (q[0] >= 254 && (s[i * 4] < 247 || s[i * 4 + 1] < 247 || s[i * 4 + 2] < 247))) clip++;
  }
  sum /= n;
  return sqrt(sum2 / n - sum * sum > 0 ? sum2 / n - sum * sum : 0) - 400.0 * clip / n;
}
int bw_auto_weights(Doc *d, int *w6)
{
  Box b = d->sel ? box_clip(d->selbox, doc_box(d)) : doc_box(d);
  Layer *l;
  u8 *s;
  long area, k, step;
  int n = 0, i, st;
  double best;
  static const int steps[] = { 40, 20, 10, 5, 0 };
  if (d->active < 0 || d->active >= d->nlay || !(l = d->lay[d->active])->px || box_empty(b)) return 0;
  if (!(s = (u8 *)malloc(BW_SAMPLES * 4))) return 0;
  area = (long)(b.x1 - b.x0) * (b.y1 - b.y0);
  step = area / BW_SAMPLES + 1;
  /* a spread of the area's pixels (opaque enough, selected enough) */
  for (k = 0; k < area && n < BW_SAMPLES; k += step) {
    i32 x = b.x0 + (i32)(k % (b.x1 - b.x0)), y = b.y0 + (i32)(k / (b.x1 - b.x0));
    const u8 *p = l->px + ((long)y * d->w + x) * 4;
    if (p[3] < 128 || sel_at(d, x, y) < 128) continue;
    memcpy(s + n * 4, p, 4); s[n * 4 + 3] = 255; n++;
  }
  if (!n) { free(s); return 0; }
  best = bw_score(s, n, w6);
  /* coordinate search, the steps shrinking */
  for (st = 0; steps[st]; st++) {
    int improved = 1, rounds = 0;
    while (improved && rounds++ < 20) {
      improved = 0;
      for (i = 0; i < 6; i++) {
        int dir;
        for (dir = -1; dir <= 1; dir += 2) {
          int old = w6[i], v = old + dir * steps[st];
          double sc;
          if (v < -200 || v > 300) continue;
          w6[i] = v;
          sc = bw_score(s, n, w6);
          if (sc > best + 1e-9) { best = sc; improved = 1; } else w6[i] = old;
        }
      }
    }
  }
  free(s);
  return 1;
}
