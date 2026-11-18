/*
 * gallery.c - the Filter Gallery's effects (see gallery.h). The recipes follow PhotoCraft's
 * artistic_fx.rs (MIT licence): colours in 0..1 in a float window over a tile and the reach around
 * it; the helpers (Gaussian and line blurs, Sobel, Kuwahara, value noise, strokes, textures, cells)
 * are written afresh here in C.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "util.h"
#include "gallery.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ the effects' settings */
static const char *const tex_names[] = { "Canvas", "Brick", "Burlap", "Sandstone", NULL };
static const char *const light_names[] = { "Bottom", "Bottom Left", "Left", "Top Left", "Top", "Top Right", "Right", "Bottom Right", NULL };
static const char *const daub_names[] = { "Simple", "Light Rough", "Dark Rough", "Wide Sharp", "Wide Blurry", "Sparkle", NULL };
static const char *const dir_names[] = { "Right Diagonal", "Horizontal", "Left Diagonal", "Vertical", NULL };
static const char *const grain_names[] = { "Regular", "Soft", "Sprinkles", "Clumped", "Contrasty", "Enlarged", "Stippled", "Horizontal", "Vertical", "Speckle", NULL };
static const char *const glow_names[] = { "Neon Blue", "Foreground Color", "Background Color", NULL };
const char *const gallery_categories[4] = { "Artistic", "Brush Strokes", "Sketch", "Texture" };

#define NOL NULL, NULL, 0
const GalleryDef gallery_defs[GX_N] = {
  { "Colored Pencil", "Artistic", 3, { { "Pencil Width:", 1, 24, 4 }, { "Stroke Pressure:", 0, 15, 8 }, { "Paper Brightness:", 0, 50, 25 } }, NOL, NOL, NULL },
  { "Dry Brush", "Artistic", 3, { { "Brush Size:", 0, 10, 2 }, { "Brush Detail:", 0, 10, 8 }, { "Texture:", 1, 3, 1 } }, NOL, NOL, NULL },
  { "Fresco", "Artistic", 3, { { "Brush Size:", 0, 10, 2 }, { "Brush Detail:", 0, 10, 8 }, { "Texture:", 1, 3, 1 } }, NOL, NOL, NULL },
  { "Neon Glow", "Artistic", 2, { { "Glow Size:", -24, 24, 5 }, { "Glow Brightness:", 0, 50, 15 } }, glow_names, "Glow Color:", 0, NOL, NULL },
  { "Paint Daubs", "Artistic", 2, { { "Brush Size:", 1, 50, 8 }, { "Sharpness:", 0, 40, 7 } }, daub_names, "Brush Type:", 0, NOL, NULL },
  { "Palette Knife", "Artistic", 3, { { "Stroke Size:", 1, 50, 25 }, { "Stroke Detail:", 1, 3, 3 }, { "Softness:", 0, 10, 0 } }, NOL, NOL, NULL },
  { "Rough Pastels", "Artistic", 4, { { "Stroke Length:", 0, 40, 6 }, { "Stroke Detail:", 1, 20, 4 }, { "Scaling (%):", 50, 200, 100 }, { "Relief:", 0, 50, 20 } },
    tex_names, "Texture:", 0, light_names, "Light:", 0, "Invert" },
  { "Smudge Stick", "Artistic", 3, { { "Stroke Length:", 0, 10, 2 }, { "Highlight Area:", 0, 20, 0 }, { "Intensity:", 0, 10, 10 } }, NOL, NOL, NULL },
  { "Sponge", "Artistic", 3, { { "Brush Size:", 0, 10, 2 }, { "Definition:", 0, 25, 12 }, { "Smoothness:", 1, 15, 5 } }, NOL, NOL, NULL },
  { "Underpainting", "Artistic", 4, { { "Brush Size:", 0, 40, 6 }, { "Texture Coverage:", 0, 40, 16 }, { "Scaling (%):", 50, 200, 100 }, { "Relief:", 0, 50, 4 } },
    tex_names, "Texture:", 0, light_names, "Light:", 4, "Invert" },
  { "Watercolor", "Artistic", 3, { { "Brush Detail:", 1, 14, 9 }, { "Shadow Intensity:", 0, 10, 1 }, { "Texture:", 1, 3, 1 } }, NOL, NOL, NULL },
  { "Accented Edges", "Brush Strokes", 3, { { "Edge Width:", 1, 14, 2 }, { "Edge Brightness:", 0, 50, 38 }, { "Smoothness:", 1, 15, 5 } }, NOL, NOL, NULL },
  { "Angled Strokes", "Brush Strokes", 3, { { "Direction Balance:", 0, 100, 50 }, { "Stroke Length:", 3, 50, 15 }, { "Sharpness:", 0, 10, 3 } }, NOL, NOL, NULL },
  { "Crosshatch", "Brush Strokes", 3, { { "Stroke Length:", 3, 50, 9 }, { "Sharpness:", 0, 20, 6 }, { "Strength:", 1, 3, 1 } }, NOL, NOL, NULL },
  { "Dark Strokes", "Brush Strokes", 3, { { "Balance:", 0, 10, 5 }, { "Black Intensity:", 0, 10, 6 }, { "White Intensity:", 0, 10, 2 } }, NOL, NOL, NULL },
  { "Ink Outlines", "Brush Strokes", 3, { { "Stroke Length:", 1, 50, 4 }, { "Dark Intensity:", 0, 50, 20 }, { "Light Intensity:", 0, 50, 10 } }, NOL, NOL, NULL },
  { "Sprayed Strokes", "Brush Strokes", 2, { { "Stroke Length:", 0, 20, 12 }, { "Spray Radius:", 0, 25, 7 } }, dir_names, "Stroke Direction:", 0, NOL, NULL },
  { "Sumi-e", "Brush Strokes", 3, { { "Stroke Width:", 3, 15, 10 }, { "Stroke Pressure:", 0, 15, 2 }, { "Contrast:", 0, 40, 16 } }, NOL, NOL, NULL },
  { "Bas Relief", "Sketch", 2, { { "Detail:", 1, 15, 13 }, { "Smoothness:", 1, 15, 3 } }, light_names, "Light:", 0, NOL, NULL },
  { "Chalk & Charcoal", "Sketch", 3, { { "Charcoal Area:", 0, 20, 6 }, { "Chalk Area:", 0, 20, 6 }, { "Stroke Pressure:", 0, 5, 1 } }, NOL, NOL, NULL },
  { "Charcoal", "Sketch", 3, { { "Charcoal Thickness:", 1, 7, 1 }, { "Detail:", 0, 5, 5 }, { "Light/Dark Balance:", 0, 100, 50 } }, NOL, NOL, NULL },
  { "Chrome", "Sketch", 2, { { "Detail:", 0, 10, 4 }, { "Smoothness:", 0, 10, 7 } }, NOL, NOL, NULL },
  { "Conte Crayon", "Sketch", 4, { { "Foreground Level:", 1, 15, 11 }, { "Background Level:", 1, 15, 7 }, { "Scaling (%):", 50, 200, 100 }, { "Relief:", 0, 50, 4 } },
    tex_names, "Texture:", 0, light_names, "Light:", 4, "Invert" },
  { "Graphic Pen", "Sketch", 2, { { "Stroke Length:", 1, 15, 15 }, { "Light/Dark Balance:", 0, 100, 50 } }, dir_names, "Stroke Direction:", 0, NOL, NULL },
  { "Note Paper", "Sketch", 3, { { "Image Balance:", 0, 50, 25 }, { "Graininess:", 0, 20, 10 }, { "Relief:", 0, 25, 11 } }, NOL, NOL, NULL },
  { "Plaster", "Sketch", 2, { { "Image Balance:", 0, 50, 20 }, { "Smoothness:", 1, 15, 2 } }, light_names, "Light:", 4, NOL, NULL },
  { "Reticulation", "Sketch", 3, { { "Density:", 0, 50, 12 }, { "Foreground Level:", 0, 50, 40 }, { "Background Level:", 0, 50, 5 } }, NOL, NOL, NULL },
  { "Stamp", "Sketch", 2, { { "Light/Dark Balance:", 0, 50, 25 }, { "Smoothness:", 1, 50, 5 } }, NOL, NOL, NULL },
  { "Torn Edges", "Sketch", 3, { { "Image Balance:", 0, 50, 25 }, { "Smoothness:", 1, 15, 11 }, { "Contrast:", 1, 25, 17 } }, NOL, NOL, NULL },
  { "Water Paper", "Sketch", 3, { { "Fiber Length:", 3, 50, 15 }, { "Brightness:", 0, 100, 60 }, { "Contrast:", 0, 100, 80 } }, NOL, NOL, NULL },
  { "Craquelure", "Texture", 3, { { "Crack Spacing:", 2, 100, 15 }, { "Crack Depth:", 0, 10, 6 }, { "Crack Brightness:", 0, 10, 9 } }, NOL, NOL, NULL },
  { "Grain", "Texture", 2, { { "Intensity:", 0, 100, 40 }, { "Contrast:", 0, 100, 50 } }, grain_names, "Grain Type:", 0, NOL, NULL }
};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int count_list(const char *const *l) { int n = 0; if (l) while (l[n]) n++; return n; }
void gallery_default(int fx, GalleryEffect *e)
{
  const GalleryDef *g;
  int i;
  memset(e, 0, sizeof *e);
  if (fx < 0 || fx >= GX_N) fx = 0;
  g = &gallery_defs[fx];
  e->fx = fx;
  for (i = 0; i < g->nsl; i++) e->v[i] = g->sl[i].def;
  e->list1 = g->list1def; e->list2 = g->list2def;
  e->fg = 0x000000; e->bg = 0xFFFFFF; e->glow = 0x009EFF;
}
/* the settings held to their ranges (what every use reads) */
static GalleryEffect clamped(const GalleryEffect *e)
{
  GalleryEffect c = *e;
  const GalleryDef *g;
  int i, n;
  if (c.fx < 0 || c.fx >= GX_N) c.fx = 0;
  g = &gallery_defs[c.fx];
  for (i = 0; i < GX_SLIDERS; i++) c.v[i] = i < g->nsl ? clampi(c.v[i], g->sl[i].min, g->sl[i].max) : 0;
  n = count_list(g->list1); c.list1 = n ? clampi(c.list1, 0, n - 1) : 0;
  n = count_list(g->list2); c.list2 = n ? clampi(c.list2, 0, n - 1) : 0;
  c.tick = c.tick != 0;
  return c;
}

/* ------------------------------------------------------------------ small maths */
static double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static double smoothstep(double e0, double e1, double x)
{
  double t;
  if (fabs(e1 - e0) < 1e-9) return x >= e1 ? 1.0 : 0.0;
  t = clamp01((x - e0) / (e1 - e0));
  return t * t * (3 - 2 * t);
}
static double posterize(double v, double levels) { double l = levels - 1 < 1 ? 1 : levels - 1; return floor(clamp01(v) * l + 0.5) / l; }
static double lum3(const float *p) { return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]; }
static void mix3(float *o, const float *a, const float *b, double t) { int c; for (c = 0; c < 3; c++) o[c] = (float)(a[c] + (b[c] - a[c]) * t); }
static void rgbf(float *o, u32 c) { o[0] = (float)(((c >> 16) & 255) / 255.0); o[1] = (float)(((c >> 8) & 255) / 255.0); o[2] = (float)((c & 255) / 255.0); }
static void saturate3(float *p, double k) { double l = lum3(p); int c; for (c = 0; c < 3; c++) p[c] = (float)(l + (p[c] - l) * k); }
/* a well-mixed hash of a canvas position, a channel and a seed, in 0..1 */
static double hash01(i32 x, i32 y, u32 c, u32 seed)
{
  u32 h = (u32)x * 0x8DA6B343UL ^ (u32)y * 0xD8163841UL ^ c * 0xCB1AB31FUL ^ seed * 0x9E3779B9UL;
  h &= 0xFFFFFFFFUL;
  h ^= h >> 16; h = (h * 0x7FEB352DUL) & 0xFFFFFFFFUL; h ^= h >> 15; h = (h * 0x846CA68BUL) & 0xFFFFFFFFUL; h ^= h >> 16;
  return (double)(h >> 8) * (1.0 / 16777216.0);
}
static double vnoise(double x, double y, u32 seed)
{
  double fx = floor(x), fy = floor(y), tx = x - fx, ty = y - fy, sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty), top, bot;
  i32 ix = (i32)fx, iy = (i32)fy;
  top = hash01(ix, iy, 0, seed) + (hash01(ix + 1, iy, 0, seed) - hash01(ix, iy, 0, seed)) * sx;
  bot = hash01(ix, iy + 1, 0, seed) + (hash01(ix + 1, iy + 1, 0, seed) - hash01(ix, iy + 1, 0, seed)) * sx;
  return top + (bot - top) * sy;
}
static double fbm(double x, double y, u32 seed)
{
  return vnoise(x, y, seed) * 0.57 + vnoise(x * 2.03, y * 2.03, seed ^ 0x51) * 0.29 + vnoise(x * 4.1, y * 4.1, seed ^ 0xa3) * 0.14;
}
/* streaks along direction (dx, dy), len long and wid wide */
static double strokes(i32 x, i32 y, int dx, int dy, double len, double wid, u32 seed)
{
  double l = sqrt((double)(dx * dx + dy * dy)), ux = dx / l, uy = dy / l, u = x * ux + y * uy, v = -x * uy + y * ux;
  return vnoise(u / (len < 0.5 ? 0.5 : len), v / (wid < 0.3 ? 0.3 : wid), seed);
}
static void light_dir(int k, double *lx, double *ly)
{
#define S2 0.70710678118654752
  static const double t[8][2] = { { 0, 1 }, { -S2, S2 }, { -1, 0 }, { -S2, -S2 }, { 0, -1 }, { S2, -S2 }, { 1, 0 }, { S2, S2 } };
#undef S2
  k = clampi(k, 0, 7);
  *lx = t[k][0]; *ly = t[k][1];
}
static double shade(double gx, double gy, double lx, double ly, double k)
{
  double nx = -k * gx, ny = -k * gy, nz = 1, nl = sqrt(nx * nx + ny * ny + nz * nz), ll = sqrt(lx * lx + ly * ly + 1);
  return (nx * lx + ny * ly + nz) / (nl * ll) / (1 / ll);
}
static double weave(double x, double y, double p, double noise)
{
  i32 cx = (i32)floor(x / p), cy = (i32)floor(y / p);
  double u = (x - floor(x / p) * p) / p, v = (y - floor(y / p) * p) / p;
  double ridge = ((cx + cy) & 1) == 0 ? sin(v * M_PI) : sin(u * M_PI);
  return ridge * (1 - noise) + vnoise(x / (p * 0.7), y / (p * 2), 97) * noise;
}
static double fmodp(double a, double b) { return a - floor(a / b) * b; }
/* the textures: 0 canvas, 1 brick, 2 burlap, 3 sandstone */
static double texture(int kind, double x, double y, double s)
{
  x /= s; y /= s;
  switch (kind) {
  case 1: {
    double bw = 24, bh = 12, row = floor(y / bh), xo = x + (((i32)row & 1) == 0 ? 0 : bw / 2), u = fmodp(xo, bw), v = fmodp(y, bh), d;
    d = u < bw - u ? u : bw - u;
    if (v < d) d = v;
    if (bh - v < d) d = bh - v;
    return smoothstep(0.6, 2.2, d) * (0.85 + 0.15 * vnoise(x / 3, y / 3, 91)); }
  case 2: return weave(x, y, 7, 0.35);
  case 3: return fbm(x / 7, y / 7, 93) * 0.7 + hash01((i32)floor(x), (i32)floor(y), 0, 94) * 0.3;
  default: return weave(x, y, 3.5, 0.18);
  }
}
/* the nearest and second-nearest of the random points one to a cell, and the nearest */
static void voronoi(double x, double y, double cell, u32 seed, double *d1, double *d2, double *px, double *py)
{
  i32 cx = (i32)floor(x / cell), cy = (i32)floor(y / cell), i, j;
  *d1 = *d2 = 1e30; *px = x; *py = y;
  for (j = -1; j <= 1; j++)
    for (i = -1; i <= 1; i++) {
      double qx = (cx + i + hash01(cx + i, cy + j, 0, seed)) * cell, qy = (cy + j + hash01(cx + i, cy + j, 1, seed)) * cell;
      double d = sqrt((qx - x) * (qx - x) + (qy - y) * (qy - y));
      if (d < *d1) { *d2 = *d1; *d1 = d; *px = qx; *py = qy; }
      else if (d < *d2) *d2 = d;
    }
}
static void highlights(float *p, double l, double area, double intensity)
{
  double m;
  int c;
  if (area <= 0) return;
  m = smoothstep(1 - area / 20 * 0.75, 1.02, l) * (1 - intensity / 10 * 0.6);
  for (c = 0; c < 3; c++) p[c] = (float)(p[c] + (1 - p[c]) * m);
}

/* ------------------------------------------------------------------ the window and its blurs */
typedef struct { i32 x0, y0; int w, h; float *c; } Win;     /* c: w * h * 3 */
static int oom;
static void *gx_alloc(size_t n) { void *p = calloc(n ? n : 1, 1); if (!p) oom = 1; return p; }
static float *planef(const Win *w) { return (float *)gx_alloc((size_t)w->w * w->h * sizeof(float)); }
static float *dupc(const Win *w) { float *c = (float *)gx_alloc((size_t)w->w * w->h * 3 * sizeof(float)); if (c) memcpy(c, w->c, (size_t)w->w * w->h * 3 * sizeof(float)); return c; }
static float *lum_plane(const Win *w)
{
  float *l = planef(w);
  long i, n = (long)w->w * w->h;
  if (l) for (i = 0; i < n; i++) l[i] = (float)lum3(w->c + i * 3);
  return l;
}
/* box blur of radius r over n interleaved channels, edges repeated */
static void box_blur_n(float *p, int w, int h, int n, int r)
{
  int x, y, c, len = w > h ? w : h;
  float *line;
  double norm = 1.0 / (2 * r + 1);
  if (r < 1 || !p) return;
  if (!(line = (float *)gx_alloc((size_t)len * n * sizeof(float)))) return;
  for (y = 0; y < h; y++) {
    float *row = p + (long)y * w * n;
    memcpy(line, row, (size_t)w * n * sizeof(float));
    for (c = 0; c < n; c++) {
      double acc = 0;
      int k;
      for (k = -r; k <= r; k++) acc += line[clampi(k, 0, w - 1) * n + c];
      for (x = 0; x < w; x++) {
        row[x * n + c] = (float)(acc * norm);
        acc += line[clampi(x + r + 1, 0, w - 1) * n + c] - line[clampi(x - r, 0, w - 1) * n + c];
      }
    }
  }
  for (x = 0; x < w; x++) {
    for (y = 0; y < h; y++) memcpy(line + y * n, p + ((long)y * w + x) * n, n * sizeof(float));
    for (c = 0; c < n; c++) {
      double acc = 0;
      int k;
      for (k = -r; k <= r; k++) acc += line[clampi(k, 0, h - 1) * n + c];
      for (y = 0; y < h; y++) {
        p[((long)y * w + x) * n + c] = (float)(acc * norm);
        acc += line[clampi(y + r + 1, 0, h - 1) * n + c] - line[clampi(y - r, 0, h - 1) * n + c];
      }
    }
  }
  free(line);
}
/* the three box radii nearest a Gaussian of sigma */
static void box_radii(double sigma, int *rad)
{
  double wi = sqrt(12 * sigma * sigma / 3 + 1);
  int wl = (int)floor(wi), wu, m, p;
  if (wl % 2 == 0) wl--;
  if (wl < 1) wl = 1;
  wu = wl + 2;
  m = (int)floor((12 * sigma * sigma - 3 * wl * wl - 12 * wl - 9) / (-4.0 * wl - 4) + 0.5);
  for (p = 0; p < 3; p++) rad[p] = ((p < m ? wl : wu) - 1) / 2;
}
static int gs(double sigma) { int r[3]; if (sigma < 0.3) return 0; box_radii(sigma, r); return r[0] + r[1] + r[2] + 1; }
static void gauss_n(float *p, int w, int h, int n, double sigma)
{
  int r[3], k;
  if (sigma < 0.3 || !p) return;
  box_radii(sigma, r);
  for (k = 0; k < 3; k++) box_blur_n(p, w, h, n, r[k]);
}
static void sobel(const float *p, int w, int h, float *gx, float *gy)
{
  int x, y;
#define AT(xx, yy) p[(long)clampi(yy, 0, h - 1) * w + clampi(xx, 0, w - 1)]
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      long i = (long)y * w + x;
      gx[i] = (float)(((AT(x + 1, y - 1) + 2 * AT(x + 1, y) + AT(x + 1, y + 1)) - (AT(x - 1, y - 1) + 2 * AT(x - 1, y) + AT(x - 1, y + 1))) / 8);
      gy[i] = (float)(((AT(x - 1, y + 1) + 2 * AT(x, y + 1) + AT(x + 1, y + 1)) - (AT(x - 1, y - 1) + 2 * AT(x, y - 1) + AT(x + 1, y - 1))) / 8);
    }
#undef AT
}
/* the gradient's size (malloc'd), and its parts if gx, gy are wanted */
static float *grad_mag(const float *p, int w, int h, float **gxo, float **gyo)
{
  long i, n = (long)w * h;
  float *gx = (float *)gx_alloc(n * sizeof(float)), *gy = (float *)gx_alloc(n * sizeof(float)), *m = (float *)gx_alloc(n * sizeof(float));
  if (!gx || !gy || !m || !p) { free(gx); free(gy); free(m); if (gxo) *gxo = NULL; if (gyo) *gyo = NULL; return NULL; }
  sobel(p, w, h, gx, gy);
  for (i = 0; i < n; i++) m[i] = (float)sqrt((double)gx[i] * gx[i] + (double)gy[i] * gy[i]);
  if (gxo) *gxo = gx; else free(gx);
  if (gyo) *gyo = gy; else free(gy);
  return m;
}
/* a box blur of radius r along the lines in direction (dx, dy) */
static void line_blur(float *buf, int n, int w, int h, int dx, int dy, int r)
{
  int sx, sy, len = (w > h ? w : h) + 2;
  long *idx;
  double *vals;
  double norm = 1.0 / (2 * r + 1);
  if (r < 1 || !buf) return;
  idx = (long *)gx_alloc(len * sizeof(long));
  vals = (double *)gx_alloc(len * sizeof(double));
  if (!idx || !vals) { free(idx); free(vals); return; }
#define INSIDE(xx, yy) ((xx) >= 0 && (yy) >= 0 && (xx) < w && (yy) < h)
  for (sy = 0; sy < h; sy++)
    for (sx = 0; sx < w; sx++) {
      int x = sx, y = sy, m = 0, c, k;
      if (INSIDE(sx - dx, sy - dy)) continue;            /* not where a line starts */
      while (INSIDE(x, y) && m < len) { idx[m++] = ((long)y * w + x) * n; x += dx; y += dy; }
      for (c = 0; c < n; c++) {
        double acc = 0;
        for (k = 0; k < m; k++) vals[k] = buf[idx[k] + c];
        for (k = -r; k <= r; k++) acc += vals[clampi(k, 0, m - 1)];
        for (k = 0; k < m; k++) {
          buf[idx[k] + c] = (float)(acc * norm);
          acc += vals[clampi(k + r + 1, 0, m - 1)] - vals[clampi(k - r, 0, m - 1)];
        }
      }
    }
#undef INSIDE
  free(idx); free(vals);
}
static void line_blur3(Win *w, int dx, int dy, int r) { line_blur(w->c, 3, w->w, w->h, dx, dy, r); }
static int kuwahara_reach(int r) { int h2 = (r + 1) / 2; if (h2 < 1) h2 = 1; return 2 * h2 + 1; }
/* each pixel the mean of the least varied of the four boxes at its corners */
static void kuwahara(Win *w, int r)
{
  int h2 = (r + 1) / 2, x, y;
  long i, n = (long)w->w * w->h;
  float *m;
  if (h2 < 1) h2 = 1;
  if (!(m = (float *)gx_alloc(n * 5 * sizeof(float)))) return;
  for (i = 0; i < n; i++) {
    const float *p = w->c + i * 3;
    double l = lum3(p);
    m[i * 5] = p[0]; m[i * 5 + 1] = p[1]; m[i * 5 + 2] = p[2]; m[i * 5 + 3] = (float)l; m[i * 5 + 4] = (float)(l * l);
  }
  box_blur_n(m, w->w, w->h, 5, h2);
  for (y = 0; y < w->h; y++)
    for (x = 0; x < w->w; x++) {
      static const int q[4][2] = { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
      double best = 1e30;
      const float *col = NULL;
      int k;
      for (k = 0; k < 4; k++) {
        const float *s = m + ((long)clampi(y + q[k][1] * h2, 0, w->h - 1) * w->w + clampi(x + q[k][0] * h2, 0, w->w - 1)) * 5;
        double var = s[4] - (double)s[3] * s[3];
        if (var < best) { best = var; col = s; }
      }
      memcpy(w->c + ((long)y * w->w + x) * 3, col, 3 * sizeof(float));
    }
  free(m);
}
static void unsharp(Win *w, double sigma, double amount)
{
  float *b;
  long i, n = (long)w->w * w->h * 3;
  if (amount <= 0 || !(b = dupc(w))) return;
  gauss_n(b, w->w, w->h, 3, sigma);
  for (i = 0; i < n; i++) w->c[i] = (float)(w->c[i] + (w->c[i] - b[i]) * amount);
  free(b);
}
/* a bilinear sample of c (a window's colours) at canvas position x, y (pixel centres at .5) */
static void sample(const Win *w, const float *c, double x, double y, float *o)
{
  double fx = x - 0.5 - w->x0, fy = y - 0.5 - w->y0, ax, ay;
  int x0, y0, x1, y1, k;
  if (fx < 0) fx = 0;
  if (fy < 0) fy = 0;
  if (fx > w->w - 1) fx = w->w - 1;
  if (fy > w->h - 1) fy = w->h - 1;
  x0 = (int)fx; y0 = (int)fy; x1 = x0 + 1 < w->w ? x0 + 1 : x0; y1 = y0 + 1 < w->h ? y0 + 1 : y0;
  ax = fx - x0; ay = fy - y0;
  for (k = 0; k < 3; k++) {
    double t = c[((long)y0 * w->w + x0) * 3 + k] + (c[((long)y0 * w->w + x1) * 3 + k] - c[((long)y0 * w->w + x0) * 3 + k]) * ax;
    double b = c[((long)y1 * w->w + x0) * 3 + k] + (c[((long)y1 * w->w + x1) * 3 + k] - c[((long)y1 * w->w + x0) * 3 + k]) * ax;
    o[k] = (float)(t + (b - t) * ay);
  }
}
/* a paint stroke along (dx, dy) through x, y: the colour where its lane's stroke started, and how far along it is */
static double paint_stroke(const Win *w, const float *c, i32 x, i32 y, int dx, int dy, double len, double wid, u32 seed, float *o)
{
  double l = sqrt((double)(dx * dx + dy * dy)), ux = dx / l, uy = dy / l, fx = x + 0.5, fy = y + 0.5, u, v, off, t, du;
  if (len < 1) len = 1;
  if (wid < 0.5) wid = 0.5;
  u = fx * ux + fy * uy; v = -fx * uy + fy * ux;
  off = hash01((i32)floor(v / wid), 0, 0, seed) * len;
  t = fmodp((u + off) / len, 1.0);
  du = (0.5 - t) * len;
  sample(w, c, fx + du * ux, fy + du * uy, o);
  return t;
}
/* relief from a texture, lit from the light; weight (if given) how much of it each pixel takes */
static void texturize(Win *w, int kind, int scaling, int relief, int light, int invert, int pastel)
{
  double s = scaling / 100.0, k = relief / 50.0 * 3, sign = invert ? -1 : 1, lx, ly;
  int x, y, c;
  if (relief <= 0) return;
  if (s < 0.5) s = 0.5;
  if (s > 2) s = 2;
  light_dir(light, &lx, &ly);
  for (y = 0; y < w->h; y++)
    for (x = 0; x < w->w; x++) {
      float *p = w->c + ((long)y * w->w + x) * 3;
      double fx = w->x0 + x, fy = w->y0 + y, gx, gy, f, sh, wt = pastel ? 0.15 + 0.85 * (1 - clamp01(lum3(p))) : 1;
      gx = (texture(kind, fx + 1, fy, s) - texture(kind, fx - 1, fy, s)) * 0.5 * sign;
      gy = (texture(kind, fx, fy + 1, s) - texture(kind, fx, fy - 1, s)) * 0.5 * sign;
      sh = shade(gx, gy, lx, ly, k);
      if (sh < 0) sh = 0;
      if (sh > 2) sh = 2;
      f = 1 + (sh - 1) * wt;
      for (c = 0; c < 3; c++) p[c] = (float)(p[c] * f);
    }
}
static int daubs_radius(const GalleryEffect *e) { int r = (int)(e->v[0] / 2.0 * (e->list1 == 3 || e->list1 == 4 ? 1.5 : 1)); return r < 1 ? 1 : r; }
static int watercolor_radius(const GalleryEffect *e) { return 1 + (int)((15 - e->v[0]) / 2.5); }
static void stroke_dir(int k, int *dx, int *dy)
{
  switch (k) { case 1: *dx = 1; *dy = 0; break; case 2: *dx = 1; *dy = 1; break; case 3: *dx = 0; *dy = 1; break; default: *dx = 1; *dy = -1; }
}

int gallery_reach(const GalleryEffect *ein)
{
  GalleryEffect e = clamped(ein);
  const int *v = e.v;
  switch (e.fx) {
  case GX_COLORED_PENCIL: return 2;
  case GX_DRY_BRUSH: case GX_FRESCO: return kuwahara_reach(1 + v[0]) + gs(1.0) + 2;
  case GX_NEON_GLOW: return gs(abs(v[0]) * 0.6 + 0.5);
  case GX_PAINT_DAUBS: return kuwahara_reach(daubs_radius(&e)) + gs(1.5) + 1;
  case GX_PALETTE_KNIFE: return kuwahara_reach(v[0] / 3 < 1 ? 1 : v[0] / 3) + gs(v[2] * 0.5);
  case GX_ROUGH_PASTELS: return gs(1.8) + (2 + v[0]) / 2 + 3;
  case GX_SMUDGE_STICK: return (int)(v[0] * 1.5) + 2;
  case GX_SPONGE: return kuwahara_reach(1 + v[0]);
  case GX_UNDERPAINTING: return gs(0.5 + v[0] * 0.4);
  case GX_WATERCOLOR: return kuwahara_reach(watercolor_radius(&e)) + 2;
  case GX_ACCENTED_EDGES: return gs(0.4 + v[2] * 0.3) + gs(v[0] * 0.5) + 2;
  case GX_ANGLED_STROKES: return gs(0.8) + v[1] / 2 + gs(1.0) + 3;
  case GX_CROSSHATCH: return v[0] / 3 + gs(1.0) + 2;
  case GX_DARK_STROKES: return 9;
  case GX_INK_OUTLINES: return v[0] / 2 + 3;
  case GX_SPRAYED_STROKES: return v[1] + v[0] / 2 + 3;
  case GX_SUMI_E: return gs(v[0] * 0.2) + v[0] / 2 + 2;
  case GX_BAS_RELIEF: return gs(0.3 + v[1] * 0.5 + (15 - v[0]) * 0.12) + 2;
  case GX_CHARCOAL: return gs(1.0) + 2;
  case GX_CHROME: return gs(1.0 + v[1] * 0.4) + 2;
  case GX_GRAPHIC_PEN: return v[0] / 2 + 2;
  case GX_NOTE_PAPER: return gs(1.0) + gs(1.2) + 2;
  case GX_PLASTER: return gs(0.5 + v[1] * 0.8) + gs(1.5) + 2;
  case GX_STAMP: return gs(0.3 + v[1] * 0.3);
  case GX_TORN_EDGES: return gs(1.0);
  case GX_WATER_PAPER: return 2 * (v[0] / 6) + 2;
  default: return 0;                 /* Chalk & Charcoal, Conte Crayon, Reticulation, Craquelure, Grain: pixel by pixel */
  }
}

/* ------------------------------------------------------------------ the effects */
#define EACH for (y = 0; y < w->h; y++) for (x = 0; x < w->w; x++) if ((i = (long)y * w->w + x, p = w->c + i * 3, X = w->x0 + x, Y = w->y0 + y), 1)
static void run_effect(const GalleryEffect *ein, Win *w)
{
  GalleryEffect e = clamped(ein);
  const int *v = e.v;
  int x, y, c;
  long i;
  i32 X, Y;
  float *p, fg[3], bg[3];
  float *l = NULL, *m = NULL, *a = NULL, *gx = NULL, *gy = NULL;
  rgbf(fg, e.fg); rgbf(bg, e.bg);
  switch (e.fx) {
  case GX_COLORED_PENCIL: {
    double pw = v[0], pressure = v[1] / 15.0, pb = 0.7 + 0.3 * v[2] / 50.0;
    float paper[3];
    for (c = 0; c < 3; c++) paper[c] = (float)(bg[c] * pb);
    if (!(l = lum_plane(w)) || !(m = grad_mag(l, w->w, w->h, NULL, NULL))) break;
    EACH {
      double d = 1 - l[i], h1 = strokes(X, Y, 1, 1, 5 + pw * 3, 0.5 + pw * 0.45, 11), h2 = strokes(X, Y, 1, -1, 5 + pw * 3, 0.5 + pw * 0.45, 12);
      double hatch = h2 * smoothstep(0.3, 0.8, d), cov;
      float q[3];
      if (h1 > hatch) hatch = h1;
      cov = clamp01(d * (0.55 + pressure) + m[i] * 3 - (1 - hatch) * 0.8 + 0.25);
      for (c = 0; c < 3; c++) q[c] = (float)(p[c] * 0.88);
      mix3(p, paper, q, cov);
    }
    break; }
  case GX_DRY_BRUSH: case GX_FRESCO: {
    int fresco = e.fx == GX_FRESCO;
    double levels = 3 + v[1], amp = 0.02 + (v[2] - 1) * 0.05;
    kuwahara(w, 1 + v[0]);
    unsharp(w, 1.0, fresco ? 0.9 : 0.5);
    if (fresco && (!(a = lum_plane(w)) || !(l = grad_mag(a, w->w, w->h, NULL, NULL)))) break;
    EACH {
      double n = (hash01(X, Y, 0, 21) - 0.5) * amp;
      for (c = 0; c < 3; c++) {
        double q = posterize(p[c], levels) * 0.6 + p[c] * 0.4 + n;
        if (fresco) q = pow(clamp01((q - 0.5) * 1.3 + 0.45), 1.15) * (1 - clamp01(l[i] * 2.5) * 0.6);
        p[c] = (float)q;
      }
    }
    break; }
  case GX_NEON_GLOW: {
    double br = v[1] / 50.0;
    float glow[3];
    rgbf(glow, e.list1 == 1 ? e.fg : e.list1 == 2 ? e.bg : e.glow);
    if (!(l = lum_plane(w)) || !(m = planef(w))) break;
    for (i = 0; i < (long)w->w * w->h; i++) m[i] = v[0] >= 0 ? l[i] : 1 - l[i];
    gauss_n(m, w->w, w->h, 1, abs(v[0]) * 0.6 + 0.5);
    EACH {
      float base[3];
      double k = clamp01(m[i] * 1.2) * br * 2;
      mix3(base, fg, bg, l[i]);
      for (c = 0; c < 3; c++) p[c] = (float)(1 - (1 - base[c]) * (1 - clamp01(glow[c] * k)));
    }
    break; }
  case GX_PAINT_DAUBS: {
    int ty = e.list1;
    double sharp = v[1] / 40.0;
    kuwahara(w, daubs_radius(&e));
    if (ty == 4) gauss_n(w->c, w->w, w->h, 3, 1.5);
    else if (ty == 3) unsharp(w, 1.5, sharp * 4);
    else unsharp(w, 1.2, sharp * 2.5);
    if (ty == 1 || ty == 2 || ty == 5) {
      if (ty == 5 && (!(a = lum_plane(w)) || !(l = grad_mag(a, w->w, w->h, NULL, NULL)))) break;
      EACH {
        double n = fbm(X / 2.0, Y / 2.0, 41) - 0.5;
        for (c = 0; c < 3; c++) {
          if (ty == 1) p[c] = (float)(p[c] + (n > 0 ? n : 0) * 0.35);
          else if (ty == 2) p[c] = (float)(p[c] * (1 - (n > 0 ? n : 0) * 0.7));
          else p[c] = (float)(p[c] + clamp01(l[i] * 4) * smoothstep(0.75, 0.95, hash01(X, Y, 0, 42)) * 0.8);
        }
      }
    }
    break; }
  case GX_PALETTE_KNIFE: {
    double levels = 2 + v[1] * 3;
    kuwahara(w, v[0] / 3 < 1 ? 1 : v[0] / 3);
    EACH for (c = 0; c < 3; c++) p[c] = (float)(posterize(p[c], levels) * 0.5 + p[c] * 0.5);
    gauss_n(w->c, w->w, w->h, 3, v[2] * 0.5);
    break; }
  case GX_ROUGH_PASTELS: {
    double len = v[0], det = (v[1] - 1) / 19.0;
    if (len >= 1) {
      float *soft = dupc(w);
      if (!soft) break;
      gauss_n(soft, w->w, w->h, 3, 0.6 + (1 - det) * 1.2);
      EACH {
        float q[3];
        double t = paint_stroke(w, soft, X, Y, 1, -1, 2 + len, 1.5, 51, q);
        double n = (strokes(X, Y, 1, -1, 2 + len * 0.5, 0.8, 52) - 0.5) * 0.12 + (0.5 - fabs(t - 0.5)) * 0.06;
        mix3(q, q, p, det * 0.6);
        for (c = 0; c < 3; c++) p[c] = (float)(q[c] + n);
      }
      free(soft);
    }
    texturize(w, e.list1, v[2], v[3], e.list2, e.tick, 1);
    break; }
  case GX_SMUDGE_STICK:
    line_blur3(w, 1, 1, (int)(v[0] * 1.5));
    EACH {
      double lv = lum3(p);
      for (c = 0; c < 3; c++) p[c] = (float)(p[c] * (0.7 + 0.3 * lv));
      highlights(p, lv, v[1], v[2]);
    }
    break;
  case GX_SPONGE: {
    double def = v[1] / 25.0, sc = 1.5 + v[2] * 0.5;
    kuwahara(w, 1 + v[0]);
    EACH {
      double hole = smoothstep(0.58, 0.75, fbm(X / sc, Y / sc, 61));
      for (c = 0; c < 3; c++) p[c] = (float)(clamp01((p[c] - 0.5) * (1 + def * 0.6) + 0.5) * (1 - hole * def * 0.4));
    }
    break; }
  case GX_UNDERPAINTING: {
    double cov = v[1] / 40.0;
    if (!(a = dupc(w))) break;
    gauss_n(w->c, w->w, w->h, 3, 0.5 + v[0] * 0.4);
    EACH { mix3(p, a + i * 3, p, 0.8); saturate3(p, 1.15); }
    texturize(w, e.list1, v[2], (int)(v[3] * (0.5 + cov * 1.5) + 0.5), e.list2, e.tick, 0);
    break; }
  case GX_WATERCOLOR: {
    double sh = v[1] / 10.0, amp = (v[2] - 1) * 0.04;
    kuwahara(w, watercolor_radius(&e));
    if (!(a = lum_plane(w)) || !(l = grad_mag(a, w->w, w->h, NULL, NULL))) break;
    EACH {
      double n = (fbm(X / 3.0, Y / 3.0, 71) - 0.5) * amp, k = 1 - clamp01(l[i] * (3 + sh * 8)) * 0.6;
      saturate3(p, 1.2);
      for (c = 0; c < 3; c++) p[c] = (float)pow(clamp01(p[c] * k + n), 1.1 + sh * 0.6);
    }
    break; }
  case GX_ACCENTED_EDGES: {
    double eb = v[1] / 50.0;
    if (!(l = lum_plane(w))) break;
    gauss_n(l, w->w, w->h, 1, 0.4 + v[2] * 0.3);
    if (!(m = grad_mag(l, w->w, w->h, NULL, NULL))) break;
    gauss_n(m, w->w, w->h, 1, v[0] * 0.5);
    EACH {
      double k = clamp01(m[i] * 6);
      for (c = 0; c < 3; c++) p[c] = (float)(eb >= 0.5 ? p[c] + (1 - p[c]) * k * (eb - 0.5) * 2 : p[c] * (1 - k * (0.5 - eb) * 2));
    }
    break; }
  case GX_ANGLED_STROKES: {
    double len = v[1] < 3 ? 3 : v[1], bal = v[0] / 100.0;
    float *soft = dupc(w);
    if (!soft) break;
    gauss_n(soft, w->w, w->h, 3, 0.8);
    EACH {
      float dn[3], up[3];
      double k = smoothstep(1 - bal - 0.05, 1 - bal + 0.05, lum3(soft + i * 3)), n;
      paint_stroke(w, soft, X, Y, 1, 1, len, 2, 83, dn);
      paint_stroke(w, soft, X, Y, 1, -1, len, 2, 84, up);
      n = (strokes(X, Y, 1, 1, len * 0.5, 0.7, 85) - 0.5) * (1 - k) + (strokes(X, Y, 1, -1, len * 0.5, 0.7, 86) - 0.5) * k;
      mix3(p, dn, up, k);
      for (c = 0; c < 3; c++) p[c] = (float)(p[c] + n * 0.1);
    }
    free(soft);
    unsharp(w, 1.0, 0.2 + v[2] / 10.0 * 2.3);
    break; }
  case GX_CROSSHATCH: {
    int r = v[0] / 3;
    EACH { double n = (hash01(X, Y, 0, 81) - 0.5) * 0.12 * v[2]; for (c = 0; c < 3; c++) p[c] = (float)(p[c] + n); }
    if (!(a = dupc(w))) break;
    line_blur(a, 3, w->w, w->h, 1, 1, r);
    line_blur3(w, 1, -1, r);
    EACH mix3(p, a + i * 3, p, 0.5);
    unsharp(w, 1.0, v[1] / 20.0 * 3);
    break; }
  case GX_DARK_STROKES: {
    double bal = v[0] / 10.0, bi = v[1] / 10.0, wi = v[2] / 10.0, t = 0.25 + bal * 0.5;
    if (!(l = lum_plane(w)) || !(a = dupc(w))) break;
    line_blur(a, 3, w->w, w->h, 1, 1, 3);
    line_blur3(w, 1, -1, 8);
    EACH {
      float d[3], li[3];
      for (c = 0; c < 3; c++) { d[c] = (float)(a[i * 3 + c] * (1 - bi * 0.85)); li[c] = (float)(p[c] + (1 - p[c]) * wi * 0.5); }
      mix3(p, d, li, smoothstep(t - 0.1, t + 0.1, l[i]));
    }
    break; }
  case GX_INK_OUTLINES: {
    double di = v[1] / 50.0, li = v[2] / 50.0;
    if (!(l = lum_plane(w)) || !(m = grad_mag(l, w->w, w->h, NULL, NULL))) break;
    line_blur3(w, 1, 1, v[0] / 2);
    EACH {
      double ink = clamp01(m[i] * 5) * di * 1.5, lt = smoothstep(0.5, 1, l[i]) * li, dk = smoothstep(0.5, 0, l[i]) * di * 0.8;
      for (c = 0; c < 3; c++) p[c] = (float)((p[c] + (1 - p[c]) * lt) * (1 - dk) * (1 - clamp01(ink)));
    }
    break; }
  case GX_SPRAYED_STROKES: {
    double rad = v[1], sc = 1.2;
    int dx, dy;
    if (!(a = dupc(w))) break;
    EACH {
      double ox = (vnoise(X / sc, Y / sc, 91) - 0.5) * 2 * rad, oy = (vnoise(X / sc, Y / sc, 92) - 0.5) * 2 * rad;
      sample(w, a, X + 0.5 + ox, Y + 0.5 + oy, p);
    }
    stroke_dir(e.list1, &dx, &dy);
    line_blur3(w, dx, dy, v[0] / 2);
    break; }
  case GX_SUMI_E: {
    double pr = v[1] / 15.0, con = v[2] / 40.0;
    gauss_n(w->c, w->w, w->h, 3, v[0] * 0.2);
    line_blur3(w, 1, 1, v[0] / 2);
    EACH {
      double ink = pow(smoothstep(0.65, 0.15, lum3(p)), 1 - pr * 0.5);
      for (c = 0; c < 3; c++) p[c] = (float)(clamp01((p[c] - 0.5) * (1 + con) + 0.5) * (1 - ink * 0.9 * (0.5 + con)));
    }
    break; }
  case GX_BAS_RELIEF: {
    double lx, ly, k = 3 + v[0] * 0.4;
    light_dir(e.list1, &lx, &ly);
    if (!(a = lum_plane(w)) || !(l = lum_plane(w))) break;
    gauss_n(l, w->w, w->h, 1, 0.3 + v[1] * 0.5 + (15 - v[0]) * 0.12);
    if (!(m = grad_mag(l, w->w, w->h, &gx, &gy))) break;
    EACH mix3(p, fg, bg, clamp01(a[i] * 0.5 + 0.25 + k * -(gx[i] * lx + gy[i] * ly)));
    break; }
  case GX_CHALK_CHARCOAL: {
    double ca = v[0] / 20.0, ch = v[1] / 20.0, pr = v[2] / 5.0;
    float mid[3];
    mix3(mid, fg, bg, 0.5);
    EACH {
      double lv = lum3(p), cn = strokes(X, Y, 1, 1, 9, 0.9, 121) - 0.5, kn = strokes(X, Y, 1, -1, 9, 0.9, 122) - 0.5;
      double cc = smoothstep(0.25, 0.6, (1 - lv) * (0.4 + ca * 1.2) + cn * (0.6 + pr * 0.4) - 0.15);
      double kc = smoothstep(0.25, 0.6, lv * (0.4 + ch * 1.2) + kn * (0.6 + pr * 0.4) - 0.15);
      mix3(p, mid, fg, cc); mix3(p, p, bg, kc * (1 - cc));
    }
    break; }
  case GX_CHARCOAL: {
    double th = v[0], det = v[1] / 5.0, bal = v[2] / 100.0;
    if (!(l = lum_plane(w))) break;
    gauss_n(l, w->w, w->h, 1, 1.0);
    if (!(m = grad_mag(l, w->w, w->h, NULL, NULL))) break;
    EACH {
      double sn = strokes(X, Y, 1, 1, 8 + th * 3, 0.6 + th * 0.5, 131) - 0.5;
      double cov = smoothstep(1 - bal - 0.2, 1 - bal + 0.2, 1 - l[i] + sn * 0.6) + clamp01(m[i] * det * 6);
      mix3(p, bg, fg, clamp01(cov));
    }
    break; }
  case GX_CHROME: {
    double det = v[0] / 10.0, sm = v[1] / 10.0;
    if (!(l = lum_plane(w))) break;
    gauss_n(l, w->w, w->h, 1, 1 + sm * 4);
    if (!(m = grad_mag(l, w->w, w->h, NULL, NULL))) break;
    EACH {
      double t = smoothstep(0.05, 0.95, 0.5 + 0.5 * sin((l[i] * (2.5 + det * 3) + m[i] * 6) * 2 * M_PI));
      p[0] = p[1] = p[2] = (float)t;
    }
    break; }
  case GX_CONTE_CRAYON: {
    double fl = v[0] / 15.0, bl = v[1] / 15.0;
    float mid[3];
    mix3(mid, fg, bg, 0.5);
    EACH {
      double lv = lum3(p), sn = strokes(X, Y, 1, -1, 7, 1, 141) - 0.5;
      double cf = clamp01((0.6 - lv) * 2 * fl * 1.4 + sn * 0.5), cb = clamp01((lv - 0.4) * 2 * bl * 1.4 - sn * 0.5);
      mix3(p, mid, fg, cf); mix3(p, p, bg, cb * (1 - cf));
    }
    texturize(w, e.list1, v[2], v[3], e.list2, e.tick, 0);
    break; }
  case GX_GRAPHIC_PEN: {
    int r = v[0] / 2, dx, dy;
    double bal = v[1] / 100.0, sd = 0.2887 / sqrt(2.0 * r + 1);
    if (!(l = lum_plane(w)) || !(m = planef(w))) break;
    EACH m[i] = (float)hash01(X, Y, 0, 151);
    stroke_dir(e.list1, &dx, &dy);
    line_blur(m, 1, w->w, w->h, dx, dy, r);
    EACH mix3(p, bg, fg, smoothstep(bal + 0.04, bal - 0.04, l[i] + (m[i] - 0.5) / sd * 0.16));
    break; }
  case GX_NOTE_PAPER: {
    double bal = v[0] / 50.0, gr = v[1] / 20.0, rel = v[2] / 25.0, lx, ly;
    light_dir(3, &lx, &ly);
    if (!(l = lum_plane(w)) || !(m = planef(w))) break;
    gauss_n(l, w->w, w->h, 1, 1.0);
    for (i = 0; i < (long)w->w * w->h; i++) m[i] = (float)smoothstep(bal - 0.05, bal + 0.05, l[i]);
    gauss_n(m, w->w, w->h, 1, 1.2);
    if (!(a = grad_mag(m, w->w, w->h, &gx, &gy))) break;
    EACH {
      double n = (hash01(X, Y, 0, 161) - 0.5) * gr * 0.3, f = shade(gx[i], gy[i], lx, ly, rel * 6);
      if (f < 0) f = 0;
      if (f > 2) f = 2;
      mix3(p, fg, bg, m[i]);
      for (c = 0; c < 3; c++) p[c] = (float)((p[c] + n) * f);
    }
    break; }
  case GX_PLASTER: {
    double bal = v[0] / 50.0, lx, ly;
    light_dir(e.list1, &lx, &ly);
    if (!(l = lum_plane(w)) || !(m = planef(w))) break;
    gauss_n(l, w->w, w->h, 1, 0.5 + v[1] * 0.8);
    for (i = 0; i < (long)w->w * w->h; i++) m[i] = (float)smoothstep(bal + 0.1, bal - 0.1, l[i]);
    gauss_n(m, w->w, w->h, 1, 1.5);
    if (!(a = grad_mag(m, w->w, w->h, &gx, &gy))) break;
    EACH {
      double f = shade(gx[i], gy[i], lx, ly, 6);
      if (f < 0) f = 0;
      if (f > 2) f = 2;
      mix3(p, bg, fg, m[i] * 0.6);
      for (c = 0; c < 3; c++) p[c] = (float)(p[c] * f);
    }
    break; }
  case GX_RETICULATION: {
    double den = v[0] / 50.0, fl = v[1] / 50.0, bl = v[2] / 50.0;
    float ink[3], paper[3];
    mix3(ink, bg, fg, 0.3 + fl * 0.7); mix3(paper, bg, fg, bl * 0.6);
    EACH {
      double n = 0.5 * hash01(X, Y, 0, 171) + 0.5 * vnoise(X / 1.8, Y / 1.8, 172), t = (1 - lum3(p)) + (n - 0.5) * (0.4 + den * 1.2);
      mix3(p, paper, ink, smoothstep(0.45, 0.55, t));
    }
    break; }
  case GX_STAMP: {
    double bal = v[0] / 50.0;
    if (!(l = lum_plane(w))) break;
    gauss_n(l, w->w, w->h, 1, 0.3 + v[1] * 0.3);
    EACH mix3(p, bg, fg, smoothstep(bal + 0.02, bal - 0.02, l[i]));
    break; }
  case GX_TORN_EDGES: {
    double bal = v[0] / 50.0, rough = (16 - v[1]) / 15.0 * 0.35, aa = 0.25 / v[2];
    if (!(l = lum_plane(w))) break;
    gauss_n(l, w->w, w->h, 1, 1.0);
    EACH mix3(p, bg, fg, smoothstep(bal + aa, bal - aa, l[i] + (fbm(X / 2.0, Y / 2.0, 181) - 0.5) * rough));
    break; }
  case GX_WATER_PAPER: {
    double fl = v[0], br = v[1] / 100.0, co = v[2] / 100.0;
    int r = (int)(fl / 6);
    line_blur3(w, 1, 0, r);
    line_blur3(w, 0, 1, r);
    EACH {
      double f1 = strokes(X, Y, 1, 0, fl, 1, 191), f2 = strokes(X, Y, 0, 1, fl, 1, 192), fib = f1 > f2 ? f1 : f2;
      for (c = 0; c < 3; c++) p[c] = (float)((p[c] * (0.85 + 0.25 * fib) - 0.5) * (co * 1.6 + 0.2) + 0.5 + (br - 0.5) * 0.8);
    }
    break; }
  case GX_CRAQUELURE: {
    double cell = v[0] * 1.3 + 1, dep = v[1] / 10.0, bri = v[2] / 10.0, width = 0.8 + dep * 0.8, lx, ly;
    light_dir(3, &lx, &ly);
    EACH {
      double fx = X + 0.5, fy = Y + 0.5, d1, d2, qx, qy, c0, cx, cy, f;
      voronoi(fx, fy, cell, 201, &d1, &d2, &qx, &qy); c0 = 1 - smoothstep(0, width, d2 - d1);
      voronoi(fx + 1, fy, cell, 201, &d1, &d2, &qx, &qy); cx = 1 - smoothstep(0, width, d2 - d1);
      voronoi(fx, fy + 1, cell, 201, &d1, &d2, &qx, &qy); cy = 1 - smoothstep(0, width, d2 - d1);
      f = shade(-(cx - c0), -(cy - c0), lx, ly, dep * 2);
      if (f < 0) f = 0;
      if (f > 2) f = 2;
      for (c = 0; c < 3; c++) p[c] = (float)(p[c] * (0.55 + bri * 0.5) * (1 - c0 * dep * 0.6) * f);
    }
    break; }
  case GX_GRAIN: {
    double it = v[0] / 100.0, co = v[1] / 100.0;
    EACH {
      float q[3];
      memcpy(q, p, sizeof q);
      switch (e.list1) {
      case 1: for (c = 0; c < 3; c++) q[c] = (float)(p[c] + (hash01(X, Y, c, 211) - 0.5) * it * 0.4); break;
      case 2: if (hash01(X, Y, 3, 211) < it * 0.15) memcpy(q, bg, sizeof q); break;
      case 3: { double n = (vnoise(X / 2.0, Y / 2.0, 212) - 0.5) * it * 1.2; for (c = 0; c < 3; c++) q[c] = (float)(p[c] + n); break; }
      case 4: { double n = (hash01(X, Y, 4, 211) - 0.5) * it * 0.5; for (c = 0; c < 3; c++) q[c] = (float)((p[c] - 0.5) * (1 + it * 2) + 0.5 + n); break; }
      case 5: for (c = 0; c < 3; c++) q[c] = (float)(p[c] + (vnoise(X / 3.0, Y / 3.0, 213 + c) - 0.5) * it); break;
      case 6: mix3(q, p, lum3(p) > hash01(X, Y, 5, 211) ? bg : fg, 0.3 + it * 0.7); break;
      case 7: { double n = (vnoise(X / 25.0, Y, 216) - 0.5) * it; for (c = 0; c < 3; c++) q[c] = (float)(p[c] + n); break; }
      case 8: { double n = (vnoise(X, Y / 25.0, 217) - 0.5) * it; for (c = 0; c < 3; c++) q[c] = (float)(p[c] + n); break; }
      case 9: if (hash01(X, Y, 6, 211) < it * 0.1) mix3(q, p, fg, 0.85); break;
      default: for (c = 0; c < 3; c++) q[c] = (float)(p[c] + (hash01(X, Y, c, 211) - 0.5) * it * 0.8); break;
      }
      for (c = 0; c < 3; c++) p[c] = (float)((q[c] - 0.5) * (0.5 + co) + 0.5);
    }
    break; }
  }
  free(l); free(m); free(a); free(gx); free(gy);
  /* (colours held to 0..1 between effects, as they would be in pixels) */
  for (i = 0; i < (long)w->w * w->h * 3; i++) w->c[i] = (float)clamp01(w->c[i]);
}
#undef EACH

/* ------------------------------------------------------------------ applying a stack */
static int stack_reach(const GalleryEffect *e, int n)
{
  long r = 0;
  int k;
  for (k = 0; k < n; k++) r += gallery_reach(&e[k]);
  return r > 256 ? 256 : (int)r;
}
#define TILE 128
long gallery_memory(Doc *d, const GalleryEffect *e, int n)
{
  int R = stack_reach(e, n), side = TILE + 2 * R;
  Box b = d->sel ? box_clip(d->selbox, doc_box(d)) : doc_box(d);
  long bw = b.x1 - b.x0, bh = b.y1 - b.y0;
  if (bw <= 0 || bh <= 0) return 0;
  /* the undo copy, two bands of source rows, a band of results, the window and its working planes */
  return bw * bh * 4 + 2L * side * (bw + 2 * R) * 4 + (long)TILE * bw * 4 + (long)side * side * 4 * 16;
}
/* window from w x h RGBA rows src (row stride sw pixels; at canvas sx, sy) */
static void win_load(Win *w, const u8 *src, long stride, i32 sx, i32 sy)
{
  int x, y, c;
  for (y = 0; y < w->h; y++) {
    const u8 *s = src + ((long)(w->y0 - sy + y) * stride + (w->x0 - sx)) * 4;
    float *o = w->c + (long)y * w->w * 3;
    for (x = 0; x < w->w; x++, s += 4, o += 3) for (c = 0; c < 3; c++) o[c] = (float)(s[c] / 255.0);
  }
}
static u8 to8(float v) { double t = clamp01(v) * 255 + 0.5; return (u8)(int)t; }
int gallery_run_rgba(u8 *px, i32 w, i32 h, i32 ox, i32 oy, const GalleryEffect *e, int n)
{
  Win wn;
  long i;
  int k;
  oom = 0;
  wn.x0 = ox; wn.y0 = oy; wn.w = w; wn.h = h;
  if (!(wn.c = (float *)gx_alloc((size_t)w * h * 3 * sizeof(float)))) return 0;
  win_load(&wn, px, w, ox, oy);
  for (k = 0; k < n && !oom; k++) run_effect(&e[k], &wn);
  if (!oom) for (i = 0; i < (long)w * h; i++) { px[i * 4] = to8(wn.c[i * 3]); px[i * 4 + 1] = to8(wn.c[i * 3 + 1]); px[i * 4 + 2] = to8(wn.c[i * 3 + 2]); }
  free(wn.c);
  return !oom;
}
int flt_gallery(Doc *d, const GalleryEffect *e, int n, const char *name)
{
  Layer *l;
  Box b;
  int R, k, changed = 0, aborted = 0;
  i32 y, cx0, cx1, bw, old_y0 = 0, old_y1 = 0;
  long cw, tiles, done = 0;
  u8 *oldb = NULL, *newb = NULL, *outb = NULL;
  Win wn;
  if (n < 1 || n > GX_STACK || d->active < 0 || d->active >= d->nlay) return 0;
  l = d->lay[d->active];
  if (!l->px || l->locked || l->lock_all || !l->visible) return 0;
  b = d->sel ? box_clip(d->selbox, doc_box(d)) : doc_box(d);
  if (box_empty(b)) return 0;
  oom = 0;
  R = stack_reach(e, n);
  cx0 = b.x0 - R < 0 ? 0 : b.x0 - R; cx1 = b.x1 + R > d->w ? d->w : b.x1 + R; cw = cx1 - cx0; bw = b.x1 - b.x0;
  oldb = (u8 *)malloc((size_t)(TILE + 2 * R) * cw * 4);
  newb = (u8 *)malloc((size_t)(TILE + 2 * R) * cw * 4);
  outb = (u8 *)malloc((size_t)TILE * bw * 4);
  wn.c = (float *)malloc((size_t)(TILE + 2 * R) * (TILE + 2 * R) * 3 * sizeof(float));
  if (!oldb || !newb || !outb || !wn.c) { free(oldb); free(newb); free(outb); free(wn.c); return -1; }
  doc_begin(d, name ? name : gallery_defs[clampi(e[0].fx, 0, GX_N - 1)].name);
  doc_note_layer(d, d->active, b);
  tiles = (long)((b.y1 - b.y0 + TILE - 1) / TILE) * ((bw + TILE - 1) / TILE);
  for (y = b.y0; y < b.y1 && !aborted && !oom; y += TILE) {
    i32 yb1 = y + TILE < b.y1 ? y + TILE : b.y1, sy0 = y - R < 0 ? 0 : y - R, sy1 = yb1 + R > d->h ? d->h : yb1 + R, r, tx;
    u8 *t;
    /* this band's source rows as they were: those above it from the last band's copy */
    for (r = sy0; r < sy1; r++) {
      const u8 *s = r < y && oldb && r >= old_y0 && r < old_y1 ? oldb + (long)(r - old_y0) * cw * 4 : l->px + ((long)r * d->w + cx0) * 4;
      memcpy(newb + (long)(r - sy0) * cw * 4, s, (size_t)cw * 4);
    }
    for (tx = b.x0; tx < b.x1 && !oom; tx += TILE) {
      i32 tx1 = tx + TILE < b.x1 ? tx + TILE : b.x1, xx, yy;
      if (progress(done++, tiles)) { aborted = 1; break; }
      wn.x0 = tx - R < 0 ? 0 : tx - R; wn.y0 = sy0;
      wn.w = (tx1 + R > d->w ? d->w : tx1 + R) - wn.x0; wn.h = sy1 - sy0;
      win_load(&wn, newb, cw, cx0, sy0);
      for (k = 0; k < n && !oom; k++) run_effect(&e[k], &wn);
      for (yy = y; yy < yb1; yy++)
        for (xx = tx; xx < tx1; xx++) {
          const float *c = wn.c + ((long)(yy - wn.y0) * wn.w + (xx - wn.x0)) * 3;
          u8 *o = outb + ((long)(yy - y) * bw + (xx - b.x0)) * 4;
          o[0] = to8(c[0]); o[1] = to8(c[1]); o[2] = to8(c[2]);
        }
    }
    if (aborted || oom) break;
    /* the band written, blended with the selection; the transparency kept */
    for (r = y; r < yb1; r++) {
      i32 x;
      for (x = b.x0; x < b.x1; x++) {
        long kk = (long)r * d->w + x;
        int s = d->sel ? d->sel[kk] : 255, c;
        const u8 *q = newb + ((long)(r - sy0) * cw + (x - cx0)) * 4;
        u8 *o = outb + ((long)(r - y) * bw + (x - b.x0)) * 4, *dst = l->px + kk * 4;
        if (!s) continue;
        for (c = 0; c < 3; c++) {
          int v = s == 255 ? o[c] : q[c] + ((o[c] - q[c]) * s + (o[c] >= q[c] ? 127 : -127)) / 255;
          if (dst[c] != v) changed = 1;
          dst[c] = (u8)v;
        }
      }
    }
    t = oldb; oldb = newb; newb = t; old_y0 = sy0; old_y1 = sy1;
  }
  free(oldb); free(newb); free(outb); free(wn.c);
  doc_end(d);
  if (oom) return -1;
  return aborted ? 0 : changed;
}
