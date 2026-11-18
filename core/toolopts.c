/* toolopts.c - see toolopts.h */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "toolopts.h"
#include "layers.h"
#include "gradient.h"

/* ------------------------------------------------------------------ Dodge, Burn, Sponge */
/* PhotoCraft's tone_curve for v and e in 0..1 */
static double curve(double v, double e, int range, int burn)
{
  double w;
  if (v < 0) v = 0;
  if (v > 1) v = 1;
  switch (range) {
  case TR_SHADOWS: return burn ? v - e * (1 - v) * (1 - v) * v : v + e * (1 - v) * (1 - v) * (1 - v);
  case TR_HIGHLIGHTS: return burn ? v - e * v * v * v : v + e * v * v * (1 - v);
  case TR_MIDTONES:
    w = 4 * v * (1 - v);
    if (w > 1) w = 1;
    return v + w * (pow(v, burn ? 1 + e : 1 / (1 + e)) - v);
  default:                                    /* Easel 1.0's even change */
    return burn ? v - v * e * 255 / 400 : v + (1 - v) * e * 255 / 400;
  }
}
void tone_lut(u8 *lut, int range, int burn, int exposure)
{
  int v;
  double e = exposure < 0 ? 0 : exposure > 100 ? 1 : exposure / 100.0, o;
  for (v = 0; v < 256; v++) {
    o = curve(v / 255.0, e, range, burn) * 255 + 0.5;
    lut[v] = (u8)(o < 0 ? 0 : o > 255 ? 255 : o);
  }
}
static int luma8(const u8 *p) { return (p[0] * 299 + p[1] * 587 + p[2] * 114 + 500) / 1000; }
/* c (three doubles, 0-255) brought into 0-255 by mixing towards l, as PhotoCraft's compress_gamut */
static void compress(double *c, double l, u8 *out)
{
  double lo = c[0], hi = c[0], t = 1;
  int k;
  if (l < 0) l = 0;
  if (l > 255) l = 255;
  for (k = 1; k < 3; k++) { if (c[k] < lo) lo = c[k]; if (c[k] > hi) hi = c[k]; }
  if (hi > 255 && hi - l > 1e-6 && (255 - l) / (hi - l) < t) t = (255 - l) / (hi - l);
  if (lo < 0 && l - lo > 1e-6 && l / (l - lo) < t) t = l / (l - lo);
  for (k = 0; k < 3; k++) {
    double v = l + (c[k] - l) * t + 0.5;
    out[k] = (u8)(v < 0 ? 0 : v > 255 ? 255 : v);
  }
}
void tone_apply(u8 *p, const u8 *lut, int cov, int protect)
{
  int k;
  if (cov <= 0) return;
  if (cov > 255) cov = 255;
  if (protect) {
    int l = luma8(p);
    double nl = l + (lut[l] - l) * cov / 255.0, c[3];
    if (l <= 0) { c[0] = c[1] = c[2] = nl; }
    else for (k = 0; k < 3; k++) c[k] = p[k] * nl / l;
    compress(c, nl, p);
    return;
  }
  for (k = 0; k < 3; k++) p[k] = (u8)(p[k] + ((lut[p[k]] - p[k]) * cov + (lut[p[k]] >= p[k] ? 127 : -127)) / 255);
}
void tone_sponge(u8 *p, int amount, int saturate, int vibrance)
{
  double a = amount / 255.0, l, c[3], kk;
  int k, lo = p[0], hi = p[0];
  if (a <= 0) return;
  if (a > 1) a = 1;
  for (k = 1; k < 3; k++) { if (p[k] < lo) lo = p[k]; if (p[k] > hi) hi = p[k]; }
  if (vibrance) {
    double s = hi ? (double)(hi - lo) / hi : 0;
    a *= saturate ? 1 - s : 0.25 + 0.75 * s;
  }
  l = (p[0] * 299 + p[1] * 587 + p[2] * 114) / 1000.0;
  kk = saturate ? 1 + a : 1 - a;
  for (k = 0; k < 3; k++) c[k] = l + (p[k] - l) * kk;
  compress(c, l, p);
}

/* ------------------------------------------------------------------ sampling layers */
const int pick_sizes[NPICK_SIZES] = { 1, 3, 5, 11, 31, 51, 101 };
/* whether layer g is i itself or a group i is in */
static int holds(const Doc *d, int g, int i)
{
  for (; i >= 0; i = layer_parent(d, i)) if (i == g) return 1;
  return 0;
}
void doc_composite_below(Doc *d, int upto, Box r, u8 *out, long stride)
{
  u8 vis[MAXLAYERS];
  int i;
  if (upto < 0) upto = 0;
  if (upto >= d->nlay) upto = d->nlay - 1;
  /* the layers above it, but not the groups that hold it, hidden for the moment */
  for (i = 0; i < d->nlay; i++) {
    vis[i] = d->lay[i]->visible;
    if (i > upto && !holds(d, i, upto)) d->lay[i]->visible = 0;
  }
  doc_composite(d, r, out, stride);
  for (i = 0; i < d->nlay; i++) d->lay[i]->visible = vis[i];
}
u32 doc_pick_ex(Doc *d, i32 x, i32 y, int from, int size)
{
  long r = 0, g = 0, b = 0, n = 0, k, cnt;
  int rad = size > 1 ? size / 2 : 0;
  Box bx = box_clip(box_make(x - rad, y - rad, x + rad + 1, y + rad + 1), doc_box(d));
  i32 w = bx.x1 - bx.x0, h = bx.y1 - bx.y0, j;
  u8 *buf;
  const Layer *al = d->lay[d->active];
  if (box_empty(bx)) return 0xFFFFFFFFUL;
  if (from == SAMPLE_CURRENT && !al->px) from = SAMPLE_ALL;
  if (!(buf = (u8 *)malloc((size_t)w * h * 4))) return 0xFFFFFFFFUL;
  if (from == SAMPLE_CURRENT)
    for (j = 0; j < h; j++) memcpy(buf + (long)j * w * 4, al->px + ((long)(bx.y0 + j) * d->w + bx.x0) * 4, (size_t)w * 4);
  else if (from == SAMPLE_BELOW) doc_composite_below(d, d->active, bx, buf, (long)w * 4);
  else doc_composite(d, bx, buf, (long)w * 4);
  cnt = (long)w * h;
  for (k = 0; k < cnt; k++) {
    const u8 *p = buf + k * 4;
    if (!p[3]) continue;
    r += p[0]; g += p[1]; b += p[2]; n++;
  }
  free(buf);
  if (!n) return 0xFFFFFFFFUL;
  return (u32)((r / n) << 16 | (g / n) << 8 | (b / n));
}

/* ------------------------------------------------------------------ the Crop tool's overlay */
const char *const crop_overlay_names[CO_N] = { "No Overlay", "Rule of Thirds", "Grid", "Diagonal", "Triangle", "Golden Ratio" };
static int seg(double *s, int n, int max, double a, double b, double c, double e)
{
  if (n >= max) return n;
  s[n * 4] = a; s[n * 4 + 1] = b; s[n * 4 + 2] = c; s[n * 4 + 3] = e;
  return n + 1;
}
int crop_overlay_lines(int kind, double x0, double y0, double x1, double y1, double grid, double *s, int max)
{
  double w, h, t, m, vx, vy, l2, px, py, f;
  int n = 0, k;
  if (x1 < x0) { t = x0; x0 = x1; x1 = t; }
  if (y1 < y0) { t = y0; y0 = y1; y1 = t; }
  w = x1 - x0; h = y1 - y0;
  if (w <= 0 || h <= 0) return 0;
  switch (kind) {
  case CO_THIRDS: case CO_GOLDEN:
    f = kind == CO_THIRDS ? 1 / 3.0 : 0.381966;             /* (1 - 1/phi) */
    n = seg(s, n, max, x0 + w * f, y0, x0 + w * f, y1);
    n = seg(s, n, max, x1 - w * f, y0, x1 - w * f, y1);
    n = seg(s, n, max, x0, y0 + h * f, x1, y0 + h * f);
    n = seg(s, n, max, x0, y1 - h * f, x1, y1 - h * f);
    break;
  case CO_GRID:
    if (grid <= 0) grid = 32;
    for (k = 1; x0 + k * grid < x1; k++) n = seg(s, n, max, x0 + k * grid, y0, x0 + k * grid, y1);
    for (k = 1; y0 + k * grid < y1; k++) n = seg(s, n, max, x0, y0 + k * grid, x1, y0 + k * grid);
    break;
  case CO_DIAGONAL:
    /* 45 degree lines from each corner, as long as the shorter side */
    m = w < h ? w : h;
    n = seg(s, n, max, x0, y0, x0 + m, y0 + m);
    n = seg(s, n, max, x1, y0, x1 - m, y0 + m);
    n = seg(s, n, max, x0, y1, x0 + m, y1 - m);
    n = seg(s, n, max, x1, y1, x1 - m, y1 - m);
    break;
  case CO_TRIANGLE:
    /* a diagonal, and from the other two corners the perpendiculars to it */
    n = seg(s, n, max, x0, y1, x1, y0);
    vx = w; vy = -h; l2 = vx * vx + vy * vy;
    f = ((x0 - x0) * vx + (y0 - y1) * vy) / l2;  px = x0 + f * vx; py = y1 + f * vy;
    n = seg(s, n, max, x0, y0, px, py);
    f = ((x1 - x0) * vx + (y1 - y1) * vy) / l2;  px = x0 + f * vx; py = y1 + f * vy;
    n = seg(s, n, max, x1, y1, px, py);
    break;
  }
  return n;
}

/* ------------------------------------------------------------------ the Move tool */
int auto_select_target(const Doc *d, int i, int group)
{
  int p;
  if (!group || i < 0) return i;
  while ((p = layer_parent(d, i)) >= 0) i = p;
  return i;
}

/* ------------------------------------------------------------------ painting on the grid */
void grid_snap_paint(const Doc *d, double *x, double *y)
{
  double gx = d->guides.grid_x ? d->guides.grid_x : 16, gy = d->guides.grid_y ? d->guides.grid_y : 16;
  double ox = d->guides.grid_ox, oy = d->guides.grid_oy;
  *x = floor((*x - ox) / gx + 0.5) * gx + ox + 0.5;
  *y = floor((*y - oy) / gy + 0.5) * gy + oy + 0.5;
}

/* ------------------------------------------------------------------ the History panel */
int doc_delete_state(Doc *d, int k)
{
  int cap = 256, n;
  const char **names;
  for (;;) {
    if (!(names = (const char **)malloc((size_t)cap * sizeof *names))) return 0;
    n = doc_history(d, names, cap);
    free(names);
    if (n < cap) break;
    cap *= 2;
  }
  if (k == -1) k = n - 1;
  if (k < 0 || k >= n) return 0;
  while (n > k && doc_undo(d)) n--;
  if (n > k) return 0;
  /* (the redo list dropped a step at a time: redone and discarded) */
  while (doc_can_redo(d)) if (!doc_redo(d) || !doc_discard_last(d)) return 0;
  return 1;
}

/* ------------------------------------------------------------------ DPaint's ranges and stencils */
int gradient_from_palette(Gradient *g, const u8 *pal, int npal, int lo, int hi)
{
  int n, k, e;
  if (lo > hi) { k = lo; lo = hi; hi = k; }
  if (lo < 0) lo = 0;
  if (hi >= npal) hi = npal - 1;
  if (hi < lo || npal < 1) return 0;
  n = hi - lo + 1;
  if (n > GRAD_MAXSTOPS) n = GRAD_MAXSTOPS;
  gradient_init(g, "Palette Range", 0, 0);
  g->ncolor = n;
  for (k = 0; k < n; k++) {
    e = n > 1 ? lo + (int)(((long)k * (hi - lo) * 2 + (n - 1)) / (2 * (n - 1))) : lo;
    g->color[k].pos = n > 1 ? (double)k / (n - 1) : 0;
    g->color[k].mid = 0.5;
    g->color[k].kind = GS_RGB;
    g->color[k].rgb = (u32)pal[e * 3] << 16 | (u32)pal[e * 3 + 1] << 8 | pal[e * 3 + 2];
  }
  if (n == 1) { g->ncolor = 2; g->color[1] = g->color[0]; g->color[1].pos = 1; }
  return 1;
}
void doc_fill_range(const Doc *d, int *lo, int *hi)
{
  int k;
  for (k = 0; k < d->ncycle; k++)
    if (d->cycle[k].active && d->cycle[k].hi > d->cycle[k].lo && d->cycle[k].hi < d->npal) { *lo = d->cycle[k].lo; *hi = d->cycle[k].hi; return; }
  *lo = 0; *hi = d->npal > 0 ? d->npal - 1 : 0;
}
int stencil_colors(const Doc *d, int kind, u32 *out)
{
  int n = 0, k;
  if (kind == STC_BACKGROUND) out[n++] = d->bg & 0xFFFFFF;
  else if (kind == STC_LOCKED && d->mode == MODE_INDEXED)
    for (k = 0; k < d->npal && k < 256; k++)
      if (d->pal_lock[k]) out[n++] = (u32)d->pal[k * 3] << 16 | (u32)d->pal[k * 3 + 1] << 8 | d->pal[k * 3 + 2];
  return n;
}
