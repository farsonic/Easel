/*
 * brush.c - the pieces of Easel's brushes (see brush.h): sampled tips, Deluxe Paint colour brushes
 * with their transforms and ILBM files, the random numbers and symmetry the stroke engine uses,
 * snapshots for the History Brush.
 *
 * The ILBM brush reader and writer are this file's own (ilbm.c reads pictures, not brushes): a
 * brush keeps its handle in a GRAB chunk and its background as a transparent colour or a mask
 * plane. Every length in a file is checked before it is used.
 *
 * Parts ported from PhotoCraft (https://github.com/storytold/photocraft), Copyright (c) 2026
 * ArtCraft Team and the PhotoCraft contributors, used under the MIT License (see Licenses.txt):
 * counter-based random numbers (crates/paint/src/rng.rs), symmetry's reflections
 * (crates/paint/src/symmetry.rs) and the procedural tips (crates/paint/src/procedural.rs).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "paint.h"
#include "path.h"                       /* r3: symmetry along a path */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAXTIP 8192
const char *const cb_mode_names[CB_N] = { "Matte", "Color", "Replace", "Smear", "Shade", "Blend", "Cycle", "Tint" };
const char *const ctl_names[CTL_N] = { "Off", "Fade", "Pen Pressure", "Direction", "Initial Direction", "Pen Tilt" };
const char *const sym_names[SYM_N] = { "Off", "Vertical", "Horizontal", "Vertical and Horizontal", "Radial", "Mandala", "Diagonal", "Tile", "Path" };
const struct Poly *brush_sym_path;

static int clamp255(double v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5); }
static double clampf(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ------------------------------------------------------------------ random numbers */
/* a 32-bit avalanche (the "lowbias32" mixer) */
static u32 mix32(u32 x)
{
  x ^= x >> 16; x *= 0x7FEB352DUL; x ^= x >> 15; x *= 0x846CA68BUL; x ^= x >> 16;
  return x;
}
u32 brush_hash(u32 seed, u32 index, u32 stream)
{
  u32 h = mix32(seed ^ 0x9E3779B9UL);
  h = mix32(h ^ (index * 0x9E3779B1UL));
  return mix32(h ^ (stream * 0x85EBCA77UL));
}
double brush_rand01(u32 seed, u32 index, u32 stream) { return (brush_hash(seed, index, stream) >> 8) / 16777216.0; }
/* integer coordinates to [0, 1) (the procedural tips) */
static double hash2(i32 x, i32 y, u32 salt) { return (brush_hash(salt, (u32)x * 0x10001UL ^ (u32)y, (u32)y) >> 8) / 16777216.0; }

/* ------------------------------------------------------------------ symmetry */
/* a point turned anticlockwise (on the screen, y down) by deg round cx, cy */
static void turn(double x, double y, double cx, double cy, double deg, double *ox, double *oy)
{
  double a = deg * M_PI / 180, c = cos(a), s = sin(a), dx = x - cx, dy = y - cy;
  *ox = cx + dx * c + dy * s; *oy = cy - dx * s + dy * c;
}
/* a point mirrored across the line through cx, cy at deg (anticlockwise from the x axis) */
static void mirror(double x, double y, double cx, double cy, double deg, double *ox, double *oy)
{
  double a = deg * M_PI / 180, ux = cos(a), uy = -sin(a), dx = x - cx, dy = y - cy, along = dx * ux + dy * uy;
  *ox = cx + 2 * along * ux - dx; *oy = cy + 2 * along * uy - dy;
}
int brush_symmetry(const BrushDynamics *dy, i32 w, i32 h, double x, double y, double *ox, double *oy, double *rot, int *mir)
{
  double cx = dy->sym_center_set ? dy->sym_cx : w / 2.0, cy = dy->sym_center_set ? dy->sym_cy : h / 2.0;
  double th = -(double)dy->sym_angle;   /* (the setting turns clockwise) */
  int n = dy->sym_segments < 2 ? 2 : dy->sym_segments > 32 ? 32 : dy->sym_segments, k, c = 1;
  ox[0] = x; oy[0] = y; rot[0] = 0; mir[0] = 0;
  switch (dy->sym_mode) {
  case SYM_VERTICAL:
    mirror(x, y, cx, cy, 90 + th, &ox[1], &oy[1]); rot[1] = 2 * (90 + th); mir[1] = 1; c = 2;
    /* an axis along the canvas: exact, not through sin and cos */
    if (dy->sym_angle % 360 == 0) { ox[1] = 2 * cx - x; oy[1] = y; }
    break;
  case SYM_HORIZONTAL:
    mirror(x, y, cx, cy, th, &ox[1], &oy[1]); rot[1] = 2 * th; mir[1] = 1; c = 2;
    if (dy->sym_angle % 360 == 0) { ox[1] = x; oy[1] = 2 * cy - y; }
    break;
  case SYM_BOTH:
    mirror(x, y, cx, cy, 90 + th, &ox[1], &oy[1]); rot[1] = 2 * (90 + th); mir[1] = 1;
    mirror(x, y, cx, cy, th, &ox[2], &oy[2]); rot[2] = 2 * th; mir[2] = 1;
    turn(x, y, cx, cy, 180, &ox[3], &oy[3]); rot[3] = 180; mir[3] = 0;
    if (dy->sym_angle % 360 == 0) {
      ox[1] = 2 * cx - x; oy[1] = y; ox[2] = x; oy[2] = 2 * cy - y; ox[3] = 2 * cx - x; oy[3] = 2 * cy - y;
    }
    c = 4;
    break;
  case SYM_RADIAL:
    for (k = 1; k < n; k++) { turn(x, y, cx, cy, 360.0 * k / n, &ox[k], &oy[k]); rot[k] = 360.0 * k / n; mir[k] = 0; }
    c = n;
    break;
  case SYM_MANDALA:
    for (k = 1; k < n; k++) { turn(x, y, cx, cy, 360.0 * k / n, &ox[k], &oy[k]); rot[k] = 360.0 * k / n; mir[k] = 0; }
    for (k = 0; k < n; k++) {
      double ax = 90 + th + 180.0 * k / n;
      mirror(x, y, cx, cy, ax, &ox[n + k], &oy[n + k]); rot[n + k] = 2 * ax; mir[n + k] = 1;
    }
    c = 2 * n;
    break;
  case SYM_DIAGONAL:                                       /* 1.2: mirrored across both diagonals through the centre */
    mirror(x, y, cx, cy, 45 + th, &ox[1], &oy[1]); rot[1] = 2 * (45 + th); mir[1] = 1;
    mirror(x, y, cx, cy, 135 + th, &ox[2], &oy[2]); rot[2] = 2 * (135 + th); mir[2] = 1;
    turn(x, y, cx, cy, 180, &ox[3], &oy[3]); rot[3] = 180; mir[3] = 0;
    if (dy->sym_angle % 360 == 0) {
      /* (exact: x and y swapped about the centre) */
      ox[1] = cx - (y - cy); oy[1] = cy - (x - cx); ox[2] = cx + (y - cy); oy[2] = cy + (x - cx); ox[3] = 2 * cx - x; oy[3] = 2 * cy - y;
    }
    c = 4;
    break;
  case SYM_PATH: {                                         /* r3: across the path's tangent where it is nearest */
    const Poly *pl = brush_sym_path;
    i32 ci, s0 = 0, i;
    double best = 1e300, qx = 0, qy = 0, tx = 1, ty = 0;
    if (!pl || pl->n < 2) break;
    for (ci = 0; ci < pl->nc; s0 = pl->end[ci], ci++) {
      i32 n = pl->end[ci] - s0, m = pl->closed[ci] ? n : n - 1;
      for (i = 0; i < m; i++) {
        const double *a = pl->xy + 2 * (s0 + i), *b = pl->xy + 2 * (s0 + (i + 1) % n);
        double vx = b[0] - a[0], vy = b[1] - a[1], L2 = vx * vx + vy * vy, t = L2 > 0 ? ((x - a[0]) * vx + (y - a[1]) * vy) / L2 : 0, px, py, d2;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        px = a[0] + vx * t; py = a[1] + vy * t;
        d2 = (x - px) * (x - px) + (y - py) * (y - py);
        if (d2 < best && L2 > 0) { best = d2; qx = px; qy = py; tx = vx / sqrt(L2); ty = vy / sqrt(L2); }
      }
    }
    if (best < 1e300) {
      double dx = x - qx, dy = y - qy, along = dx * tx + dy * ty;
      ox[1] = qx + 2 * along * tx - dx; oy[1] = qy + 2 * along * ty - dy;
      rot[1] = 2 * atan2(ty, tx) * 180 / 3.14159265358979323846; mir[1] = 1; c = 2;
    }
    break; }
  case SYM_TILE: {                                         /* 1.2: the canvas as n x n tiles, the dab in each */
    int t = dy->sym_segments < 2 ? 2 : dy->sym_segments > 8 ? 8 : dy->sym_segments, i, j;
    double tw = (double)w / t, tht = (double)h / t, fx = fmod(x, tw), fy = fmod(y, tht);
    if (fx < 0) fx += tw;
    if (fy < 0) fy += tht;
    c = 1;
    for (j = 0; j < t; j++) for (i = 0; i < t; i++) {
      double px = fx + i * tw, py = fy + j * tht;
      if (fabs(px - x) < 1e-9 && fabs(py - y) < 1e-9) continue;
      if (c >= SYM_MAXCOPIES) break;
      ox[c] = px; oy[c] = py; rot[c] = 0; mir[c] = 0; c++;
    }
    break; }
  }
  return c > SYM_MAXCOPIES ? SYM_MAXCOPIES : c;
}

/* ------------------------------------------------------------------ tips */
Tip *tip_new(i32 w, i32 h)
{
  Tip *t;
  if (w < 1 || h < 1 || w > MAXTIP || h > MAXTIP) return NULL;
  if (!(t = (Tip *)malloc(sizeof *t))) return NULL;
  if (!(t->a = (u8 *)calloc((size_t)w * h, 1))) { free(t); return NULL; }
  t->w = w; t->h = h;
  return t;
}
Tip *tip_copy(const Tip *t)
{
  Tip *o;
  if (!t || !(o = tip_new(t->w, t->h))) return NULL;
  memcpy(o->a, t->a, (size_t)t->w * t->h);
  return o;
}
void tip_free(Tip *t) { if (t) { free(t->a); free(t); } }
/* a tip cut to what paints */
static Tip *tip_trim(Tip *t)
{
  i32 x, y, x0 = t->w, y0 = t->h, x1 = 0, y1 = 0;
  Tip *o;
  for (y = 0; y < t->h; y++)
    for (x = 0; x < t->w; x++)
      if (t->a[(long)y * t->w + x]) {
        if (x < x0) x0 = x;
        if (x >= x1) x1 = x + 1;
        if (y < y0) y0 = y;
        if (y >= y1) y1 = y + 1;
      }
  if (x1 <= x0) { tip_free(t); return NULL; }
  if (x0 == 0 && y0 == 0 && x1 == t->w && y1 == t->h) return t;
  if (!(o = tip_new(x1 - x0, y1 - y0))) { tip_free(t); return NULL; }
  for (y = y0; y < y1; y++) memcpy(o->a + (long)(y - y0) * o->w, t->a + (long)y * t->w + x0, (size_t)o->w);
  tip_free(t);
  return o;
}
Tip *tip_from_pixels(const u8 *px, i32 w, i32 h, int bpp, int invert)
{
  Tip *t = tip_new(w, h);
  long k, n = (long)w * h;
  if (!t) return NULL;
  for (k = 0; k < n; k++) {
    int g, a = 255;
    if (bpp == 4) { const u8 *p = px + k * 4; g = (p[0] * 77 + p[1] * 150 + p[2] * 29 + 128) >> 8; a = p[3]; }
    else g = px[k];
    g = invert ? g : 255 - g;
    t->a[k] = (u8)((g * a + 127) / 255);
  }
  return t;
}
/* the pixels of box b of the active layer or the merged picture, unselected ones transparent */
static u8 *grab_pixels(Doc *d, Box *b, int merged)
{
  u8 *px;
  i32 x, y, w, h;
  if (d->sel) *b = box_clip(*b, d->selbox);
  *b = box_clip(*b, doc_box(d));
  if (box_empty(*b) || d->active < 0 || d->active >= d->nlay) return NULL;
  w = b->x1 - b->x0; h = b->y1 - b->y0;
  if (!(px = (u8 *)malloc((size_t)w * h * 4))) return NULL;
  if (merged || !d->lay[d->active]->px) doc_composite(d, *b, px, (long)w * 4);
  else for (y = 0; y < h; y++) memcpy(px + (long)y * w * 4, d->lay[d->active]->px + ((long)(b->y0 + y) * d->w + b->x0) * 4, (size_t)w * 4);
  if (d->sel)
    for (y = 0; y < h; y++)
      for (x = 0; x < w; x++) {
        u8 *p = px + ((long)y * w + x) * 4;
        int s = d->sel[(long)(b->y0 + y) * d->w + b->x0 + x];
        p[3] = (u8)((p[3] * s + 127) / 255);
      }
  return px;
}
Tip *tip_from_doc(Doc *d, Box b, int merged)
{
  u8 *px = grab_pixels(d, &b, merged);
  Tip *t;
  if (!px) return NULL;
  t = tip_from_pixels(px, b.x1 - b.x0, b.y1 - b.y0, 4, 0);
  free(px);
  return t ? tip_trim(t) : NULL;
}

/* procedural tips (PhotoCraft's procedural.rs, in 32 bits) */
static double smooth3(double t) { return t * t * (3 - 2 * t); }
static double value_noise(double x, double y, i32 period, u32 salt)
{
  double x0 = floor(x), y0 = floor(y), tx = smooth3(x - x0), ty = smooth3(y - y0), a, b;
  i32 xi = (i32)x0, yi = (i32)y0;
#define VN(i, j) hash2((((i) % period) + period) % period, (((j) % period) + period) % period, salt)
  a = VN(xi, yi) + (VN(xi + 1, yi) - VN(xi, yi)) * tx;
  b = VN(xi, yi + 1) + (VN(xi + 1, yi + 1) - VN(xi, yi + 1)) * tx;
#undef VN
  return a + (b - a) * ty;
}
Tip *tip_procedural(int kind, int n, u32 seed)
{
  Tip *t;
  i32 x, y;
  double c = n / 2.0, s = n;
  int i, k;
  double bl[20][4];
  if (n < 4) n = 4;
  if (n > 512) n = 512;
  if (!(t = tip_new(n, n))) return NULL;
  c = n / 2.0; s = n;
  if (kind == PTIP_SPATTER) for (i = 0; i < 20; i++) {
    double a = hash2(i, 0, seed + 202) * 2 * M_PI, dd = sqrt(hash2(i, 1, seed + 202)) * 0.38 * s, h2 = hash2(i, 2, seed + 202);
    bl[i][0] = s / 2 + cos(a) * dd; bl[i][1] = s / 2 + sin(a) * dd; bl[i][2] = (0.03 + 0.09 * h2 * h2) * s;
  }
  if (kind == PTIP_GRASS) for (i = 0; i < 9; i++) {
    bl[i][0] = (0.3 + 0.4 * hash2(i, 0, seed + 505)) * s; bl[i][1] = (0.55 + 0.42 * hash2(i, 1, seed + 505)) * s;
    bl[i][2] = (hash2(i, 2, seed + 505) - 0.5) * 0.9; bl[i][3] = (0.025 + 0.025 * hash2(i, 3, seed + 505)) * s;
  }
  for (y = 0; y < n; y++)
    for (x = 0; x < n; x++) {
      double v = 0, px = x + 0.5, py = y + 0.5, dx = px - c, dy = py - c;
      switch (kind) {
      case PTIP_CHALK: {
        double r = sqrt(dx * dx + dy * dy) / c, ang = atan2(dy, dx), edge = 0.8 + 0.15 * value_noise((ang + M_PI) * 3, 0.5, 19, seed + 101);
        double inside = clampf((edge - r) * 8, 0, 1), g = hash2(x, y, seed + 101);
        v = inside * (g < 0.2 ? 0.2 : 0.7 + 0.3 * g);
        break; }
      case PTIP_SPATTER:
        for (k = 0; k < 20; k++) {
          double e = bl[k][2] + 0.5 - sqrt((px - bl[k][0]) * (px - bl[k][0]) + (py - bl[k][1]) * (py - bl[k][1]));
          e = clampf(e, 0, 1);
          if (e > v) v = e;
        }
        break;
      case PTIP_BRISTLE: {
        double u = dx / c, w2 = dy / c * 1.8, r = sqrt(u * u + w2 * w2), hair;
        if (r > 1) break;
        hair = hash2(0, (i32)(y / 1.5), seed + 303);
        v = (hair < 0.25 ? 0 : 0.5 + 0.5 * hair) * (1 - r * r * r * r);
        break; }
      case PTIP_STAR: {
        double u = fabs(dx / c), w2 = fabs(dy / c), r1, r2, glow;
        r1 = (1 - u) * (1 - fmin(w2 / (0.08 * (1 - u) + 0.005), 1)); if (r1 < 0) r1 = 0;
        r2 = (1 - w2) * (1 - fmin(u / (0.08 * (1 - w2) + 0.005), 1)); if (r2 < 0) r2 = 0;
        glow = 1 - sqrt(u * u + w2 * w2) * 2.5; glow = glow > 0 ? glow * glow : 0;
        v = fmin(fmax(fmax(r1, r2), glow), 1);
        break; }
      case PTIP_LEAF: {
        double u = dx / c, w2 = dy / c, tt = clampf((u + 1) / 2, 0, 1), half = 0.42 * sin(M_PI * tt) * (1 - 0.25 * tt);
        double inside = clampf((half - fabs(w2)) * c, 0, 1), rib = 1 - 0.45 * (1 - clampf(fabs(w2) * c / 1.2, 0, 1));
        double fr = u * 7 - fabs(w2) * 5, veins;
        fr -= floor(fr);
        veins = 1 - 0.2 * (1 - clampf(fabs(fr - 0.5) * 8, 0, 1));
        v = inside * rib * veins;
        if (u < -0.75 && fabs(w2) * c < 0.9) v = 1;
        break; }
      case PTIP_GRASS: {
        double up = s - py;
        for (k = 0; k < 9; k++) {
          double tt, cx2, ww, e;
          if (up < 0 || up > bl[k][1]) continue;
          tt = up / bl[k][1]; cx2 = bl[k][0] + bl[k][2] * bl[k][1] * tt * tt; ww = bl[k][3] * (1 - tt) + 0.3;
          e = clampf(ww - fabs(px - cx2), 0, 1);
          if (e > v) v = e;
        }
        break; }
      default: { double r = sqrt(dx * dx + dy * dy); v = r <= c ? 1 : 0; }
      }
      t->a[(long)y * n + x] = (u8)clamp255(v * 255);
    }
  return t;
}

/* ------------------------------------------------------------------ DPaint custom brushes */
CustomBrush *cbrush_new(i32 w, i32 h)
{
  CustomBrush *b;
  if (w < 1 || h < 1 || w > MAXDIM || h > MAXDIM) return NULL;
  if (!(b = (CustomBrush *)malloc(sizeof *b))) return NULL;
  if (!(b->px = (u8 *)calloc((size_t)w * h, 4))) { free(b); return NULL; }
  b->w = w; b->h = h; b->hx = w / 2; b->hy = h / 2;
  return b;
}
CustomBrush *cbrush_copy(const CustomBrush *b)
{
  CustomBrush *o;
  if (!b || !(o = cbrush_new(b->w, b->h))) return NULL;
  memcpy(o->px, b->px, (size_t)b->w * b->h * 4);
  o->hx = b->hx; o->hy = b->hy;
  return o;
}
void cbrush_free(CustomBrush *b) { if (b) { free(b->px); free(b); } }
/* a brush cut to its opaque pixels (the handle moved with it), b freed if replaced */
static CustomBrush *cb_trim(CustomBrush *b)
{
  i32 x, y, x0 = b->w, y0 = b->h, x1 = 0, y1 = 0;
  CustomBrush *o;
  for (y = 0; y < b->h; y++)
    for (x = 0; x < b->w; x++)
      if (b->px[((long)y * b->w + x) * 4 + 3]) {
        if (x < x0) x0 = x;
        if (x >= x1) x1 = x + 1;
        if (y < y0) y0 = y;
        if (y >= y1) y1 = y + 1;
      }
  if (x1 <= x0) { cbrush_free(b); return NULL; }
  if (x0 == 0 && y0 == 0 && x1 == b->w && y1 == b->h) return b;
  if (!(o = cbrush_new(x1 - x0, y1 - y0))) { cbrush_free(b); return NULL; }
  for (y = y0; y < y1; y++) memcpy(o->px + (long)(y - y0) * o->w * 4, b->px + ((long)y * b->w + x0) * 4, (size_t)o->w * 4);
  o->hx = b->hx - x0; o->hy = b->hy - y0;
  if (o->hx < 0 || o->hx >= o->w) o->hx = o->w / 2;
  if (o->hy < 0 || o->hy >= o->h) o->hy = o->h / 2;
  cbrush_free(b);
  return o;
}
CustomBrush *cbrush_pickup(Doc *d, Box b, int use_sel, int merged, int key_bg, u32 key)
{
  CustomBrush *cb;
  u8 *saved = NULL, *px;
  Box sb;
  long k, n;
  u8 kr = (u8)(key >> 16), kg = (u8)(key >> 8), kb = (u8)key;
  if (!use_sel && d->sel) { saved = d->sel; d->sel = NULL; sb = d->selbox; }
  px = grab_pixels(d, &b, merged);
  if (saved) { d->sel = saved; d->selbox = sb; }
  if (!px) return NULL;
  if (!(cb = (CustomBrush *)malloc(sizeof *cb))) { free(px); return NULL; }
  cb->px = px; cb->w = b.x1 - b.x0; cb->h = b.y1 - b.y0; cb->hx = cb->w / 2; cb->hy = cb->h / 2;
  n = (long)cb->w * cb->h;
  for (k = 0; k < n; k++) {
    u8 *p = px + k * 4;
    /* (half transparent pixels go one way or the other: a DPaint brush is opaque or not) */
    if (p[3] < 128 || (key_bg && p[0] == kr && p[1] == kg && p[2] == kb)) p[0] = p[1] = p[2] = p[3] = 0;
    else p[3] = 255;
  }
  cb = cb_trim(cb);
  if (cb) { cb->hx = cb->w / 2; cb->hy = cb->h / 2; }
  return cb;
}
CustomBrush *cbrush_flip(const CustomBrush *b, int vertical)
{
  CustomBrush *o = cbrush_new(b->w, b->h);
  i32 x, y;
  if (!o) return NULL;
  for (y = 0; y < b->h; y++)
    for (x = 0; x < b->w; x++) {
      i32 sx = vertical ? x : b->w - 1 - x, sy = vertical ? b->h - 1 - y : y;
      memcpy(o->px + ((long)y * b->w + x) * 4, b->px + ((long)sy * b->w + sx) * 4, 4);
    }
  o->hx = vertical ? b->hx : b->w - 1 - b->hx;
  o->hy = vertical ? b->h - 1 - b->hy : b->hy;
  return o;
}
CustomBrush *cbrush_rotate90(const CustomBrush *b, int q)
{
  CustomBrush *o;
  i32 x, y;
  q &= 3;
  if (q == 0) return cbrush_copy(b);
  if (q == 2) {
    CustomBrush *t = cbrush_flip(b, 0);
    if (!t) return NULL;
    o = cbrush_flip(t, 1);
    cbrush_free(t);
    return o;
  }
  if (!(o = cbrush_new(b->h, b->w))) return NULL;
  for (y = 0; y < o->h; y++)
    for (x = 0; x < o->w; x++) {
      /* clockwise: new (x, y) comes from old (y, h-1-x); anticlockwise: from (w-1-y, x) */
      i32 sx = q == 1 ? y : b->w - 1 - y, sy = q == 1 ? b->h - 1 - x : x;
      memcpy(o->px + ((long)y * o->w + x) * 4, b->px + ((long)sy * b->w + sx) * 4, 4);
    }
  if (q == 1) { o->hx = b->h - 1 - b->hy; o->hy = b->hx; }
  else { o->hx = b->hy; o->hy = b->w - 1 - b->hx; }
  return o;
}
CustomBrush *cbrush_rotate(const CustomBrush *b, double deg)
{
  double a = -deg * M_PI / 180, c = cos(a), s = sin(a), cx = b->hx + 0.5, cy = b->hy + 0.5, minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
  CustomBrush *o;
  i32 x, y, w, h;
  int k;
  double q = deg / 90;
  if (fabs(q - floor(q + 0.5)) < 1e-9) return cbrush_rotate90(b, ((int)floor(q + 0.5) % 4 + 4) % 4);
  for (k = 0; k < 4; k++) {
    double dx = (k & 1 ? b->w : 0) - cx, dy = (k & 2 ? b->h : 0) - cy, rx = dx * c + dy * s, ry = -dx * s + dy * c;
    if (rx < minx) minx = rx;
    if (rx > maxx) maxx = rx;
    if (ry < miny) miny = ry;
    if (ry > maxy) maxy = ry;
  }
  w = (i32)ceil(maxx - minx); h = (i32)ceil(maxy - miny);
  if (!(o = cbrush_new(w < 1 ? 1 : w, h < 1 ? 1 : h))) return NULL;
  for (y = 0; y < o->h; y++)
    for (x = 0; x < o->w; x++) {
      double dx = x + 0.5 + minx, dy = y + 0.5 + miny, u = dx * c - dy * s + cx, v = dx * s + dy * c + cy;
      i32 sx = (i32)floor(u), sy = (i32)floor(v);
      if (sx < 0 || sy < 0 || sx >= b->w || sy >= b->h) continue;
      memcpy(o->px + ((long)y * o->w + x) * 4, b->px + ((long)sy * b->w + sx) * 4, 4);
    }
  o->hx = (i32)floor(-minx); o->hy = (i32)floor(-miny);
  if (o->hx >= o->w) o->hx = o->w - 1;
  if (o->hy >= o->h) o->hy = o->h - 1;
  return cb_trim(o);        /* (the corners the turn left empty cut off) */
}
CustomBrush *cbrush_scale(const CustomBrush *b, i32 w, i32 h)
{
  CustomBrush *o;
  i32 x, y;
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (!(o = cbrush_new(w, h))) return NULL;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      i32 sx = (i32)(((long)x * b->w + b->w / 2) / w), sy = (i32)(((long)y * b->h + b->h / 2) / h);
      if (sx >= b->w) sx = b->w - 1;
      if (sy >= b->h) sy = b->h - 1;
      memcpy(o->px + ((long)y * w + x) * 4, b->px + ((long)sy * b->w + sx) * 4, 4);
    }
  o->hx = (i32)((long)b->hx * w / b->w); o->hy = (i32)((long)b->hy * h / b->h);
  return o;
}
CustomBrush *cbrush_half(const CustomBrush *b) { return cbrush_scale(b, (b->w + 1) / 2, (b->h + 1) / 2); }
CustomBrush *cbrush_double(const CustomBrush *b) { return cbrush_scale(b, b->w * 2, b->h * 2); }
CustomBrush *cbrush_outline(const CustomBrush *b, u32 color)
{
  CustomBrush *o = cbrush_new(b->w + 2, b->h + 2);
  i32 x, y;
  if (!o) return NULL;
  for (y = 0; y < b->h; y++) memcpy(o->px + ((long)(y + 1) * o->w + 1) * 4, b->px + (long)y * b->w * 4, (size_t)b->w * 4);
  for (y = 0; y < o->h; y++)
    for (x = 0; x < o->w; x++) {
      u8 *p = o->px + ((long)y * o->w + x) * 4;
      int e = 0, dx, dy;
      if (p[3]) continue;
      for (dy = -1; dy <= 1 && !e; dy++)
        for (dx = -1; dx <= 1 && !e; dx++) {
          i32 sx = x + dx - 1, sy = y + dy - 1;
          if ((dx && dy) || sx < 0 || sy < 0 || sx >= b->w || sy >= b->h) continue;
          if (b->px[((long)sy * b->w + sx) * 4 + 3]) e = 1;
        }
      if (e) { p[0] = (u8)(color >> 16); p[1] = (u8)(color >> 8); p[2] = (u8)color; p[3] = 255; }
    }
  o->hx = b->hx + 1; o->hy = b->hy + 1;
  return o;
}
Tip *cbrush_to_tip(const CustomBrush *b)
{
  Tip *t = tip_new(b->w, b->h);
  long k, n = (long)b->w * b->h;
  if (!t) return NULL;
  for (k = 0; k < n; k++) t->a[k] = b->px[k * 4 + 3];
  return t;
}

/* ------------------------------------------------------------------ ILBM brushes */
static void put16(u8 *p, u32 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static u32 be16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }
static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
/* a chunk appended to buf: id, length, data, padding */
static void chunk(Buf *b, const char *id, const u8 *data, size_t n)
{
  u8 h[8];
  memcpy(h, id, 4); put32(h + 4, (u32)n);
  buf_add(b, h, 8);
  buf_add(b, data, n);
  if (n & 1) buf_addc(b, 0);
}
/* one row of bytes ByteRun1-packed onto buf */
static void byterun1(Buf *b, const u8 *s, long n)
{
  long i = 0;
  while (i < n) {
    long run = 1;
    while (i + run < n && run < 128 && s[i + run] == s[i]) run++;
    if (run >= 3) { buf_addc(b, (int)(257 - run) & 255); buf_addc(b, s[i]); i += run; continue; }
    {
      long lit = 0;
      while (i + lit < n && lit < 128) {
        if (i + lit + 2 < n && s[i + lit] == s[i + lit + 1] && s[i + lit] == s[i + lit + 2]) break;
        lit++;
      }
      buf_addc(b, (int)(lit - 1));
      buf_add(b, s + i, (size_t)lit);
      i += lit;
    }
  }
}
/* the palette: up to 256 opaque colours (a hash of 1024 places), -1 if there are more */
static int find_colors(const u8 *px, long n, u32 *pal, int max, u16 *slots)
{
  int np = 0;
  long k;
  memset(slots, 0xFF, 1024 * sizeof(u16));
  for (k = 0; k < n; k++) {
    const u8 *p = px + k * 4;
    u32 c = ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
    unsigned h = (unsigned)(mix32(c) & 1023);
    if (p[3] < 128) continue;
    while (slots[h] != 0xFFFF && pal[slots[h]] != c) h = (h + 1) & 1023;
    if (slots[h] != 0xFFFF) continue;
    if (np >= max) return -1;
    pal[np] = c; slots[h] = (u16)np; np++;
  }
  return np;
}
static int color_index(u32 c, const u32 *pal, const u16 *slots)
{
  unsigned h = (unsigned)(mix32(c) & 1023);
  while (slots[h] != 0xFFFF) { if (pal[slots[h]] == c) return slots[h]; h = (h + 1) & 1023; }
  return 0;
}
u8 *ilbm_brush_encode(const u8 *px, i32 w, i32 h, i32 hx, i32 hy, const char *name, size_t *len)
{
  Buf b;
  u8 hdr[20], *plane = NULL, *idx = NULL;
  u32 *pal = NULL;
  u16 *slots = NULL;
  long n = (long)w * h, k;
  int np, planes, masking = 0, tcol = 0, transparent = 0, deep, p, ncol;
  i32 y, x, rowbytes = ((w + 15) / 16) * 2;
  if (w < 1 || h < 1 || w > 65535 || h > 65535) return NULL;
  for (k = 0; k < n; k++) if (px[k * 4 + 3] < 128) { transparent = 1; break; }
  pal = (u32 *)malloc(257 * sizeof(u32));
  slots = (u16 *)malloc(1024 * sizeof(u16));
  plane = (u8 *)malloc((size_t)rowbytes);
  if (!pal || !slots || !plane) { free(pal); free(slots); free(plane); return NULL; }
  np = find_colors(px, n, pal, transparent ? 255 : 256, slots);
  deep = np < 0;
  if (!deep && transparent) {
    /* colour 0 is the background (DPaint's way): moved up to make room for it */
    u32 bgc = 0;
    int i, used = 1;
    while (used) {
      used = 0;
      for (i = 0; i < np; i++) if (pal[i] == bgc) { used = 1; bgc += 0x010101; break; }
    }
    memmove(pal + 1, pal, (size_t)np * sizeof(u32));
    pal[0] = bgc; np++;
    find_colors(px, 0, pal, 0, slots);                       /* (slots cleared, rebuilt below) */
    for (i = 1; i < np; i++) {
      unsigned hh = (unsigned)(mix32(pal[i]) & 1023);
      while (slots[hh] != 0xFFFF) hh = (hh + 1) & 1023;
      slots[hh] = (u16)i;
    }
    masking = 2; tcol = 0;
  }
  if (deep) { planes = 24; if (transparent) masking = 1; ncol = 0; }
  else { for (planes = 1; (1 << planes) < np; planes++) ; ncol = 1 << planes; }
  buf_init(&b);
  buf_add(&b, "FORM\0\0\0\0ILBM", 12);
  memset(hdr, 0, sizeof hdr);
  put16(hdr, (u32)w); put16(hdr + 2, (u32)h);
  hdr[8] = (u8)planes; hdr[9] = (u8)masking; hdr[10] = 1;
  put16(hdr + 12, (u32)tcol); hdr[14] = 1; hdr[15] = 1;
  put16(hdr + 16, (u32)w); put16(hdr + 18, (u32)h);
  chunk(&b, "BMHD", hdr, 20);
  if (!deep) {
    u8 *cm = (u8 *)calloc((size_t)ncol, 3);
    int i;
    if (!cm) { buf_free(&b); free(pal); free(slots); free(plane); return NULL; }
    for (i = 0; i < np; i++) { cm[i * 3] = (u8)(pal[i] >> 16); cm[i * 3 + 1] = (u8)(pal[i] >> 8); cm[i * 3 + 2] = (u8)pal[i]; }
    chunk(&b, "CMAP", cm, (size_t)ncol * 3);
    free(cm);
  }
  put16(hdr, (u32)(hx < 0 ? 0 : hx)); put16(hdr + 2, (u32)(hy < 0 ? 0 : hy));
  chunk(&b, "GRAB", hdr, 4);
  if (name && name[0]) chunk(&b, "NAME", (const u8 *)name, strlen(name));
  /* BODY: each row's planes (and its mask) packed */
  {
    Buf body;
    buf_init(&body);
    if (!deep && !(idx = (u8 *)malloc((size_t)w))) { buf_free(&b); free(pal); free(slots); free(plane); return NULL; }
    for (y = 0; y < h; y++) {
      const u8 *row = px + (long)y * w * 4;
      if (!deep)
        for (x = 0; x < w; x++) {
          const u8 *q = row + x * 4;
          idx[x] = (u8)(q[3] < 128 ? tcol : color_index(((u32)q[0] << 16) | ((u32)q[1] << 8) | q[2], pal, slots));
        }
      for (p = 0; p < planes + (masking == 1); p++) {
        memset(plane, 0, (size_t)rowbytes);
        for (x = 0; x < w; x++) {
          int bit;
          const u8 *q = row + x * 4;
          if (p == planes) bit = q[3] >= 128;
          else if (deep) bit = (q[p / 8] >> (p % 8)) & 1;
          else bit = (idx[x] >> p) & 1;
          if (bit) plane[x >> 3] |= (u8)(0x80 >> (x & 7));
        }
        byterun1(&body, plane, rowbytes);
      }
    }
    chunk(&b, "BODY", (const u8 *)body.data, body.len);
    buf_free(&body);
  }
  free(idx); free(pal); free(slots); free(plane);
  if (b.oom) { buf_free(&b); return NULL; }
  put32((u8 *)b.data + 4, (u32)(b.len - 8));
  *len = b.len;
  return (u8 *)buf_take(&b);
}
static int unpack_row(const u8 *s, size_t n, size_t *i, u8 *out, long size)
{
  long o = 0;
  while (o < size) {
    int c;
    if (*i >= n) return 0;
    c = s[(*i)++];
    if (c < 128) {
      long k = c + 1;
      if (*i + (size_t)k > n) return 0;
      if (k > size - o) { memcpy(out + o, s + *i, (size_t)(size - o)); *i += (size_t)k; return 1; }
      memcpy(out + o, s + *i, (size_t)k);
      o += k; *i += (size_t)k;
    } else if (c > 128) {
      long k = 257 - c;
      if (*i >= n) return 0;
      if (k > size - o) k = size - o;
      memset(out + o, s[(*i)++], (size_t)k);
      o += k;
    }
  }
  return 1;
}
static void set_err(char *err, size_t cap, const char *msg) { if (err && cap) { strncpy(err, msg, cap - 1); err[cap - 1] = 0; } }
u8 *ilbm_brush_decode(const u8 *d, size_t n, i32 *wp, i32 *hp, i32 *hxp, i32 *hyp, char *name, size_t namecap, char *err, size_t errcap)
{
  size_t end, o, bodylen = 0, bi = 0;
  const u8 *body = NULL;
  u8 pal[256][3], *px = NULL, *row = NULL;
  u32 camg = 0;
  int have = 0, np = 0, masking = 0, comp = 0, tcol = 0, ncmap = 0, havegrab = 0, ehb, i, p;
  i32 w = 0, h = 0, gx = 0, gy = 0, y, x, rowbytes;
  if (name && namecap) name[0] = 0;
  if (n < 12 || memcmp(d, "FORM", 4) || memcmp(d + 8, "ILBM", 4)) { set_err(err, errcap, "Not an IFF ILBM brush"); return NULL; }
  end = be32(d + 4);
  end = end > n - 8 ? n : end + 8;
  memset(pal, 0, sizeof pal);
  for (o = 12; o + 8 <= end; ) {
    size_t len = be32(d + o + 4), avail = end - o - 8;
    const u8 *c = d + o + 8;
    if (len > avail) len = avail;
    if (!memcmp(d + o, "BMHD", 4) && len >= 20) { w = (i32)be16(c); h = (i32)be16(c + 2); np = c[8]; masking = c[9]; comp = c[10]; tcol = (int)be16(c + 12); have = 1; }
    else if (!memcmp(d + o, "CMAP", 4)) { ncmap = (int)(len / 3 > 256 ? 256 : len / 3); for (i = 0; i < ncmap; i++) memcpy(pal[i], c + i * 3, 3); }
    else if (!memcmp(d + o, "CAMG", 4) && len >= 4) camg = be32(c);
    else if (!memcmp(d + o, "GRAB", 4) && len >= 4) { gx = (i32)be16(c); gy = (i32)be16(c + 2); havegrab = 1; }
    else if (!memcmp(d + o, "NAME", 4) && name && namecap) { size_t k = len < namecap - 1 ? len : namecap - 1; memcpy(name, c, k); name[k] = 0; }
    else if (!memcmp(d + o, "BODY", 4)) { body = c; bodylen = len; break; }
    o += 8 + len + (len & 1);
  }
  if (!have || !body || w < 1 || h < 1 || w > MAXDIM || h > MAXDIM || comp > 1 || !((np >= 1 && np <= 8) || np == 24 || np == 32)) {
    set_err(err, errcap, "Unsupported or damaged ILBM brush");
    return NULL;
  }
  ehb = np == 6 && (camg & 0x80) && ncmap <= 32;
  if (ehb) for (i = 0; i < 32; i++) { pal[32 + i][0] = pal[i][0] >> 1; pal[32 + i][1] = pal[i][1] >> 1; pal[32 + i][2] = pal[i][2] >> 1; }
  if (np <= 8 && !ncmap) for (i = 0; i < (1 << np); i++) pal[i][0] = pal[i][1] = pal[i][2] = (u8)(i * 255 / ((1 << np) - 1));
  rowbytes = ((w + 15) / 16) * 2;
  if (!(px = (u8 *)malloc((size_t)w * h * 4)) || !(row = (u8 *)malloc((size_t)rowbytes * 33))) { free(px); set_err(err, errcap, "Out of memory"); return NULL; }
  for (y = 0; y < h; y++) {
    int nrows = np + (masking == 1);
    for (p = 0; p < nrows; p++) {
      u8 *r = row + (long)p * rowbytes;
      if (comp) { if (!unpack_row(body, bodylen, &bi, r, rowbytes)) goto bad; }
      else { if (bi + (size_t)rowbytes > bodylen) goto bad; memcpy(r, body + bi, (size_t)rowbytes); bi += (size_t)rowbytes; }
    }
    for (x = 0; x < w; x++) {
      u8 *q = px + ((long)y * w + x) * 4;
      u32 v = 0;
      int bit = 0x80 >> (x & 7);
      for (p = 0; p < np; p++) if (row[(long)p * rowbytes + (x >> 3)] & bit) v |= (u32)1 << p;
      if (np <= 8) { memcpy(q, pal[v], 3); q[3] = 255; if (masking == 2 && (int)v == tcol) q[3] = 0; }
      else { q[0] = (u8)v; q[1] = (u8)(v >> 8); q[2] = (u8)(v >> 16); q[3] = np == 32 ? (u8)(v >> 24) : 255; }
      if (masking == 1 && !(row[(long)np * rowbytes + (x >> 3)] & bit)) q[3] = 0;
      if (!q[3]) q[0] = q[1] = q[2] = 0;
    }
  }
  free(row);
  *wp = w; *hp = h;
  if (!havegrab || gx >= w || gy >= h) { gx = w / 2; gy = h / 2; }
  if (hxp) *hxp = gx;
  if (hyp) *hyp = gy;
  return px;
bad:
  free(px); free(row);
  set_err(err, errcap, "Damaged ILBM brush");
  return NULL;
}
u8 *cbrush_write_ilbm(const CustomBrush *b, size_t *len) { return ilbm_brush_encode(b->px, b->w, b->h, b->hx, b->hy, NULL, len); }
CustomBrush *cbrush_read_ilbm(const u8 *data, size_t n, char *err, size_t errcap)
{
  CustomBrush *b;
  i32 w, h, hx, hy;
  u8 *px = ilbm_brush_decode(data, n, &w, &h, &hx, &hy, NULL, 0, err, errcap);
  if (!px) return NULL;
  if (!(b = (CustomBrush *)malloc(sizeof *b))) { free(px); set_err(err, errcap, "Out of memory"); return NULL; }
  b->px = px; b->w = w; b->h = h; b->hx = hx; b->hy = hy;
  return b;
}
int cbrush_save(const CustomBrush *b, const char *path)
{
  size_t len;
  u8 *f = cbrush_write_ilbm(b, &len);
  int ok;
  if (!f) return 0;
  ok = file_write(path, f, len);
  free(f);
  return ok;
}
CustomBrush *cbrush_load(const char *path, char *err, size_t errcap)
{
  size_t len;
  u8 *f = file_read(path, &len);
  CustomBrush *b;
  if (!f) { set_err(err, errcap, "The file can't be read"); return NULL; }
  b = cbrush_read_ilbm(f, len, err, errcap);
  free(f);
  return b;
}

/* ------------------------------------------------------------------ snapshots */
static Snapshot *snap(Doc *d, int merged)
{
  Snapshot *s;
  if (d->active < 0 || d->active >= d->nlay) return NULL;
  if (!(s = (Snapshot *)calloc(1, sizeof *s))) return NULL;
  if (!(s->px = (u8 *)malloc((size_t)d->w * d->h * 4))) { free(s); return NULL; }
  s->w = d->w; s->h = d->h;
  if (merged || !d->lay[d->active]->px) { doc_composite(d, doc_box(d), s->px, (long)d->w * 4); strcpy(s->name, "Merged"); }
  else { memcpy(s->px, d->lay[d->active]->px, (size_t)d->w * d->h * 4); strncpy(s->name, d->lay[d->active]->name, sizeof s->name - 1); }
  return s;
}
Snapshot *snapshot_take(Doc *d) { return snap(d, 0); }
Snapshot *snapshot_take_merged(Doc *d) { return snap(d, 1); }
void snapshot_free(Snapshot *s) { if (s) { free(s->px); free(s); } }
