/*
 * quicktest.c - native tests of Easel 0.2.1's quick wins: the new blend modes, the filters in
 * filters2.c, and Reselect, Grow and Similar.
 * cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. core/util.c core/doc.c core/paint.c core/adjust.c core/filters2.c core/selmore.c tests/quicktest.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../core/doc.h"
#include "../core/paint.h"
#include "../core/adjust.h"
#include "../core/filters2.h"
#include "../core/selmore.h"

static int tests, failed;
static void ok(const char *name, int cond) { tests++; if (!cond) { failed++; printf("FAIL %s\n", name); } }
static u8 *px(Doc *d, i32 x, i32 y) { return d->lay[d->active]->px + ((long)y * d->w + x) * 4; }
static unsigned long rs = 1;
static int rnd(int n) { rs = rs * 1103515245UL + 12345UL; return (int)((rs >> 8) % (unsigned long)n); }
static int lum(const u8 *p) { return (p[0] * 77 + p[1] * 150 + p[2] * 29 + 128) >> 8; }

/* documents to work on */
static Doc *flat(i32 w, i32 h, u32 c) { return doc_new(w, h, c, 0); }
static Doc *noisy(i32 w, i32 h, int alpha)
{
  Doc *d = doc_new(w, h, 0, 0);
  long k;
  for (k = 0; k < (long)w * h * 4; k++) d->lay[0]->px[k] = (u8)rnd(256);
  if (!alpha) for (k = 0; k < (long)w * h; k++) d->lay[0]->px[k * 4 + 3] = 255;
  return d;
}
static Doc *smooth(i32 w, i32 h)
{
  Doc *d = doc_new(w, h, 0, 0);
  i32 x, y;
  for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
    u8 *p = px(d, x, y);
    p[0] = (u8)(128 + 100 * sin(x / 9.0)); p[1] = (u8)(128 + 100 * cos(y / 7.0)); p[2] = (u8)((x * 3 + y * 2) & 255); p[3] = 255;
  }
  return d;
}
static u8 *copy_px(Doc *d) { u8 *c = (u8 *)malloc((size_t)d->w * d->h * 4); memcpy(c, d->lay[0]->px, (size_t)d->w * d->h * 4); return c; }
static int same_px(Doc *d, const u8 *c) { return !memcmp(d->lay[0]->px, c, (size_t)d->w * d->h * 4); }
static int is_flat(Doc *d, u32 c, int tol)
{
  long k;
  for (k = 0; k < (long)d->w * d->h; k++) {
    const u8 *p = d->lay[0]->px + k * 4;
    if (abs(p[0] - (int)(c >> 16 & 255)) > tol || abs(p[1] - (int)(c >> 8 & 255)) > tol || abs(p[2] - (int)(c & 255)) > tol || p[3] != 255) return 0;
  }
  return 1;
}

/* ------------------------------------------------------------------ blend modes */
static double ref_mode(int mode, double b, double s)
{
  double v;
  switch (mode) {
  case BM_LINEARBURN: v = b + s - 1; return v < 0 ? 0 : v;
  case BM_VIVIDLIGHT:
    if (s < 128 / 255.0) { if (b >= 1) return 1; if (s <= 0) return 0; v = 1 - (1 - b) / (2 * s); return v < 0 ? 0 : v; }
    if (b <= 0) return 0;
    if (s >= 1) return 1;
    v = b / (2 * (1 - s)); return v > 1 ? 1 : v;
  case BM_LINEARLIGHT: v = b + 2 * s - 1; return v < 0 ? 0 : v > 1 ? 1 : v;
  case BM_PINLIGHT: return s < 128 / 255.0 ? (b < 2 * s ? b : 2 * s) : (b > 2 * s - 1 ? b : 2 * s - 1);
  case BM_HARDMIX: return b + s >= 1 - 1e-9 ? 1 : 0;
  case BM_DIVIDE: if (s <= 0) return b <= 0 ? 0 : 1; v = b / s; return v > 1 ? 1 : v;
  }
  return s;
}
static void blend_modes(void)
{
  static const int sep[6] = { BM_LINEARBURN, BM_VIVIDLIGHT, BM_LINEARLIGHT, BM_PINLIGHT, BM_HARDMIX, BM_DIVIDE };
  static const int pts[12] = { 0, 1, 37, 64, 100, 127, 128, 129, 180, 200, 254, 255 };
  int m, i, j, bad, k;
  char name[80];
  for (m = 0; m < 6; m++) {
    bad = 0;
    for (i = 0; i < 12 + 200; i++) for (j = 0; j < 12; j++) {
      int cb = i < 12 ? pts[i] : rnd(256), cs = pts[j], want;
      u8 dst[4], src[4];
      if (i >= 12) cs = rnd(256);
      dst[0] = (u8)cb; dst[1] = (u8)(255 - cb); dst[2] = (u8)cb; dst[3] = 255;
      src[0] = (u8)cs; src[1] = (u8)cs; src[2] = (u8)(255 - cs); src[3] = 255;
      blend_pixel(dst, src, 255, sep[m]);
      want = (int)floor(ref_mode(sep[m], cb / 255.0, cs / 255.0) * 255 + 0.5);
      if (abs(dst[0] - want) > 1 || dst[3] != 255) bad++;
      want = (int)floor(ref_mode(sep[m], (255 - cb) / 255.0, cs / 255.0) * 255 + 0.5);
      if (abs(dst[1] - want) > 1) bad++;
      want = (int)floor(ref_mode(sep[m], cb / 255.0, (255 - cs) / 255.0) * 255 + 0.5);
      if (abs(dst[2] - want) > 1) bad++;
    }
    sprintf(name, "blend: %s matches the reference formula", blend_names[sep[m]]);
    ok(name, bad == 0);
  }
  /* a few exact values */
  {
    u8 d1[4] = { 200, 100, 50, 255 }, s1[4] = { 100, 100, 100, 255 };
    blend_pixel(d1, s1, 255, BM_LINEARBURN);
    ok("blend: Linear Burn 200+100-255", d1[0] == 45 && d1[1] == 0 && d1[2] == 0);
  }
  {
    u8 d1[4] = { 100, 200, 0, 255 }, s1[4] = { 50, 100, 0, 255 };
    blend_pixel(d1, s1, 255, BM_DIVIDE);
    ok("blend: Divide 100/50 clips, 200/100 clips, 0/0 is 0", d1[0] == 255 && d1[1] == 255 && d1[2] == 0);
  }
  {
    u8 d1[4] = { 100, 100, 100, 255 }, s1[4] = { 50, 255, 200, 255 };
    blend_pixel(d1, s1, 255, BM_HARDMIX);
    ok("blend: Hard Mix thresholds the sum", d1[0] == 0 && d1[1] == 255 && d1[2] == 255);
  }
  {
    u8 b1[4] = { 200, 30, 30, 255 }, s1[4] = { 20, 160, 20, 255 }, t[4];
    memcpy(t, b1, 4); blend_pixel(t, s1, 255, BM_LIGHTERCOLOR);
    ok("blend: Lighter Color takes the lighter whole colour", t[0] == 20 && t[1] == 160 && t[2] == 20);
    memcpy(t, b1, 4); blend_pixel(t, s1, 255, BM_DARKERCOLOR);
    ok("blend: Darker Color keeps the backdrop when it is darker", t[0] == 200 && t[1] == 30 && t[2] == 30);
  }
  /* partial alpha: the usual compositing of the blend */
  {
    u8 dst[4] = { 180, 90, 30, 200 }, src[4] = { 60, 160, 240, 255 };
    int as = 128, ab = 200, ao, c;
    double asf = as / 255.0, abf = ab / 255.0, aof;
    blend_pixel(dst, src, 128, BM_LINEARLIGHT);
    aof = asf + abf - asf * abf; ao = (int)floor(aof * 255 + 0.5);
    bad = abs(dst[3] - ao) > 1;
    {
      u8 b0[3] = { 180, 90, 30 }, s0[3] = { 60, 160, 240 };
      for (c = 0; c < 3; c++) {
        double B = ref_mode(BM_LINEARLIGHT, b0[c] / 255.0, s0[c] / 255.0);
        double v = (asf * (1 - abf) * s0[c] / 255.0 + asf * abf * B + (1 - asf) * abf * b0[c] / 255.0) / aof;
        if (abs(dst[c] - (int)floor(v * 255 + 0.5)) > 2) bad = 1;
      }
    }
    ok("blend: partial alpha composites like the other modes", !bad);
  }
  /* Dissolve */
  {
    int n0 = 0, n1 = 0, subset = 1, exact = 1, x, y, again = 1;
    for (y = 0; y < 64; y++) for (x = 0; x < 64; x++) {
      u8 a[4] = { 10, 20, 30, 255 }, b[4] = { 10, 20, 30, 255 }, c[4] = { 10, 20, 30, 255 }, s[4] = { 200, 100, 50, 255 };
      blend_pixel_at(a, s, 100, BM_DISSOLVE, x, y);
      blend_pixel_at(b, s, 200, BM_DISSOLVE, x, y);
      blend_pixel_at(c, s, 100, BM_DISSOLVE, x, y);
      if (a[0] == 200) n0++;
      if (b[0] == 200) n1++;
      if (a[0] == 200 && b[0] != 200) subset = 0;
      if ((a[0] != 200 && a[0] != 10) || (b[0] != 200 && b[0] != 10)) exact = 0;
      if (memcmp(a, c, 4)) again = 0;
    }
    ok("dissolve: about the opacity's share of pixels", n0 > 4096 * 100 / 255 - 200 && n0 < 4096 * 100 / 255 + 200 && n1 > 4096 * 200 / 255 - 200 && n1 < 4096 * 200 / 255 + 200);
    ok("dissolve: each pixel all or nothing", exact);
    ok("dissolve: more opacity only adds pixels", subset);
    ok("dissolve: the same each time", again);
    {
      u8 a[4] = { 10, 20, 30, 0 }, s[4] = { 200, 100, 50, 255 }, b[4] = { 1, 2, 3, 255 };
      for (x = 0; x < 16; x++) blend_pixel_at(a, s, 255, BM_DISSOLVE, x, 3);
      blend_pixel_at(b, s, 0, BM_DISSOLVE, 5, 5);
      ok("dissolve: full opacity covers, none leaves", a[0] == 200 && a[3] == 255 && b[0] == 1);
      memcpy(a, b, 4);
      blend_pixel(a, s, 128, BM_DISSOLVE);
      blend_pixel(b, s, 128, BM_NORMAL);
      ok("dissolve: blend_pixel without a position is Normal", !memcmp(a, b, 4));
    }
  }
  /* other modes unchanged through blend_pixel_at */
  {
    u8 a[4] = { 10, 200, 30, 180 }, b[4] = { 10, 200, 30, 180 }, s[4] = { 90, 50, 250, 140 };
    blend_pixel(a, s, 170, BM_SCREEN); blend_pixel_at(b, s, 170, BM_SCREEN, 3, 4);
    ok("blend_pixel_at: other modes as blend_pixel", !memcmp(a, b, 4));
  }
  /* random: every mode, alpha out as the union */
  bad = 0;
  for (k = 0; k < 200000; k++) {
    u8 dst[4], src[4];
    int mode = rnd(BM_N + 1), op = rnd(256), c, as, ab, ao;
    if (mode == BM_N) mode = BM_PASS;
    for (c = 0; c < 4; c++) { dst[c] = (u8)rnd(256); src[c] = (u8)rnd(256); }
    if (rnd(4) == 0) dst[3] = 0;
    if (rnd(4) == 0) src[3] = 255;
    ab = dst[3]; as = (src[3] * op + 127) / 255;
    if (mode == BM_DISSOLVE) { blend_pixel_at(dst, src, op, mode, rnd(1000) - 500, rnd(1000)); continue; }
    blend_pixel(dst, src, op, mode);
    ao = as == 0 ? ab : (int)floor((as / 255.0 + ab / 255.0 - as * ab / 65025.0) * 255 + 0.5);
    if (abs(dst[3] - ao) > 1) bad++;
  }
  ok("blend: random runs keep alpha the union", bad == 0);
}

/* ------------------------------------------------------------------ every filter, for the generic checks */
typedef int (*Flt)(Doc *d, int variant);
static unsigned SEED = 7;
static int f_dclouds(Doc *d, int v) { return flt_difference_clouds(d, v ? 0x102030 : 0, 0xFFFFFF, SEED); }
static int f_fibers(Doc *d, int v) { return flt_fibers(d, 1 + v * 7 % 64, 1 + v * 13 % 64, SEED); }
static int f_flare(Doc *d, int v) { return flt_lens_flare(d, d->w / 3, d->h / 4, 10 + v * 37 % 300, v % 4); }
static int f_light(Doc *d, int v) { return flt_lighting_basic(d, d->w / 2, d->h / 2, 1 + v * 5, v * 23 % 201 - 100, 0xFFE0C0, v * 17 % 201 - 100); }
static int f_polar(Doc *d, int v) { return flt_polar_coordinates(d, v & 1); }
static int f_shear(Doc *d, int v) { int p[6] = { 0, 128, 128, 0, 255, 255 }; p[3] = v * 41 % 256; return flt_shear(d, p, 3, v & 1); }
static int f_zigzag(Doc *d, int v) { return flt_zigzag(d, v * 31 % 201 - 100, 1 + v % 20, v % 3); }
static int f_glass(Doc *d, int v) { return flt_glass(d, v % 21, 1 + v % 15, 50 + v * 11 % 151, SEED); }
static int f_ocean(Doc *d, int v) { return flt_ocean_ripple(d, 1 + v % 15, v % 21, SEED); }
static int f_dust(Doc *d, int v) { return flt_dust_scratches(d, 1 + v % 16, v * 29 % 256); }
static int f_rnoise(Doc *d, int v) { return flt_reduce_noise(d, v % 11, v * 7 % 101, v * 13 % 101); }
static int f_smart(Doc *d, int v) { return flt_smart_blur(d, 1 + v % 20, v * 19 % 256, v % 3); }
static int f_surface(Doc *d, int v) { return flt_surface_blur(d, 1 + v % 32, 1 + v * 19 % 255); }
static int f_lens(Doc *d, int v) { return flt_lens_blur(d, 1 + v % 64, v * 7 % 101, v * 37 % 256); }
static int f_shape(Doc *d, int v) { return flt_shape_blur(d, 1 + v % 64, v % 5); }
static int f_spin(Doc *d, int v) { return flt_spin_blur(d, d->w / 2 + v % 5, d->h / 3, 1 + v * 47 % 360); }
static int f_cryst(Doc *d, int v) { return flt_crystallize(d, 3 + v * 7 % 298, SEED); }
static int f_point(Doc *d, int v) { return flt_pointillize(d, 3 + v * 7 % 298, SEED); }
static int f_halftone(Doc *d, int v) { return flt_color_halftone(d, 4 + v * 5 % 124, 108, 162 + v, 90 - v); }
static int f_fragment(Doc *d, int v) { (void)v; return flt_fragment(d); }
static int f_mezzo(Doc *d, int v) { return flt_mezzotint(d, v % 10, SEED); }
static int f_facet(Doc *d, int v) { (void)v; return flt_facet(d); }
static int f_trace(Doc *d, int v) { return flt_trace_contour(d, v * 53 % 256, v & 1); }
static int f_diffuse(Doc *d, int v) { return flt_diffuse(d, v % 4, SEED); }
static int f_tiles(Doc *d, int v) { return flt_tiles(d, 1 + v * 3 % 99, 1 + v * 7 % 99, v % 4, SEED); }
static int f_wind(Doc *d, int v) { return flt_wind(d, v % 3, v & 1, SEED); }
static int f_deint(Doc *d, int v) { return flt_de_interlace(d, v & 1, (v >> 1) & 1); }
static int f_ntsc(Doc *d, int v) { return flt_ntsc_colors(d, v & 1); }
static int f_custom(Doc *d, int v)
{
  int k[25], i;
  for (i = 0; i < 25; i++) k[i] = (int)((i * 7919u + (unsigned)v * 104729u) % 21) - 10;
  return flt_custom(d, k, v % 7 - 3, v % 41 - 20);
}
static const struct { const char *name; Flt fn; } filters[] = {
  { "Difference Clouds", f_dclouds }, { "Fibers", f_fibers }, { "Lens Flare", f_flare }, { "Lighting", f_light },
  { "Polar", f_polar }, { "Shear", f_shear }, { "ZigZag", f_zigzag }, { "Glass", f_glass }, { "Ocean Ripple", f_ocean },
  { "Dust & Scratches", f_dust }, { "Reduce Noise", f_rnoise }, { "Smart Blur", f_smart }, { "Surface Blur", f_surface },
  { "Lens Blur", f_lens }, { "Shape Blur", f_shape }, { "Spin Blur", f_spin }, { "Crystallize", f_cryst },
  { "Pointillize", f_point }, { "Color Halftone", f_halftone }, { "Fragment", f_fragment }, { "Mezzotint", f_mezzo },
  { "Facet", f_facet }, { "Trace Contour", f_trace }, { "Diffuse", f_diffuse }, { "Tiles", f_tiles }, { "Wind", f_wind },
  { "De-Interlace", f_deint }, { "NTSC Colors", f_ntsc }, { "Custom", f_custom } };
#define NFILTERS ((int)(sizeof filters / sizeof filters[0]))

/* each filter: inside a selection only, undone exactly, alpha kept when locked, a locked layer refused */
static void filters_generic(void)
{
  int f, bad_out, bad_undo, bad_lock, bad_alpha;
  char name[96];
  for (f = 0; f < NFILTERS; f++) {
    Doc *d = noisy(37, 29, 1);
    u8 *before = copy_px(d), *after;
    i32 x, y;
    bad_out = bad_undo = bad_lock = bad_alpha = 0;
    sel_rect(d, 6, 5, 25, 20, SEL_REPLACE, 0);
    doc_forget_undo(d);
    filters[f].fn(d, 5);
    for (y = 0; y < d->h; y++) for (x = 0; x < d->w; x++)
      if ((x < 6 || x >= 25 || y < 5 || y >= 20) && memcmp(px(d, x, y), before + ((long)y * d->w + x) * 4, 4)) bad_out = 1;
    if (!doc_undo(d) || !same_px(d, before)) bad_undo = 1;
    sel_none(d);
    d->lay[0]->locked = 1;
    if (filters[f].fn(d, 3) || !same_px(d, before)) bad_lock = 1;
    d->lay[0]->locked = 0;
    d->lay[0]->alpha_lock = 1;
    filters[f].fn(d, 3);
    after = d->lay[0]->px;
    { long k; for (k = 0; k < (long)d->w * d->h; k++) if (after[k * 4 + 3] != before[k * 4 + 3]) bad_alpha = 1; }
    sprintf(name, "%s: only inside the selection", filters[f].name); ok(name, !bad_out);
    sprintf(name, "%s: undone exactly", filters[f].name); ok(name, !bad_undo);
    sprintf(name, "%s: locked layer refused", filters[f].name); ok(name, !bad_lock);
    sprintf(name, "%s: locked transparency kept", filters[f].name); ok(name, !bad_alpha);
    free(before);
    doc_free(d);
  }
}

/* randomised runs: sizes, selections (hard and feathered), transparency and settings, for the sanitizers */
static void filters_random(void)
{
  int it, f, bad = 0;
  for (it = 0; it < 20; it++)
    for (f = 0; f < NFILTERS; f++) {
      i32 w = 1 + rnd(it < 2 ? 4 : 41), h = 1 + rnd(it < 2 ? 4 : 33);
      Doc *d = noisy(w, h, rnd(2));
      u8 *before = copy_px(d);
      int s = rnd(3);
      if (s == 1) sel_rect(d, rnd(w), rnd(h), rnd(w + 1), rnd(h + 1), SEL_REPLACE, 0);
      if (s == 2) sel_ellipse(d, rnd(w) - 2, rnd(h) - 2, rnd(w) + 3, rnd(h) + 3, SEL_REPLACE, 3);
      memcpy(d->lay[0]->px, before, (size_t)w * h * 4);
      doc_forget_undo(d);
      SEED = (unsigned)rnd(100000);
      if (filters[f].fn(d, rnd(1000)) && (!doc_undo(d) || !same_px(d, before))) { bad++; printf("  random %s %dx%d sel %d\n", filters[f].name, (int)w, (int)h, s); }
      free(before);
      doc_free(d);
    }
  ok("random filter runs undo exactly", bad == 0);
}

/* ------------------------------------------------------------------ the particular filters */
static void filters_particular(void)
{
  Doc *d, *e;
  u8 *c;
  i32 x, y;
  int bad, k25[25], i;

  /* Custom */
  d = noisy(20, 16, 1); c = copy_px(d);
  memset(k25, 0, sizeof k25); k25[12] = 1;
  flt_custom(d, k25, 1, 0);
  ok("custom: the identity kernel leaves the picture", same_px(d, c));
  for (i = 0; i < 25; i++) k25[i] = 1;
  flt_custom(d, k25, 25, 0);
  {
    long s = 0; int j;
    for (j = -2; j <= 2; j++) for (i = -2; i <= 2; i++) s += c[((long)(8 + j) * 20 + 9 + i) * 4 + 1];
    ok("custom: a 5 x 5 box averages", abs(px(d, 9, 8)[1] - (int)((s + 12) / 25)) <= 0 && px(d, 9, 8)[3] == c[(8 * 20 + 9) * 4 + 3]);
  }
  memcpy(d->lay[0]->px, c, 20 * 16 * 4);
  memset(k25, 0, sizeof k25); k25[12] = -1;
  flt_custom(d, k25, 1, 255);
  ok("custom: negative with an offset inverts", px(d, 3, 3)[0] == 255 - c[(3 * 20 + 3) * 4]);
  free(c); doc_free(d);

  /* Polar Coordinates: there and back again */
  d = smooth(96, 96); c = copy_px(d);
  flt_polar_coordinates(d, 1);
  ok("polar: it changes the picture", !same_px(d, c));
  flt_polar_coordinates(d, 0);
  {
    long err = 0, n = 0;
    for (y = 40; y < 92; y++) for (x = 4; x < 92; x++) { int ch; for (ch = 0; ch < 3; ch++) { err += abs(px(d, x, y)[ch] - c[((long)y * 96 + x) * 4 + ch]); n++; } }
    ok("polar: to polar and back is near the original", err / (double)n < 4);
  }
  free(c); doc_free(d);

  /* Dust & Scratches */
  d = flat(15, 15, 0x000000);
  memset(px(d, 7, 7), 255, 3);
  for (y = 1; y < 6; y++) for (x = 1; x < 6; x++) memset(px(d, x, y), 200, 3);
  c = copy_px(d);
  flt_dust_scratches(d, 1, 255);
  ok("dust: a threshold of 255 changes nothing", same_px(d, c));
  flt_dust_scratches(d, 1, 10);
  ok("dust: the speck is gone", px(d, 7, 7)[0] == 0 && px(d, 7, 7)[1] == 0);
  ok("dust: a 5 x 5 block keeps its middle", px(d, 3, 3)[0] == 200 && px(d, 2, 2)[0] == 200);
  free(c); doc_free(d);

  /* Difference Clouds on black is Clouds */
  d = flat(40, 30, 0); e = flat(40, 30, 0);
  flt_difference_clouds(d, 0x000000, 0xFFFFFF, 99);
  flt_clouds(e, 0x000000, 0xFFFFFF, 99);
  ok("difference clouds: on black, the clouds", !memcmp(d->lay[0]->px, e->lay[0]->px, 40 * 30 * 4));
  flt_difference_clouds(d, 0x000000, 0xFFFFFF, 99);
  ok("difference clouds: twice the same is black", is_flat(d, 0, 0));
  doc_free(d); doc_free(e);

  /* the blurs and noise filters leave a flat colour alone */
  {
    static const int which[] = { 9, 10, 11, 12, 13, 14, 15, 16, 19, 21, 23 };
    unsigned w;
    for (w = 0; w < sizeof which / sizeof which[0]; w++) {
      char name[80];
      d = flat(23, 19, 0x4080C0);
      filters[which[w]].fn(d, 6);
      sprintf(name, "%s: a flat colour stays flat", filters[which[w]].name);
      ok(name, is_flat(d, 0x4080C0, 1));
      doc_free(d);
    }
  }
  /* blurs smooth noise */
  {
    static const int which[] = { 10, 11, 12, 13, 14 };
    unsigned w;
    for (w = 0; w < sizeof which / sizeof which[0]; w++) {
      double v0 = 0, v1 = 0;
      long k, n;
      char name[80];
      d = noisy(30, 30, 0); n = 30 * 30;
      for (k = 0; k < n; k++) v0 += (d->lay[0]->px[k * 4] - 127.5) * (d->lay[0]->px[k * 4] - 127.5);
      if (which[w] == 10) flt_reduce_noise(d, 10, 0, 100);
      else if (which[w] == 11) flt_smart_blur(d, 3, 255, 0);
      else if (which[w] == 12) flt_surface_blur(d, 3, 255);
      else filters[which[w]].fn(d, 2);
      for (k = 0; k < n; k++) v1 += (d->lay[0]->px[k * 4] - 127.5) * (d->lay[0]->px[k * 4] - 127.5);
      sprintf(name, "%s: noise smoothed", filters[which[w]].name);
      ok(name, v1 < v0 * 0.6);
      doc_free(d);
    }
  }
  /* a bright point through Lens Blur becomes a disc */
  d = flat(41, 41, 0); memset(px(d, 20, 20), 255, 3);
  flt_lens_blur(d, 6, 0, 255);
  {
    int n = 0, sym = 1;
    for (y = 0; y < 41; y++) for (x = 0; x < 41; x++) {
      if (px(d, x, y)[0]) n++;
      if (px(d, x, y)[0] != px(d, 40 - x, y)[0] || px(d, x, y)[0] != px(d, y, x)[0]) sym = 0;
    }
    ok("lens blur: a point spreads into a round disc", n > 100 && n < 150 && sym);
  }
  doc_free(d);
  /* Smart Blur's edges */
  d = flat(20, 10, 0);
  for (y = 0; y < 10; y++) for (x = 10; x < 20; x++) memset(px(d, x, y), 220, 3);
  flt_smart_blur(d, 2, 30, 1);
  ok("smart blur: edges only marks the step", px(d, 9, 5)[0] == 255 && px(d, 10, 5)[0] == 255 && px(d, 3, 5)[0] == 0 && px(d, 16, 5)[0] == 0);
  doc_free(d);

  /* Trace Contour */
  d = flat(32, 8, 0);
  for (y = 0; y < 8; y++) for (x = 0; x < 16; x++) memset(px(d, x, y), 200, 3);
  for (y = 0; y < 8; y++) for (x = 16; x < 32; x++) memset(px(d, x, y), 50, 3);
  flt_trace_contour(d, 128, 1);
  bad = 0;
  for (y = 0; y < 8; y++) for (x = 0; x < 32; x++) if (px(d, x, y)[0] != (x == 15 ? 0 : 255)) bad = 1;
  ok("trace contour: one line where it crosses", !bad);
  doc_free(d);

  /* Diffuse takes only neighbours; darken only darkens */
  d = noisy(20, 20, 0); c = copy_px(d);
  flt_diffuse(d, 0, 3);
  bad = 0;
  for (y = 0; y < 20; y++) for (x = 0; x < 20; x++) {
    int j, found = 0;
    for (j = -1; j <= 1 && !found; j++) for (i = -1; i <= 1 && !found; i++) {
      i32 xx = x + i < 0 ? 0 : x + i > 19 ? 19 : x + i, yy = y + j < 0 ? 0 : y + j > 19 ? 19 : y + j;
      found = !memcmp(px(d, x, y), c + ((long)yy * 20 + xx) * 4, 4);
    }
    if (!found) bad = 1;
  }
  ok("diffuse: each pixel from a neighbour", !bad);
  memcpy(d->lay[0]->px, c, 20 * 20 * 4);
  flt_diffuse(d, 1, 3);
  bad = 0;
  for (y = 0; y < 20; y++) for (x = 0; x < 20; x++) if (lum(px(d, x, y)) > lum(c + ((long)y * 20 + x) * 4)) bad = 1;
  ok("diffuse: darken only never lightens", !bad);
  free(c); doc_free(d);

  /* Wind only brightens, and leaves flat colour */
  d = noisy(40, 10, 0); c = copy_px(d);
  flt_wind(d, 1, 1, 5);
  bad = 0;
  for (y = 0; y < 10; y++) for (x = 0; x < 40; x++) if (lum(px(d, x, y)) + 1 < lum(c + ((long)y * 40 + x) * 4)) bad = 1;
  ok("wind: streaks only brighten", !bad && !same_px(d, c));
  free(c); doc_free(d);

  /* Crystallize and Pointillize take colours from the picture */
  d = noisy(30, 30, 0); c = copy_px(d);
  flt_crystallize(d, 6, 1);
  bad = 0;
  for (y = 0; y < 30; y++) for (x = 0; x < 30; x++) {
    long k, found = 0;
    for (k = 0; k < 900 && !found; k++) found = !memcmp(px(d, x, y), c + k * 4, 4);
    if (!found) bad = 1;
  }
  ok("crystallize: every colour from the picture", !bad && !same_px(d, c));
  memcpy(d->lay[0]->px, c, 900 * 4);
  d->bg = 0x123456;
  flt_pointillize(d, 6, 1);
  bad = 0;
  {
    int nbg = 0;
    for (y = 0; y < 30; y++) for (x = 0; x < 30; x++) {
      const u8 *p = px(d, x, y);
      long k, found = p[0] == 0x12 && p[1] == 0x34 && p[2] == 0x56 && p[3] == 255;
      nbg += (int)found;
      for (k = 0; k < 900 && !found; k++) found = !memcmp(p, c + k * 4, 4);
      if (!found) bad = 1;
    }
    ok("pointillize: dots of the picture's colours on the background", !bad && nbg > 0 && nbg < 900);
  }
  free(c); doc_free(d);

  /* Color Halftone: black and white stay so */
  d = flat(40, 40, 0x000000); flt_color_halftone(d, 4, 108, 162, 90);
  ok("halftone: black stays black", is_flat(d, 0, 0));
  doc_free(d);
  d = flat(40, 40, 0xFFFFFF); flt_color_halftone(d, 8, 108, 162, 90);
  ok("halftone: white stays white", is_flat(d, 0xFFFFFF, 0));
  doc_free(d);
  d = flat(64, 64, 0x808080); flt_color_halftone(d, 6, 45, 45, 45);
  {
    long s = 0, k;
    for (k = 0; k < 64 * 64; k++) s += d->lay[0]->px[k * 4];
    ok("halftone: mid grey about half inked", s / (64 * 64) > 90 && s / (64 * 64) < 165);
  }
  doc_free(d);

  /* Mezzotint */
  d = flat(32, 32, 0x80FF00);
  flt_mezzotint(d, 2, 4);
  bad = 0;
  {
    long n = 0;
    for (y = 0; y < 32; y++) for (x = 0; x < 32; x++) {
      const u8 *p = px(d, x, y);
      if ((p[0] && p[0] != 255) || p[1] != 255 || p[2] != 0) bad = 1;
      n += p[0] == 255;
    }
    ok("mezzotint: two levels a channel, about in proportion", !bad && n > 350 && n < 700);
  }
  doc_free(d);

  /* Facet keeps a step sharp */
  d = flat(20, 10, 0);
  for (y = 0; y < 10; y++) for (x = 10; x < 20; x++) memset(px(d, x, y), 220, 3);
  c = copy_px(d);
  flt_facet(d);
  ok("facet: a clean step stays", same_px(d, c));
  free(c); doc_free(d);

  /* Shear: none, and a steady shift wrapped */
  d = noisy(32, 12, 0); c = copy_px(d);
  { int p[4] = { 0, 128, 255, 128 }; flt_shear(d, p, 2, 0); }
  ok("shear: no offset, no change", same_px(d, c));
  { int p[4] = { 0, 192, 255, 192 }; flt_shear(d, p, 2, 1); }
  bad = 0;
  for (y = 0; y < 12; y++) for (x = 0; x < 32; x++) if (memcmp(px(d, x, y), c + ((long)y * 32 + (x - 8 + 32) % 32) * 4, 4)) bad = 1;
  ok("shear: a steady shift wraps round", !bad);
  free(c); doc_free(d);

  /* distortions with nothing to do */
  d = noisy(30, 20, 0); c = copy_px(d);
  flt_zigzag(d, 0, 5, 2); ok("zigzag: amount 0, no change", same_px(d, c));
  flt_glass(d, 0, 3, 100, 1); ok("glass: distortion 0, no change", same_px(d, c));
  flt_ocean_ripple(d, 3, 0, 1); ok("ocean ripple: magnitude 0, no change", same_px(d, c));
  flt_lighting_basic(d, 5, 5, 10, 0, 0xFFFFFF, 100); ok("lighting: full ambient, no light, no change", same_px(d, c));
  flt_tiles(d, 10, 1, 0, 1); ok("tiles: too little offset to move, no change", same_px(d, c));
  flt_zigzag(d, 80, 5, 0); ok("zigzag: it moves things", !same_px(d, c));
  free(c); doc_free(d);

  /* De-Interlace */
  d = flat(10, 9, 0);
  for (y = 0; y < 9; y++) for (x = 0; x < 10; x++) { u8 *p = px(d, x, y); p[0] = (u8)(y & 1 ? 200 : 10); }
  flt_de_interlace(d, 1, 0);
  bad = 0;
  for (y = 0; y < 9; y++) if (px(d, 4, y)[0] != 200) bad = 1;
  ok("de-interlace: the even rows duplicated from the odd", !bad);
  for (y = 0; y < 9; y++) { px(d, 4, y)[0] = (u8)(y & 1 ? 100 : y * 10); }
  flt_de_interlace(d, 0, 1);
  ok("de-interlace: odd rows interpolated", px(d, 4, 3)[0] == 30 && px(d, 4, 4)[0] == 40 && px(d, 4, 8)[0] == 80);
  doc_free(d);

  /* NTSC Colors */
  d = flat(4, 4, 0xFFFF00); flt_ntsc_colors(d, 0);
  {
    const u8 *p = px(d, 1, 1);
    double r = p[0] / 255.0, g = p[1] / 255.0, b = p[2] / 255.0, Y = 0.299 * r + 0.587 * g + 0.114 * b;
    double I = 0.596 * r - 0.274 * g - 0.322 * b, Q = 0.211 * r - 0.523 * g + 0.312 * b, C = sqrt(I * I + Q * Q);
    ok("ntsc: saturated yellow brought within 120 IRE", Y + C <= 1.2 + 1e-6 && Y + C > 1.15 && fabs(Y - 0.886) < 0.01);
  }
  doc_free(d);
  d = flat(4, 4, 0x808080); flt_ntsc_colors(d, 0); ok("ntsc: grey unchanged", is_flat(d, 0x808080, 0)); doc_free(d);
  d = flat(4, 4, 0x806040); flt_ntsc_colors(d, 1); ok("pal: a safe colour unchanged", is_flat(d, 0x806040, 0)); doc_free(d);

  /* Lens Flare only brightens */
  d = noisy(40, 30, 0); c = copy_px(d);
  flt_lens_flare(d, 10, 8, 100, 0);
  bad = 0;
  for (y = 0; y < 30; y++) for (x = 0; x < 40; x++) { int ch; for (ch = 0; ch < 3; ch++) if (px(d, x, y)[ch] < c[((long)y * 40 + x) * 4 + ch]) bad = 1; }
  ok("lens flare: only brightens, the centre white", !bad && px(d, 10, 8)[0] == 255 && px(d, 10, 8)[2] >= 250);
  free(c); doc_free(d);

  /* Fibers between the two colours */
  d = flat(30, 30, 0); d->fg = 0x204060; d->bg = 0xE0C0A0;
  flt_fibers(d, 16, 4, 3);
  bad = 0;
  for (y = 0; y < 30; y++) for (x = 0; x < 30; x++) { const u8 *p = px(d, x, y); if (p[0] < 0x20 || p[0] > 0xE0 || p[1] < 0x40 || p[1] > 0xC0) bad = 1; }
  ok("fibers: between the foreground and background", !bad);
  doc_free(d);
}

/* ------------------------------------------------------------------ selections */
static void selections(void)
{
  Doc *d = flat(40, 30, 0x0000FF);
  i32 x, y;
  int bad;
  /* two red squares apart */
  for (y = 2; y < 12; y++) for (x = 2; x < 12; x++) { u8 *p = px(d, x, y); p[0] = 250; p[1] = 0; p[2] = (u8)(x & 1 ? 3 : 0); }
  for (y = 15; y < 25; y++) for (x = 25; x < 35; x++) { u8 *p = px(d, x, y); p[0] = 252; p[1] = 2; p[2] = 1; }
  ok("reselect: nothing to bring back", !sel_reselect(d));
  sel_rect(d, 4, 4, 6, 6, SEL_REPLACE, 0);
  ok("grow: tolerance 0 still joins the same colours", sel_grow_similar(d, 0) && d->sel[3 * 40 + 3] == 255 && d->sel[3 * 40 + 2] == 255 && !d->sel[16 * 40 + 26]);
  doc_undo(d);
  ok("grow: undone", d->sel && d->sel[3 * 40 + 3] == 0 && d->sel[4 * 40 + 4] == 255);
  sel_grow_similar(d, 8);
  bad = 0;
  for (y = 0; y < 30; y++) for (x = 0; x < 40; x++) if ((d->sel[y * 40 + x] == 255) != (x >= 2 && x < 12 && y >= 2 && y < 12)) bad = 1;
  ok("grow: the joined square exactly", !bad && d->selbox.x0 == 2 && d->selbox.x1 == 12);
  sel_rect(d, 4, 4, 6, 6, SEL_REPLACE, 0);
  sel_similar(d, 8);
  bad = 0;
  for (y = 0; y < 30; y++) for (x = 0; x < 40; x++) if ((d->sel[y * 40 + x] == 255) != ((x >= 2 && x < 12 && y >= 2 && y < 12) || (x >= 25 && x < 35 && y >= 15 && y < 25))) bad = 1;
  ok("similar: both squares", !bad && d->selbox.x1 == 35 && d->selbox.y1 == 25);
  ok("similar: nothing more to add", !sel_similar(d, 8));
  /* reselect */
  sel_deselect(d);
  ok("deselect: no selection", !d->sel && d->lastsel);
  ok("reselect: brings it back", sel_reselect(d) && d->sel && d->sel[16 * 40 + 26] == 255 && d->sel[0] == 0 && d->selbox.x0 == 2);
  doc_undo(d);
  ok("reselect: undone", !d->sel);
  sel_rect(d, 0, 0, 3, 3, SEL_REPLACE, 0);
  sel_remember(d);
  sel_none(d);
  sel_reselect(d);
  ok("remember + sel_none + reselect", d->sel && d->sel[0] == 255 && d->sel[5] == 0);
  canvas_resize(d, 50, 30, 4, 0);
  sel_none(d);
  ok("reselect: refused after a resize", !sel_reselect(d) || (d->w == 50 && d->sel && d->sel[0] == 255));
  ok("grow, similar: no selection, nothing", !sel_grow_similar(d, 10) && !sel_similar(d, 10));
  doc_free(d);
  /* random grows under the sanitizers */
  {
    int it;
    for (it = 0; it < 200; it++) {
      i32 w = 1 + rnd(30), h = 1 + rnd(30);
      Doc *e = noisy(w, h, rnd(2));
      long k;
      for (k = 0; k < (long)w * h * 4; k++) e->lay[0]->px[k] = (u8)(e->lay[0]->px[k] & 0xC0);
      sel_ellipse(e, rnd(w) - 3, rnd(h) - 3, rnd(w) + 3, rnd(h) + 3, SEL_REPLACE, rnd(2) * 4);
      if (rnd(2)) sel_grow_similar(e, rnd(256)); else sel_similar(e, rnd(256));
      if (rnd(2)) sel_deselect(e);
      sel_reselect(e);
      doc_free(e);
    }
    ok("random grows and reselects run", 1);
  }
}

int main(void)
{
  blend_modes();
  filters_particular();
  filters_generic();
  filters_random();
  selections();
  printf("%d tests, %d failed\n", tests, failed);
  return failed != 0;
}
