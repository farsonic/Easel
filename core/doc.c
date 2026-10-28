/*
 * doc.c - the document: layers, compositing with blend modes, and undo (see doc.h).
 *
 * Undo keeps, for each step, the tiles (64 x 64 pixels) of each layer as they were before the step
 * first touched them, so a brush stroke costs only the tiles it crossed. Undoing swaps those tiles
 * with the layer's, so the same data serves to redo. Changes to the stack itself (layers added,
 * deleted, moved, the canvas resized) keep a copy of the whole document instead.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "doc.h"
#include "palette.h"

const char *const blend_names[BM_N] = { "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "Difference", "Linear Dodge (Add)",
  "Subtract", "Color Dodge", "Color Burn", "Soft Light", "Hard Light", "Exclusion", "Hue", "Saturation", "Color", "Luminosity",
  "Dissolve", "Linear Burn", "Darker Color", "Lighter Color", "Vivid Light", "Linear Light", "Pin Light", "Hard Mix", "Divide" };

/* ------------------------------------------------------------------ boxes */
Box box_make(i32 x0, i32 y0, i32 x1, i32 y1) { Box b; b.x0 = x0; b.y0 = y0; b.x1 = x1; b.y1 = y1; return b; }
int box_empty(Box b) { return b.x1 <= b.x0 || b.y1 <= b.y0; }
Box box_clip(Box a, Box b)
{
  Box r;
  r.x0 = a.x0 > b.x0 ? a.x0 : b.x0; r.y0 = a.y0 > b.y0 ? a.y0 : b.y0;
  r.x1 = a.x1 < b.x1 ? a.x1 : b.x1; r.y1 = a.y1 < b.y1 ? a.y1 : b.y1;
  if (box_empty(r)) r.x1 = r.x0, r.y1 = r.y0;
  return r;
}
Box box_union(Box a, Box b)
{
  Box r;
  if (box_empty(a)) return b;
  if (box_empty(b)) return a;
  r.x0 = a.x0 < b.x0 ? a.x0 : b.x0; r.y0 = a.y0 < b.y0 ? a.y0 : b.y0;
  r.x1 = a.x1 > b.x1 ? a.x1 : b.x1; r.y1 = a.y1 > b.y1 ? a.y1 : b.y1;
  return r;
}
Box doc_box(Doc *d) { return box_make(0, 0, d->w, d->h); }

/* ------------------------------------------------------------------ layers */
Layer *layer_new(Doc *d, const char *name)
{
  Layer *l = (Layer *)calloc(1, sizeof *l);
  if (!l) return NULL;
  if (!(l->px = (u8 *)calloc((size_t)d->w * d->h, 4))) { free(l); return NULL; }
  strncpy(l->name, name ? name : "Layer", sizeof l->name - 1);
  l->opacity = 255; l->visible = 1;
  return l;
}
void layer_free(Layer *l)
{
  if (!l) return;
  free(l->px);
  free(l->mask);
  free(l->idx);
  free(l->maskfx);
  if (l->adjprog) { if (adjlayer_free) adjlayer_free(l->adjprog); else free(l->adjprog); }
  free(l);
}
Layer *layer_copy(const Layer *l, i32 w, i32 h)
{
  Layer *c = (Layer *)malloc(sizeof *c);
  if (!c) return NULL;
  *c = *l;
  c->px = c->mask = c->idx = c->maskfx = NULL; c->maskfx_ok = 0;
  c->adjprog = NULL; c->adjprog_ok = 0;
  if (l->px && !(c->px = (u8 *)malloc((size_t)w * h * 4))) { free(c); return NULL; }
  if (l->px) memcpy(c->px, l->px, (size_t)w * h * 4);
  if (l->mask && (c->mask = (u8 *)malloc((size_t)w * h)) != NULL) memcpy(c->mask, l->mask, (size_t)w * h);
  if (l->idx && (c->idx = (u8 *)malloc((size_t)w * h)) != NULL) memcpy(c->idx, l->idx, (size_t)w * h);
  if ((l->mask && !c->mask) || (l->idx && !c->idx)) { layer_free(c); return NULL; }
  return c;
}
Doc *doc_new(i32 w, i32 h, u32 fill, int transparent)
{
  Doc *d;
  Layer *l;
  long i;
  if (w < 1 || h < 1 || w > MAXDIM || h > MAXDIM) return NULL;
  if (!(d = (Doc *)calloc(1, sizeof *d))) return NULL;
  d->w = w; d->h = h; d->fg = 0x000000; d->bg = 0xFFFFFF; d->dpi = 72; d->aspect_x = d->aspect_y = 1;
  if (!(l = layer_new(d, transparent ? "Layer 1" : "Background"))) { free(d); return NULL; }
  if (!transparent)
    for (i = 0; i < (long)w * h; i++) { u8 *p = l->px + i * 4; p[0] = (u8)(fill >> 16); p[1] = (u8)(fill >> 8); p[2] = (u8)fill; p[3] = 255; }
  d->lay[0] = l; d->nlay = 1;
  return d;
}
Doc *doc_adopt(i32 w, i32 h, u8 *px)
{
  Doc *d;
  Layer *l;
  if (w < 1 || h < 1 || w > MAXDIM || h > MAXDIM || !px) return NULL;
  if (!(d = (Doc *)calloc(1, sizeof *d))) return NULL;
  if (!(l = (Layer *)calloc(1, sizeof *l))) { free(d); return NULL; }
  d->w = w; d->h = h; d->fg = 0x000000; d->bg = 0xFFFFFF; d->dpi = 72; d->aspect_x = d->aspect_y = 1;
  l->px = px;
  strcpy(l->name, "Background");
  l->opacity = 255; l->visible = 1;
  d->lay[0] = l; d->nlay = 1;
  return d;
}
void doc_free(Doc *d)
{
  int i;
  if (!d) return;
  doc_forget_undo(d);
  for (i = 0; i < d->nlay; i++) layer_free(d->lay[i]);
  free(d->sel);
  free(d->lastsel);
  for (i = 0; i < d->nchan; i++) free(d->chan[i]);     /* 0.7 */
  free(d->qmask);
  free(d);
}

/* ------------------------------------------------------------------ blending */
static int mul255(int a, int b) { int t = a * b + 128; return (t + (t >> 8)) >> 8; }
/* one channel of a separable blend mode: cb backdrop, cs source (0-255) */
static int blend_ch(int mode, int cb, int cs)
{
  switch (mode) {
  case BM_MULTIPLY: return mul255(cb, cs);
  case BM_SCREEN: return cb + cs - mul255(cb, cs);
  case BM_OVERLAY: return cb < 128 ? mul255(2 * cb, cs) : 255 - mul255(2 * (255 - cb), 255 - cs);
  case BM_DARKEN: return cb < cs ? cb : cs;
  case BM_LIGHTEN: return cb > cs ? cb : cs;
  case BM_DIFFERENCE: return cb > cs ? cb - cs : cs - cb;
  case BM_ADD: return cb + cs > 255 ? 255 : cb + cs;
  case BM_SUBTRACT: return cb - cs < 0 ? 0 : cb - cs;
  case BM_DODGE: return cb == 0 ? 0 : cs >= 255 ? 255 : (cb * 255 / (255 - cs) > 255 ? 255 : cb * 255 / (255 - cs));
  case BM_BURN: return cb >= 255 ? 255 : cs == 0 ? 0 : (255 - (255 - cb) * 255 / cs < 0 ? 0 : 255 - (255 - cb) * 255 / cs);
  case BM_HARDLIGHT: return cs < 128 ? mul255(2 * cs, cb) : 255 - mul255(2 * (255 - cs), 255 - cb);
  case BM_SOFTLIGHT: {
    /* the W3C formula */
    double b = cb / 255.0, s = cs / 255.0, r, dd;
    if (s <= 0.5) r = b - (1 - 2 * s) * b * (1 - b);
    else { dd = b <= 0.25 ? ((16 * b - 12) * b + 4) * b : sqrt(b); r = b + (2 * s - 1) * (dd - b); }
    return (int)(r * 255 + 0.5);
  }
  case BM_EXCLUSION: return cb + cs - 2 * mul255(cb, cs);
  default: return cs;
  }
}
/* the non-separable modes (W3C compositing): luminosity and saturation of RGB 0-1 */
static double lum(const double *c) { return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]; }
static void clip_color(double *c)
{
  double l = lum(c), n = c[0], x = c[0];
  int i;
  for (i = 1; i < 3; i++) { if (c[i] < n) n = c[i]; if (c[i] > x) x = c[i]; }
  for (i = 0; i < 3; i++) {
    if (n < 0) c[i] = l + (c[i] - l) * l / (l - n);
    if (x > 1) c[i] = l + (c[i] - l) * (1 - l) / (x - l);
  }
}
static void set_lum(double *c, double l) { double d = l - lum(c); c[0] += d; c[1] += d; c[2] += d; clip_color(c); }
static double sat(const double *c)
{
  double n = c[0], x = c[0];
  if (c[1] < n) n = c[1];
  if (c[2] < n) n = c[2];
  if (c[1] > x) x = c[1];
  if (c[2] > x) x = c[2];
  return x - n;
}
static void set_sat(double *c, double s)
{
  int imax = 0, imin = 0, imid, i;
  for (i = 1; i < 3; i++) { if (c[i] > c[imax]) imax = i; if (c[i] < c[imin]) imin = i; }
  if (imax == imin) { c[0] = c[1] = c[2] = 0; return; }
  imid = 3 - imax - imin;
  c[imid] = (c[imid] - c[imin]) * s / (c[imax] - c[imin]);
  c[imax] = s; c[imin] = 0;
}
static void blend_nonsep(int mode, const u8 *b8, const u8 *s8, int *out)
{
  double b[3], s[3], r[3];
  int i;
  for (i = 0; i < 3; i++) { b[i] = b8[i] / 255.0; s[i] = s8[i] / 255.0; }
  switch (mode) {
  case BM_HUE: r[0] = s[0]; r[1] = s[1]; r[2] = s[2]; set_sat(r, sat(b)); set_lum(r, lum(b)); break;
  case BM_SATURATION: r[0] = b[0]; r[1] = b[1]; r[2] = b[2]; set_sat(r, sat(s)); set_lum(r, lum(b)); break;
  case BM_COLOR: r[0] = s[0]; r[1] = s[1]; r[2] = s[2]; set_lum(r, lum(b)); break;
  default: r[0] = b[0]; r[1] = b[1]; r[2] = b[2]; set_lum(r, lum(s)); break;
  }
  for (i = 0; i < 3; i++) { int v = (int)(r[i] * 255 + 0.5); out[i] = v < 0 ? 0 : v > 255 ? 255 : v; }
}
/* 0.2.1: the rest of the separable modes (W3C and Photoshop definitions), cb backdrop, cs source */
static int blend_ch2(int mode, int cb, int cs)
{
  int v;
  switch (mode) {
  case BM_LINEARBURN: v = cb + cs - 255; return v < 0 ? 0 : v;
  case BM_VIVIDLIGHT:
    /* Color Burn with 2 cs below the middle, Color Dodge with 2 cs - 255 above it */
    if (cs < 128) {
      if (cb >= 255) return 255;
      if (cs == 0) return 0;
      v = 255 - ((255 - cb) * 255 + cs) / (2 * cs);
      return v < 0 ? 0 : v;
    }
    if (cb == 0) return 0;
    if (cs >= 255) return 255;
    v = (cb * 255 + (255 - cs)) / (2 * (255 - cs));
    return v > 255 ? 255 : v;
  case BM_LINEARLIGHT: v = cb + 2 * cs - 255; return v < 0 ? 0 : v > 255 ? 255 : v;
  case BM_PINLIGHT:
    if (cs < 128) return cb < 2 * cs ? cb : 2 * cs;
    return cb > 2 * cs - 255 ? cb : 2 * cs - 255;
  case BM_HARDMIX: return cb + cs >= 255 ? 255 : 0;
  case BM_DIVIDE:
    if (cs == 0) return cb == 0 ? 0 : 255;
    v = (cb * 255 + cs / 2) / cs;
    return v > 255 ? 255 : v;
  default: return cs;
  }
}
/* Darker Color and Lighter Color: whichever whole colour has the lower (higher) luminosity */
static void blend_pick(int mode, const u8 *b, const u8 *s, int *out)
{
  int lb = 30 * b[0] + 59 * b[1] + 11 * b[2], ls = 30 * s[0] + 59 * s[1] + 11 * s[2], i;
  const u8 *c = (mode == BM_DARKERCOLOR ? ls < lb : ls > lb) ? s : b;
  for (i = 0; i < 3; i++) out[i] = c[i];
}
/* Dissolve: a pixel is either all there or not at all, at random (a hash of the position, so the
   pattern holds still while painting and only fills in as the opacity rises) */
static int dissolve_noise(i32 x, i32 y)
{
  u32 h = (u32)x * 0x8DA6B343UL ^ (u32)y * 0xD8163841UL ^ 0x9E3779B9UL;
  h ^= h >> 15; h *= 0x2C1B3C6DUL; h ^= h >> 12; h *= 0x297A2D39UL; h ^= h >> 15;
  return (int)(h & 255);
}
void blend_pixel_at(u8 *dst, const u8 *src, int opacity, int mode, i32 x, i32 y)
{
  if (mode == BM_DISSOLVE) {
    u8 s[4];
    int as = mul255(src[3], opacity);
    if (as == 0 || dissolve_noise(x, y) >= as) return;
    s[0] = src[0]; s[1] = src[1]; s[2] = src[2]; s[3] = 255;
    blend_pixel(dst, s, 255, BM_NORMAL);
    return;
  }
  blend_pixel(dst, src, opacity, mode);
}
void blend_pixel(u8 *dst, const u8 *src, int opacity, int mode)
{
  int as = mul255(src[3], opacity), ab = dst[3], ao, i;
  if (mode == BM_DISSOLVE || mode < 0 || mode >= BM_N) mode = BM_NORMAL;    /* (no position: see blend_pixel_at) */
  if (as == 0) return;
  if (ab == 0) { dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = (u8)as; return; }
  if (mode == BM_NORMAL && as == 255) { dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 255; return; }
  ao = as + ab - mul255(as, ab);
  if (mode == BM_NORMAL) {
    for (i = 0; i < 3; i++) dst[i] = (u8)((src[i] * as + dst[i] * (ab - mul255(as, ab)) + ao / 2) / ao);
  } else {
    int bl[3];
    if (mode >= BM_HUE && mode <= BM_LUMINOSITY) blend_nonsep(mode, dst, src, bl);
    else if (mode == BM_DARKERCOLOR || mode == BM_LIGHTERCOLOR) blend_pick(mode, dst, src, bl);
    else if (mode >= BM_LINEARBURN) for (i = 0; i < 3; i++) bl[i] = blend_ch2(mode, dst[i], src[i]);
    else for (i = 0; i < 3; i++) bl[i] = blend_ch(mode, dst[i], src[i]);
    /* the blend where both are opaque, the source alone where the backdrop isn't, the backdrop alone where the source isn't */
    for (i = 0; i < 3; i++) {
      long v = (long)mul255(as, 255 - ab) * src[i] + (long)mul255(as, ab) * bl[i] + (long)(ab - mul255(as, ab)) * dst[i];
      dst[i] = (u8)((v + ao / 2) / ao);
    }
  }
  dst[3] = (u8)ao;
}
/* ------------------------------------------------------------------ compositing (0.3: live layers)
   The stack is flat, bottom first. A group's members are the layers just below it whose depth is
   greater than the group's. Each row is composited by comp_range: layers are blended onto an
   accumulating row; a group is composited into a row of its own (isolated) and then blended, or
   straight onto the accumulation (Pass Through); a layer with clipped layers above it is
   composited with them into a row of its own first, the clipped layers keeping to its alpha; an
   adjustment layer changes the accumulation itself. Masks, fill opacity and Blend If thin each
   pixel's opacity. Rows come from a small pool, as deep as the nesting. */

/* one pixel's colour (not alpha) blended by mode: b backdrop, s source, out */
static void blend_color(int mode, const u8 *b, const u8 *s, int *out)
{
  int i;
  if (mode == BM_NORMAL) { out[0] = s[0]; out[1] = s[1]; out[2] = s[2]; return; }
  if (mode >= BM_HUE && mode <= BM_LUMINOSITY) { blend_nonsep(mode, b, s, out); return; }
  if (mode == BM_DARKERCOLOR || mode == BM_LIGHTERCOLOR) { blend_pick(mode, b, s, out); return; }
  if (mode >= BM_LINEARBURN && mode < BM_N) { for (i = 0; i < 3; i++) out[i] = blend_ch2(mode, b[i], s[i]); return; }
  if (mode == BM_DISSOLVE || mode >= BM_N) { out[0] = s[0]; out[1] = s[1]; out[2] = s[2]; return; }
  for (i = 0; i < 3; i++) out[i] = blend_ch(mode, b[i], s[i]);
}
/* the layer's mask, feathered if it asks (worked out once, kept until the mask changes) */
static void box_blur_u8(u8 *m, i32 w, i32 h, int r)
{
  u8 *t = (u8 *)malloc((size_t)(w > h ? w : h));
  i32 x, y;
  if (!t || r < 1) { free(t); return; }
  for (y = 0; y < h; y++) {
    u8 *row = m + (long)y * w;
    long sum = 0;
    for (x = -r; x <= r; x++) sum += row[x < 0 ? 0 : x >= w ? w - 1 : x];
    for (x = 0; x < w; x++) {
      i32 a = x - r, b = x + r + 1;
      t[x] = (u8)(sum / (2 * r + 1));
      sum += row[b >= w ? w - 1 : b] - row[a < 0 ? 0 : a];
    }
    memcpy(row, t, (size_t)w);
  }
  for (x = 0; x < w; x++) {
    long sum = 0;
    for (y = -r; y <= r; y++) sum += m[(long)(y < 0 ? 0 : y >= h ? h - 1 : y) * w + x];
    for (y = 0; y < h; y++) {
      i32 a = y - r, b = y + r + 1;
      t[y] = (u8)(sum / (2 * r + 1));
      sum += m[(long)(b >= h ? h - 1 : b) * w + x] - m[(long)(a < 0 ? 0 : a) * w + x];
    }
    for (y = 0; y < h; y++) m[(long)y * w + x] = t[y];
  }
  free(t);
}
const u8 *layer_mask_used(Doc *d, Layer *l)
{
  if (!l->mask || !l->mask_on) return NULL;
  if (!l->mask_feather) return l->mask;
  if (!l->maskfx_ok) {
    int pass, r = (l->mask_feather + 1) / 2;
    if (!l->maskfx && !(l->maskfx = (u8 *)malloc((size_t)d->w * d->h))) return l->mask;
    memcpy(l->maskfx, l->mask, (size_t)d->w * d->h);
    for (pass = 0; pass < 3; pass++) box_blur_u8(l->maskfx, d->w, d->h, r > 1 ? r / 2 + 1 : 1);
    l->maskfx_ok = 1;
  }
  return l->maskfx;
}
/* hooks for adjustment layers (adjust.c): set by adjust.c's adjlayer_init, so doc.c needs no
   adjustment code of its own (the tests that link doc.c alone see plain layers) */
void *(*adjlayer_make)(const AdjParams *p);
void (*adjlayer_free)(void *prog);
void (*adjlayer_row)(const void *prog, u8 *px, int n);
/* hook for fill layers' gradients (paint.c may set a better one); the default is linear/radial here */
typedef struct {
  Doc *d;
  i32 y, x0, n;
  u8 *pool; int nrows, used;
  int simple;
} Comp;
static u8 *row_get(Comp *c) { if (c->used >= c->nrows) return NULL; return c->pool + (size_t)(c->used++) * c->n * 4; }
/* a fill layer's row: solid, or a gradient across the canvas */
static void fill_row(Comp *c, const Layer *l, u8 *o)
{
  const FillParams *f = &l->fillp;
  i32 x;
  if (f->kind == 0) {
    for (x = 0; x < c->n; x++) { o[x * 4] = (u8)(f->c0 >> 16); o[x * 4 + 1] = (u8)(f->c0 >> 8); o[x * 4 + 2] = (u8)f->c0; o[x * 4 + 3] = 255; }
    return;
  }
  {
    double cx = c->d->w / 2.0, cy = c->d->h / 2.0, a = f->angle * 3.14159265358979 / 180, ca = cos(a), sa = -sin(a);
    double half = sqrt(cx * cx + cy * cy) * (f->scale > 0 ? f->scale : 100) / 100.0, len;
    if (half < 1) half = 1;
    len = fabs(cx * ca) + fabs(cy * sa);
    if (len < 1) len = 1;
    len = len * (f->scale > 0 ? f->scale : 100) / 100.0;
    for (x = 0; x < c->n; x++) {
      double px = c->x0 + x + 0.5 - cx, py = c->y + 0.5 - cy, t;
      int k, ti;
      switch (f->gkind) {
      case 1: t = sqrt(px * px + py * py) / half; break;                                   /* radial */
      case 2: t = fmod(atan2(py, px) - a + 4 * 3.14159265358979, 2 * 3.14159265358979) / (2 * 3.14159265358979); break;   /* angle */
      case 3: t = fabs(px * ca + py * sa) / len; break;                                    /* reflected */
      case 4: t = (fabs(px) / (cx > 0 ? cx : 1) > fabs(py) / (cy > 0 ? cy : 1) ? fabs(px) / (cx > 0 ? cx : 1) : fabs(py) / (cy > 0 ? cy : 1)) * 100.0 / (f->scale > 0 ? f->scale : 100); break;   /* diamond */
      default: t = (px * ca + py * sa) / (2 * len) + 0.5; break;                           /* linear */
      }
      if (f->reverse) t = 1 - t;
      ti = (int)(t * 256);
      if (ti < 0) ti = 0;
      if (ti > 256) ti = 256;
      for (k = 0; k < 3; k++) {
        int c0 = (int)((f->c0 >> (16 - 8 * k)) & 255), c1 = (int)((f->c1 >> (16 - 8 * k)) & 255);
        o[x * 4 + k] = (u8)(c0 + ((c1 - c0) * ti) / 256);
      }
      o[x * 4 + 3] = 255;
    }
  }
}
/* the opacity layer l has at pixel x (its opacity, fill, mask; Blend If against the source s and the backdrop b) */
static int px_cover(Comp *c, const Layer *l, const u8 *mask, int base, i32 x, const u8 *s, const u8 *b)
{
  int op = base;
  if (mask) {
    int m = mask[(long)c->y * c->d->w + c->x0 + x];
    m = 255 - mul255(255 - m, 255 - l->mask_fade);
    op = mul255(op, m);
  }
  if (s && (l->blendif[0] || l->blendif[1])) {
    int g = (s[0] * 77 + s[1] * 150 + s[2] * 29 + 128) >> 8;
    if (g < l->blendif[0] || g > 255 - l->blendif[1]) return 0;
  }
  if (b && (l->blendif[2] || l->blendif[3]) && b[3]) {
    int g = (b[0] * 77 + b[1] * 150 + b[2] * 29 + 128) >> 8;
    if (g < l->blendif[2] || g > 255 - l->blendif[3]) return 0;
  }
  return op;
}
/* the source row of a pixel or fill layer (NULL for others) */
static const u8 *src_row(Comp *c, const Layer *l, u8 *tmp)
{
  if (l->kind == LK_FILL) { fill_row(c, l, tmp); return tmp; }
  if (!l->px) return NULL;
  return l->px + ((long)c->y * c->d->w + c->x0) * 4;
}
static void adj_mix(Comp *c, Layer *l, u8 *acc, int keep_alpha_only);
/* layer l (pixel or fill) blended onto acc; keep: the clipped case, acc's alpha kept */
static void put_layer(Comp *c, Layer *l, u8 *acc, int keep)
{
  int base = mul255(l->opacity, 255 - l->fill_cut);
  const u8 *mask, *s;
  u8 *tmp;
  i32 x;
  if (!l->visible || (!base && !keep)) return;
  if (l->kind == LK_ADJUST) { adj_mix(c, l, acc, keep); return; }
  mask = layer_mask_used(c->d, l);
  tmp = l->kind == LK_FILL ? row_get(c) : NULL;
  if (l->kind == LK_FILL && !tmp) return;
  s = src_row(c, l, tmp);
  if (s) {
    for (x = 0; x < c->n; x++, s += 4) {
      u8 *p = acc + x * 4;
      int op;
      if (!s[3] || (keep && !p[3])) continue;
      op = (mask || l->blendif[0] || l->blendif[1] || l->blendif[2] || l->blendif[3]) ? px_cover(c, l, mask, base, x, s, p) : base;
      if (!op) continue;
      if (keep) { u8 a = p[3]; blend_pixel_at(p, s, op, l->blend, c->x0 + x, c->y); p[3] = a; }
      else blend_pixel_at(p, s, op, l->blend, c->x0 + x, c->y);
    }
  }
  if (tmp) c->used--;
}
/* an adjustment layer: acc adjusted, mixed back by the layer's opacity and mask, alpha untouched */
static void adj_mix(Comp *c, Layer *l, u8 *acc, int keep)
{
  int base = mul255(l->opacity, 255 - l->fill_cut);
  const u8 *mask = layer_mask_used(c->d, l);
  u8 *t;
  i32 x;
  (void)keep;
  if (!adjlayer_make || !base) return;
  if (!l->adjprog_ok) {
    if (l->adjprog && adjlayer_free) adjlayer_free(l->adjprog);
    l->adjprog = adjlayer_make(&l->adj);
    l->adjprog_ok = 1;
  }
  if (!l->adjprog || !(t = row_get(c))) return;
  memcpy(t, acc, (size_t)c->n * 4);
  adjlayer_row(l->adjprog, t, c->n);
  for (x = 0; x < c->n; x++) {
    u8 *p = acc + x * 4, *q = t + x * 4;
    int op, bl[3], k;
    if (!p[3]) continue;
    op = (mask || l->blendif[0] || l->blendif[1] || l->blendif[2] || l->blendif[3]) ? px_cover(c, l, mask, base, x, q, p) : base;
    if (!op) continue;
    blend_color(l->blend, p, q, bl);
    for (k = 0; k < 3; k++) p[k] = (u8)(p[k] + ((bl[k] - p[k]) * op + (bl[k] >= p[k] ? 127 : -127)) / 255);
  }
  c->used--;
}
static void comp_range(Comp *c, int lo, int hi, int depth, u8 *acc);
/* group g (members lo..g-1) onto acc */
static void put_group(Comp *c, int lo, int g, u8 *acc)
{
  Layer *G = c->d->lay[g];
  const u8 *mask;
  u8 *t;
  i32 x;
  if (!G->visible || !G->opacity) return;
  mask = layer_mask_used(c->d, G);
  if (G->blend == BM_PASS && G->opacity == 255 && !mask) { comp_range(c, lo, g, G->depth + 1, acc); return; }
  if (!(t = row_get(c))) { comp_range(c, lo, g, G->depth + 1, acc); return; }
  if (G->blend == BM_PASS) {
    /* the members straight onto a copy of what's below, then mixed in by the group's opacity and mask */
    memcpy(t, acc, (size_t)c->n * 4);
    comp_range(c, lo, g, G->depth + 1, t);
    for (x = 0; x < c->n; x++) {
      int op = mask ? px_cover(c, G, mask, G->opacity, x, NULL, NULL) : G->opacity, k;
      u8 *p = acc + x * 4, *q = t + x * 4;
      if (op == 255) memcpy(p, q, 4);
      else if (op) for (k = 0; k < 4; k++) p[k] = (u8)(p[k] + ((q[k] - p[k]) * op + (q[k] >= p[k] ? 127 : -127)) / 255);
    }
  } else {
    memset(t, 0, (size_t)c->n * 4);
    comp_range(c, lo, g, G->depth + 1, t);
    for (x = 0; x < c->n; x++) {
      u8 *q = t + x * 4;
      int op;
      if (!q[3]) continue;
      op = mask ? px_cover(c, G, mask, G->opacity, x, NULL, NULL) : G->opacity;
      if (op) blend_pixel_at(acc + x * 4, q, op, G->blend, c->x0 + x, c->y);
    }
  }
  c->used--;
}
static void comp_range(Comp *c, int lo, int hi, int depth, u8 *acc)
{
  Doc *d = c->d;
  int i = lo;
  while (i < hi) {
    Layer *l = d->lay[i];
    int j, k;
    if (l->depth > depth) {
      /* a group's members: up to its header */
      for (j = i; j < hi && d->lay[j]->depth > depth; j++) continue;
      if (j < hi && d->lay[j]->kind == LK_GROUP && d->lay[j]->depth == depth) { put_group(c, i, j, acc); i = j + 1; }
      else { comp_range(c, i, j, depth + 1, acc); i = j; }
      continue;
    }
    if (l->kind == LK_GROUP) { i++; continue; }              /* an empty group */
    /* layers clipped to this one */
    for (k = i + 1; k < hi && d->lay[k]->depth == depth && d->lay[k]->clip && d->lay[k]->kind != LK_GROUP; k++) continue;
    if (k > i + 1 && l->kind != LK_ADJUST) {
      u8 *t = row_get(c);
      int m;
      if (!t) { put_layer(c, l, acc, 0); i++; continue; }
      if (l->visible) {
        /* the base at full opacity (its fill, mask and Blend If kept), the clipped ones on it, then the
           result by the base's opacity and mode */
        u8 op = l->opacity, bl = l->blend;
        memset(t, 0, (size_t)c->n * 4);
        l->opacity = 255; l->blend = BM_NORMAL;
        put_layer(c, l, t, 0);
        l->opacity = op; l->blend = bl;
        for (m = i + 1; m < k; m++) put_layer(c, d->lay[m], t, 1);
        if (op) {
          i32 x;
          for (x = 0; x < c->n; x++) if (t[x * 4 + 3]) blend_pixel_at(acc + x * 4, t + x * 4, op, bl, c->x0 + x, c->y);
        }
      }
      c->used--;
      i = k;
      continue;
    }
    put_layer(c, l, acc, 0);
    i++;
  }
}
/* is the stack plain (no groups, masks, clipping, adjustment or fill layers, fill or Blend If)? */
static int doc_plain(const Doc *d)
{
  int i;
  for (i = 0; i < d->nlay; i++) {
    const Layer *l = d->lay[i];
    if (l->kind != LK_PIXEL || l->depth || l->clip || (l->mask && l->mask_on) || l->fill_cut ||
        l->blendif[0] || l->blendif[1] || l->blendif[2] || l->blendif[3] || !l->px) return 0;
  }
  return 1;
}
static int doc_maxdepth(const Doc *d)
{
  int i, m = 0;
  for (i = 0; i < d->nlay; i++) if (d->lay[i]->depth > m) m = d->lay[i]->depth;
  return m;
}
/* layers lo..hi-1 (and their nesting from depth) composited in rect r onto out (stride a row);
   out is not cleared first when onto is set */
void doc_composite_range(Doc *d, int lo, int hi, int depth, Box r, u8 *out, long stride, int onto)
{
  Comp c;
  i32 y;
  r = box_clip(r, doc_box(d));
  if (box_empty(r)) return;
  memset(&c, 0, sizeof c);
  c.d = d; c.x0 = r.x0; c.n = r.x1 - r.x0;
  c.nrows = 3 * (doc_maxdepth(d) + 2) + 2;
  if (!(c.pool = (u8 *)malloc((size_t)c.nrows * c.n * 4))) {
    /* no room for the working rows: the pixel layers alone, plainly */
    for (y = r.y0; y < r.y1; y++) {
      u8 *o = out + (long)(y - r.y0) * stride;
      int i;
      i32 x;
      if (!onto) memset(o, 0, (size_t)c.n * 4);
      for (i = lo; i < hi; i++) {
        const Layer *l = d->lay[i];
        if (l->px && l->visible && l->opacity)
          for (x = 0; x < c.n; x++) { const u8 *s = l->px + ((long)y * d->w + r.x0 + x) * 4; if (s[3]) blend_pixel(o + x * 4, s, l->opacity, l->blend); }
      }
    }
    return;
  }
  for (y = r.y0; y < r.y1; y++) {
    u8 *o = out + (long)(y - r.y0) * stride;
    if (!onto) memset(o, 0, (size_t)c.n * 4);
    c.y = y; c.used = 0;
    comp_range(&c, lo, hi, depth, o);
  }
  free(c.pool);
}
void doc_composite(Doc *d, Box r, u8 *out, long stride)
{
  i32 y, x;
  int i;
  r = box_clip(r, doc_box(d));
  if (!doc_plain(d)) { doc_composite_range(d, 0, d->nlay, 0, r, out, stride, 0); return; }
  for (y = r.y0; y < r.y1; y++) {
    u8 *o = out + (long)(y - r.y0) * stride;
    memset(o, 0, (size_t)(r.x1 - r.x0) * 4);
    for (i = 0; i < d->nlay; i++) {
      const Layer *l = d->lay[i];
      const u8 *s;
      u8 *p;
      if (!l->visible || !l->opacity) continue;
      s = l->px + ((long)y * d->w + r.x0) * 4;
      p = o;
      if (l->blend == BM_NORMAL && l->opacity == 255) {
        for (x = r.x0; x < r.x1; x++, s += 4, p += 4) {
          if (s[3] == 255) { p[0] = s[0]; p[1] = s[1]; p[2] = s[2]; p[3] = 255; }
          else if (s[3]) blend_pixel(p, s, 255, BM_NORMAL);
        }
      } else
        for (x = r.x0; x < r.x1; x++, s += 4, p += 4) if (s[3]) blend_pixel_at(p, s, l->opacity, l->blend, x, y);
    }
  }
}

/* ------------------------------------------------------------------ undo */
long undo_budget = 40L * 1024 * 1024;
#define TS 64
typedef struct { int layer; i32 tx, ty; u8 plane; u8 *px; u8 *ix; } Tile;   /* plane 0: pixels (TS*TS*4, or the edge
                                                                       part's size); 1: the mask (1 byte a pixel);
                                                                       ix (0.4): an indexed layer's indices, or NULL */
/* 0.4: the palette state an undo step can keep (doc_note_pal, doc_note_all) */
typedef struct {
  u8 mode; int npal; u8 pal[256 * 3]; u8 lock[256];
  CycleRange cycle[MAXCYCLES]; int ncycle; u32 modeid; u8 ax, ay;
} PalState;
typedef Layer Props;                  /* a layer's settings: everything but its buffers (see props_of) */
/* 0.7: the alpha channels and Quick Mask plane as they were (doc_note_channels; copies, w x h) */
typedef struct { u8 *chan[MAXCHANNELS]; char name[MAXCHANNELS][32]; int n; u8 *qmask; } ChanState;
struct UndoStep {
  char name[64];
  Tile *tiles; int ntiles, captiles;
  int has_sel; u8 *sel; Box selbox;
  /* a copy of the whole document (doc_note_all) */
  int whole; i32 w, h; Layer *lay[MAXLAYERS]; int nlay, active;
  Props *props; int nprops, pactive;   /* the layers' settings before the step (nprops of them) */
  int has_pal; PalState *pal;         /* 0.4: the palette as it was (malloc'd) */
  int has_chan; ChanState *chan;      /* 0.7: the channels as they were (malloc'd) */
  long bytes;
  UndoStep *next;
  int open;
};
static UndoStep *open_step(Doc *d) { return d->undo && d->undo->open ? d->undo : NULL; }
static void step_free(UndoStep *u)
{
  int i;
  for (i = 0; i < u->ntiles; i++) { free(u->tiles[i].px); free(u->tiles[i].ix); }
  free(u->tiles);
  free(u->sel);
  free(u->pal);                                            /* 0.4 */
  if (u->chan) { for (i = 0; i < u->chan->n; i++) free(u->chan->chan[i]); free(u->chan->qmask); free(u->chan); }   /* 0.7 */
  if (u->whole) for (i = 0; i < u->nlay; i++) layer_free(u->lay[i]);
  free(u->props);
  free(u);
}
static void list_free(UndoStep *u) { while (u) { UndoStep *n = u->next; step_free(u); u = n; } }
void doc_forget_undo(Doc *d) { list_free(d->undo); list_free(d->redo); d->undo = d->redo = NULL; d->undo_depth = 0; d->undo_bytes = 0; }
/* a layer's settings without its buffers, caches or panel state (what undo keeps and compares) */
static void props_of(const Layer *l, Props *p)
{
  *p = *l;
  p->px = p->mask = p->idx = p->maskfx = NULL; p->adjprog = NULL;
  p->maskfx_ok = p->adjprog_ok = 0; p->picked = 0; p->edit_mask = 0;
}
/* settings p put back on layer l (its buffers, caches and panel state kept) */
static void props_set(Layer *l, const Props *p)
{
  Layer keep = *l;
  *l = *p;
  l->px = keep.px; l->mask = keep.mask; l->idx = keep.idx; l->maskfx = keep.maskfx; l->adjprog = keep.adjprog;
  l->picked = keep.picked; l->edit_mask = keep.edit_mask;
  l->maskfx_ok = keep.maskfx_ok && p->mask_feather == keep.mask_feather;
  l->adjprog_ok = 0;
}
static void save_props(Doc *d, UndoStep *u)
{
  int i;
  u->nprops = 0; u->pactive = d->active;
  if (!(u->props = (Props *)malloc((size_t)(d->nlay ? d->nlay : 1) * sizeof(Props)))) return;
  u->nprops = d->nlay;
  for (i = 0; i < d->nlay; i++) props_of(d->lay[i], &u->props[i]);
}
void doc_begin(Doc *d, const char *action)
{
  UndoStep *u = open_step(d);
  if (u) { u->open++; return; }
  if (!(u = (UndoStep *)calloc(1, sizeof *u))) return;
  strncpy(u->name, action ? action : "", sizeof u->name - 1);
  save_props(d, u);
  u->open = 1;
  u->next = d->undo;
  d->undo = u;
}
/* the buffer a tile's plane is in: 0 layer li's pixels, 1 its mask; 0.7: 2 the Quick Mask plane,
   3 + k alpha channel k (li is 0 for those) */
static u8 *plane_buf(Doc *d, int li, int plane)
{
  if (plane >= 3) return plane - 3 < d->nchan ? d->chan[plane - 3] : NULL;
  if (plane == 2) return d->qmask;
  if (li < 0 || li >= d->nlay) return NULL;
  return plane ? d->lay[li]->mask : d->lay[li]->px;
}
static void note_plane(Doc *d, int li, Box r, int plane)
{
  UndoStep *u = open_step(d);
  i32 tx, ty;
  int bpp = plane ? 1 : 4;
  u8 *buf;
  if (!u || u->whole || (plane >= 2 && u->has_chan)) return;    /* (0.7: the channels' copy covers their planes) */
  if (!(buf = plane_buf(d, li, plane))) return;
  r = box_clip(r, doc_box(d));
  if (box_empty(r)) return;
  for (ty = r.y0 / TS; ty <= (r.y1 - 1) / TS; ty++)
    for (tx = r.x0 / TS; tx <= (r.x1 - 1) / TS; tx++) {
      int k, row;
      i32 x0 = tx * TS, y0 = ty * TS, tw = d->w - x0 < TS ? d->w - x0 : TS, th = d->h - y0 < TS ? d->h - y0 : TS;
      Tile *t;
      for (k = 0; k < u->ntiles; k++) if (u->tiles[k].layer == li && u->tiles[k].tx == tx && u->tiles[k].ty == ty && u->tiles[k].plane == plane) break;
      if (k < u->ntiles) continue;                         /* (saved already in this step) */
      if (u->ntiles == u->captiles) {
        int nc = u->captiles ? u->captiles * 2 : 32;
        Tile *nt = (Tile *)realloc(u->tiles, (size_t)nc * sizeof *nt);
        if (!nt) return;
        u->tiles = nt; u->captiles = nc;
      }
      t = &u->tiles[u->ntiles];
      if (!(t->px = (u8 *)malloc((size_t)tw * th * bpp))) return;
      t->layer = li; t->tx = tx; t->ty = ty; t->plane = (u8)plane; t->ix = NULL;
      for (row = 0; row < th; row++) memcpy(t->px + (long)row * tw * bpp, buf + ((long)(y0 + row) * d->w + x0) * bpp, (size_t)tw * bpp);
      /* 0.4: an indexed layer's indices kept with the pixels */
      if (!plane && d->lay[li]->idx && (t->ix = (u8 *)malloc((size_t)tw * th)) != NULL) {
        for (row = 0; row < th; row++) memcpy(t->ix + (long)row * tw, d->lay[li]->idx + (long)(y0 + row) * d->w + x0, (size_t)tw);
        u->bytes += (long)tw * th;
      }
      u->ntiles++;
      u->bytes += (long)tw * th * bpp;
    }
}
void doc_note_layer(Doc *d, int li, Box r) { note_plane(d, li, r, 0); }
void doc_note_mask(Doc *d, int li, Box r) { note_plane(d, li, r, 1); }
/* 0.7: the channels' planes */
void doc_note_chan(Doc *d, int k, Box r) { if (k >= 0 && k < d->nchan) note_plane(d, 0, r, 3 + k); }
void doc_note_qmask(Doc *d, Box r) { note_plane(d, 0, r, 2); }
void doc_note_channels(Doc *d)
{
  UndoStep *u = open_step(d);
  ChanState *c;
  size_t n = (size_t)d->w * d->h;
  int i, k;
  if (!u || u->has_chan) return;
  if (!(c = (ChanState *)calloc(1, sizeof *c))) return;
  for (i = 0; i < d->nchan; i++) {
    if (!(c->chan[i] = (u8 *)malloc(n))) { while (i--) free(c->chan[i]); free(c); return; }
    memcpy(c->chan[i], d->chan[i], n);
  }
  c->n = d->nchan;
  memcpy(c->name, d->chan_name, sizeof c->name);
  if (d->qmask && (c->qmask = (u8 *)malloc(n)) != NULL) memcpy(c->qmask, d->qmask, n);
  /* tiles of these planes noted already are covered by the copy */
  for (i = k = 0; i < u->ntiles; i++) {
    if (u->tiles[i].plane >= 2) {
      Tile *t = &u->tiles[i];
      i32 tw = d->w - t->tx * TS < TS ? d->w - t->tx * TS : TS, th = d->h - t->ty * TS < TS ? d->h - t->ty * TS : TS;
      u->bytes -= (long)tw * th; free(t->px); continue;
    }
    u->tiles[k++] = u->tiles[i];
  }
  u->ntiles = k;
  u->chan = c; u->has_chan = 1;
  u->bytes += (long)(c->n + (c->qmask != NULL)) * (long)n + (long)sizeof *c;
}
void doc_note_sel(Doc *d)
{
  UndoStep *u = open_step(d);
  if (!u || u->whole || u->has_sel) return;
  u->has_sel = 1; u->selbox = d->selbox;
  if (d->sel && (u->sel = (u8 *)malloc((size_t)d->w * d->h))) { memcpy(u->sel, d->sel, (size_t)d->w * d->h); u->bytes += (long)d->w * d->h; }
}
/* 0.4: the palette state saved into or swapped with a step */
static void pal_get(const Doc *d, PalState *p)
{
  p->mode = d->mode; p->npal = d->npal; memcpy(p->pal, d->pal, sizeof p->pal); memcpy(p->lock, d->pal_lock, sizeof p->lock);
  memcpy(p->cycle, d->cycle, sizeof p->cycle); p->ncycle = d->ncycle; p->modeid = d->modeid; p->ax = d->aspect_x; p->ay = d->aspect_y;
}
static void pal_put(Doc *d, const PalState *p)
{
  d->mode = p->mode; d->npal = p->npal; memcpy(d->pal, p->pal, sizeof d->pal); memcpy(d->pal_lock, p->lock, sizeof d->pal_lock);
  memcpy(d->cycle, p->cycle, sizeof d->cycle); d->ncycle = p->ncycle; d->modeid = p->modeid; d->aspect_x = p->ax; d->aspect_y = p->ay;
}
void doc_note_pal(Doc *d)
{
  UndoStep *u = open_step(d);
  if (!u || u->has_pal) return;
  if (!(u->pal = (PalState *)malloc(sizeof *u->pal))) return;
  pal_get(d, u->pal);
  u->has_pal = 1;
  u->bytes += (long)sizeof *u->pal;
}
/* 0.4: every indexed layer's RGB from its indices (alpha kept), after its palette was swapped back */
static void pal_rederive(Doc *d)
{
  int i;
  long k, n = (long)d->w * d->h;
  for (i = 0; i < d->nlay; i++) {
    Layer *l = d->lay[i];
    if (!l->px || !l->idx) continue;
    for (k = 0; k < n; k++) { const u8 *c = d->pal + l->idx[k] * 3; u8 *p = l->px + k * 4; p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; }
  }
}
void doc_note_all(Doc *d)
{
  UndoStep *u = open_step(d);
  int i;
  if (!u || u->whole) return;
  doc_note_pal(d);                                         /* 0.4: the palette and mode go with the copy */
  /* tiles noted already are covered by the copy */
  for (i = 0; i < u->ntiles; i++) { free(u->tiles[i].px); free(u->tiles[i].ix); }
  free(u->tiles); u->tiles = NULL; u->ntiles = u->captiles = 0;
  free(u->sel); u->sel = NULL; u->has_sel = 0;
  u->whole = 1; u->w = d->w; u->h = d->h; u->nlay = d->nlay; u->active = d->active; u->bytes = 0;
  for (i = 0; i < d->nlay; i++) {
    if (!(u->lay[i] = layer_copy(d->lay[i], d->w, d->h))) { u->nlay = i; break; }
    u->bytes += (long)d->w * d->h * 4;
  }
  u->has_sel = 1; u->selbox = d->selbox;
  if (d->sel && (u->sel = (u8 *)malloc((size_t)d->w * d->h))) memcpy(u->sel, d->sel, (size_t)d->w * d->h);
  doc_note_channels(d);                                    /* 0.7: and the channels */
}
void doc_end(Doc *d)
{
  UndoStep *u = open_step(d), *p;
  long total;
  int k;
  if (!u || --u->open > 0) return;
  /* nothing noted and nothing about the layers changed: no step */
  if (!u->whole && !u->ntiles && !u->has_sel && !u->has_pal && !u->has_chan) {
    int same = u->nprops == d->nlay && u->pactive == d->active, i;
    for (i = 0; same && i < d->nlay; i++) {
      Props now;
      props_of(d->lay[i], &now);
      same = !memcmp(&now, &u->props[i], sizeof now);
    }
    if (same) { d->undo = u->next; step_free(u); return; }
  }
  /* 0.4: in an indexed, grey or bitmap document whatever was painted is brought back to the palette
     (or grey) inside the same step; palette edits and mode changes keep their own pixels */
  if (d->mode != MODE_RGB && !u->has_pal) {
    if (u->whole) doc_index_sync_all(d);
    else
      for (k = 0; k < u->ntiles; k++) {
        Tile *t = &u->tiles[k];
        if (!t->plane) doc_index_sync(d, t->layer, box_make(t->tx * TS, t->ty * TS, t->tx * TS + TS, t->ty * TS + TS));
      }
  }
  list_free(d->redo); d->redo = NULL;
  d->dirty = 1;
  d->undo_depth++;
  strncpy(d->last_action, u->name, sizeof d->last_action - 1);
  /* the oldest steps forgotten past the memory budget (the newest is always kept) */
  for (total = 0, k = 0, p = d->undo; p; p = p->next, k++) {
    total += p->bytes;
    if (p->next && (total + p->next->bytes > undo_budget || k >= 99)) { list_free(p->next); p->next = NULL; d->undo_depth = k + 1; break; }
  }
  d->undo_bytes = total;
}
/* a step undone or redone: its saved state and the document's swapped */
static void swap_step(Doc *d, UndoStep *u)
{
  int i;
  if (u->whole) {
    Layer *tl[MAXLAYERS];
    int tn = d->nlay, ta = d->active;
    i32 tw = d->w, th = d->h;
    memcpy(tl, d->lay, sizeof tl);
    memcpy(d->lay, u->lay, sizeof tl); d->nlay = u->nlay; d->active = u->active; d->w = u->w; d->h = u->h;
    memcpy(u->lay, tl, sizeof tl); u->nlay = tn; u->active = ta; u->w = tw; u->h = th;
  } else {
    for (i = 0; i < u->ntiles; i++) {
      Tile *t = &u->tiles[i];
      i32 x0 = t->tx * TS, y0 = t->ty * TS, tw = d->w - x0 < TS ? d->w - x0 : TS, th = d->h - y0 < TS ? d->h - y0 : TS, row;
      int bpp = t->plane ? 1 : 4;
      u8 tmp[TS * 4], *buf;
      if (t->layer >= d->nlay) continue;
      buf = plane_buf(d, t->layer, t->plane);              /* (0.7: or a channel's plane) */
      if (!buf) continue;
      if (t->plane == 1) d->lay[t->layer]->maskfx_ok = 0;
      for (row = 0; row < th; row++) {
        u8 *a = t->px + (long)row * tw * bpp, *b = buf + ((long)(y0 + row) * d->w + x0) * bpp;
        memcpy(tmp, a, (size_t)tw * bpp); memcpy(a, b, (size_t)tw * bpp); memcpy(b, tmp, (size_t)tw * bpp);
      }
      if (t->ix && d->lay[t->layer]->idx)                  /* 0.4: the indices too */
        for (row = 0; row < th; row++) {
          u8 *a = t->ix + (long)row * tw, *b = d->lay[t->layer]->idx + (long)(y0 + row) * d->w + x0;
          memcpy(tmp, a, (size_t)tw); memcpy(a, b, (size_t)tw); memcpy(b, tmp, (size_t)tw);
        }
    }
    /* the layers' settings */
    if (u->nprops == d->nlay) {
      Props cur;
      int ca = d->active;
      for (i = 0; i < d->nlay; i++) {
        props_of(d->lay[i], &cur);
        props_set(d->lay[i], &u->props[i]);
        u->props[i] = cur;
      }
      d->active = u->pactive < d->nlay ? u->pactive : d->nlay - 1; u->pactive = ca;
    }
  }
  if (u->has_sel) {
    u8 *ts = d->sel;
    Box tb = d->selbox;
    if (u->whole || !u->sel || !ts || 1) { d->sel = u->sel; d->selbox = u->selbox; u->sel = ts; u->selbox = tb; }
  }
  if (u->has_chan && u->chan) {                            /* 0.7: the channels swapped */
    ChanState *c = u->chan;
    u8 *tq = d->qmask, *tc[MAXCHANNELS];
    char tn[32];
    int tcount = d->nchan;
    memcpy(tc, d->chan, sizeof tc);
    memcpy(d->chan, c->chan, sizeof tc); d->nchan = c->n; d->qmask = c->qmask;
    memcpy(c->chan, tc, sizeof tc); c->n = tcount; c->qmask = tq;
    for (i = 0; i < MAXCHANNELS; i++) {
      memcpy(tn, d->chan_name[i], 32); memcpy(d->chan_name[i], c->name[i], 32); memcpy(c->name[i], tn, 32);
    }
  }
  if (u->has_pal && u->pal) {                              /* 0.4: the palette and mode swapped */
    PalState *cur = (PalState *)malloc(sizeof *cur);       /* (off the stack: it is over 1 KB) */
    if (cur) { pal_get(d, cur); pal_put(d, u->pal); *u->pal = *cur; free(cur); if (!u->whole) pal_rederive(d); }
  }
  if (d->active >= d->nlay) d->active = d->nlay - 1;
  if (d->active < 0) d->active = 0;
}
int doc_undo(Doc *d)
{
  UndoStep *u = d->undo;
  if (!u || u->open) return 0;
  swap_step(d, u);
  d->undo = u->next; u->next = d->redo; d->redo = u;
  if (d->undo_depth) d->undo_depth--;
  d->dirty = 1;
  return 1;
}
int doc_redo(Doc *d)
{
  UndoStep *u = d->redo;
  if (!u) return 0;
  swap_step(d, u);
  d->redo = u->next; u->next = d->undo; d->undo = u;
  d->undo_depth++;
  d->dirty = 1;
  return 1;
}
int doc_discard_last(Doc *d)
{
  UndoStep *u;
  if (!doc_undo(d)) return 0;
  u = d->redo; d->redo = u->next;
  step_free(u);
  return 1;
}
int doc_can_undo(Doc *d) { return d->undo && !d->undo->open; }
int doc_can_redo(Doc *d) { return d->redo != NULL; }
const char *doc_undo_name(Doc *d) { return d->undo ? d->undo->name : ""; }
const char *doc_redo_name(Doc *d) { return d->redo ? d->redo->name : ""; }
int doc_history(Doc *d, const char **names, int max)
{
  int n = 0, i;
  UndoStep *u;
  for (u = d->undo; u; u = u->next) n++;
  if (n > max) n = max;
  for (i = n - 1, u = d->undo; u && i >= 0; u = u->next, i--) names[i] = u->name;
  return n;
}
int doc_redo_history(Doc *d, const char **names, int max)
{
  int n = 0;
  UndoStep *u;
  for (u = d->redo; u && n < max; u = u->next) names[n++] = u->name;
  return n;
}

/* ------------------------------------------------------------------ the stack */
/* the lowest index of layer i's block (a group's first member; i itself for any other layer) */
static int blk_start(const Doc *d, int i)
{
  int j;
  if (i < 0 || i >= d->nlay || d->lay[i]->kind != LK_GROUP) return i;
  for (j = i; j > 0 && d->lay[j - 1]->depth > d->lay[i]->depth; j--) continue;
  return j;
}
int doc_add_layer(Doc *d, Layer *l, int at)
{
  if (!l || d->nlay >= MAXLAYERS) { layer_free(l); return 0; }
  if (at < 0 || at > d->nlay) at = d->nlay;
  /* a new layer joins the group of the layer just below where it goes */
  if (!l->depth) l->depth = at > 0 ? d->lay[at - 1]->depth : d->nlay ? d->lay[0]->depth : 0;
  doc_begin(d, "New Layer");
  doc_note_all(d);
  memmove(&d->lay[at + 1], &d->lay[at], (size_t)(d->nlay - at) * sizeof(Layer *));
  d->lay[at] = l; d->nlay++;
  d->active = at;
  doc_end(d);
  return 1;
}
int doc_delete_layer(Doc *d, int i)
{
  int s, n, k;
  if (i < 0 || i >= d->nlay) return 0;
  s = blk_start(d, i); n = i - s + 1;                    /* a group goes with its members */
  if (n >= d->nlay) return 0;
  doc_begin(d, "Delete Layer");
  doc_note_all(d);
  for (k = s; k <= i; k++) layer_free(d->lay[k]);
  memmove(&d->lay[s], &d->lay[i + 1], (size_t)(d->nlay - i - 1) * sizeof(Layer *));
  d->nlay -= n;
  d->active = s < d->nlay ? s : d->nlay - 1;
  if (d->active > 0 && s > 0 && s >= d->nlay) d->active = d->nlay - 1;
  doc_end(d);
  return 1;
}
int doc_move_layer(Doc *d, int from, int to)
{
  Layer *l;
  if (from < 0 || from >= d->nlay || to < 0 || to >= d->nlay || from == to) return 0;
  doc_begin(d, "Move Layer");
  doc_note_all(d);
  l = d->lay[from];
  if (to < from) memmove(&d->lay[to + 1], &d->lay[to], (size_t)(from - to) * sizeof(Layer *));
  else memmove(&d->lay[from], &d->lay[from + 1], (size_t)(to - from) * sizeof(Layer *));
  d->lay[to] = l;
  d->active = to;
  doc_end(d);
  return 1;
}
int doc_duplicate_layer(Doc *d, int i)
{
  Layer *c[MAXLAYERS];
  int s, n, k;
  if (i < 0 || i >= d->nlay) return 0;
  s = blk_start(d, i); n = i - s + 1;
  if (d->nlay + n > MAXLAYERS) return 0;
  for (k = 0; k < n; k++)
    if (!(c[k] = layer_copy(d->lay[s + k], d->w, d->h))) { while (k--) layer_free(c[k]); return 0; }
  {
    char nm[64];
    size_t l = strlen(d->lay[i]->name);
    if (l > 54) l = 54;
    memcpy(nm, d->lay[i]->name, l); strcpy(nm + l, " copy");
    strcpy(c[n - 1]->name, nm);
  }
  doc_begin(d, "Duplicate Layer");
  doc_note_all(d);
  memmove(&d->lay[i + 1 + n], &d->lay[i + 1], (size_t)(d->nlay - i - 1) * sizeof(Layer *));
  memcpy(&d->lay[i + 1], c, (size_t)n * sizeof(Layer *));
  d->nlay += n;
  d->active = i + n;
  doc_end(d);
  return 1;
}
/* layers s..e (whole blocks at one level) replaced by one pixel layer of their composite */
static int merge_blocks(Doc *d, int s, int e, const char *name)
{
  Layer *m;
  int depth = d->lay[e]->depth, k;
  if (!(m = layer_new(d, d->lay[s]->kind == LK_PIXEL ? d->lay[s]->name : d->lay[e]->name))) return 0;
  doc_composite_range(d, s, e + 1, depth, doc_box(d), m->px, (long)d->w * 4, 0);
  doc_begin(d, name);
  doc_note_all(d);
  for (k = s; k <= e; k++) layer_free(d->lay[k]);
  memmove(&d->lay[s + 1], &d->lay[e + 1], (size_t)(d->nlay - e - 1) * sizeof(Layer *));
  d->nlay -= e - s;
  d->lay[s] = m; m->depth = (u8)depth;
  d->active = s;
  doc_end(d);
  return 1;
}
int doc_merge_down(Doc *d, int i)
{
  int below;
  if (i < 0 || i >= d->nlay) return 0;
  if (d->lay[i]->kind == LK_GROUP) return merge_blocks(d, blk_start(d, i), i, "Merge Group");
  below = i - 1;
  if (below < 0 || d->lay[below]->depth < d->lay[i]->depth) return 0;    /* the bottom of its group */
  return merge_blocks(d, blk_start(d, below), i, "Merge Down");
}
int doc_merge_visible(Doc *d)
{
  Layer *m;
  int i, k, first = -1, shown[MAXLAYERS];
  /* what shows: visible, inside groups that are all visible (each enclosing header found going up) */
  for (i = 0; i < d->nlay; i++) {
    int j, cur = d->lay[i]->depth, ok = d->lay[i]->visible;
    for (j = i + 1; ok && cur > 0 && j < d->nlay; j++)
      if (d->lay[j]->depth < cur) { cur = d->lay[j]->depth; if (!d->lay[j]->visible) ok = 0; }
    shown[i] = ok;
  }
  for (i = 0; i < d->nlay; i++) if (shown[i] && d->lay[i]->kind != LK_GROUP) { first = i; break; }
  if (first < 0) return 0;
  if (!(m = layer_new(d, d->lay[first]->name))) return 0;
  doc_composite(d, doc_box(d), m->px, (long)d->w * 4);
  doc_begin(d, "Merge Visible");
  doc_note_all(d);
  for (i = k = 0; i < d->nlay; i++) {
    if (i == first) { layer_free(d->lay[i]); d->lay[k++] = m; }
    else if (shown[i] || d->lay[i]->kind == LK_GROUP) layer_free(d->lay[i]);
    else { d->lay[i]->depth = 0; d->lay[k++] = d->lay[i]; }
  }
  d->nlay = k;
  for (i = 0; i < d->nlay; i++) if (d->lay[i] == m) d->active = i;
  doc_end(d);
  return 1;
}
int doc_flatten(Doc *d)
{
  Layer *m;
  long k, n = (long)d->w * d->h;
  int i;
  if (!(m = layer_new(d, "Background"))) return 0;
  doc_composite(d, doc_box(d), m->px, (long)d->w * 4);
  /* flattened onto the background colour: no transparency left */
  for (k = 0; k < n; k++) {
    u8 bgp[4], *p = m->px + k * 4;
    bgp[0] = (u8)(d->bg >> 16); bgp[1] = (u8)(d->bg >> 8); bgp[2] = (u8)d->bg; bgp[3] = 255;
    blend_pixel(bgp, p, 255, BM_NORMAL);
    memcpy(p, bgp, 4);
  }
  doc_begin(d, "Flatten Image");
  doc_note_all(d);
  for (i = 0; i < d->nlay; i++) layer_free(d->lay[i]);
  d->lay[0] = m; d->nlay = 1; d->active = 0;
  doc_end(d);
  return 1;
}
void doc_set_layer_props(Doc *d, int i, const char *name, int opacity, int blend, int visible)
{
  Layer *l;
  if (i < 0 || i >= d->nlay) return;
  l = d->lay[i];
  doc_begin(d, name ? "Rename Layer" : visible >= 0 ? "Layer Visibility" : opacity >= 0 ? "Layer Opacity" : "Blend Mode");
  if (name) { strncpy(l->name, name, sizeof l->name - 1); l->name[sizeof l->name - 1] = 0; }
  if (opacity >= 0) l->opacity = (u8)(opacity > 255 ? 255 : opacity);
  if (blend >= 0 && (blend < BM_N || (blend == BM_PASS && l->kind == LK_GROUP))) l->blend = (u8)blend;
  if (visible >= 0) l->visible = (u8)(visible != 0);
  doc_end(d);
}
