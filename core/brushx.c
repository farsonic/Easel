/*
 * brushx.c - Easel 1.2's brush extras (brushx.h): Texture, Dual Brush, Noise, Wet Edges and Brush
 * Pose; the Mixer Brush's paint; the Art History Brush; healing with Diffusion and Create Texture;
 * tablet events; Photoshop .abr writing and .grd and .pat reading.
 *
 * The Mixer Brush follows the behavioural model of PhotoCraft's crates/paint/src/mixer.rs
 * (https://github.com/storytold/photocraft, Copyright (c) 2026 ArtCraft Team and the PhotoCraft
 * contributors, MIT License; see Licenses.txt). The file formats are Adobe's, read and written from
 * their published layouts.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "brushx.h"
#include "pattern.h"
#include "gradient.h"
#include "retouch.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int clamp255(double v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5); }
static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

const char *const txm_names[TXM_N] = { "Multiply", "Subtract", "Darken", "Height", "Linear Height" };
const char *const dbm_names[DBM_N] = { "Multiply", "Darken", "Subtract" };

/* ------------------------------------------------------------------ Texture, Dual Brush, Noise, Wet Edges */
int brushx_active(const Brush *b)
{
  return (b->texture && b->texture->px && b->tex_depth > 0) || b->dual_on || b->noise > 0;
}
int brushx_texture(const Brush *b, i32 x, i32 y)
{
  const struct Pattern *p = b->texture;
  int sc = b->tex_scale > 0 ? b->tex_scale : 100, g;
  i32 u, v;
  const u8 *q;
  double t;
  if (!p || !p->px || p->w < 1 || p->h < 1) return 255;
  u = (i32)floor(x * 100.0 / sc); v = (i32)floor(y * 100.0 / sc);
  u %= p->w; if (u < 0) u += p->w;
  v %= p->h; if (v < 0) v += p->h;
  q = p->px + ((long)v * p->w + u) * 4;
  g = (q[0] * 77 + q[1] * 150 + q[2] * 29 + 128) >> 8;
  g = 255 - ((255 - g) * q[3] + 127) / 255;           /* transparent is high (no texture) */
  if (b->tex_invert) g = 255 - g;
  t = g;
  if (b->tex_brightness) t += b->tex_brightness;
  if (b->tex_contrast) t = 128 + (t - 128) * (1 + clampd(b->tex_contrast, -50, 100) / 50.0);
  return clamp255(t);
}
/* a dual tip's coverage at distance (dx, dy) from its centre, diameter dd */
static int dual_tip_at(const Brush *b, double dx, double dy, double dd)
{
  double r = dd / 2;
  if (r < 0.5) r = 0.5;
  if (b->dual_tip && b->dual_tip->a && b->dual_tip->w > 0 && b->dual_tip->h > 0) {
    const Tip *t = b->dual_tip;
    double s = (t->w > t->h ? t->w : t->h) / dd;
    i32 u = (i32)floor(dx * s + t->w / 2.0), v = (i32)floor(dy * s + t->h / 2.0);
    if (u < 0 || v < 0 || u >= t->w || v >= t->h) return 0;
    return t->a[(long)v * t->w + u];
  } else {
    double d = sqrt(dx * dx + dy * dy);
    if (d >= r) return 0;
    if (d <= r - 1) return 255;
    return clamp255((r - d) * 255);
  }
}
int brushx_cov(const Brush *b, int a, i32 x, i32 y, double cx, double cy, double diam, u32 idx)
{
  if (a <= 0) return 0;
  if (b->texture && b->tex_depth > 0) {
    double t = brushx_texture(b, x, y) / 255.0, dep = clampd(b->tex_depth / 100.0, 0, 1), f;
    switch (b->tex_mode) {
    case TXM_SUBTRACT: a = clamp255(a - dep * (1 - t) * 255); break;
    case TXM_DARKEN: f = 255 - dep * (1 - t) * 255; if (a > f) a = clamp255(f); break;
    case TXM_HEIGHT: if (t < 1 - dep) a = 0; break;
    case TXM_LINEARHEIGHT: f = dep > 0 ? clampd((t - (1 - dep)) / dep * 2, 0, 1) : 1; a = clamp255(a * f); break;
    default: a = clamp255(a * (1 - dep + dep * t));
    }
    if (a <= 0) return 0;
  }
  if (b->dual_on) {
    /* the second tip's dabs scattered round this one, the coverage the most of them */
    int n = b->dual_count < 1 ? 1 : b->dual_count > 16 ? 16 : b->dual_count, k, best = 0;
    double dd = b->dual_size > 0 ? b->dual_size : diam / 3, sc = clampd(b->dual_scatter / 100.0, 0, 10) * diam / 2;
    if (dd < 1) dd = 1;
    for (k = 0; k < n && best < 255; k++) {
      double ox = (brush_rand01(b->dyn.seed ^ 0x5D0A1u, idx, (u32)(40 + k * 2)) * 2 - 1) * (sc + diam / 3);
      double oy = (brush_rand01(b->dyn.seed ^ 0x5D0A1u, idx, (u32)(41 + k * 2)) * 2 - 1) * (sc + diam / 3);
      int c = dual_tip_at(b, x + 0.5 - (cx + ox), y + 0.5 - (cy + oy), dd);
      if (c > best) best = c;
    }
    switch (b->dual_mode) {
    case DBM_DARKEN: if (best < a) a = best; break;
    case DBM_SUBTRACT: a = a - (255 - best) < 0 ? 0 : a - (255 - best); break;
    default: a = (a * best + 127) / 255;
    }
    if (a <= 0) return 0;
  }
  if (b->noise > 0 && a < 255) {
    /* fixed noise in the soft edge (the same at the same pixel, as Photoshop's) */
    double h = brush_rand01(0x6E01u, (u32)(y * 40503 + x), 7) * 2 - 1, edge = (a < 128 ? a : 255 - a) / 128.0;
    a = clamp255(a + h * edge * 255 * clampd(b->noise / 100.0, 0, 1));
  }
  return a;
}
int brushx_wet(const Brush *b, int m)
{
  double f, amt;
  if (b->wet_edges <= 0 || m <= 0) return m;
  amt = clampd(b->wet_edges / 100.0, 0, 1);
  /* the middle (full coverage) down to about half, the soft edge kept: a ring of paint at the edge */
  f = 1 - amt * 0.5 * (m / 255.0) * (m / 255.0);
  f += amt * 0.35 * sin(M_PI * m / 255.0);
  return clamp255(m * clampd(f, 0, 1.2) > 255 ? 255 : m * clampd(f, 0, 1.2));
}
double brushx_pose_pressure(const Brush *b, double pressure)
{
  if (b->pose_flags & POSE_PRESSURE) return clampd(b->pose_pressure / 100.0, 0, 1);
  return pressure;
}
void brushx_protect_texture(Brush *picked, const Brush *cur)
{
  picked->texture = cur->texture;
  picked->tex_scale = cur->tex_scale; picked->tex_depth = cur->tex_depth; picked->tex_mode = cur->tex_mode;
  picked->tex_invert = cur->tex_invert; picked->tex_brightness = cur->tex_brightness; picked->tex_contrast = cur->tex_contrast;
}

/* ------------------------------------------------------------------ the Mixer Brush */
#define PICKUP_RATE 0.25
void mixer_init(MixerState *m) { memset(m, 0, sizeof *m); m->auto_load = 1; m->auto_clean = 1; }
void mixer_load(MixerState *m, u32 color)
{
  m->loaded = 1; m->amount = 1;
  m->res[0] = (color >> 16) & 255; m->res[1] = (color >> 8) & 255; m->res[2] = color & 255; m->res[3] = 255;
}
void mixer_clean(MixerState *m) { m->loaded = 0; m->amount = 0; m->has_pickup = 0; }
void mixer_begin(MixerState *m, u32 color)
{
  if (!m->has_color || m->color != (color & 0xFFFFFF)) { mixer_load(m, color); m->color = color & 0xFFFFFF; m->has_color = 1; }
}
void mixer_end(MixerState *m, u32 color)
{
  if (m->auto_clean) mixer_clean(m);
  if (m->auto_load) mixer_load(m, color);
  m->color = color & 0xFFFFFF; m->has_color = 1;
}
double mixer_dab(MixerState *m, const double *canvas, int has, int wet, int load, int mix, u8 *out)
{
  double w = clampd(wet / 100.0, 0, 1), mx = clampd(mix / 100.0, 0, 1), wr, wp, tot;
  int k;
  if (has && w > 0) {
    if (!m->has_pickup) { memcpy(m->pick, canvas, sizeof m->pick); m->has_pickup = 1; }
    else for (k = 0; k < 4; k++) m->pick[k] += (canvas[k] - m->pick[k]) * w * PICKUP_RATE;
  }
  wr = m->loaded ? m->amount * (1 - mx * w) : 0;
  wp = m->has_pickup ? mx * w : 0;
  if (!m->loaded && m->has_pickup && w > 0) wp = w;      /* a clean, wet brush smears what it picks up */
  tot = wr + wp;
  if (tot <= 1e-9) { out[0] = out[1] = out[2] = 0; return 0; }
  for (k = 0; k < 3; k++) out[k] = (u8)clamp255(((m->loaded ? m->res[k] : 0) * wr + (m->has_pickup ? m->pick[k] : 0) * wp) / tot);
  /* the reservoir drains as it paints, unless Load is full */
  if (m->loaded && load < 100) {
    m->amount -= (1 - clampd(load / 100.0, 0, 1)) * 0.04;
    if (m->amount < 0) m->amount = 0;
  }
  return tot > 1 ? 1 : tot;
}

/* ------------------------------------------------------------------ the Art History Brush */
const char *const ah_names[AH_N] = { "Tight Short", "Tight Medium", "Tight Long", "Loose Medium", "Loose Long", "Dab",
                                     "Tight Curl", "Tight Curl Long", "Loose Curl", "Loose Curl Long" };
static double lum_at(const u8 *src, i32 w, i32 h, i32 x, i32 y)
{
  const u8 *p;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= w) x = w - 1;
  if (y >= h) y = h - 1;
  p = src + ((long)y * w + x) * 4;
  return p[0] * 0.3 + p[1] * 0.59 + p[2] * 0.11;
}
Box art_history(Doc *d, const Brush *b0, const u8 *src, double x, double y, int style, int area, int tolerance, u32 seed)
{
  Box all = box_make(0, 0, 0, 0), ch;
  Brush b = *b0;
  int n, k, size;
  double len, loose, curl;
  Layer *l;
  if (!src || d->active < 0 || d->active >= d->nlay || !(l = d->lay[d->active])->px || l->kind != LK_PIXEL || l->locked || l->lock_all || !l->visible) return all;
  if (style < 0 || style >= AH_N) style = AH_TIGHT_SHORT;
  if (area <= 0) area = 50;
  size = b.size < 1 ? 1 : b.size;
  b.tool = PT_BRUSH;
  b.dyn.smoothing = 0;
  switch (style) {
  case AH_TIGHT_SHORT: len = 1.5; loose = 0.1; curl = 0; break;
  case AH_TIGHT_MEDIUM: len = 3; loose = 0.1; curl = 0; break;
  case AH_TIGHT_LONG: len = 6; loose = 0.1; curl = 0; break;
  case AH_LOOSE_MEDIUM: len = 3; loose = 0.6; curl = 0; break;
  case AH_LOOSE_LONG: len = 6; loose = 0.6; curl = 0; break;
  case AH_DAB: len = 0; loose = 0; curl = 0; break;
  case AH_TIGHT_CURL: len = 3; loose = 0.1; curl = 0.5; break;
  case AH_TIGHT_CURL_LONG: len = 6; loose = 0.1; curl = 0.5; break;
  case AH_LOOSE_CURL: len = 3; loose = 0.6; curl = 0.5; break;
  default: len = 6; loose = 0.6; curl = 0.5; break;
  }
  /* about as many strokes as fit the area twice over */
  n = (int)(2.0 * area * area / ((double)size * size + 1));
  if (n < 1) n = 1;
  if (n > 60) n = 60;
  doc_begin(d, "Art History Brush");
  for (k = 0; k < n; k++) {
    double r = sqrt(brush_rand01(seed, (u32)k, 1)) * area / 2, a = brush_rand01(seed, (u32)k, 2) * 2 * M_PI;
    double px = x + r * cos(a), py = y + r * sin(a), dir;
    i32 ix = (i32)floor(px), iy = (i32)floor(py);
    const u8 *c;
    u32 col;
    Stroke *s;
    int step, steps;
    if (ix < 0 || iy < 0 || ix >= d->w || iy >= d->h) continue;
    c = src + ((long)iy * d->w + ix) * 4;
    if (tolerance > 0) {
      /* only where the picture now differs from the source */
      const u8 *q = l->px + ((long)iy * d->w + ix) * 4;
      int diff = abs(q[0] - c[0]) + abs(q[1] - c[1]) + abs(q[2] - c[2]);
      if (diff * 100 < tolerance * 3 * 255 / 4) continue;
    }
    col = (u32)c[0] << 16 | (u32)c[1] << 8 | c[2];
    /* along the edges: perpendicular to the brightness gradient, with some looseness */
    {
      double gx = lum_at(src, d->w, d->h, ix + 2, iy) - lum_at(src, d->w, d->h, ix - 2, iy);
      double gy = lum_at(src, d->w, d->h, ix, iy + 2) - lum_at(src, d->w, d->h, ix, iy - 2);
      dir = fabs(gx) + fabs(gy) > 1 ? atan2(gx, -gy) : brush_rand01(seed, (u32)k, 3) * 2 * M_PI;
    }
    dir += (brush_rand01(seed, (u32)k, 4) * 2 - 1) * loose * M_PI;
    if (!(s = stroke_begin(d, &b, col, px, py, &ch))) break;
    all = box_union(all, ch);
    steps = (int)(len * 4);
    for (step = 1; step <= steps; step++) {
      dir += curl * 0.35 + (brush_rand01(seed, (u32)(k * 64 + step), 5) * 2 - 1) * loose * 0.3;
      px += cos(dir) * size * 0.25 * (len > 0 ? 1 : 0); py += sin(dir) * size * 0.25;
      stroke_to(s, px, py, &ch);
      all = box_union(all, ch);
    }
    stroke_end(s);
  }
  doc_end(d);
  return all;
}

/* ------------------------------------------------------------------ healing */
static void box_blur_rgb(const u8 *in, u8 *out, i32 w, i32 h, int r)
{
  i32 x, y;
  int c;
  long *row = (long *)malloc((size_t)(w + 1) * 3 * sizeof(long));
  u8 *tmp = (u8 *)malloc((size_t)w * h * 4);
  if (!row || !tmp) { free(row); free(tmp); memcpy(out, in, (size_t)w * h * 4); return; }
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      long s[3] = { 0, 0, 0 };
      int n = 0;
      i32 k;
      for (k = x - r; k <= x + r; k++) { i32 q = k < 0 ? 0 : k >= w ? w - 1 : k; for (c = 0; c < 3; c++) s[c] += in[((long)y * w + q) * 4 + c]; n++; }
      for (c = 0; c < 3; c++) tmp[((long)y * w + x) * 4 + c] = (u8)(s[c] / n);
      tmp[((long)y * w + x) * 4 + 3] = in[((long)y * w + x) * 4 + 3];
    }
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      long s[3] = { 0, 0, 0 };
      int n = 0;
      i32 k;
      for (k = y - r; k <= y + r; k++) { i32 q = k < 0 ? 0 : k >= h ? h - 1 : k; for (c = 0; c < 3; c++) s[c] += tmp[((long)q * w + x) * 4 + c]; n++; }
      for (c = 0; c < 3; c++) out[((long)y * w + x) * 4 + c] = (u8)(s[c] / n);
      out[((long)y * w + x) * 4 + 3] = tmp[((long)y * w + x) * 4 + 3];
    }
  free(row); free(tmp);
}
int heal_blend_ex(u8 *out, const u8 *base, const u8 *src, const u8 *mask, i32 bw, i32 bh, int diffusion)
{
  u8 *s2, *bl;
  long k, n = (long)bw * bh;
  double keep;
  int c, ok;
  if (diffusion <= 0) diffusion = 5;
  if (diffusion > 7) diffusion = 7;
  if (diffusion == 1) return heal_blend(out, base, src, mask, bw, bh);
  keep = 1 - (diffusion - 1) / 8.0;                       /* 7: a quarter of the fine detail */
  s2 = (u8 *)malloc((size_t)n * 4); bl = (u8 *)malloc((size_t)n * 4);
  if (!s2 || !bl) { free(s2); free(bl); return heal_blend(out, base, src, mask, bw, bh); }
  box_blur_rgb(src, bl, bw, bh, 2);
  for (k = 0; k < n; k++) {
    for (c = 0; c < 3; c++) s2[k * 4 + c] = (u8)clamp255(bl[k * 4 + c] + (src[k * 4 + c] - bl[k * 4 + c]) * keep);
    s2[k * 4 + 3] = src[k * 4 + 3];
  }
  ok = heal_blend(out, base, s2, mask, bw, bh);
  free(s2); free(bl);
  return ok;
}
int spot_texture_fill(const u8 *px, i32 w, i32 h, Box b, const u8 *mask, u8 *out, u32 seed)
{
  i32 bw = b.x1 - b.x0, bh = b.y1 - b.y0, x, y, bx, by;
  u8 *base, *src;
  int ok;
  if (bw < 1 || bh < 1) return 1;
  base = (u8 *)malloc((size_t)bw * bh * 4); src = (u8 *)malloc((size_t)bw * bh * 4);
  if (!base || !src) { free(base); free(src); return 0; }
  for (y = 0; y < bh; y++) memcpy(base + (size_t)y * bw * 4, px + ((long)(y + b.y0) * w + b.x0) * 4, (size_t)bw * 4);
  memcpy(src, base, (size_t)bw * bh * 4);
  /* 6 x 6 blocks of the area each copied from a random place round it that isn't in the area */
  for (by = 0; by < bh; by += 6)
    for (bx = 0; bx < bw; bx += 6) {
      int any = 0, tries;
      i32 sx = 0, sy = 0;
      for (y = by; y < by + 6 && y < bh && !any; y++) for (x = bx; x < bx + 6 && x < bw; x++) if (mask[(long)y * bw + x]) { any = 1; break; }
      if (!any) continue;
      for (tries = 0; tries < 40; tries++) {
        int clean = 1;
        sx = (i32)(brush_rand01(seed, (u32)(by * 4099 + bx), (u32)(tries * 2)) * (w - 6));
        sy = (i32)(brush_rand01(seed, (u32)(by * 4099 + bx), (u32)(tries * 2 + 1)) * (h - 6));
        /* near the area: within the box's size of it */
        sx = b.x0 + (sx % (bw + 1)) - bw / 2; sy = b.y0 + (sy % (bh + 1)) - bh / 2;
        if (sx < 0) sx = 0;
        if (sy < 0) sy = 0;
        if (sx > w - 6) sx = w - 6 < 0 ? 0 : w - 6;
        if (sy > h - 6) sy = h - 6 < 0 ? 0 : h - 6;
        for (y = 0; y < 6 && clean; y++) for (x = 0; x < 6; x++) {
          i32 cx = sx + x - b.x0, cy = sy + y - b.y0;
          if (cx >= 0 && cy >= 0 && cx < bw && cy < bh && mask[(long)cy * bw + cx]) { clean = 0; break; }
        }
        if (clean) break;
      }
      for (y = by; y < by + 6 && y < bh; y++) for (x = bx; x < bx + 6 && x < bw; x++) {
        i32 qx = sx + (x - bx), qy = sy + (y - by);
        if (qx >= w) qx = w - 1;
        if (qy >= h) qy = h - 1;
        memcpy(src + ((long)y * bw + x) * 4, px + ((long)qy * w + qx) * 4, 3);
      }
    }
  ok = heal_blend(out, base, src, mask, bw, bh);
  free(base); free(src);
  return ok;
}

/* ------------------------------------------------------------------ tablets */
int tablet_parse(const unsigned long *tags, TabletState *st)
{
  unsigned long z = 0, rz = 0;
  int hz = 0, hrz = 0, guard = 0;
  memset(st, 0, sizeof *st);
  st->in_proximity = 1; st->pressure = 1;
  while (tags && guard++ < 256) {
    unsigned long t = tags[0], v = tags[1];
    if (t == 0) break;                                     /* TAG_DONE */
    if (t == 1) { tags += 2; continue; }                   /* TAG_IGNORE */
    if (t == 2) { tags = (const unsigned long *)v; continue; }   /* TAG_MORE */
    if (t == 3) { tags += 2 * (v + 1); continue; }         /* TAG_SKIP */
    switch (t - TABLET_TAG_BASE) {
    case 0x01: z = v; hz = 1; break;                       /* TabletZ */
    case 0x02: rz = v; hrz = 1; break;                     /* RangeZ */
    case 0x03: st->tilt_y = (double)(long)(int)(unsigned int)v / 2147483648.0; st->has_tilt = 1; break;   /* AngleX: towards +y */
    case 0x04: st->tilt_x = -(double)(long)(int)(unsigned int)v / 2147483648.0; st->has_tilt = 1; break;  /* AngleY: towards -x */
    case 0x06: {                                           /* Pressure */
      long p = (long)(int)(unsigned int)v;
      st->pressure = p <= 0 ? 0 : p / 2147483647.0;
      st->has_pressure = 1;
      break; }
    case 0x08: st->in_proximity = v != 0; break;
    default: break;
    }
    tags += 2;
  }
  if (!st->has_pressure && hz && hrz && rz > 0) { st->pressure = clampd((double)z / rz, 0, 1); st->has_pressure = 1; }
  st->tilt_x = clampd(st->tilt_x, -1, 1); st->tilt_y = clampd(st->tilt_y, -1, 1);
  return st->has_pressure;
}
double tablet_curve(double p, int curve, int floor_pc)
{
  double fl = clampd(floor_pc / 100.0, 0, 0.9), g;
  p = clampd(p, 0, 1);
  if (p <= fl) return 0;
  p = (p - fl) / (1 - fl);
  if (curve <= 0) curve = 100;
  g = 100.0 / clampd(curve, 25, 400);                      /* 200 % soft: a light touch gives more */
  return pow(p, g);
}

/* ------------------------------------------------------------------ Photoshop .abr (writing) */
typedef struct { u8 *b; size_t n, cap; int bad; } Wr;
static void wb(Wr *w, const void *p, size_t k)
{
  if (w->bad) return;
  if (w->n + k > w->cap) {
    size_t c = w->cap ? w->cap * 2 : 1024;
    u8 *nb;
    while (c < w->n + k) c *= 2;
    if (!(nb = (u8 *)realloc(w->b, c))) { w->bad = 1; return; }
    w->b = nb; w->cap = c;
  }
  memcpy(w->b + w->n, p, k); w->n += k;
}
static void w8(Wr *w, u32 v) { u8 c = (u8)v; wb(w, &c, 1); }
static void w16(Wr *w, u32 v) { u8 c[2]; c[0] = (u8)(v >> 8); c[1] = (u8)v; wb(w, c, 2); }
static void w32(Wr *w, u32 v) { u8 c[4]; c[0] = (u8)(v >> 24); c[1] = (u8)(v >> 16); c[2] = (u8)(v >> 8); c[3] = (u8)v; wb(w, c, 4); }
static void put32at(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
/* UTF-8 to UTF-16 code units (Latin-1 and the BMP), with the count first */
static void wustr(Wr *w, const char *s, int nul)
{
  u32 cp[256];
  int n = 0, k;
  const u8 *p = (const u8 *)(s ? s : "");
  while (*p && n < 255) {
    u32 c = *p++;
    if (c >= 0xC0 && (*p & 0xC0) == 0x80) {
      if (c < 0xE0) { c = ((c & 0x1F) << 6) | (*p++ & 0x3F); }
      else if (c < 0xF0 && (p[1] & 0xC0) == 0x80) { c = ((c & 0x0F) << 12) | ((u32)(p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
      else { while ((*p & 0xC0) == 0x80) p++; c = '?'; }
    }
    cp[n++] = c;
  }
  w32(w, (u32)(n + (nul ? 1 : 0)));
  for (k = 0; k < n; k++) w16(w, cp[k]);
  if (nul) w16(w, 0);
}
u8 *abr_write(const Brush *bs, const char *const *names, int n, size_t *len)
{
  Wr w;
  int i;
  memset(&w, 0, sizeof w);
  w16(&w, 2); w16(&w, (u32)n);
  for (i = 0; i < n; i++) {
    const Brush *b = &bs[i];
    size_t at;
    int spacing = b->spacing > 0 ? b->spacing : 25;
    if (!b->tip || !b->tip->a) {
      w16(&w, 1); w32(&w, 14);
      w32(&w, 0); w16(&w, (u32)spacing); w16(&w, (u32)(b->size < 1 ? 1 : b->size > 30000 ? 30000 : b->size));
      w16(&w, (u32)(b->roundness > 0 ? b->roundness : 100)); w16(&w, (u32)(i16)b->angle); w16(&w, (u32)b->hardness);
    } else {
      const Tip *t = b->tip;
      i32 y;
      w16(&w, 2);
      at = w.n; w32(&w, 0);
      w32(&w, 0); w16(&w, (u32)spacing);
      wustr(&w, names && names[i] ? names[i] : "Brush", 0);
      w8(&w, 1);
      w16(&w, 0); w16(&w, 0); w16(&w, (u32)t->h); w16(&w, (u32)t->w);
      w32(&w, 0); w32(&w, 0); w32(&w, (u32)t->h); w32(&w, (u32)t->w);
      w16(&w, 8); w8(&w, 0);
      for (y = 0; y < t->h; y++) wb(&w, t->a + (long)y * t->w, (size_t)t->w);
      if (!w.bad) put32at(w.b + at, (u32)(w.n - at - 4));
    }
  }
  if (w.bad) { free(w.b); return NULL; }
  *len = w.n;
  return w.b;
}

/* ------------------------------------------------------------------ reading big-endian data */
typedef struct { const u8 *b; size_t n, p; int bad; } Rd;
static u32 r8(Rd *r) { if (r->p + 1 > r->n) { r->bad = 1; return 0; } return r->b[r->p++]; }
static u32 r16(Rd *r) { u32 v; if (r->p + 2 > r->n) { r->bad = 1; r->p = r->n; return 0; } v = ((u32)r->b[r->p] << 8) | r->b[r->p + 1]; r->p += 2; return v; }
static u32 r32(Rd *r) { u32 v; if (r->p + 4 > r->n) { r->bad = 1; r->p = r->n; return 0; } v = ((u32)r->b[r->p] << 24) | ((u32)r->b[r->p + 1] << 16) | ((u32)r->b[r->p + 2] << 8) | r->b[r->p + 3]; r->p += 4; return v; }
static void rskip(Rd *r, size_t k) { if (r->n - r->p < k) { r->bad = 1; r->p = r->n; } else r->p += k; }
static double rdouble(Rd *r)
{
  u8 c[8];
  double v;
  int k;
  union { double d; u8 b[8]; } u;
  for (k = 0; k < 8; k++) c[k] = (u8)r8(r);
  /* big-endian IEEE to this machine's order */
  u.d = 1.0;
  if (u.b[0] == 0x3F) { for (k = 0; k < 8; k++) u.b[k] = c[k]; }
  else { for (k = 0; k < 8; k++) u.b[k] = c[7 - k]; }
  v = u.d;
  if (r->bad || v != v) return 0;
  return v;
}
static void set_err(char *err, size_t cap, const char *m) { if (err && cap) { strncpy(err, m, cap - 1); err[cap - 1] = 0; } }
/* a UTF-16 string into Latin-1/UTF-8 (cap bytes) */
static void rustr(Rd *r, char *out, size_t cap)
{
  u32 n = r32(r), k;
  size_t q = 0;
  if (n > 100000) { r->bad = 1; return; }
  for (k = 0; k < n && !r->bad; k++) {
    u32 c = r16(r);
    if (!c) continue;
    if (c < 0x80) { if (q + 1 < cap) out[q++] = (char)c; }
    else if (c < 0x800) { if (q + 2 < cap) { out[q++] = (char)(0xC0 | (c >> 6)); out[q++] = (char)(0x80 | (c & 0x3F)); } }
    else if (q + 3 < cap) { out[q++] = (char)(0xE0 | (c >> 12)); out[q++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[q++] = (char)(0x80 | (c & 0x3F)); }
  }
  if (cap) out[q < cap ? q : cap - 1] = 0;
}
/* a descriptor key or class id: length then that many bytes, or 0 then four */
static void rkey(Rd *r, char *k, size_t cap)
{
  u32 n = r32(r);
  size_t m;
  if (n == 0) n = 4;
  if (n > 1000 || n > r->n - r->p) { r->bad = 1; if (cap) k[0] = 0; return; }
  m = n < cap - 1 ? n : cap - 1;
  memcpy(k, r->b + r->p, m); k[m] = 0;
  r->p += n;
}

/* ------------------------------------------------------------------ .grd */
/* what the descriptor walk is building */
typedef struct {
  Gradient *out; int max, n;
  Gradient *g;                        /* the one being read */
  int in_color, in_trans;             /* inside a colour stop or an opacity stop */
  double loc, mid, opct; int kind;
  double c[4]; char model[8];
} GrdCtx;
static void grd_desc(Rd *r, GrdCtx *c, int depth);
static void hsb_rgb(double h, double s, double v, double *o)
{
  int k = (int)floor(h / 60) % 6;
  double f = h / 60 - floor(h / 60), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  double rr[6] = { v, q, p, p, t, v }, gg[6] = { t, v, v, q, p, p }, bb[6] = { p, p, t, v, v, q };
  if (k < 0) k += 6;
  o[0] = rr[k]; o[1] = gg[k]; o[2] = bb[k];
}
static u32 grd_color(const GrdCtx *c)
{
  double o[3];
  if (!strcmp(c->model, "HSBC")) hsb_rgb(fmod(c->c[0] + 360, 360), c->c[1] / 100, c->c[2] / 100, o), o[0] *= 255, o[1] *= 255, o[2] *= 255;
  else if (!strcmp(c->model, "Grsc")) o[0] = o[1] = o[2] = 255 - c->c[0] * 2.55;
  else if (!strcmp(c->model, "CMYC")) { int k; for (k = 0; k < 3; k++) o[k] = 255 * (1 - c->c[k] / 100) * (1 - c->c[3] / 100); }
  else if (!strcmp(c->model, "LbCl")) {
    /* Lab (D50) to sRGB, roughly */
    double L = c->c[0], a = c->c[1], b = c->c[2], fy = (L + 16) / 116, fx = fy + a / 500, fz = fy - b / 200, X, Y, Z, rl, gl, bl;
    X = 0.9642 * (fx > 0.2069 ? fx * fx * fx : (fx - 16.0 / 116) / 7.787);
    Y = fy > 0.2069 ? fy * fy * fy : (fy - 16.0 / 116) / 7.787;
    Z = 0.8249 * (fz > 0.2069 ? fz * fz * fz : (fz - 16.0 / 116) / 7.787);
    rl = 3.1339 * X - 1.6169 * Y - 0.4906 * Z; gl = -0.9788 * X + 1.9161 * Y + 0.0334 * Z; bl = 0.0719 * X - 0.2290 * Y + 1.4052 * Z;
    o[0] = 255 * (rl <= 0.0031308 ? 12.92 * rl : 1.055 * pow(rl < 0 ? 0 : rl, 1 / 2.4) - 0.055);
    o[1] = 255 * (gl <= 0.0031308 ? 12.92 * gl : 1.055 * pow(gl < 0 ? 0 : gl, 1 / 2.4) - 0.055);
    o[2] = 255 * (bl <= 0.0031308 ? 12.92 * bl : 1.055 * pow(bl < 0 ? 0 : bl, 1 / 2.4) - 0.055);
  } else { o[0] = c->c[0]; o[1] = c->c[1]; o[2] = c->c[2]; }
  return (u32)clamp255(o[0]) << 16 | (u32)clamp255(o[1]) << 8 | (u32)clamp255(o[2]);
}
static void grd_number(GrdCtx *c, const char *key, double v)
{
  if (!strcmp(key, "Lctn")) c->loc = v / 4096.0;
  else if (!strcmp(key, "Mdpn")) c->mid = v / 100.0;
  else if (!strcmp(key, "Opct")) c->opct = v / 100.0;
  else if (!strcmp(key, "Rd  ") || !strcmp(key, "H   ") || !strcmp(key, "Gry ") || !strcmp(key, "Cyn ") || !strcmp(key, "Lmnc")) c->c[0] = v;
  else if (!strcmp(key, "Grn ") || !strcmp(key, "Strt") || !strcmp(key, "Mgnt") || !strcmp(key, "A   ")) c->c[1] = v;
  else if (!strcmp(key, "Bl  ") || !strcmp(key, "Brgh") || !strcmp(key, "Ylw ") || !strcmp(key, "B   ")) c->c[2] = v;
  else if (!strcmp(key, "Blck")) c->c[3] = v;
}
/* one item's value of type t under key */
static void grd_value(Rd *r, GrdCtx *c, const char *key, const char *t, int depth)
{
  char k1[64], k2[64];
  if (depth > 20) { r->bad = 1; return; }
  if (!strcmp(t, "Objc") || !strcmp(t, "GlbO")) {
    char name[80], cls[64];
    rustr(r, name, sizeof name); rkey(r, cls, sizeof cls);
    if (r->bad) return;
    if (!strcmp(key, "Grad") && c->n < c->max) {
      c->g = &c->out[c->n];
      memset(c->g, 0, sizeof *c->g);
      grd_desc(r, c, depth + 1);
      if (c->g->ncolor > 0 && !r->bad) { if (!c->g->nopacity) gradient_add_opacity(c->g, 0, 1), gradient_add_opacity(c->g, 1, 1); gradient_sort(c->g); c->n++; }
      c->g = NULL;
    } else if (c->g && !strcmp(cls, "Clrt")) {
      c->in_color = 1; c->loc = 0; c->mid = 0.5; c->kind = GS_RGB; memset(c->c, 0, sizeof c->c); strcpy(c->model, "RGBC");
      grd_desc(r, c, depth + 1);
      c->in_color = 0;
      if (c->g->ncolor < GRAD_MAXSTOPS) {
        GradColorStop *s = &c->g->color[c->g->ncolor++];
        s->pos = clampd(c->loc, 0, 1); s->mid = clampd(c->mid, 0.05, 0.95); s->kind = c->kind; s->rgb = grd_color(c);
      }
    } else if (c->g && !strcmp(cls, "TrnS")) {
      c->in_trans = 1; c->loc = 0; c->mid = 0.5; c->opct = 1;
      grd_desc(r, c, depth + 1);
      c->in_trans = 0;
      if (c->g->nopacity < GRAD_MAXSTOPS) {
        GradOpacityStop *s = &c->g->opacity[c->g->nopacity++];
        s->pos = clampd(c->loc, 0, 1); s->mid = clampd(c->mid, 0.05, 0.95); s->opacity = clampd(c->opct, 0, 1);
      }
    } else {
      if (c->in_color && !strcmp(key, "Clr ")) { strncpy(c->model, cls, sizeof c->model - 1); c->model[sizeof c->model - 1] = 0; }
      grd_desc(r, c, depth + 1);
    }
  } else if (!strcmp(t, "VlLs")) {
    u32 n = r32(r), i;
    if (n > 100000) { r->bad = 1; return; }
    for (i = 0; i < n && !r->bad; i++) {
      char tt[8];
      if (r->n - r->p < 4) { r->bad = 1; return; }
      memcpy(tt, r->b + r->p, 4); tt[4] = 0; r->p += 4;
      grd_value(r, c, key, tt, depth + 1);
    }
  } else if (!strcmp(t, "doub")) grd_number(c, key, rdouble(r));
  else if (!strcmp(t, "UntF")) { rskip(r, 4); grd_number(c, key, rdouble(r)); }
  else if (!strcmp(t, "long")) grd_number(c, key, (double)(i32)r32(r));
  else if (!strcmp(t, "bool")) r8(r);
  else if (!strcmp(t, "TEXT")) {
    char s[64];
    rustr(r, s, sizeof s);
    if (c->g && !c->in_color && !c->in_trans && !strcmp(key, "Nm  ")) strncpy(c->g->name, s, sizeof c->g->name - 1);
  } else if (!strcmp(t, "enum")) {
    rkey(r, k1, sizeof k1); rkey(r, k2, sizeof k2);
    if (c->in_color && !strcmp(key, "Type")) c->kind = !strcmp(k2, "FrgC") ? GS_FG : !strcmp(k2, "BckC") ? GS_BG : GS_RGB;
  } else if (!strcmp(t, "type") || !strcmp(t, "GlbC")) { rustr(r, k1, sizeof k1); rkey(r, k2, sizeof k2); }
  else if (!strcmp(t, "tdta") || !strcmp(t, "alis")) { u32 n = r32(r); rskip(r, n); }
  else if (!strcmp(t, "comp")) { rskip(r, 8); }
  else r->bad = 1;                                         /* (a type no gradient uses) */
}
static void grd_desc(Rd *r, GrdCtx *c, int depth)
{
  u32 n = r32(r), i;
  if (n > 100000 || depth > 20) { r->bad = 1; return; }
  for (i = 0; i < n && !r->bad; i++) {
    char key[64], t[8];
    rkey(r, key, sizeof key);
    if (r->bad || r->n - r->p < 4) { r->bad = 1; return; }
    memcpy(t, r->b + r->p, 4); t[4] = 0; r->p += 4;
    grd_value(r, c, key, t, depth);
  }
}
int grd_read(const u8 *data, size_t n, struct Gradient *out, int max, char *err, size_t errcap)
{
  Rd r;
  u32 ver;
  r.b = data; r.n = n; r.p = 0; r.bad = 0;
  if (n < 6 || memcmp(data, "8BGR", 4)) { set_err(err, errcap, "Not a Photoshop gradient file"); return 0; }
  r.p = 4;
  ver = r16(&r);
  if (ver == 5) {
    GrdCtx c;
    char name[80], cls[64];
    memset(&c, 0, sizeof c);
    c.out = out; c.max = max;
    r32(&r);                                               /* (the descriptor version, 16) */
    rustr(&r, name, sizeof name); rkey(&r, cls, sizeof cls);
    grd_desc(&r, &c, 0);
    if (!c.n) set_err(err, errcap, "The gradient file holds no gradients Easel can read");
    return c.n;
  }
  if (ver == 3) {
    u32 cnt = r16(&r), i;
    int got = 0;
    for (i = 0; i < cnt && !r.bad && got < max; i++) {
      Gradient *g = &out[got];
      u32 nl = r8(&r), k, nc, nt;
      memset(g, 0, sizeof *g);
      for (k = 0; k < nl; k++) { u32 ch = r8(&r); if (k < sizeof g->name - 1) g->name[k] = (char)ch; }
      nc = r16(&r);
      for (k = 0; k < nc && !r.bad; k++) {
        double loc = r32(&r) / 4096.0, mid = r32(&r) / 100.0, v[4];
        u32 model = r16(&r), type, q;
        GrdCtx cc;
        for (q = 0; q < 4; q++) v[q] = r16(&r);
        type = r16(&r);
        memset(&cc, 0, sizeof cc);
        if (model == 1) { strcpy(cc.model, "HSBC"); cc.c[0] = v[0] * 360.0 / 65535; cc.c[1] = v[1] * 100.0 / 65535; cc.c[2] = v[2] * 100.0 / 65535; }
        else if (model == 8) { strcpy(cc.model, "Grsc"); cc.c[0] = 100 - v[0] * 100.0 / 10000; }
        else if (model == 2) { strcpy(cc.model, "CMYC"); for (q = 0; q < 4; q++) cc.c[q] = 100 - v[q] * 100.0 / 65535; }
        else { strcpy(cc.model, "RGBC"); for (q = 0; q < 3; q++) cc.c[q] = v[q] / 257.0; }
        if (g->ncolor < GRAD_MAXSTOPS) {
          GradColorStop *s = &g->color[g->ncolor++];
          s->pos = clampd(loc, 0, 1); s->mid = clampd(mid, 0.05, 0.95); s->kind = type == 1 ? GS_FG : type == 2 ? GS_BG : GS_RGB; s->rgb = grd_color(&cc);
        }
      }
      nt = r16(&r);
      for (k = 0; k < nt && !r.bad; k++) {
        double loc = r32(&r) / 4096.0, mid = r32(&r) / 100.0, op = r16(&r) / 255.0;
        if (g->nopacity < GRAD_MAXSTOPS) {
          GradOpacityStop *s = &g->opacity[g->nopacity++];
          s->pos = clampd(loc, 0, 1); s->mid = clampd(mid, 0.05, 0.95); s->opacity = clampd(op, 0, 1);
        }
      }
      rskip(&r, 6);
      if (r.bad && !g->ncolor) break;
      if (!g->nopacity) { gradient_add_opacity(g, 0, 1); gradient_add_opacity(g, 1, 1); }
      if (g->ncolor) { gradient_sort(g); got++; }
    }
    if (!got) set_err(err, errcap, "The gradient file holds no gradients Easel can read");
    return got;
  }
  set_err(err, errcap, "Not a gradient file this version of Easel knows");
  return 0;
}

/* ------------------------------------------------------------------ .pat */
/* one channel of a virtual memory array: into out (w x h, at the pattern's offsets), 1 if read */
static int pat_channel(Rd *r, u8 *out, i32 w, i32 h, int *written)
{
  u32 is_written = r32(r), len, depth, top, left, bottom, right, comp;
  size_t end;
  i32 cw, ch, y;
  *written = 0;
  if (r->bad || !is_written) return 1;
  len = r32(r);
  if (!len) return 1;
  if (len > r->n - r->p) { r->bad = 1; return 0; }
  end = r->p + len;
  depth = r32(r); top = r32(r); left = r32(r); bottom = r32(r); right = r32(r);
  r16(r); comp = r8(r);
  cw = (i32)(right - left); ch = (i32)(bottom - top);
  if (r->bad || depth != 8 || cw != w || ch != h || w < 1 || h < 1) { r->p = end; return 1; }
  if (comp == 0) {
    if ((size_t)w * h > end - r->p) { r->p = end; return 1; }
    memcpy(out, r->b + r->p, (size_t)w * h);
  } else if (comp == 1) {
    size_t data = r->p + (size_t)h * 2;
    if (data > end) { r->p = end; return 1; }
    for (y = 0; y < h; y++) {
      size_t ln = ((size_t)r->b[r->p + y * 2] << 8) | r->b[r->p + y * 2 + 1], i = 0;
      i32 o = 0;
      u8 *row = out + (size_t)y * w;
      if (data + ln > end) break;
      while (o < w && i < ln) {
        int c = r->b[data + i++];
        if (c < 128) { i32 k = c + 1; if (k > w - o) k = w - o; if (i + (size_t)k > ln) k = (i32)(ln - i); memcpy(row + o, r->b + data + i, (size_t)k); i += (size_t)(c + 1); o += k; }
        else if (c > 128) { i32 k = 257 - c; if (i >= ln) break; if (k > w - o) k = w - o; memset(row + o, r->b[data + i], (size_t)k); i++; o += k; }
      }
      data += ln;
    }
  } else { r->p = end; return 1; }
  r->p = end;
  *written = 1;
  return 1;
}
int pat_read(PatternList *l, const u8 *data, size_t n, char *err, size_t errcap)
{
  Rd r;
  u32 cnt, i;
  int got = 0;
  r.b = data; r.n = n; r.p = 0; r.bad = 0;
  if (n < 10 || memcmp(data, "8BPT", 4)) { set_err(err, errcap, "Not a Photoshop pattern file"); return 0; }
  r.p = 4;
  if (r16(&r) != 1) { set_err(err, errcap, "Not a pattern file this version of Easel knows"); return 0; }
  cnt = r32(&r);
  for (i = 0; i < cnt && !r.bad && i < 10000; i++) {
    u32 ver = r32(&r), mode = r32(&r), h = r16(&r), w = r16(&r), nl, k;
    char name[64];
    u8 pal[768], *ch[4] = { NULL, NULL, NULL, NULL };
    int nch, c, wr;
    u32 vver, vlen, nchan;
    size_t vend;
    Pattern *p;
    (void)ver;
    rustr(&r, name, sizeof name);
    nl = r8(&r); rskip(&r, nl);                             /* the unique id */
    if (mode == 2) { for (k = 0; k < 768; k++) pal[k] = (u8)r8(&r); rskip(&r, 4); }
    vver = r32(&r); vlen = r32(&r);
    if (r.bad || vver != 3 || vlen > r.n - r.p) break;
    vend = r.p + vlen;
    rskip(&r, 16);
    nchan = r32(&r);
    nch = mode == 3 ? 3 : 1;
    if ((mode != 1 && mode != 2 && mode != 3) || w < 1 || h < 1 || w > 4096 || h > 4096) { r.p = vend; continue; }
    for (c = 0; c < 4; c++) if (!(ch[c] = (u8 *)malloc((size_t)w * h))) break;
    if (c < 4) { for (c = 0; c < 4; c++) free(ch[c]); r.p = vend; continue; }
    memset(ch[3], 255, (size_t)w * h);
    for (c = 0; c < nch && !r.bad; c++) pat_channel(&r, ch[c], (i32)w, (i32)h, &wr);
    /* the arrays after the colour channels: a transparency mask when one is written */
    for (k = (u32)nch; k < nchan + 2 && k < 26 && !r.bad && r.p + 8 <= vend; k++) {
      int written;
      pat_channel(&r, ch[3], (i32)w, (i32)h, &written);
      if (written) break;
    }
    r.p = vend;
    if ((p = pattern_new(name[0] ? name : "Pattern", (i32)w, (i32)h)) != NULL) {
      long q, np = (long)w * h;
      for (q = 0; q < np; q++) {
        u8 *o = p->px + q * 4;
        if (mode == 3) { o[0] = ch[0][q]; o[1] = ch[1][q]; o[2] = ch[2][q]; }
        else if (mode == 2) { o[0] = pal[ch[0][q] * 3]; o[1] = pal[ch[0][q] * 3 + 1]; o[2] = pal[ch[0][q] * 3 + 2]; }
        else o[0] = o[1] = o[2] = ch[0][q];
        o[3] = ch[3][q];
      }
      if (patlist_add(l, p)) got++;
    }
    for (c = 0; c < 4; c++) free(ch[c]);
  }
  if (!got) set_err(err, errcap, "The pattern file holds no patterns Easel can read");
  return got;
}
u8 *pat_write(const struct Pattern *const *ps, int n, size_t *len)
{
  Wr w;
  int i, c;
  memset(&w, 0, sizeof w);
  wb(&w, "8BPT", 4); w16(&w, 1); w32(&w, (u32)n);
  for (i = 0; i < n; i++) {
    const Pattern *p = ps[i];
    size_t at;
    char id[40];
    i32 x, y;
    w32(&w, 1); w32(&w, 3); w16(&w, (u32)p->h); w16(&w, (u32)p->w);
    wustr(&w, p->name, 1);
    sprintf(id, "easel-pattern-%d", i);
    w8(&w, (u32)strlen(id)); wb(&w, id, strlen(id));
    w32(&w, 3); at = w.n; w32(&w, 0);
    w32(&w, 0); w32(&w, 0); w32(&w, (u32)p->h); w32(&w, (u32)p->w);
    w32(&w, 24);
    for (c = 0; c < 4; c++) {
      /* R, G, B, then the transparency in the user mask's place */
      w32(&w, 1); w32(&w, (u32)(4 * 5 + 3 + (size_t)p->w * p->h));
      w32(&w, 8); w32(&w, 0); w32(&w, 0); w32(&w, (u32)p->h); w32(&w, (u32)p->w); w16(&w, 8); w8(&w, 0);
      for (y = 0; y < p->h; y++) for (x = 0; x < p->w; x++) w8(&w, p->px[((long)y * p->w + x) * 4 + c]);
    }
    w32(&w, 0);                                            /* the sheet mask: not written */
    if (!w.bad) put32at(w.b + at, (u32)(w.n - at - 4));
  }
  if (w.bad) { free(w.b); return NULL; }
  *len = w.n;
  return w.b;
}
