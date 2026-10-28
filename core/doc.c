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
  free(l);
}
Layer *layer_copy(const Layer *l, i32 w, i32 h)
{
  Layer *c = (Layer *)malloc(sizeof *c);
  if (!c) return NULL;
  *c = *l;
  c->px = c->mask = c->idx = NULL;
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
void doc_composite(Doc *d, Box r, u8 *out, long stride)
{
  i32 y, x;
  int i;
  r = box_clip(r, doc_box(d));
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
        for (x = r.x0; x < r.x1; x++, s += 4, p += 4) if (s[3]) blend_pixel(p, s, l->opacity, l->blend);
    }
  }
}

/* ------------------------------------------------------------------ undo */
#define TS 64
typedef struct { int layer; i32 tx, ty; u8 *px; } Tile;      /* px: TS*TS*4, or the edge part's size */
typedef struct { char name[64]; u8 opacity, blend, visible, locked, alpha_lock; } Props;
struct UndoStep {
  char name[64];
  Tile *tiles; int ntiles, captiles;
  int has_sel; u8 *sel; Box selbox;
  /* a copy of the whole document (doc_note_all) */
  int whole; i32 w, h; Layer *lay[MAXLAYERS]; int nlay, active;
  Props props[MAXLAYERS]; int nprops, pactive;
  long bytes;
  UndoStep *next;
  int open;
};
static UndoStep *open_step(Doc *d) { return d->undo && d->undo->open ? d->undo : NULL; }
static void step_free(UndoStep *u)
{
  int i;
  for (i = 0; i < u->ntiles; i++) free(u->tiles[i].px);
  free(u->tiles);
  free(u->sel);
  if (u->whole) for (i = 0; i < u->nlay; i++) layer_free(u->lay[i]);
  free(u);
}
static void list_free(UndoStep *u) { while (u) { UndoStep *n = u->next; step_free(u); u = n; } }
void doc_forget_undo(Doc *d) { list_free(d->undo); list_free(d->redo); d->undo = d->redo = NULL; d->undo_depth = 0; d->undo_bytes = 0; }
static void save_props(Doc *d, UndoStep *u)
{
  int i;
  u->nprops = d->nlay; u->pactive = d->active;
  for (i = 0; i < d->nlay; i++) {
    Layer *l = d->lay[i];
    memcpy(u->props[i].name, l->name, sizeof l->name);
    u->props[i].opacity = l->opacity; u->props[i].blend = l->blend; u->props[i].visible = l->visible;
    u->props[i].locked = l->locked; u->props[i].alpha_lock = l->alpha_lock;
  }
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
void doc_note_layer(Doc *d, int li, Box r)
{
  UndoStep *u = open_step(d);
  i32 tx, ty;
  if (!u || u->whole || li < 0 || li >= d->nlay) return;
  r = box_clip(r, doc_box(d));
  if (box_empty(r)) return;
  for (ty = r.y0 / TS; ty <= (r.y1 - 1) / TS; ty++)
    for (tx = r.x0 / TS; tx <= (r.x1 - 1) / TS; tx++) {
      int k, row;
      i32 x0 = tx * TS, y0 = ty * TS, tw = d->w - x0 < TS ? d->w - x0 : TS, th = d->h - y0 < TS ? d->h - y0 : TS;
      Tile *t;
      for (k = 0; k < u->ntiles; k++) if (u->tiles[k].layer == li && u->tiles[k].tx == tx && u->tiles[k].ty == ty) break;
      if (k < u->ntiles) continue;                         /* (saved already in this step) */
      if (u->ntiles == u->captiles) {
        int nc = u->captiles ? u->captiles * 2 : 32;
        Tile *nt = (Tile *)realloc(u->tiles, (size_t)nc * sizeof *nt);
        if (!nt) return;
        u->tiles = nt; u->captiles = nc;
      }
      t = &u->tiles[u->ntiles];
      if (!(t->px = (u8 *)malloc((size_t)tw * th * 4))) return;
      t->layer = li; t->tx = tx; t->ty = ty;
      for (row = 0; row < th; row++) memcpy(t->px + (long)row * tw * 4, d->lay[li]->px + ((long)(y0 + row) * d->w + x0) * 4, (size_t)tw * 4);
      u->ntiles++;
      u->bytes += (long)tw * th * 4;
    }
}
void doc_note_sel(Doc *d)
{
  UndoStep *u = open_step(d);
  if (!u || u->whole || u->has_sel) return;
  u->has_sel = 1; u->selbox = d->selbox;
  if (d->sel && (u->sel = (u8 *)malloc((size_t)d->w * d->h))) { memcpy(u->sel, d->sel, (size_t)d->w * d->h); u->bytes += (long)d->w * d->h; }
}
void doc_note_all(Doc *d)
{
  UndoStep *u = open_step(d);
  int i;
  if (!u || u->whole) return;
  /* tiles noted already are covered by the copy */
  for (i = 0; i < u->ntiles; i++) free(u->tiles[i].px);
  free(u->tiles); u->tiles = NULL; u->ntiles = u->captiles = 0;
  free(u->sel); u->sel = NULL; u->has_sel = 0;
  u->whole = 1; u->w = d->w; u->h = d->h; u->nlay = d->nlay; u->active = d->active; u->bytes = 0;
  for (i = 0; i < d->nlay; i++) {
    if (!(u->lay[i] = layer_copy(d->lay[i], d->w, d->h))) { u->nlay = i; break; }
    u->bytes += (long)d->w * d->h * 4;
  }
  u->has_sel = 1; u->selbox = d->selbox;
  if (d->sel && (u->sel = (u8 *)malloc((size_t)d->w * d->h))) memcpy(u->sel, d->sel, (size_t)d->w * d->h);
}
void doc_end(Doc *d)
{
  UndoStep *u = open_step(d), *p;
  long total;
  int k;
  if (!u || --u->open > 0) return;
  /* nothing noted and nothing about the layers changed: no step */
  if (!u->whole && !u->ntiles && !u->has_sel) {
    int same = u->nprops == d->nlay && u->pactive == d->active, i;
    for (i = 0; same && i < d->nlay; i++)
      same = !strcmp(u->props[i].name, d->lay[i]->name) && u->props[i].opacity == d->lay[i]->opacity && u->props[i].blend == d->lay[i]->blend &&
             u->props[i].visible == d->lay[i]->visible && u->props[i].locked == d->lay[i]->locked && u->props[i].alpha_lock == d->lay[i]->alpha_lock;
    if (same) { d->undo = u->next; step_free(u); return; }
  }
  list_free(d->redo); d->redo = NULL;
  d->dirty = 1;
  d->undo_depth++;
  strncpy(d->last_action, u->name, sizeof d->last_action - 1);
  /* the oldest steps forgotten past the memory budget (the newest is always kept) */
  for (total = 0, k = 0, p = d->undo; p; p = p->next, k++) {
    total += p->bytes;
    if (p->next && (total + p->next->bytes > UNDO_BUDGET || k >= 99)) { list_free(p->next); p->next = NULL; d->undo_depth = k + 1; break; }
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
      u8 tmp[TS * 4];
      if (t->layer >= d->nlay) continue;
      for (row = 0; row < th; row++) {
        u8 *a = t->px + (long)row * tw * 4, *b = d->lay[t->layer]->px + ((long)(y0 + row) * d->w + x0) * 4;
        memcpy(tmp, a, (size_t)tw * 4); memcpy(a, b, (size_t)tw * 4); memcpy(b, tmp, (size_t)tw * 4);
      }
    }
    /* the layers' settings */
    if (u->nprops == d->nlay) {
      Props cur[MAXLAYERS];
      int ca = d->active;
      for (i = 0; i < d->nlay; i++) {
        Layer *l = d->lay[i];
        memcpy(cur[i].name, l->name, sizeof l->name); cur[i].opacity = l->opacity; cur[i].blend = l->blend; cur[i].visible = l->visible;
        cur[i].locked = l->locked; cur[i].alpha_lock = l->alpha_lock;
        memcpy(l->name, u->props[i].name, sizeof l->name); l->opacity = u->props[i].opacity; l->blend = u->props[i].blend;
        l->visible = u->props[i].visible; l->locked = u->props[i].locked; l->alpha_lock = u->props[i].alpha_lock;
      }
      memcpy(u->props, cur, sizeof(Props) * (size_t)d->nlay);
      d->active = u->pactive < d->nlay ? u->pactive : d->nlay - 1; u->pactive = ca;
    }
  }
  if (u->has_sel) {
    u8 *ts = d->sel;
    Box tb = d->selbox;
    if (u->whole || !u->sel || !ts || 1) { d->sel = u->sel; d->selbox = u->selbox; u->sel = ts; u->selbox = tb; }
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
int doc_add_layer(Doc *d, Layer *l, int at)
{
  if (!l || d->nlay >= MAXLAYERS) { layer_free(l); return 0; }
  if (at < 0 || at > d->nlay) at = d->nlay;
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
  if (i < 0 || i >= d->nlay || d->nlay < 2) return 0;
  doc_begin(d, "Delete Layer");
  doc_note_all(d);
  layer_free(d->lay[i]);
  memmove(&d->lay[i], &d->lay[i + 1], (size_t)(d->nlay - i - 1) * sizeof(Layer *));
  d->nlay--;
  if (d->active >= d->nlay) d->active = d->nlay - 1;
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
  Layer *c;
  if (i < 0 || i >= d->nlay || d->nlay >= MAXLAYERS || !(c = layer_copy(d->lay[i], d->w, d->h))) return 0;
  {
    char nm[64];
    size_t l = strlen(d->lay[i]->name);
    if (l > 54) l = 54;
    memcpy(nm, d->lay[i]->name, l); strcpy(nm + l, " copy");
    strcpy(c->name, nm);
  }
  doc_begin(d, "Duplicate Layer");
  doc_add_layer(d, c, i + 1);
  doc_end(d);
  return 1;
}
/* layer src blended onto layer dst (both canvas-size) */
static void merge_into(Doc *d, Layer *dst, const Layer *src)
{
  long i, n = (long)d->w * d->h;
  if (!src->visible) return;
  for (i = 0; i < n; i++) if (src->px[i * 4 + 3]) blend_pixel(dst->px + i * 4, src->px + i * 4, src->opacity, src->blend);
}
int doc_merge_down(Doc *d, int i)
{
  Layer *below;
  if (i < 1 || i >= d->nlay) return 0;
  doc_begin(d, "Merge Down");
  doc_note_all(d);
  below = d->lay[i - 1];
  if (below->opacity != 255 || !below->visible) {
    /* the layer below's opacity goes into its pixels first, so the merge looks as before */
    long k, n = (long)d->w * d->h;
    if (!below->visible) memset(below->px, 0, (size_t)n * 4);
    else for (k = 0; k < n; k++) below->px[k * 4 + 3] = (u8)mul255(below->px[k * 4 + 3], below->opacity);
    below->opacity = 255; below->visible = 1;
  }
  merge_into(d, below, d->lay[i]);
  layer_free(d->lay[i]);
  memmove(&d->lay[i], &d->lay[i + 1], (size_t)(d->nlay - i - 1) * sizeof(Layer *));
  d->nlay--;
  d->active = i - 1;
  doc_end(d);
  return 1;
}
int doc_merge_visible(Doc *d)
{
  Layer *m;
  int i, k, first = -1;
  for (i = 0; i < d->nlay; i++) if (d->lay[i]->visible) { if (first < 0) first = i; }
  if (first < 0) return 0;
  if (!(m = layer_new(d, d->lay[first]->name))) return 0;
  doc_composite(d, doc_box(d), m->px, (long)d->w * 4);
  doc_begin(d, "Merge Visible");
  doc_note_all(d);
  for (i = k = 0; i < d->nlay; i++) {
    if (i == first) { layer_free(d->lay[i]); d->lay[k++] = m; }
    else if (d->lay[i]->visible) layer_free(d->lay[i]);
    else d->lay[k++] = d->lay[i];
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
  if (blend >= 0 && blend < BM_N) l->blend = (u8)blend;
  if (visible >= 0) l->visible = (u8)(visible != 0);
  doc_end(d);
}
