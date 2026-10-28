/*
 * filters2.c - more of the Filter menu (see filters2.h), in the manner of adjust.c: each works out
 * new pixels for the active layer in the selection's box from a copy of the layer as it was, and
 * where the selection is partial the new pixels are blended with the old in proportion.
 *
 * Inner loops keep to integers, sqrt and tables (the 68040 has no sine or arctangent in hardware:
 * those trap to software), so the trigonometry goes through a sine table and a polynomial arctangent.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "filters2.h"
#include "adjust.h"

static int clamp255(double v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5); }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int lum8(const u8 *p) { return (p[0] * 77 + p[1] * 150 + p[2] * 29 + 128) >> 8; }
static void rgb_of(u32 c, u8 *p) { p[0] = (u8)(c >> 16); p[1] = (u8)(c >> 8); p[2] = (u8)c; }

/* ------------------------------------------------------------------ the frame every filter shares */
typedef struct { Doc *d; Layer *l; Box b; u8 *orig; } Op;
/* 0.8: row y of the box reached (progress, and an Abort asked for: the loop stops) */
static int op_tick(const Op *o, i32 y) { return progress(y - o->b.y0, o->b.y1 - o->b.y0); }
/* begun: the layer, its working box, an undo step and a copy of the layer as it was (always kept
   when copy is asked, or there's a selection or the alpha is locked) */
static int op_begin(Op *o, Doc *d, const char *name, int copy)
{
  Layer *l;
  o->d = d; o->orig = NULL;
  if (d->active < 0 || d->active >= d->nlay) return 0;
  l = d->lay[d->active];
  if (!l->px || l->locked || !l->visible) return 0;
  o->l = l;
  o->b = d->sel ? box_clip(d->selbox, doc_box(d)) : doc_box(d);
  if (box_empty(o->b)) return 0;
  if (copy || d->sel || l->alpha_lock) {
    if (!(o->orig = (u8 *)malloc((size_t)d->w * d->h * 4))) return 0;
    memcpy(o->orig, l->px, (size_t)d->w * d->h * 4);
  }
  doc_begin(d, name);
  doc_note_layer(d, d->active, o->b);
  return 1;
}
/* finished: the transparency put back if it's locked; partial selection blended with the old */
static int op_end(Op *o)
{
  Doc *d = o->d;
  i32 x, y;
  if (o->orig && (d->sel || o->l->alpha_lock))
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
/* the original at x, y with the coordinates held to the canvas */
static const u8 *orig_at(const Op *o, i32 x, i32 y)
{
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= o->d->w) x = o->d->w - 1;
  if (y >= o->d->h) y = o->d->h - 1;
  return ORIG(o, x, y);
}
/* a bilinear sample of the original at sx, sy (pixel centres at whole numbers), premultiplied,
   held to box c, or wrapped across it left to right */
static void sample_bl(const Op *o, Box c, int wrapx, double sx, double sy, u8 *out)
{
  /* weights in 1/128ths each way, so the sums stay within 32 bits */
  i32 ix = (i32)floor(sx), iy = (i32)floor(sy), w = c.x1 - c.x0;
  int fx = (int)((sx - ix) * 128), fy = (int)((sy - iy) * 128), k, ch;
  long acc[4] = { 0, 0, 0, 0 };
  for (k = 0; k < 4; k++) {
    i32 xx = ix + (k & 1), yy = iy + (k >> 1);
    long wgt = (long)((k & 1) ? fx : 128 - fx) * ((k >> 1) ? fy : 128 - fy);
    const u8 *q;
    if (!wgt) continue;
    if (wrapx) xx = c.x0 + ((xx - c.x0) % w + w) % w;
    else xx = xx < c.x0 ? c.x0 : xx >= c.x1 ? c.x1 - 1 : xx;
    yy = yy < c.y0 ? c.y0 : yy >= c.y1 ? c.y1 - 1 : yy;
    q = ORIG(o, xx, yy);
    for (ch = 0; ch < 3; ch++) acc[ch] += (long)q[ch] * q[3] * wgt;
    acc[3] += q[3] * wgt;
  }
  out[3] = (u8)((acc[3] + 8192) >> 14);
  for (ch = 0; ch < 3; ch++) { long v = acc[3] ? (acc[ch] + acc[3] / 2) / acc[3] : 0; out[ch] = (u8)(v > 255 ? 255 : v); }
}
/* each pixel of the box from the original at the point fn gives (bilinear) */
typedef void (*MapFn)(const Op *o, const double *a, double x, double y, double *sx, double *sy);
static int remap(Doc *d, const char *name, MapFn fn, const double *a, int inbox, int wrapx)
{
  Op o;
  i32 x, y;
  if (!op_begin(&o, d, name, 1)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      double sx, sy;
      fn(&o, a, x + 0.5, y + 0.5, &sx, &sy);
      sample_bl(&o, inbox ? o.b : doc_box(d), wrapx, sx - 0.5, sy - 0.5, DEST(&o, x, y));
    }
  return op_end(&o);
}

/* ------------------------------------------------------------------ tables and noise */
#define SINN 1024
static double sintab[SINN + 1];
static int sin_ready;
/* sine and cosine of t turns (a whole turn is 1), from a table, linearly interpolated */
static double tsin(double t)
{
  double f;
  long i;
  if (!sin_ready) { int k; for (k = 0; k <= SINN; k++) sintab[k] = sin(k * (2 * 3.14159265358979323846 / SINN)); sin_ready = 1; }
  f = t * SINN;
  i = (long)floor(f);
  f -= i;
  i &= SINN - 1;
  return sintab[i] + (sintab[i + 1] - sintab[i]) * f;
}
static double tcos(double t) { return tsin(t + 0.25); }
/* the direction of (x, y) in turns, 0 to 1, anticlockwise from x (a polynomial: about 1e-5 rad) */
static double tatan2(double y, double x)
{
  double ax = fabs(x), ay = fabs(y), a, t, s;
  if (ax == 0 && ay == 0) return 0;
  t = ax > ay ? ay / ax : ax / ay;
  s = t * t;
  a = t * (0.99997726 + s * (-0.33262347 + s * (0.19354346 + s * (-0.11643287 + s * (0.05265332 + s * -0.01172120)))));
  if (ay > ax) a = 1.57079632679489662 - a;
  if (x < 0) a = 3.14159265358979324 - a;
  if (y < 0) a = -a;
  a /= 2 * 3.14159265358979324;
  return a < 0 ? a + 1 : a;
}
/* a hash of three numbers, for randomness that depends only on where and the seed */
static u32 hash3(u32 a, u32 b, u32 c)
{
  u32 h = a * 0x8DA6B343UL ^ b * 0xD8163841UL ^ c * 0xCB1AB31FUL ^ 0x9E3779B9UL;
  h ^= h >> 16; h *= 0x7FEB352DUL; h ^= h >> 15; h *= 0x846CA68BUL; h ^= h >> 16;
  return h;
}
/* value noise (the same as flt_clouds', so Difference Clouds matches Clouds for a seed) */
static double lattice(int x, int y, unsigned seed)
{
  unsigned long n = (unsigned long)x * 374761393UL + (unsigned long)y * 668265263UL + seed * 2147483647UL;
  n = (n ^ (n >> 13)) * 1274126177UL;
  return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0;
}
static double vnoise(double x, double y, unsigned seed)
{
  int ix = (int)floor(x), iy = (int)floor(y);
  double fx = x - ix, fy = y - iy, a, b, c, dd;
  fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
  a = lattice(ix, iy, seed); b = lattice(ix + 1, iy, seed); c = lattice(ix, iy + 1, seed); dd = lattice(ix + 1, iy + 1, seed);
  return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + dd * fx) * fy;
}

/* ------------------------------------------------------------------ render */
int flt_difference_clouds(Doc *d, u32 c0, u32 c1, unsigned seed)
{
  Op o;
  i32 x, y;
  if (!op_begin(&o, d, "Difference Clouds", 0)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      double v = 0, amp = 0.5, f = 1 / 128.0, t;
      int k, c;
      u8 s[4];
      for (k = 0; k < 7; k++, amp /= 2, f *= 2) v += amp * vnoise(x * f, y * f, seed + k * 7919);
      t = v / 0.99;
      for (c = 0; c < 3; c++) { int a = (c0 >> (16 - 8 * c)) & 255, b = (c1 >> (16 - 8 * c)) & 255; s[c] = (u8)clamp255(a + (b - a) * t); }
      s[3] = 255;
      blend_pixel(DEST(&o, x, y), s, 255, BM_DIFFERENCE);
    }
  return op_end(&o);
}
int flt_fibers(Doc *d, int variance, int strength, unsigned seed)
{
  Op o;
  i32 x, y;
  u8 a[3], b[3];
  double fy;
  variance = clampi(variance, 1, 64); strength = clampi(strength, 1, 64);
  /* noise about a pixel wide across, stretched down the fibres by the strength */
  fy = variance / (strength * 96.0);
  rgb_of(d->fg, a); rgb_of(d->bg, b);
  if (!op_begin(&o, d, "Fibers", 0)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      double v = 0.5 * vnoise(x * 0.7, y * fy, seed) + 0.3 * vnoise(x * 1.4, y * fy * 2, seed + 101) + 0.2 * vnoise(x * 0.15, y * fy * 0.5, seed + 202);
      u8 *p = DEST(&o, x, y);
      int c;
      v = (v - 0.5) * 1.8 + 0.5;
      if (v < 0) v = 0;
      if (v > 1) v = 1;
      for (c = 0; c < 3; c++) p[c] = (u8)clamp255(a[c] + (b[c] - a[c]) * v);
      p[3] = 255;
    }
  return op_end(&o);
}
/* light added with Screen; the alpha raised to at least the light's */
static void screen_add(u8 *p, double r, double g, double b)
{
  double l[3];
  int c, mx = 0;
  l[0] = r; l[1] = g; l[2] = b;
  for (c = 0; c < 3; c++) {
    int v = clamp255(l[c]);
    if (v > mx) mx = v;
    p[c] = (u8)(255 - (255 - p[c]) * (255 - v) / 255);
  }
  if (p[3] < mx) {
    /* where the layer was clear, the colour is the light's */
    if (p[3] == 0) for (c = 0; c < 3; c++) p[c] = (u8)clamp255(l[c] * 255.0 / (mx ? mx : 1));
    p[3] = (u8)mx;
  }
}
int flt_lens_flare(Doc *d, i32 fx, i32 fy, int brightness, int lens)
{
  /* each lens: the size of the glow and halo, then ghosts along the line through the middle as
     (distance along it, 1 the middle and 2 the mirror point; radius; strength; tint r, g, b) */
  static const double ghosts[4][5][6] = {
    { { 0.45, 0.020, 0.25, 0.6, 0.8, 1.0 }, { 0.75, 0.035, 0.18, 1.0, 0.7, 0.4 }, { 1.25, 0.012, 0.30, 0.5, 1.0, 0.6 }, { 1.55, 0.060, 0.12, 0.7, 0.6, 1.0 }, { 1.90, 0.030, 0.20, 1.0, 0.5, 0.4 } },
    { { 0.60, 0.030, 0.20, 1.0, 0.8, 0.5 }, { 1.30, 0.045, 0.15, 0.6, 0.8, 1.0 }, { 1.70, 0.020, 0.25, 1.0, 0.6, 0.6 }, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } },
    { { 0.50, 0.015, 0.25, 0.8, 1.0, 0.8 }, { 1.40, 0.025, 0.20, 1.0, 0.8, 0.5 }, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } },
    { { 0.30, 0.010, 0.30, 0.5, 0.7, 1.0 }, { 0.70, 0.015, 0.25, 0.5, 0.7, 1.0 }, { 1.30, 0.020, 0.20, 0.5, 0.7, 1.0 }, { 1.70, 0.040, 0.15, 0.5, 0.7, 1.0 }, { 0, 0, 0, 0, 0, 0 } } };
  static const double size[4] = { 1.0, 1.3, 0.7, 0.9 };
  Op o;
  i32 x, y;
  double S, br, cx, cy, r0, rh, wh, gx[5], gy[5], gr[5];
  int g;
  lens = clampi(lens, 0, 3);
  br = clampi(brightness, 10, 300) / 100.0;
  S = sqrt((double)d->w * d->w + (double)d->h * d->h) * size[lens];
  cx = d->w / 2.0; cy = d->h / 2.0;
  r0 = S * 0.015 < 1.5 ? 1.5 : S * 0.015; rh = S * 0.10; wh = S * 0.012;
  for (g = 0; g < 5; g++) {
    gx[g] = fx + (cx - fx) * ghosts[lens][g][0]; gy[g] = fy + (cy - fy) * ghosts[lens][g][0];
    gr[g] = S * ghosts[lens][g][1];
  }
  if (!op_begin(&o, d, "Lens Flare", 0)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      double dx = x + 0.5 - fx, dy = y + 0.5 - fy, r2 = dx * dx + dy * dy, r = sqrt(r2), glow, ring, L[3];
      int c;
      /* the hot centre, a soft glow round it, and a faint halo */
      glow = 1.0 / (1.0 + r2 / (r0 * r0)) + 0.35 / (1.0 + r2 / (16 * r0 * r0));
      ring = 1 - fabs(r - rh) / wh;
      ring = ring > 0 ? ring * ring * 0.25 : 0;
      L[0] = (glow + ring) * 255; L[1] = (glow * 0.94 + ring * 0.75) * 255; L[2] = (glow * 0.86 + ring * 0.6) * 255;
      for (g = 0; g < 5; g++) {
        double ex, ey, e, rr;
        if (gr[g] <= 0) continue;
        ex = x + 0.5 - gx[g]; ey = y + 0.5 - gy[g]; rr = ex * ex + ey * ey;
        if (rr >= gr[g] * gr[g] * 1.21) continue;
        e = sqrt(rr) / gr[g];
        e = e < 0.9 ? 1 : (1.1 - e) * 5;                     /* a disc with a soft rim */
        e *= ghosts[lens][g][2] * 255;
        for (c = 0; c < 3; c++) L[c] += e * ghosts[lens][g][3 + c];
      }
      screen_add(DEST(&o, x, y), L[0] * br, L[1] * br, L[2] * br);
    }
  return op_end(&o);
}
int flt_lighting_basic(Doc *d, i32 lx, i32 ly, int radius, int intensity, u32 color, int ambient)
{
  Op o;
  i32 x, y;
  u8 lc[3];
  double amb = (clampi(ambient, -100, 100) + 100) / 200.0, in = clampi(intensity, -100, 100) / 100.0 * 1.5, rr;
  if (radius < 1) radius = 1;
  rr = (double)radius * radius;
  rgb_of(color, lc);
  if (!op_begin(&o, d, "Lighting Effects", 0)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      double dx = x + 0.5 - lx, dy = y + 0.5 - ly, f = 1 - (dx * dx + dy * dy) / rr;
      u8 *p = DEST(&o, x, y);
      int c;
      if (f < 0) f = 0;
      /* the ambient light everywhere, plus the light's colour fading out to its radius */
      for (c = 0; c < 3; c++) {
        double gain = amb + in * f * f * lc[c] / 255.0;
        p[c] = (u8)clamp255(p[c] * (gain < 0 ? 0 : gain));
      }
    }
  return op_end(&o);
}

/* ------------------------------------------------------------------ distort */
static void m_topolar(const Op *o, const double *a, double x, double y, double *sx, double *sy)
{
  double nx = (x - a[0]) / a[2], ny = (y - a[1]) / a[3], r = sqrt(nx * nx + ny * ny), t = tatan2(nx, -ny);   /* clockwise from the top */
  *sx = o->b.x0 + t * (o->b.x1 - o->b.x0); *sy = o->b.y0 + r * (o->b.y1 - o->b.y0);
}
static void m_frompolar(const Op *o, const double *a, double x, double y, double *sx, double *sy)
{
  double t = (x - o->b.x0) / (o->b.x1 - o->b.x0), r = (y - o->b.y0) / (o->b.y1 - o->b.y0);
  *sx = a[0] + r * a[2] * tsin(t); *sy = a[1] - r * a[3] * tcos(t);
}
int flt_polar_coordinates(Doc *d, int to_polar)
{
  double a[4];
  Box b = d->sel ? box_clip(d->selbox, doc_box(d)) : doc_box(d);
  a[0] = (b.x0 + b.x1) / 2.0; a[1] = (b.y0 + b.y1) / 2.0; a[2] = (b.x1 - b.x0) / 2.0; a[3] = (b.y1 - b.y0) / 2.0;
  if (a[2] <= 0 || a[3] <= 0) return 0;
  return to_polar ? remap(d, "Polar Coordinates", m_topolar, a, 1, 1) : remap(d, "Polar Coordinates", m_frompolar, a, 1, 0);
}
int flt_shear(Doc *d, const int *pts, int npts, int wrap)
{
  Op o;
  i32 x, y;
  u8 lut[256];
  Box c;
  if (!pts || npts < 2) return 0;
  curve_lut(lut, pts, npts);
  if (!op_begin(&o, d, "Shear", 1)) return 0;
  c = o.b;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++) {
    i32 bh = o.b.y1 - o.b.y0, k = bh > 1 ? (y - o.b.y0) * 255 / (bh - 1) : 0;
    double off = (lut[k] - 128) / 128.0 * (o.b.x1 - o.b.x0) / 2.0;
    for (x = o.b.x0; x < o.b.x1; x++) sample_bl(&o, c, wrap, x - off, y, DEST(&o, x, y));
  }
  return op_end(&o);
}
static void m_zigzag(const Op *o, const double *a, double x, double y, double *sx, double *sy)
{
  double cx = (o->b.x0 + o->b.x1) / 2.0, cy = (o->b.y0 + o->b.y1) / 2.0, R = (o->b.x1 - o->b.x0 < o->b.y1 - o->b.y0 ? o->b.x1 - o->b.x0 : o->b.y1 - o->b.y0) / 2.0;
  double dx = x - cx, dy = y - cy, r = sqrt(dx * dx + dy * dy), s, f;
  *sx = x; *sy = y;
  if (r >= R || r <= 0 || R <= 0) return;
  s = tsin(r / R * a[1]) * (1 - r / R);                  /* the ridges, fading to the edge */
  if (a[2] != 1) {
    /* around the centre: turned back and forth */
    double an = a[0] / 100.0 * s * (a[2] == 2 ? 0.05 : 0.1), cs = tcos(an), sn = tsin(an);
    double nx = dx * cs - dy * sn, ny = dx * sn + dy * cs;
    dx = nx; dy = ny;
  }
  if (a[2] != 0) {
    /* out from the centre: pushed in and out */
    f = 1 + a[0] / 100.0 * s * (R / a[1]) * (a[2] == 2 ? 0.25 : 0.5) / r;
    dx *= f; dy *= f;
  }
  *sx = cx + dx; *sy = cy + dy;
}
int flt_zigzag(Doc *d, int amount, int ridges, int style)
{
  double a[3];
  a[0] = clampi(amount, -100, 100); a[1] = clampi(ridges, 1, 20); a[2] = clampi(style, 0, 2);
  return remap(d, "ZigZag", m_zigzag, a, 0, 0);
}
static void m_glass(const Op *o, const double *a, double x, double y, double *sx, double *sy)
{
  unsigned seed = (unsigned)a[2];
  if (!o) return;
  *sx = x + (vnoise(x * a[1], y * a[1], seed) - 0.5) * 2 * a[0] + (vnoise(x * a[1] * 3, y * a[1] * 3, seed + 2) - 0.5) * a[0] * 0.5;
  *sy = y + (vnoise(x * a[1], y * a[1], seed + 1) - 0.5) * 2 * a[0] + (vnoise(x * a[1] * 3, y * a[1] * 3, seed + 3) - 0.5) * a[0] * 0.5;
}
int flt_glass(Doc *d, int distortion, int smoothness, int scale, unsigned seed)
{
  double a[3];
  a[0] = clampi(distortion, 0, 20);
  a[1] = 1.0 / ((4 + 2 * clampi(smoothness, 1, 15)) * clampi(scale, 50, 200) / 100.0);
  a[2] = seed & 0xFFFF;
  return remap(d, "Glass", m_glass, a, 0, 0);
}
static void m_ocean(const Op *o, const double *a, double x, double y, double *sx, double *sy)
{
  unsigned seed = (unsigned)a[2];
  if (!o) return;
  *sx = x + a[0] * tsin(vnoise(x * a[1], y * a[1], seed) * 2.5);
  *sy = y + a[0] * tsin(vnoise(x * a[1], y * a[1], seed + 1) * 2.5);
}
int flt_ocean_ripple(Doc *d, int size, int magnitude, unsigned seed)
{
  double a[3];
  a[0] = clampi(magnitude, 0, 20) / 2.0;
  a[1] = 1.0 / (2 + 2 * clampi(size, 1, 15));
  a[2] = seed & 0xFFFF;
  return remap(d, "Ocean Ripple", m_ocean, a, 0, 0);
}

/* ------------------------------------------------------------------ noise */
int flt_dust_scratches(Doc *d, int r, int threshold)
{
  Op o;
  i32 x, y;
  int *hist;
  r = clampi(r, 1, 16);
  if (!(hist = (int *)malloc(3 * 256 * sizeof(int)))) return 0;
  if (!op_begin(&o, d, "Dust & Scratches", 1)) { free(hist); return 0; }
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++) {
    int c, i, j, cnt = 0;
    for (x = o.b.x0; x < o.b.x1; x++) {
      if (x == o.b.x0) {
        memset(hist, 0, 3 * 256 * sizeof(int)); cnt = 0;
        for (j = -r; j <= r; j++) for (i = -r; i <= r; i++) {
          const u8 *q = orig_at(&o, x + i, y + j);
          for (c = 0; c < 3; c++) hist[c * 256 + q[c]]++;
          cnt++;
        }
      } else
        for (j = -r; j <= r; j++) {
          const u8 *qo = orig_at(&o, x - r - 1, y + j), *qn = orig_at(&o, x + r, y + j);
          for (c = 0; c < 3; c++) { hist[c * 256 + qo[c]]--; hist[c * 256 + qn[c]]++; }
        }
      for (c = 0; c < 3; c++) {
        int target = (cnt + 1) / 2, acc = 0, v, old = ORIG(&o, x, y)[c];
        for (v = 0; v < 255; v++) { acc += hist[c * 256 + v]; if (acc >= target) break; }
        if (abs(old - v) > threshold) DEST(&o, x, y)[c] = (u8)v;
      }
    }
  }
  free(hist);
  return op_end(&o);
}
int flt_reduce_noise(Doc *d, int strength, int preserve, int color)
{
  Op o;
  i32 x, y, ew, eh, i, j;
  Box e;
  short *yc;
  int rc, k;
  strength = clampi(strength, 0, 10); preserve = clampi(preserve, 0, 100); color = clampi(color, 0, 100);
  k = strength * 6;
  rc = color ? 1 + color / 25 : 0;
  if (!op_begin(&o, d, "Reduce Noise", 1)) return 0;
  e = box_clip(box_make(o.b.x0 - 4, o.b.y0 - 4, o.b.x1 + 4, o.b.y1 + 4), doc_box(d));
  ew = e.x1 - e.x0; eh = e.y1 - e.y0;
  /* luminosity and two colour differences (Y, B - Y, R - Y), which go back to RGB exactly */
  if (!(yc = (short *)malloc((size_t)ew * eh * 3 * sizeof(short)))) { op_end(&o); return 0; }
  for (y = 0; y < eh; y++)
    for (x = 0; x < ew; x++) {
      const u8 *q = ORIG(&o, x + e.x0, y + e.y0);
      short *t = yc + ((long)y * ew + x) * 3;
      t[0] = (short)lum8(q); t[1] = (short)(q[2] - t[0]); t[2] = (short)(q[0] - t[0]);
    }
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      const short *t = yc + ((long)(y - e.y0) * ew + x - e.x0) * 3;
      long sy = 0, n = 0, scb = 0, scr = 0, nc = 0;
      int Y = t[0], Cb = t[1], Cr = t[2], r, g, b;
      u8 *p = DEST(&o, x, y);
      /* the luminosity: the average of the neighbours within k of it (a sigma filter) */
      for (j = -2; j <= 2; j++) for (i = -2; i <= 2; i++) {
        i32 xx = clampi(x + i, e.x0, e.x1 - 1), yy = clampi(y + j, e.y0, e.y1 - 1);
        int v = yc[((long)(yy - e.y0) * ew + xx - e.x0) * 3];
        if (abs(v - Y) <= k) { sy += v; n++; }
      }
      if (n) { int m = (int)((sy + n / 2) / n); Y = m + (Y - m) * preserve / 100; }
      /* the colour: blurred towards its neighbourhood's */
      if (rc) {
        for (j = -rc; j <= rc; j++) for (i = -rc; i <= rc; i++) {
          i32 xx = clampi(x + i, e.x0, e.x1 - 1), yy = clampi(y + j, e.y0, e.y1 - 1);
          const short *u = yc + ((long)(yy - e.y0) * ew + xx - e.x0) * 3;
          scb += u[1]; scr += u[2]; nc++;
        }
        Cb += (int)((scb / nc - Cb) * color / 100); Cr += (int)((scr / nc - Cr) * color / 100);
      }
      r = Y + Cr; b = Y + Cb; g = (Y * 256 - 77 * r - 29 * b + 75) / 150;
      p[0] = (u8)clampi(r, 0, 255); p[1] = (u8)clampi(g, 0, 255); p[2] = (u8)clampi(b, 0, 255);
    }
  free(yc);
  return op_end(&o);
}

/* ------------------------------------------------------------------ blur */
int flt_smart_blur(Doc *d, int r, int threshold, int mode)
{
  Op o;
  i32 x, y;
  r = clampi(r, 1, 20); threshold = clampi(threshold, 0, 255);
  if (!op_begin(&o, d, "Smart Blur", 1)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      const u8 *q0 = ORIG(&o, x, y);
      u8 *p = DEST(&o, x, y);
      int lc = lum8(q0), c, i, j, edge = 0;
      if (mode) {
        static const int nx[4] = { -1, 1, 0, 0 }, ny[4] = { 0, 0, -1, 1 };
        for (i = 0; i < 4 && !edge; i++) edge = abs(lum8(orig_at(&o, x + nx[i], y + ny[i])) - lc) > threshold;
      }
      if (mode == 1) { p[0] = p[1] = p[2] = (u8)(edge ? 255 : 0); continue; }
      if (mode == 2 && edge) { p[0] = p[1] = p[2] = 255; continue; }
      {
        long acc[4] = { 0, 0, 0, 0 }, n = 0;
        for (j = -r; j <= r; j++) for (i = -r; i <= r; i++) {
          const u8 *q = orig_at(&o, x + i, y + j);
          if (abs(lum8(q) - lc) > threshold) continue;
          for (c = 0; c < 3; c++) acc[c] += q[c] * q[3];
          acc[3] += q[3]; n++;
        }
        p[3] = (u8)((acc[3] + n / 2) / n);
        for (c = 0; c < 3; c++) p[c] = acc[3] ? (u8)((acc[c] + acc[3] / 2) / acc[3]) : q0[c];
      }
    }
  return op_end(&o);
}
int flt_surface_blur(Doc *d, int r, int threshold)
{
  Op o;
  i32 x, y;
  int t5;
  r = clampi(r, 1, 32); threshold = clampi(threshold, 1, 255);
  t5 = 5 * threshold;                                    /* twice 2.5 threshold, in integers */
  if (!op_begin(&o, d, "Surface Blur", 1)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      const u8 *q0 = ORIG(&o, x, y);
      u8 *p = DEST(&o, x, y);
      int c, i, j;
      for (c = 0; c < 3; c++) {
        long sv = 0, sw = 0;
        int v0 = q0[c];
        for (j = -r; j <= r; j++) for (i = -r; i <= r; i++) {
          int v = orig_at(&o, x + i, y + j)[c], w = t5 - 2 * abs(v - v0);
          if (w > 0) { sv += (long)w * v; sw += w; }
        }
        p[c] = (u8)((sv + sw / 2) / sw);
      }
    }
  return op_end(&o);
}
/* a blur through a shape whose every row is one run: row dy (-r..r) runs from lo[dy + r] to
   hi[dy + r] (empty if lo > hi). Each row is summed from running totals, so a pixel costs one step a
   row. Pixels at least thresh bright are made bright per cent brighter first */
static int span_blur(Doc *d, const char *name, int r, const int *lo, const int *hi, int bright, int thresh)
{
  Op o;
  i32 x, y, pw, i;
  int nr = 2 * r + 1, k, c;
  u32 *rows, total = 0;
  long *tag;
  for (k = 0; k < nr; k++) if (hi[k] >= lo[k]) total += (u32)(hi[k] - lo[k] + 1);
  if (!total || !op_begin(&o, d, name, 1)) return 0;
  pw = o.b.x1 - o.b.x0 + 2 * r;
  rows = (u32 *)malloc((size_t)nr * (pw + 1) * 4 * sizeof(u32));
  tag = (long *)malloc((size_t)nr * sizeof(long));
  if (!rows || !tag) { free(rows); free(tag); op_end(&o); return 0; }
  for (k = 0; k < nr; k++) tag[k] = -0x7FFFFFFL;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++) {
    /* the running totals of the rows the shape covers (kept in a ring as y moves down) */
    for (k = -r; k <= r; k++) {
      long ry = (long)y + k;
      int slot = (int)(((ry % nr) + nr) % nr);
      u32 *t = rows + (long)slot * (pw + 1) * 4, s[4] = { 0, 0, 0, 0 };
      if (tag[slot] == ry) continue;
      tag[slot] = ry;
      t[0] = t[1] = t[2] = t[3] = 0;
      for (i = 0; i < pw; i++) {
        const u8 *q = orig_at(&o, o.b.x0 - r + i, (i32)ry);
        int boost = bright > 0 && lum8(q) >= thresh ? 100 + bright : 100;
        for (c = 0; c < 3; c++) s[c] += (u32)(q[c] * q[3] / 255 * boost / 100);
        s[3] += q[3];
        for (c = 0; c < 4; c++) t[(i + 1) * 4 + c] = s[c];
      }
    }
    for (x = o.b.x0; x < o.b.x1; x++) {
      u32 acc[4] = { 0, 0, 0, 0 };
      u8 *p = DEST(&o, x, y);
      for (k = 0; k < nr; k++) {
        long ry = (long)y + k - r;
        const u32 *t = rows + (long)(((ry % nr) + nr) % nr) * (pw + 1) * 4;
        i32 a = x - o.b.x0 + r + lo[k], b = x - o.b.x0 + r + hi[k] + 1;
        if (hi[k] < lo[k]) continue;
        for (c = 0; c < 4; c++) acc[c] += t[b * 4 + c] - t[a * 4 + c];
      }
      p[3] = (u8)((acc[3] + total / 2) / total);
      for (c = 0; c < 3; c++) { u32 v = acc[3] ? (acc[c] * 255 + acc[3] / 2) / acc[3] : 0; p[c] = (u8)(v > 255 ? 255 : v); }
    }
  }
  free(rows); free(tag);
  return op_end(&o);
}
static int shape_runs(int r, int shape, int *lo, int *hi)
{
  int dy;
  for (dy = -r; dy <= r; dy++) {
    int ady = dy < 0 ? -dy : dy, h;
    switch (shape) {
    case 0: h = r; break;
    case 1: h = r - ady; break;
    case 2: h = r - ady / 2; break;
    case 3: h = (dy + r) / 2; break;
    default: h = (int)floor(sqrt((r + 0.5) * (r + 0.5) - (double)dy * dy)); break;
    }
    lo[dy + r] = -h; hi[dy + r] = h;
  }
  return 1;
}
int flt_lens_blur(Doc *d, int radius, int brightness, int threshold)
{
  int *lo, *hi, ok;
  radius = clampi(radius, 1, 64);
  lo = (int *)malloc((size_t)(2 * radius + 1) * 2 * sizeof(int));
  if (!lo) return 0;
  hi = lo + 2 * radius + 1;
  shape_runs(radius, 4, lo, hi);
  ok = span_blur(d, "Lens Blur", radius, lo, hi, clampi(brightness, 0, 100), clampi(threshold, 0, 255));
  free(lo);
  return ok;
}
int flt_shape_blur(Doc *d, int radius, int shape)
{
  int *lo, *hi, ok;
  radius = clampi(radius, 1, 64);
  lo = (int *)malloc((size_t)(2 * radius + 1) * 2 * sizeof(int));
  if (!lo) return 0;
  hi = lo + 2 * radius + 1;
  shape_runs(radius, clampi(shape, 0, 4), lo, hi);
  ok = span_blur(d, "Shape Blur", radius, lo, hi, 0, 256);
  free(lo);
  return ok;
}
int flt_spin_blur(Doc *d, i32 cx, i32 cy, int angle)
{
  Op o;
  i32 x, y;
  double turns = clampi(angle, 1, 360) / 360.0;
  if (!op_begin(&o, d, "Spin Blur", 1)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      double dx = x + 0.5 - cx, dy = y + 0.5 - cy, rad = sqrt(dx * dx + dy * dy), cs, sn, step_c, step_s, vx, vy;
      long acc[4] = { 0, 0, 0, 0 };
      int n = (int)(rad * turns * 6.2831853) + 1, i, c;
      u8 *p = DEST(&o, x, y);
      if (n < 2) continue;
      if (n > 64) n = 64;
      /* samples along the arc: the first at half the angle back, then turned a step at a time */
      cs = tcos(-turns / 2); sn = tsin(-turns / 2);
      vx = dx * cs - dy * sn; vy = dx * sn + dy * cs;
      step_c = tcos(turns / (n - 1)); step_s = tsin(turns / (n - 1));
      for (i = 0; i < n; i++) {
        const u8 *q = orig_at(&o, (i32)floor(cx + vx), (i32)floor(cy + vy));
        double t = vx * step_c - vy * step_s;
        for (c = 0; c < 3; c++) acc[c] += q[c] * q[3];
        acc[3] += q[3];
        vy = vx * step_s + vy * step_c; vx = t;
      }
      p[3] = (u8)((acc[3] + n / 2) / n);
      for (c = 0; c < 3; c++) p[c] = acc[3] ? (u8)((acc[c] + acc[3] / 2) / acc[3]) : 0;
    }
  return op_end(&o);
}

/* ------------------------------------------------------------------ pixelate */
/* the seed point of grid cell gx, gy (cell pixels square), somewhere in the cell */
static void cell_point(i32 gx, i32 gy, int cell, unsigned seed, i32 *px, i32 *py)
{
  u32 h = hash3((u32)gx, (u32)gy, seed);
  *px = gx * cell + (i32)(h % (u32)cell); *py = gy * cell + (i32)((h >> 16) % (u32)cell);
}
/* the nearest seed point to x, y (from the 3 x 3 cells about it); its squared distance back */
static long nearest_point(i32 x, i32 y, int cell, unsigned seed, i32 *bx, i32 *by)
{
  i32 gx0 = x / cell, gy0 = y / cell, gx, gy;
  long best = -1;
  for (gy = gy0 - 1; gy <= gy0 + 1; gy++)
    for (gx = gx0 - 1; gx <= gx0 + 1; gx++) {
      i32 px, py;
      long dd;
      cell_point(gx, gy, cell, seed, &px, &py);
      dd = (long)(px - x) * (px - x) + (long)(py - y) * (py - y);
      if (best < 0 || dd < best) { best = dd; *bx = px; *by = py; }
    }
  return best;
}
int flt_crystallize(Doc *d, int cell, unsigned seed)
{
  Op o;
  i32 x, y;
  cell = clampi(cell, 3, 300);
  if (!op_begin(&o, d, "Crystallize", 1)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      i32 px = 0, py = 0;
      nearest_point(x, y, cell, seed, &px, &py);
      memcpy(DEST(&o, x, y), orig_at(&o, px, py), 4);
    }
  return op_end(&o);
}
int flt_pointillize(Doc *d, int cell, unsigned seed)
{
  Op o;
  i32 x, y;
  long rr;
  u8 bg[4];
  cell = clampi(cell, 3, 300);
  rr = (long)cell * cell * 30 / 100;                     /* dots about 0.55 of a cell across in radius */
  rgb_of(d->bg, bg); bg[3] = 255;
  if (!op_begin(&o, d, "Pointillize", 1)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      i32 px = 0, py = 0;
      long dd = nearest_point(x, y, cell, seed, &px, &py);
      memcpy(DEST(&o, x, y), dd <= rr ? orig_at(&o, px, py) : bg, 4);
    }
  return op_end(&o);
}
int flt_color_halftone(Doc *d, int max_radius, int angle_r, int angle_g, int angle_b)
{
  Op o;
  i32 x, y;
  int ang[3], c;
  double cs[3], sn[3], g;
  max_radius = clampi(max_radius, 4, 127);
  g = 2.0 * max_radius;
  ang[0] = angle_r; ang[1] = angle_g; ang[2] = angle_b;
  for (c = 0; c < 3; c++) { cs[c] = tcos(ang[c] / 360.0); sn[c] = tsin(ang[c] / 360.0); }
  if (!op_begin(&o, d, "Color Halftone", 1)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      u8 *p = DEST(&o, x, y);
      double X = x + 0.5, Y = y + 0.5;
      for (c = 0; c < 3; c++) {
        /* into the screen's rotated grid; the dots of the cell and its neighbours, sized by the ink
           (255 less the channel) at their centres; the most coverage wins */
        double u = X * cs[c] + Y * sn[c], v = -X * sn[c] + Y * cs[c], best = 0;
        i32 gu = (i32)floor(u / g), gv = (i32)floor(v / g), i, j;
        for (j = gv - 1; j <= gv + 1; j++) for (i = gu - 1; i <= gu + 1; i++) {
          double uc = (i + 0.5) * g, vc = (j + 0.5) * g, ink, rr, dist, cov;
          ink = 255 - orig_at(&o, (i32)floor(uc * cs[c] - vc * sn[c]), (i32)floor(uc * sn[c] + vc * cs[c]))[c];
          if (ink <= 0) continue;
          /* the dot's area the ink's share of the cell until dots touch, then on to the corners */
          rr = ink <= 200.3 ? g * sqrt(ink / 255.0 / 3.14159265) : 0.5 * g + (0.2072 * g + 0.5) * (ink - 200.3) / 54.7;
          dist = sqrt((u - uc) * (u - uc) + (v - vc) * (v - vc));
          cov = rr - dist + 0.5;
          if (cov > 2 * rr) cov = 2 * rr;                    /* (a dot smaller than a pixel covers less) */
          if (cov > best) best = cov;
        }
        if (best > 1) best = 1;
        p[c] = (u8)clamp255(255 * (1 - best));
      }
    }
  return op_end(&o);
}
int flt_fragment(Doc *d)
{
  static const int off[4][2] = { { -4, -4 }, { 4, -4 }, { -4, 4 }, { 4, 4 } };
  Op o;
  i32 x, y;
  if (!op_begin(&o, d, "Fragment", 1)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      long acc[4] = { 0, 0, 0, 0 };
      int k, c;
      u8 *p = DEST(&o, x, y);
      for (k = 0; k < 4; k++) {
        const u8 *q = orig_at(&o, x + off[k][0], y + off[k][1]);
        for (c = 0; c < 3; c++) acc[c] += q[c] * q[3];
        acc[3] += q[3];
      }
      p[3] = (u8)((acc[3] + 2) / 4);
      for (c = 0; c < 3; c++) p[c] = acc[3] ? (u8)((acc[c] + acc[3] / 2) / acc[3]) : 0;
    }
  return op_end(&o);
}
int flt_mezzotint(Doc *d, int type, unsigned seed)
{
  static const int len[3] = { 4, 12, 32 };
  Op o;
  i32 x, y;
  type = clampi(type, 0, 9);
  if (!op_begin(&o, d, "Mezzotint", 0)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      u8 *p = DEST(&o, x, y);
      int c;
      for (c = 0; c < 3; c++) {
        u32 s = seed * 3 + (u32)c;
        int n;
        if (type == 0) n = (int)(hash3((u32)x, (u32)y, s) & 255);
        else if (type == 1) n = (int)(hash3((u32)(x >> 1), (u32)(y >> 1), s) & 255);
        else if (type == 2) n = (int)(((hash3((u32)x, (u32)y, s) & 255) + (hash3((u32)x, (u32)y, s + 77) & 255) + 1) >> 1);
        else if (type == 3) n = (int)(hash3((u32)(x / 3), (u32)(y / 3), s) & 255);
        else if (type <= 6) {
          /* horizontal runs, each row's breaks somewhere different */
          int L = len[type - 4];
          i32 run = (x + (i32)(hash3((u32)y, 0, s) % (u32)L)) / L;
          n = (int)(hash3((u32)run, (u32)y, s + 1) & 255);
        } else {
          /* diagonal strokes */
          int L = len[type - 7];
          i32 across = x - y, run = (x + (i32)(hash3((u32)across, 1, s) % (u32)L)) / L;
          n = (int)(hash3((u32)run, (u32)across, s + 2) & 255);
        }
        p[c] = (u8)(p[c] > n ? 255 : 0);
      }
    }
  return op_end(&o);
}
int flt_facet(Doc *d)
{
  /* the Kuwahara filter: of the four 3 x 3 squares meeting at the pixel, the one of least
     variance in brightness gives its average colour, so areas flatten and edges stay */
  Op o;
  i32 x, y;
  if (!op_begin(&o, d, "Facet", 1)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      long bestv = -1, best[4] = { 0, 0, 0, 0 };
      int qd, c;
      u8 *p = DEST(&o, x, y);
      for (qd = 0; qd < 4; qd++) {
        i32 ox = qd & 1 ? 0 : -2, oy = qd & 2 ? 0 : -2, i, j;
        long s = 0, s2 = 0, var, acc[4] = { 0, 0, 0, 0 };
        for (j = 0; j < 3; j++) for (i = 0; i < 3; i++) {
          const u8 *q = orig_at(&o, x + ox + i, y + oy + j);
          int l = lum8(q);
          s += l; s2 += l * l;
          for (c = 0; c < 4; c++) acc[c] += q[c];
        }
        var = 9 * s2 - s * s;
        if (bestv < 0 || var < bestv) { bestv = var; memcpy(best, acc, sizeof best); }
      }
      for (c = 0; c < 4; c++) p[c] = (u8)((best[c] + 4) / 9);
    }
  return op_end(&o);
}

/* ------------------------------------------------------------------ stylize */
int flt_trace_contour(Doc *d, int level, int upper)
{
  Op o;
  i32 x, y;
  level = clampi(level, 0, 255);
  if (!op_begin(&o, d, "Trace Contour", 1)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      const u8 *q = ORIG(&o, x, y), *rt = x + 1 < d->w ? ORIG(&o, x + 1, y) : q, *dn = y + 1 < d->h ? ORIG(&o, x, y + 1) : q;
      u8 *p = DEST(&o, x, y);
      int c;
      for (c = 0; c < 3; c++) {
        int on = upper ? q[c] > level && (rt[c] <= level || dn[c] <= level) : q[c] < level && (rt[c] >= level || dn[c] >= level);
        p[c] = (u8)(on ? 0 : 255);
      }
    }
  return op_end(&o);
}
int flt_diffuse(Doc *d, int mode, unsigned seed)
{
  Op o;
  i32 x, y;
  mode = clampi(mode, 0, 3);
  if (!op_begin(&o, d, "Diffuse", 1)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      const u8 *q0 = ORIG(&o, x, y), *q;
      u8 *p = DEST(&o, x, y);
      if (mode == 3) {
        /* anisotropic: from the neighbour that differs least */
        int best = 1 << 30, i, j;
        q = q0;
        for (j = -1; j <= 1; j++) for (i = -1; i <= 1; i++) {
          const u8 *n = orig_at(&o, x + i, y + j);
          int dd = abs(n[0] - q0[0]) + abs(n[1] - q0[1]) + abs(n[2] - q0[2]) + (int)(hash3((u32)x * 9 + (u32)(i + 1) * 3 + (u32)(j + 1), (u32)y, seed) & 3);
          if ((i || j) && dd < best) { best = dd; q = n; }
        }
      } else {
        u32 h = hash3((u32)x, (u32)y, seed) % 9;
        q = orig_at(&o, x + (i32)(h % 3) - 1, y + (i32)(h / 3) - 1);
        if (mode == 1 && lum8(q) >= lum8(q0)) q = q0;
        if (mode == 2 && lum8(q) <= lum8(q0)) q = q0;
      }
      memcpy(p, q, 4);
    }
  return op_end(&o);
}
int flt_tiles(Doc *d, int ntiles, int max_offset, int fill, unsigned seed)
{
  Op o;
  i32 x, y, ts, tx, ty, bw, bh, mo;
  u8 col[4];
  ntiles = clampi(ntiles, 1, 99); max_offset = clampi(max_offset, 1, 99); fill = clampi(fill, 0, 3);
  if (!op_begin(&o, d, "Tiles", 1)) return 0;
  bw = o.b.x1 - o.b.x0; bh = o.b.y1 - o.b.y0;
  ts = (bw < bh ? bw : bh) / ntiles;
  if (ts < 1) ts = 1;
  mo = ts * max_offset / 100;
  rgb_of(fill == 1 ? d->fg : d->bg, col); col[3] = 255;
  /* the gaps first */
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      u8 *p = DEST(&o, x, y);
      if (fill <= 1) memcpy(p, col, 4);
      else if (fill == 2) { const u8 *q = ORIG(&o, x, y); p[0] = (u8)(255 - q[0]); p[1] = (u8)(255 - q[1]); p[2] = (u8)(255 - q[2]); p[3] = q[3]; }
      else memcpy(p, ORIG(&o, x, y), 4);
    }
  /* then each tile, moved a random way */
  for (ty = 0; ty * ts < bh; ty++)
    for (tx = 0; tx * ts < bw; tx++) {
      u32 h = hash3((u32)tx, (u32)ty, seed);
      i32 ox = mo ? (i32)(h % (u32)(2 * mo + 1)) - mo : 0, oy = mo ? (i32)((h >> 12) % (u32)(2 * mo + 1)) - mo : 0, i, j;
      for (j = 0; j < ts; j++) for (i = 0; i < ts; i++) {
        i32 sx = o.b.x0 + tx * ts + i, sy = o.b.y0 + ty * ts + j, dx = sx + ox, dy = sy + oy;
        if (sx >= o.b.x1 || sy >= o.b.y1 || dx < o.b.x0 || dy < o.b.y0 || dx >= o.b.x1 || dy >= o.b.y1) continue;
        memcpy(DEST(&o, dx, dy), ORIG(&o, sx, sy), 4);
      }
    }
  return op_end(&o);
}
int flt_wind(Doc *d, int method, int from_left, unsigned seed)
{
  static const int lmin[3] = { 4, 16, 4 }, lmax[3] = { 16, 48, 24 };
  Op o;
  i32 y, n;
  method = clampi(method, 0, 2);
  if (!op_begin(&o, d, method == 1 ? "Wind (Blast)" : method == 2 ? "Wind (Stagger)" : "Wind", 1)) return 0;
  n = o.b.x1 - o.b.x0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++) {
    i32 k;
    int left = 0, L = 1, sc[3] = { 0, 0, 0 }, sl = 0;
    /* along the row the way the wind blows, carrying a streak: a pixel at least as bright as the
       streak picks it up afresh (its colour, a random length), so bright areas trail off downwind,
       fading (or, staggered, at half strength in ragged lengths) over darker pixels */
    for (k = 0; k < n; k++) {
      i32 x = from_left ? o.b.x0 + k : o.b.x1 - 1 - k;
      const u8 *q = ORIG(&o, x, y);
      u8 *p = DEST(&o, x, y);
      int l = lum8(q), c;
      if (left > 0 && sl > l) {
        int w = method == 2 ? 128 : left * 256 / L;
        for (c = 0; c < 3; c++) p[c] = (u8)(q[c] + (sc[c] - q[c]) * w / 256);
        left--;
        continue;
      }
      {
        u32 h = hash3((u32)x, (u32)y, seed);
        L = left = lmin[method] + (int)(h % (u32)(lmax[method] - lmin[method] + 1));
        if (method == 2 && (h >> 20 & 3) == 0) left = 0;
        for (c = 0; c < 3; c++) sc[c] = q[c];
        sl = l;
      }
    }
  }
  return op_end(&o);
}

/* ------------------------------------------------------------------ video and other */
int flt_de_interlace(Doc *d, int eliminate_even, int interpolate)
{
  Op o;
  i32 x, y;
  if (!op_begin(&o, d, "De-Interlace", 1)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++) {
    int up = y > 0, dn = y + 1 < d->h;
    if ((y & 1) == (eliminate_even ? 1 : 0) || (!up && !dn)) continue;
    for (x = o.b.x0; x < o.b.x1; x++) {
      u8 *p = DEST(&o, x, y);
      if (!interpolate || !up || !dn) memcpy(p, ORIG(&o, x, up ? y - 1 : y + 1), 4);
      else {
        const u8 *a = ORIG(&o, x, y - 1), *b = ORIG(&o, x, y + 1);
        int at = a[3] + b[3], c;
        p[3] = (u8)((at + 1) / 2);
        for (c = 0; c < 3; c++) p[c] = at ? (u8)((a[c] * a[3] + b[c] * b[3] + at / 2) / at) : 0;
      }
    }
  }
  return op_end(&o);
}
int flt_ntsc_colors(Doc *d, int pal)
{
  Op o;
  i32 x, y;
  double hi = pal ? 4.0 / 3 : 1.2, lo = pal ? -1.0 / 3 : -0.2;
  if (!op_begin(&o, d, pal ? "PAL Colors" : "NTSC Colors", 0)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      u8 *p = DEST(&o, x, y);
      double r = p[0] / 255.0, g = p[1] / 255.0, b = p[2] / 255.0, Y = 0.299 * r + 0.587 * g + 0.114 * b, u, v, C, k = 1;
      int c;
      if (pal) { u = 0.492 * (b - Y); v = 0.877 * (r - Y); }
      else { u = 0.596 * r - 0.274 * g - 0.322 * b; v = 0.211 * r - 0.523 * g + 0.312 * b; }
      C = sqrt(u * u + v * v);
      if (C <= 1e-9) continue;
      if (Y + C > hi) k = (hi - Y) / C;
      if (Y - C < lo && (Y - lo) / C < k) k = (Y - lo) / C;
      if (k >= 1) continue;
      if (k < 0) k = 0;
      /* chroma is linear in the differences from Y, so scaling them scales it; hue and Y kept.
         Rounded towards Y, so the limit holds after rounding */
      for (c = 0; c < 3; c++) {
        double t = Y * 255 + (p[c] - Y * 255) * k;
        int vv = p[c] >= Y * 255 ? (int)ceil(t - 0.5 - 1e-9) : (int)floor(t + 0.5 + 1e-9);
        if (vv > 255) vv = 255;
        if (vv < 0) vv = 0;
        p[c] = (u8)vv;
      }
    }
  return op_end(&o);
}
int flt_custom(Doc *d, const int kernel[25], int scale, int offset)
{
  Op o;
  i32 x, y;
  if (!kernel) return 0;
  if (scale == 0) scale = 1;
  if (!op_begin(&o, d, "Custom", 1)) return 0;
  for (y = o.b.y0; y < o.b.y1 && !op_tick(&o, y); y++)
    for (x = o.b.x0; x < o.b.x1; x++) {
      long s[3] = { 0, 0, 0 };
      int i, j, c;
      u8 *p = DEST(&o, x, y);
      for (j = -2; j <= 2; j++) for (i = -2; i <= 2; i++) {
        int kv = kernel[(j + 2) * 5 + i + 2];
        const u8 *q;
        if (!kv) continue;
        q = orig_at(&o, x + i, y + j);
        for (c = 0; c < 3; c++) s[c] += (long)q[c] * kv;
      }
      for (c = 0; c < 3; c++) {
        /* divided rounding to nearest (either sign) */
        long v = s[c], den = scale;
        if (den < 0) { v = -v; den = -den; }
        v = v >= 0 ? (v + den / 2) / den : -((-v + den / 2) / den);
        p[c] = (u8)clampi((int)(v + offset), 0, 255);
      }
    }
  return op_end(&o);
}
