/*
 * viewx.c - Easel 1.2: pixel aspect presets, guide layouts, zoom helpers, Trim's options and the
 * canvas's looks (see viewx.h).
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "viewx.h"
#include "guides.h"
#include "layers.h"
#include "paint.h"

/* ------------------------------------------------------------------ pixel aspect ratio */
/* (the video ones as PhotoCraft and Photoshop have them; the Amiga ones are the BMHD aspects of
   amigamodes.c's PAL and NTSC modes) */
const ParPreset par_presets[PAR_N] = {
  { "Square", 1, 1 },
  { "D1/DV NTSC (0.91)", 10, 11 },
  { "D1/DV PAL (1.09)", 59, 54 },
  { "D1/DV NTSC Widescreen (1.21)", 40, 33 },
  { "HDV 1080/DVCPRO HD 720 (1.33)", 4, 3 },
  { "D1/DV PAL Widescreen (1.46)", 118, 81 },
  { "DVCPRO HD 1080 (1.5)", 3, 2 },
  { "Anamorphic 2:1 (2)", 2, 1 },
  { "Amiga PAL Low Res (10:11)", 10, 11 },
  { "Amiga PAL High Res (5:11)", 5, 11 },
  { "Amiga PAL Low Res Laced (20:11)", 20, 11 },
  { "Amiga NTSC Low Res (44:52)", 44, 52 },
  { "Amiga NTSC High Res (22:52)", 22, 52 }
};
void par_approx(double r, u8 *ax, u8 *ay)
{
  int q, bp = 1, bq = 1;
  double be = 1e30;
  if (!(r > 0)) r = 1;
  if (r > 255) r = 255;
  if (r < 1.0 / 255) r = 1.0 / 255;
  for (q = 1; q <= 255; q++) {
    int p = (int)floor(r * q + 0.5);
    double e;
    if (p < 1 || p > 255) continue;
    e = fabs((double)p / q - r);
    if (e < be - 1e-12) { be = e; bp = p; bq = q; }
  }
  *ax = (u8)bp; *ay = (u8)bq;
}
int par_set(Doc *d, int ax, int ay)
{
  if (ax < 1 || ay < 1 || ax > 255 || ay > 255) return 0;
  if (d->aspect_x && d->aspect_y && (long)d->aspect_x * ay == (long)d->aspect_y * ax) return 0;
  if (!d->aspect_x && !d->aspect_y && ax == ay) return 0;
  doc_begin(d, "Pixel Aspect Ratio");
  doc_note_pal(d);
  d->aspect_x = (u8)ax; d->aspect_y = (u8)ay;
  doc_end(d);
  return 1;
}
int par_find(const Doc *d)
{
  int i;
  long x = d->aspect_x ? d->aspect_x : 1, y = d->aspect_y ? d->aspect_y : 1;
  for (i = 0; i < PAR_N; i++) if (x * par_presets[i].ay == y * par_presets[i].ax) return i;
  return -1;
}
double par_view_ratio(const Doc *d, int correct)
{
  if (!correct || !d || !d->aspect_x || !d->aspect_y) return 1.0;
  return (double)d->aspect_y / d->aspect_x;
}

/* ------------------------------------------------------------------ guides */
static int add_g(Doc *d, int vertical, double pos)
{
  long p = (long)floor(pos + 0.5), size = vertical ? d->w : d->h;
  if (p < 0) p = 0;
  if (p > size) p = size;
  return guide_add(d, vertical, (i32)p);
}
/* one direction's guides: the space between the margins, n bands of width w (0 shared out) with gutters */
static int layout_axis(Doc *d, int vertical, int m0, int m1, int n, int w, int gut, int centre)
{
  long size = vertical ? d->w : d->h;
  double a0 = m0 > 0 ? m0 : 0, a1 = size - (m1 > 0 ? m1 : 0), bw, start;
  int k, added = 0;
  if (gut < 0) gut = 0;
  if (a1 < a0) a1 = a0;
  if (m0 > 0) added += add_g(d, vertical, a0);
  if (m1 > 0) added += add_g(d, vertical, a1);
  if (n <= 0) return added;
  if (n > MAXGUIDES / 2) n = MAXGUIDES / 2;
  if (w > 0) {
    double total = (double)n * w + (double)gut * (n - 1);
    bw = w;
    start = centre ? a0 + (a1 - a0 - total) / 2 : a0;
  } else {
    bw = (a1 - a0 - (double)gut * (n - 1)) / n;
    if (bw < 0) bw = 0;
    start = a0;
  }
  for (k = 0; k < n; k++) {
    double l = start + k * (bw + gut);
    added += add_g(d, vertical, l);
    added += add_g(d, vertical, l + bw);
  }
  return added;
}
int guide_layout(Doc *d, const GuideLayout *g)
{
  int n = 0;
  if (g->clear) guide_clear(d);
  n += layout_axis(d, 1, g->mleft, g->mright, g->cols, g->colw, g->colgut, g->centre);
  n += layout_axis(d, 0, g->mtop, g->mbottom, g->rows, g->rowh, g->rowgut, g->centre);
  if (n) d->guides.show_guides = 1;
  return n;
}
int guides_from_box(Doc *d, Box b)
{
  int n = 0;
  if (box_empty(b)) return 0;
  n += add_g(d, 1, b.x0); n += add_g(d, 1, b.x1); n += add_g(d, 1, (b.x0 + b.x1) / 2.0);
  n += add_g(d, 0, b.y0); n += add_g(d, 0, b.y1); n += add_g(d, 0, (b.y0 + b.y1) / 2.0);
  if (n) d->guides.show_guides = 1;
  return n;
}
int guide_shape_box(Doc *d, Box *b)
{
  if (d->nlay && layer_content_box(d, d->active, b)) return 1;
  if (d->sel && !box_empty(d->selbox)) { *b = d->selbox; return 1; }
  return 0;
}

/* ------------------------------------------------------------------ zoom */
double zoom_print_size(int doc_dpi, int screen_dpi)
{
  if (doc_dpi < 1) doc_dpi = 72;
  if (screen_dpi < 1) screen_dpi = 72;
  return (double)screen_dpi / doc_dpi;
}
double zoom_fit_box(Box b, long W, long H, double ratio)
{
  double zx, zy;
  if (box_empty(b) || W <= 16 || H <= 16) return 1;
  if (!(ratio > 0)) ratio = 1;
  zx = (double)(W - 8) / (b.x1 - b.x0);
  zy = (double)(H - 8) / ((b.y1 - b.y0) * ratio);
  return zx < zy ? zx : zy;
}
int fit_layers_box(Doc *d, Box *out)
{
  int i, any = 0, np = doc_npicked(d);
  Box u = box_make(0, 0, 0, 0), b;
  for (i = 0; i < d->nlay; i++)
    if ((np ? d->lay[i]->picked || i == d->active : i == d->active) && layer_content_box(d, i, &b)) { u = any ? box_union(u, b) : b; any = 1; }
  if (any) *out = u;
  return any;
}

/* ------------------------------------------------------------------ Trim */
int trim_box(Doc *d, int based_on, int sides, Box *out)
{
  u8 *img = (u8 *)malloc((size_t)d->w * d->h * 4);
  u8 ref[4];
  i32 x, y, x0 = d->w, y0 = d->h, x1 = 0, y1 = 0;
  Box b;
  if (!img) return 0;
  doc_composite(d, doc_box(d), img, (long)d->w * 4);
  if (based_on == TRIM_TOPLEFT) memcpy(ref, img, 4);
  else if (based_on == TRIM_BOTTOMRIGHT) memcpy(ref, img + ((size_t)d->w * d->h - 1) * 4, 4);
  for (y = 0; y < d->h; y++) {
    const u8 *p = img + (size_t)y * d->w * 4;
    for (x = 0; x < d->w; x++, p += 4) {
      int bg = based_on == TRIM_TRANSPARENT ? p[3] == 0 : (p[3] == 0 && ref[3] == 0) || !memcmp(p, ref, 4);
      if (bg) continue;
      if (x < x0) x0 = x;
      if (x >= x1) x1 = x + 1;
      if (y < y0) y0 = y;
      y1 = y + 1;
    }
  }
  free(img);
  if (x1 <= x0) return 0;                  /* (all of it the same: nothing would be left) */
  b = box_make(sides & TRIM_LEFT ? x0 : 0, sides & TRIM_TOP ? y0 : 0, sides & TRIM_RIGHT ? x1 : d->w, sides & TRIM_BOTTOM ? y1 : d->h);
  if (b.x0 == 0 && b.y0 == 0 && b.x1 == d->w && b.y1 == d->h) return 0;
  *out = b;
  return 1;
}
int image_trim_ex(Doc *d, int based_on, int sides)
{
  Box b;
  if (!trim_box(d, based_on, sides, &b)) return 0;
  return image_crop(d, b);
}

/* ------------------------------------------------------------------ the canvas's looks */
const char *const checker_names[CHK_N] = { "Light", "Medium", "Dark", "Red", "Orange", "Green", "Blue", "Purple", "Custom" };
const char *const checker_size_names[5] = { "None", "Small", "Medium", "Large", NULL };
u32 checker_at(long x, long y, int size, int scheme, u32 c1, u32 c2)
{
  static const u32 sc[CHK_N - 1][2] = {
    { 0xFFFFFF, 0xCCCCCC }, { 0x999999, 0x666666 }, { 0x666666, 0x333333 }, { 0xFFFFFF, 0xFFC4C4 },
    { 0xFFFFFF, 0xFFE0B0 }, { 0xFFFFFF, 0xC8F0C8 }, { 0xFFFFFF, 0xC4D8FF }, { 0xFFFFFF, 0xE6C8FF }
  };
  int sh, odd;
  if (scheme < 0 || scheme >= CHK_N) scheme = CHK_LIGHT;
  if (scheme != CHK_CUSTOM) { c1 = sc[scheme][0]; c2 = sc[scheme][1]; }
  if (size <= 0) return c1;
  sh = size == 1 ? 2 : size == 2 ? 3 : 4;
  odd = (int)(((x >> sh) + (y >> sh)) & 1);
  return odd ? c2 : c1;
}
const char *const line_style_names[LS_N + 1] = { "Lines", "Dashed Lines", "Dots", NULL };
int line_style_on(int style, long p)
{
  if (p < 0) p = -p;
  if (style == LS_DASHED) return (p & 7) < 4;
  if (style == LS_DOTS) return (p & 3) == 0;
  return 1;
}
int pixgrid_line(long s, long o, double z)
{
  double a, b;
  if (z < PIXGRID_MIN_ZOOM) return 0;
  a = floor((s - o) / z); b = floor((s - 1 - o) / z);
  return a != b;
}
long wrap_coord(long v, long size)
{
  long r;
  if (size <= 0) return 0;
  r = v % size;
  return r < 0 ? r + size : r;
}
