/*
 * gradient.c - Easel's gradients (see gradient.h): sampling with midpoints, editing the stops,
 * presets, noise gradients, GIMP .ggr files and Easel's own gradient files.
 *
 * Parts ported from PhotoCraft (https://github.com/storytold/photocraft), Copyright (c) 2026
 * ArtCraft Team and the PhotoCraft contributors, used under the MIT License (see Licenses.txt):
 * the ramp and midpoint_remap (crates/compose/src/gradient_fill.rs) and the colours of the built-in
 * presets (crates/engine/src/presets/gradients.rs).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "gradient.h"
#include "paint.h"

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
static void rgb_f(u32 c, double *o) { o[0] = ((c >> 16) & 255) / 255.0; o[1] = ((c >> 8) & 255) / 255.0; o[2] = (c & 255) / 255.0; }
static u32 stop_rgb(const Gradient *g, const GradColorStop *s) { return s->kind == GS_FG ? g->fg : s->kind == GS_BG ? g->bg : s->rgb; }

void gradient_init(Gradient *g, const char *name, u32 c0, u32 c1)
{
  memset(g, 0, sizeof *g);
  strncpy(g->name, name ? name : "", sizeof g->name - 1);
  g->ncolor = 2;
  g->color[0].pos = 0; g->color[0].mid = 0.5; g->color[0].kind = GS_RGB; g->color[0].rgb = c0 & 0xFFFFFF;
  g->color[1].pos = 1; g->color[1].mid = 0.5; g->color[1].kind = GS_RGB; g->color[1].rgb = c1 & 0xFFFFFF;
  g->nopacity = 2;
  g->opacity[0].pos = 0; g->opacity[0].mid = 0.5; g->opacity[0].opacity = 1;
  g->opacity[1].pos = 1; g->opacity[1].mid = 0.5; g->opacity[1].opacity = 1;
  g->fg = 0x000000; g->bg = 0xFFFFFF;
}
double gradient_midpoint(double u, double m)
{
  m = clampd(m, 0.05, 0.95);
  if (fabs(m - 0.5) < 1e-9) return u;
  return u <= m ? 0.5 * u / m : 0.5 + 0.5 * (u - m) / (1 - m);
}
/* hue-wise mixing (GIMP's HSV segments): the shorter way round */
static void mix_hsv(const double *a, const double *b, double k, double *o)
{
  double ha, sa, va, hb, sb, vb, h, dh;
  int r, g, bl;
  rgb_to_hsv((int)(a[0] * 255 + 0.5), (int)(a[1] * 255 + 0.5), (int)(a[2] * 255 + 0.5), &ha, &sa, &va);
  rgb_to_hsv((int)(b[0] * 255 + 0.5), (int)(b[1] * 255 + 0.5), (int)(b[2] * 255 + 0.5), &hb, &sb, &vb);
  dh = hb - ha;
  if (dh > 180) dh -= 360;
  if (dh < -180) dh += 360;
  h = ha + dh * k;
  while (h < 0) h += 360;
  while (h >= 360) h -= 360;
  hsv_to_rgb(h, sa + (sb - sa) * k, va + (vb - va) * k, &r, &g, &bl);
  o[0] = r / 255.0; o[1] = g / 255.0; o[2] = bl / 255.0;
}
/* the colour (0-1 RGB) and opacity at t, in floating point */
static void sample_f(const Gradient *g, double t, double *o)
{
  int i, n = g->ncolor, m = g->nopacity;
  double a[3], b[3];
  t = clampd(t, 0, 1);
  o[0] = o[1] = o[2] = 0; o[3] = 1;
  if (n > 0) {
    if (t <= g->color[0].pos) rgb_f(stop_rgb(g, &g->color[0]), o);
    else {
      for (i = 0; i < n - 1; i++) if (t <= g->color[i + 1].pos) break;
      if (i >= n - 1) rgb_f(stop_rgb(g, &g->color[n - 1]), o);
      else {
        const GradColorStop *s0 = &g->color[i], *s1 = &g->color[i + 1];
        double u = s1->pos > s0->pos ? (t - s0->pos) / (s1->pos - s0->pos) : 0, k = gradient_midpoint(u, s0->mid);
        int c;
        rgb_f(stop_rgb(g, s0), a); rgb_f(stop_rgb(g, s1), b);
        if (g->hsv) mix_hsv(a, b, k, o);
        else for (c = 0; c < 3; c++) o[c] = a[c] + (b[c] - a[c]) * k;
      }
    }
  }
  if (m > 0) {
    if (t <= g->opacity[0].pos) o[3] = g->opacity[0].opacity;
    else {
      for (i = 0; i < m - 1; i++) if (t <= g->opacity[i + 1].pos) break;
      if (i >= m - 1) o[3] = g->opacity[m - 1].opacity;
      else {
        const GradOpacityStop *s0 = &g->opacity[i], *s1 = &g->opacity[i + 1];
        double u = s1->pos > s0->pos ? (t - s0->pos) / (s1->pos - s0->pos) : 0;
        o[3] = s0->opacity + (s1->opacity - s0->opacity) * gradient_midpoint(u, s0->mid);
      }
    }
    o[3] = clampd(o[3], 0, 1);
  }
}
void gradient_sample(const Gradient *g, double t, u8 rgba[4])
{
  double o[4];
  int c;
  sample_f(g, t, o);
  for (c = 0; c < 4; c++) rgba[c] = (u8)(clampd(o[c], 0, 1) * 255 + 0.5);
}
void gradient_lut(const Gradient *g, u8 *lut, int n)
{
  int i;
  for (i = 0; i < n; i++) gradient_sample(g, n > 1 ? (double)i / (n - 1) : 0, lut + i * 4);
}

/* ------------------------------------------------------------------ editing */
static int cmp_color(const void *a, const void *b)
{
  double d = ((const GradColorStop *)a)->pos - ((const GradColorStop *)b)->pos;
  return d < 0 ? -1 : d > 0;
}
static int cmp_opacity(const void *a, const void *b)
{
  double d = ((const GradOpacityStop *)a)->pos - ((const GradOpacityStop *)b)->pos;
  return d < 0 ? -1 : d > 0;
}
/* (a stable insertion sort: stops at the same place keep their order, so a hard edge stays) */
void gradient_sort(Gradient *g)
{
  int i, j;
  if (g->ncolor > GRAD_MAXSTOPS) g->ncolor = GRAD_MAXSTOPS;
  if (g->nopacity > GRAD_MAXSTOPS) g->nopacity = GRAD_MAXSTOPS;
  for (i = 0; i < g->ncolor; i++) { g->color[i].pos = clampd(g->color[i].pos, 0, 1); g->color[i].mid = clampd(g->color[i].mid, 0.05, 0.95); }
  for (i = 0; i < g->nopacity; i++) { g->opacity[i].pos = clampd(g->opacity[i].pos, 0, 1); g->opacity[i].mid = clampd(g->opacity[i].mid, 0.05, 0.95); g->opacity[i].opacity = clampd(g->opacity[i].opacity, 0, 1); }
  for (i = 1; i < g->ncolor; i++) {
    GradColorStop t = g->color[i];
    for (j = i; j > 0 && cmp_color(&g->color[j - 1], &t) > 0; j--) g->color[j] = g->color[j - 1];
    g->color[j] = t;
  }
  for (i = 1; i < g->nopacity; i++) {
    GradOpacityStop t = g->opacity[i];
    for (j = i; j > 0 && cmp_opacity(&g->opacity[j - 1], &t) > 0; j--) g->opacity[j] = g->opacity[j - 1];
    g->opacity[j] = t;
  }
}
int gradient_add_color(Gradient *g, double pos, int kind, u32 rgb)
{
  int i;
  if (g->ncolor >= GRAD_MAXSTOPS) return 0;
  i = g->ncolor++;
  g->color[i].pos = clampd(pos, 0, 1); g->color[i].mid = 0.5; g->color[i].kind = kind; g->color[i].rgb = rgb & 0xFFFFFF;
  gradient_sort(g);
  return 1;
}
int gradient_add_opacity(Gradient *g, double pos, double opacity)
{
  int i;
  if (g->nopacity >= GRAD_MAXSTOPS) return 0;
  i = g->nopacity++;
  g->opacity[i].pos = clampd(pos, 0, 1); g->opacity[i].mid = 0.5; g->opacity[i].opacity = clampd(opacity, 0, 1);
  gradient_sort(g);
  return 1;
}
int gradient_del_color(Gradient *g, int i)
{
  if (i < 0 || i >= g->ncolor || g->ncolor <= 1) return 0;
  memmove(&g->color[i], &g->color[i + 1], (size_t)(g->ncolor - i - 1) * sizeof g->color[0]);
  g->ncolor--;
  return 1;
}
int gradient_del_opacity(Gradient *g, int i)
{
  if (i < 0 || i >= g->nopacity || g->nopacity <= 1) return 0;
  memmove(&g->opacity[i], &g->opacity[i + 1], (size_t)(g->nopacity - i - 1) * sizeof g->opacity[0]);
  g->nopacity--;
  return 1;
}
void gradient_reverse(Gradient *g)
{
  int i;
  GradColorStop *c = (GradColorStop *)malloc(sizeof(GradColorStop) * GRAD_MAXSTOPS);
  GradOpacityStop *o = (GradOpacityStop *)malloc(sizeof(GradOpacityStop) * GRAD_MAXSTOPS);
  if (!c || !o) { free(c); free(o); return; }
  for (i = 0; i < g->ncolor; i++) {
    c[i] = g->color[g->ncolor - 1 - i];
    c[i].pos = 1 - c[i].pos;
    /* the midpoint belongs to the segment after a stop: it moves to the stop before, mirrored */
    c[i].mid = i < g->ncolor - 1 ? 1 - g->color[g->ncolor - 2 - i].mid : 0.5;
  }
  for (i = 0; i < g->nopacity; i++) {
    o[i] = g->opacity[g->nopacity - 1 - i];
    o[i].pos = 1 - o[i].pos;
    o[i].mid = i < g->nopacity - 1 ? 1 - g->opacity[g->nopacity - 2 - i].mid : 0.5;
  }
  memcpy(g->color, c, sizeof c[0] * (size_t)g->ncolor);
  memcpy(g->opacity, o, sizeof o[0] * (size_t)g->nopacity);
  free(c); free(o);
}
void gradient_noise(Gradient *g, u32 seed, int n, int roughness, int transparency)
{
  int i, k;
  double c[3], step = clampd(roughness / 100.0, 0, 1);
  char name[64];
  if (n < 2) n = 2;
  if (n > GRAD_MAXSTOPS) n = GRAD_MAXSTOPS;
  sprintf(name, "Noise %lu", (unsigned long)seed);
  gradient_init(g, name, 0, 0);
  g->ncolor = n; g->nopacity = transparency ? n : 2;
  for (k = 0; k < 3; k++) c[k] = brush_rand01(seed, 0, (u32)(100 + k));
  for (i = 0; i < n; i++) {
    u32 rgb;
    if (i) for (k = 0; k < 3; k++) c[k] = clampd(c[k] + (brush_rand01(seed, (u32)i, (u32)(100 + k)) * 2 - 1) * step, 0, 1);
    rgb = ((u32)(c[0] * 255 + 0.5) << 16) | ((u32)(c[1] * 255 + 0.5) << 8) | (u32)(c[2] * 255 + 0.5);
    g->color[i].pos = (double)i / (n - 1); g->color[i].mid = 0.5; g->color[i].kind = GS_RGB; g->color[i].rgb = rgb;
    if (transparency) { g->opacity[i].pos = g->color[i].pos; g->opacity[i].mid = 0.5; g->opacity[i].opacity = brush_rand01(seed, (u32)i, 200); }
  }
}

/* ------------------------------------------------------------------ presets */
void gradlist_init(GradientList *l) { l->g = NULL; l->n = l->cap = 0; }
void gradlist_free(GradientList *l) { free(l->g); gradlist_init(l); }
int gradlist_add(GradientList *l, const Gradient *g)
{
  if (l->n == l->cap) {
    int nc = l->cap ? l->cap * 2 : 16;
    Gradient *ng = (Gradient *)realloc(l->g, (size_t)nc * sizeof *ng);
    if (!ng) return 0;
    l->g = ng; l->cap = nc;
  }
  l->g[l->n++] = *g;
  return 1;
}
void gradlist_remove(GradientList *l, int i)
{
  if (i < 0 || i >= l->n) return;
  memmove(&l->g[i], &l->g[i + 1], (size_t)(l->n - i - 1) * sizeof l->g[0]);
  l->n--;
}
/* a preset of evenly spaced colours, "#rrggbb" each, NULL ended */
static void from_colors(Gradient *g, const char *name, const char *const *c)
{
  int n = 0, i;
  while (c[n] && n < GRAD_MAXSTOPS) n++;
  gradient_init(g, name, 0, 0);
  g->ncolor = n;
  for (i = 0; i < n; i++) {
    g->color[i].pos = n > 1 ? (double)i / (n - 1) : 0; g->color[i].mid = 0.5; g->color[i].kind = GS_RGB;
    g->color[i].rgb = (u32)strtoul(c[i] + 1, NULL, 16);
  }
}
int gradlist_builtin(GradientList *l)
{
  static const char *const bw[] = { "#000000", "#ffffff", NULL };
  static const char *const rainbow[] = { "#ec1313", "#ec6d13", "#ecc813", "#b6ec13", "#5bec13", "#13ec25", "#13ec80", "#13ecda", "#13a4ec", "#1349ec", "#3713ec", "#9213ec", "#ec13ec", NULL };
  static const char *const pastel[] = { "#f4afaf", "#f4cbaf", "#f4e8af", "#e2f4af", "#c6f4af", "#aff4b4", "#aff4d1", "#aff4ee", "#afddf4", "#afc0f4", "#baaff4", "#d7aff4", "#f4aff4", NULL };
  static const char *const neon[] = { "#ff0000", "#ff6a00", "#ffd400", "#bfff00", "#55ff00", "#00ff15", "#00ff80", "#00ffea", "#00aaff", "#0040ff", "#2a00ff", "#9500ff", "#ff00ff", NULL };
  static const char *const oil[] = { "#0b0f1a", "#1b2a4a", "#0f6f6f", "#1fae8a", "#5ac8a8", "#7a5cff", "#b13bd6", "#e04a8f", "#f2a65a", "#3a2a6b", "#0b0f1a", NULL };
  static const char *const blue1[] = { "#0b3d91", "#4fa3e0", NULL }, *const blue2[] = { "#1c2a5a", "#3f6fd8", "#a8d4ff", NULL }, *const blue3[] = { "#00b4d8", "#03045e", NULL };
  static const char *const gray1[] = { "#212529", "#f8f9fa", NULL }, *const gray2[] = { "#000000", "#6c757d", "#ffffff", NULL }, *const gray3[] = { "#343a40", "#adb5bd", "#343a40", NULL };
  static const char *const copper[] = { "#4a3728", "#c08552", "#f3e9dc", NULL };
  /* Easel's own: the Amiga's Workbench and copper-list sunsets */
  static const char *const sunset[] = { "#1a0533", "#6b1d5c", "#d1495b", "#f79d65", "#fde68a", NULL };
  static const char *const wb[] = { "#0055aa", "#ffffff", "#000000", "#ff8800", NULL };
  struct { const char *name; const char *const *c; } fam[] = {
    { "Black, White", bw }, { "Rainbow", rainbow }, { "Pastel Rainbow", pastel }, { "Neon Rainbow", neon }, { "Oil Slick", oil },
    { "Blue 01", blue1 }, { "Blue 02", blue2 }, { "Blue 03", blue3 }, { "Gray 01", gray1 }, { "Gray 02", gray2 }, { "Gray 03", gray3 },
    { "Copper", copper }, { "Copper Sunset", sunset }, { "Workbench", wb }
  };
  Gradient *g = (Gradient *)malloc(sizeof *g);
  int n0 = l->n, i;
  if (!g) return 0;
  gradient_init(g, "Foreground to Background", 0, 0);
  g->color[0].kind = GS_FG; g->color[1].kind = GS_BG;
  gradlist_add(l, g);
  gradient_init(g, "Foreground to Transparent", 0, 0);
  g->color[0].kind = GS_FG; g->color[1].kind = GS_FG; g->opacity[1].opacity = 0;
  gradlist_add(l, g);
  for (i = 0; i < (int)(sizeof fam / sizeof fam[0]); i++) {
    from_colors(g, fam[i].name, fam[i].c);
    gradlist_add(l, g);
    if (fam[i].c == rainbow) {
      /* Rainbow Fade: the ends transparent */
      strcpy(g->name, "Rainbow Fade");
      g->nopacity = 4;
      g->opacity[0].pos = 0; g->opacity[0].opacity = 0; g->opacity[1].pos = 0.125; g->opacity[1].opacity = 1;
      g->opacity[2].pos = 0.875; g->opacity[2].opacity = 1; g->opacity[3].pos = 1; g->opacity[3].opacity = 0;
      { int k; for (k = 0; k < 4; k++) g->opacity[k].mid = 0.5; }
      gradlist_add(l, g);
    }
  }
  /* Transparent stripes: hard edges, two stops at each place */
  gradient_init(g, "Transparent Stripes", 0, 0);
  g->color[0].kind = GS_FG; g->color[1].kind = GS_FG;
  g->nopacity = 0;
  for (i = 0; i < 5; i++) {
    gradient_add_opacity(g, i / 5.0, (i & 1) ? 0 : 1);
    gradient_add_opacity(g, (i + 1) / 5.0, (i & 1) ? 0 : 1);
  }
  gradlist_add(l, g);
  free(g);
  return l->n - n0;
}

/* ------------------------------------------------------------------ GIMP .ggr */
static int near(double a, double b) { return fabs(a - b) < 1e-9; }
static int same4(const double *a, const double *b) { return near(a[0], b[0]) && near(a[1], b[1]) && near(a[2], b[2]) && near(a[3], b[3]); }
/* the value just inside [a, b] at its left (side 0) or right end (side 1) */
static void side_f(const Gradient *g, double a, double b, int side, double *o)
{
  double e = (b - a) * 1e-9;
  sample_f(g, side ? b - e : a + e, o);
  /* (then the exact end: the stops' own values where the gradient is continuous) */
  {
    double x[4];
    sample_f(g, side ? b : a, x);
    if (fabs(x[0] - o[0]) + fabs(x[1] - o[1]) + fabs(x[2] - o[2]) + fabs(x[3] - o[3]) < 1e-6) memcpy(o, x, sizeof x);
  }
}
static int add_pos(double *u, int n, int cap, double p)
{
  int i, j;
  for (i = 0; i < n; i++) if (near(u[i], p)) return n;
  if (n >= cap) return n;
  for (i = 0; i < n && u[i] < p; i++) ;
  for (j = n; j > i; j--) u[j] = u[j - 1];
  u[i] = p;
  return n + 1;
}
/* the kind of colour (GIMP's 0 fixed, 1 foreground, 3 background) the stop at p has from side */
static int end_type(const Gradient *g, double p, int right_side)
{
  int i, best = -1;
  for (i = 0; i < g->ncolor; i++) if (near(g->color[i].pos, p)) { best = i; if (!right_side) break; }
  if (best < 0) {
    /* inside an interval: fg or bg only if both its stops are that kind */
    for (i = 0; i < g->ncolor - 1; i++)
      if (g->color[i].pos < p && p < g->color[i + 1].pos) return g->color[i].kind == g->color[i + 1].kind && g->color[i].kind != GS_RGB ? (g->color[i].kind == GS_FG ? 1 : 3) : 0;
    return 0;
  }
  return g->color[best].kind == GS_FG ? 1 : g->color[best].kind == GS_BG ? 3 : 0;
}
char *gradient_write_ggr(const Gradient *gin, size_t *len)
{
  Gradient *g = (Gradient *)malloc(sizeof *g);
  double *u, *mids;
  int n = 0, i, cap = 4 * GRAD_MAXSTOPS + 4;
  Buf b;
  if (!g) return NULL;
  *g = *gin;
  gradient_sort(g);
  u = (double *)malloc((size_t)cap * sizeof(double));
  mids = (double *)malloc((size_t)cap * sizeof(double));
  if (!u || !mids) { free(u); free(mids); free(g); return NULL; }
  n = add_pos(u, n, cap, 0); n = add_pos(u, n, cap, 1);
  for (i = 0; i < g->ncolor; i++) n = add_pos(u, n, cap, g->color[i].pos);
  for (i = 0; i < g->nopacity; i++) n = add_pos(u, n, cap, g->opacity[i].pos);
  /* midpoints: a segment that is a whole colour interval (with the opacity steady over it), or a
     whole opacity interval (with the colour steady), keeps its midpoint; anywhere else a midpoint
     that isn't half way becomes a place of its own, so every segment is exact */
  for (i = 0; i < g->ncolor - 1; i++) {
    const GradColorStop *s0 = &g->color[i], *s1 = &g->color[i + 1];
    double k = s0->pos + s0->mid * (s1->pos - s0->pos), oa[4], ob[4];
    int j, inside = 0;
    if (near(s0->mid, 0.5) || s1->pos <= s0->pos) continue;
    for (j = 0; j < n; j++) if (u[j] > s0->pos + 1e-9 && u[j] < s1->pos - 1e-9) inside = 1;
    side_f(g, s0->pos, s1->pos, 0, oa); side_f(g, s0->pos, s1->pos, 1, ob);
    if (inside || !near(oa[3], ob[3])) n = add_pos(u, n, cap, k);
  }
  for (i = 0; i < g->nopacity - 1; i++) {
    const GradOpacityStop *s0 = &g->opacity[i], *s1 = &g->opacity[i + 1];
    double k = s0->pos + s0->mid * (s1->pos - s0->pos), oa[4], ob[4];
    int j, inside = 0;
    if (near(s0->mid, 0.5) || s1->pos <= s0->pos) continue;
    for (j = 0; j < n; j++) if (u[j] > s0->pos + 1e-9 && u[j] < s1->pos - 1e-9) inside = 1;
    side_f(g, s0->pos, s1->pos, 0, oa); side_f(g, s0->pos, s1->pos, 1, ob);
    if (inside || !(near(oa[0], ob[0]) && near(oa[1], ob[1]) && near(oa[2], ob[2]))) n = add_pos(u, n, cap, k);
  }
  for (i = 0; i < n - 1; i++) {
    int j;
    double a = u[i], bb = u[i + 1];
    mids[i] = (a + bb) / 2;
    for (j = 0; j < g->ncolor - 1; j++)
      if (near(g->color[j].pos, a) && near(g->color[j + 1].pos, bb) && !near(g->color[j].mid, 0.5)) mids[i] = a + g->color[j].mid * (bb - a);
    for (j = 0; j < g->nopacity - 1; j++)
      if (near(g->opacity[j].pos, a) && near(g->opacity[j + 1].pos, bb) && !near(g->opacity[j].mid, 0.5)) mids[i] = a + g->opacity[j].mid * (bb - a);
  }
  buf_init(&b);
  buf_printf(&b, "GIMP Gradient\nName: %s\n", g->name[0] ? g->name : "Easel");
  {
    int nseg = 0;
    for (i = 0; i < n - 1; i++) if (u[i + 1] > u[i]) nseg++;
    buf_printf(&b, "%d\n", nseg);
  }
  for (i = 0; i < n - 1; i++) {
    double l[4], r[4];
    if (!(u[i + 1] > u[i])) continue;
    side_f(g, u[i], u[i + 1], 0, l); side_f(g, u[i], u[i + 1], 1, r);
    buf_printf(&b, "%.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f 0 %d %d %d\n", u[i], mids[i], u[i + 1],
               l[0], l[1], l[2], l[3], r[0], r[1], r[2], r[3], g->hsv ? 1 : 0, end_type(g, u[i], 1), end_type(g, u[i + 1], 0));
  }
  free(u); free(mids); free(g);
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return buf_take(&b);
}
/* the next line of text (into line, cut at cap), 0 at the end */
static int next_line(const char *t, size_t n, size_t *o, char *line, size_t cap)
{
  size_t k = 0;
  if (*o >= n) return 0;
  while (*o < n && t[*o] != '\n') { if (k < cap - 1 && t[*o] != '\r') line[k++] = t[*o]; (*o)++; }
  if (*o < n) (*o)++;
  line[k] = 0;
  return 1;
}
static u32 rgb_of_f(const double *c)
{
  return ((u32)(clampd(c[0], 0, 1) * 255 + 0.5) << 16) | ((u32)(clampd(c[1], 0, 1) * 255 + 0.5) << 8) | (u32)(clampd(c[2], 0, 1) * 255 + 0.5);
}
int gradient_read_ggr(const char *text, size_t n, Gradient *g)
{
  char line[512];
  size_t o = 0;
  int nseg, i, hsv = 0;
  double lastr[4];
  int lastrtype = 0;
  if (!next_line(text, n, &o, line, sizeof line) || strncmp(line, "GIMP Gradient", 13)) return 0;
  gradient_init(g, "", 0, 0);
  g->ncolor = g->nopacity = 0;
  if (!next_line(text, n, &o, line, sizeof line)) return 0;
  if (!strncmp(line, "Name:", 5)) {
    const char *p = line + 5;
    while (*p == ' ') p++;
    strncpy(g->name, p, sizeof g->name - 1);
    if (!next_line(text, n, &o, line, sizeof line)) return 0;
  }
  nseg = atoi(line);
  if (nseg < 1 || nseg > 4096) return 0;
  for (i = 0; i < nseg; i++) {
    double v[11];
    int k, blend = 0, ctype = 0, lt = 0, rt = 0, cnt = 0;
    char *p = line, *e;
    if (!next_line(text, n, &o, line, sizeof line)) return 0;
    for (k = 0; k < 11; k++) { v[k] = strtod(p, &e); if (e == p) break; p = e; cnt++; }
    if (cnt < 11) return 0;
    blend = (int)strtol(p, &e, 10); if (e != p) { p = e; ctype = (int)strtol(p, &e, 10); if (e != p) { p = e; lt = (int)strtol(p, &e, 10); if (e != p) { p = e; rt = (int)strtol(p, &e, 10); } } }
    if (ctype) hsv = 1;
    {
      double L[4] = { v[3], v[4], v[5], v[6] }, R[4] = { v[7], v[8], v[9], v[10] };
      double mid = v[2] > v[0] ? (v[1] - v[0]) / (v[2] - v[0]) : 0.5;
      int lk = lt == 1 || lt == 2 ? GS_FG : lt == 3 || lt == 4 ? GS_BG : GS_RGB, rk = rt == 1 || rt == 2 ? GS_FG : rt == 3 || rt == 4 ? GS_BG : GS_RGB;
      if (lt == 2 || lt == 4) L[3] = 0;
      if (rt == 2 || rt == 4) R[3] = 0;
      if (blend == 5) mid = 0.5;   /* (step: a hard edge at the middle; read as two stops) */
      /* the left end: a stop unless it carries on from the segment before */
      if (i == 0 || !same4(L, lastr) || lk != lastrtype) {
        if (g->ncolor < GRAD_MAXSTOPS) { GradColorStop *s = &g->color[g->ncolor++]; s->pos = v[0]; s->kind = lk; s->rgb = rgb_of_f(L); s->mid = 0.5; }
        if (g->nopacity < GRAD_MAXSTOPS && (i == 0 || !near(L[3], lastr[3]))) { GradOpacityStop *s = &g->opacity[g->nopacity++]; s->pos = v[0]; s->opacity = L[3]; s->mid = 0.5; }
      }
      if (g->ncolor) g->color[g->ncolor - 1].mid = clampd(mid, 0.05, 0.95);
      if (g->nopacity) g->opacity[g->nopacity - 1].mid = clampd(mid, 0.05, 0.95);
      if (blend == 5) {
        double m = v[1];
        if (g->ncolor < GRAD_MAXSTOPS - 1) {
          GradColorStop *s = &g->color[g->ncolor++]; s->pos = m; s->kind = lk; s->rgb = rgb_of_f(L); s->mid = 0.5;
          s = &g->color[g->ncolor++]; s->pos = m; s->kind = rk; s->rgb = rgb_of_f(R); s->mid = 0.5;
        }
      }
      /* the right end: always a stop (the next segment may start differently) */
      if (g->ncolor < GRAD_MAXSTOPS) { GradColorStop *s = &g->color[g->ncolor++]; s->pos = v[2]; s->kind = rk; s->rgb = rgb_of_f(R); s->mid = 0.5; }
      if (g->nopacity < GRAD_MAXSTOPS) { GradOpacityStop *s = &g->opacity[g->nopacity++]; s->pos = v[2]; s->opacity = R[3]; s->mid = 0.5; }
      memcpy(lastr, R, sizeof lastr); lastrtype = rk;
    }
  }
  /* (stops in a row with the same value and place are one) */
  {
    int j, k = 0;
    for (j = 0; j < g->nopacity; j++) {
      if (k > 0 && near(g->opacity[k - 1].pos, g->opacity[j].pos) && near(g->opacity[k - 1].opacity, g->opacity[j].opacity)) continue;
      g->opacity[k++] = g->opacity[j];
    }
    g->nopacity = k;
  }
  g->hsv = hsv;
  return g->ncolor > 0;
}

/* ------------------------------------------------------------------ Easel's gradient files */
char *gradlist_write(const GradientList *l, size_t *len)
{
  Buf b;
  int i, k;
  buf_init(&b);
  buf_adds(&b, "EaselGradients 1\n");
  for (i = 0; i < l->n; i++) {
    const Gradient *g = &l->g[i];
    char nm[64];
    size_t j;
    strncpy(nm, g->name, sizeof nm - 1); nm[sizeof nm - 1] = 0;
    for (j = 0; nm[j]; j++) if (nm[j] == '"' || nm[j] == '\n') nm[j] = '\'';
    buf_printf(&b, "gradient \"%s\"\n", nm);
    if (g->hsv) buf_adds(&b, "hsv 1\n");
    for (k = 0; k < g->ncolor; k++) {
      const GradColorStop *s = &g->color[k];
      if (s->kind == GS_FG) buf_printf(&b, "color %.6f %.6f fg\n", s->pos, s->mid);
      else if (s->kind == GS_BG) buf_printf(&b, "color %.6f %.6f bg\n", s->pos, s->mid);
      else buf_printf(&b, "color %.6f %.6f rgb %06lX\n", s->pos, s->mid, (unsigned long)s->rgb);
    }
    for (k = 0; k < g->nopacity; k++) buf_printf(&b, "opacity %.6f %.6f %.6f\n", g->opacity[k].pos, g->opacity[k].mid, g->opacity[k].opacity);
    buf_adds(&b, "end\n");
  }
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return buf_take(&b);
}
int gradlist_read(GradientList *l, const char *text, size_t n)
{
  char line[256];
  size_t o = 0;
  int count = 0, in = 0;
  Gradient *g;
  if (!next_line(text, n, &o, line, sizeof line) || strncmp(line, "EaselGradients", 14)) return 0;
  if (!(g = (Gradient *)malloc(sizeof *g))) return 0;
  while (next_line(text, n, &o, line, sizeof line)) {
    if (!strncmp(line, "gradient", 8)) {
      char *q = strchr(line, '"'), *e;
      gradient_init(g, "", 0, 0);
      g->ncolor = g->nopacity = 0;
      if (q && (e = strchr(q + 1, '"'))) { *e = 0; strncpy(g->name, q + 1, sizeof g->name - 1); }
      in = 1;
    } else if (in && !strncmp(line, "hsv", 3)) g->hsv = atoi(line + 3) != 0;
    else if (in && !strncmp(line, "color", 5) && g->ncolor < GRAD_MAXSTOPS) {
      GradColorStop *s = &g->color[g->ncolor];
      char kind[8] = "", hex[16] = "";
      if (sscanf(line + 5, "%lf %lf %7s %15s", &s->pos, &s->mid, kind, hex) >= 3) {
        s->kind = !strcmp(kind, "fg") ? GS_FG : !strcmp(kind, "bg") ? GS_BG : GS_RGB;
        s->rgb = s->kind == GS_RGB ? (u32)strtoul(hex, NULL, 16) & 0xFFFFFF : 0;
        g->ncolor++;
      }
    } else if (in && !strncmp(line, "opacity", 7) && g->nopacity < GRAD_MAXSTOPS) {
      GradOpacityStop *s = &g->opacity[g->nopacity];
      if (sscanf(line + 7, "%lf %lf %lf", &s->pos, &s->mid, &s->opacity) == 3) g->nopacity++;
    } else if (in && !strncmp(line, "end", 3)) {
      if (!g->ncolor) { g->ncolor = 1; g->color[0].pos = 0; g->color[0].mid = 0.5; g->color[0].kind = GS_FG; }
      if (!g->nopacity) { g->nopacity = 1; g->opacity[0].pos = 0; g->opacity[0].mid = 0.5; g->opacity[0].opacity = 1; }
      gradient_sort(g);
      if (gradlist_add(l, g)) count++;
      in = 0;
    }
  }
  free(g);
  return count;
}
static int ends_with(const char *s, const char *ext)
{
  size_t a = strlen(s), b = strlen(ext);
  return a >= b && str_ieq(s + a - b, ext);
}
int gradlist_save(const GradientList *l, const char *path)
{
  size_t len;
  char *t;
  int ok;
  if (!l->n) return 0;
  t = ends_with(path, ".ggr") ? gradient_write_ggr(&l->g[0], &len) : gradlist_write(l, &len);
  if (!t) return 0;
  ok = file_write(path, t, len);
  free(t);
  return ok;
}
int gradlist_load(GradientList *l, const char *path)
{
  size_t len;
  u8 *t = file_read(path, &len);
  int n = 0;
  if (!t) return 0;
  if (len >= 13 && !strncmp((const char *)t, "GIMP Gradient", 13)) {
    Gradient *g = (Gradient *)malloc(sizeof *g);
    if (g && gradient_read_ggr((const char *)t, len, g)) n = gradlist_add(l, g);
    free(g);
  } else n = gradlist_read(l, (const char *)t, len);
  free(t);
  return n;
}
