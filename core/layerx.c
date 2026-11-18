/*
 * layerx.c - Easel 1.2's layer extras (layerx.h): vector masks, linked layers, pattern fill layers
 * and Pattern Overlay, smart objects with their source on disk, artboards and frames.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "layerx.h"
#include "layers.h"
#include "path.h"
#include "pattern.h"
#include "paint.h"
#include "codec.h"
#include "live.h"

static int mul255(int a, int b) { int t = a * b + 128; return (t + (t >> 8)) >> 8; }

const LayerX *layerx_get(const Layer *l)
{
  const LayerX *x = l ? (const LayerX *)l->x : NULL;
  if (!x || l->xlen < sizeof(LayerX) || x->magic != LAYERX_MAGIC || x->len != l->xlen) return NULL;
  return x;
}
static const char *vm_svg(const Layer *l)
{
  const LayerX *x = layerx_get(l);
  if (!x || !x->vm_has || !x->vm_len || x->vm_off + x->vm_len > x->len) return NULL;
  return (const char *)l->x + x->vm_off;
}
static const u8 *pat_px(const Layer *l)
{
  const LayerX *x = layerx_get(l);
  if (!x || !x->pat_off || x->pat_w < 1 || x->pat_h < 1 || x->pat_off + (u32)x->pat_w * x->pat_h * 4 > x->len) return NULL;
  return (const u8 *)l->x + x->pat_off;
}

/* the block made again from its header and parts (svg NULL: none; pat NULL: none), put on layer i
   inside the open step (noted first). 0 if out of memory */
static int x_put(Doc *d, int i, const LayerX *h, const char *svg, const u8 *pat, i32 pw, i32 ph)
{
  Layer *l = d->lay[i];
  size_t sl = svg ? strlen(svg) + 1 : 0, pl = pat ? (size_t)pw * ph * 4 : 0, n = sizeof(LayerX) + sl + pl;
  LayerX *x;
  u8 *b;
  /* (svg and pat may point into the old block: it is freed only after the copy) */
  if (!(b = (u8 *)calloc(1, n))) return 0;
  x = (LayerX *)b;
  *x = *h;
  x->magic = LAYERX_MAGIC; x->len = (u32)n;
  x->vm_off = x->vm_len = 0; x->pat_off = 0;
  if (svg) { x->vm_off = sizeof(LayerX); x->vm_len = (u32)sl; memcpy(b + x->vm_off, svg, sl); }
  else x->vm_has = 0;
  if (pat) { x->pat_off = (u32)(sizeof(LayerX) + sl); x->pat_w = pw; x->pat_h = ph; memcpy(b + x->pat_off, pat, pl); }
  else x->pat_w = x->pat_h = 0;
  doc_note_x(d, i);
  free(l->x);
  l->x = b; l->xlen = (u32)n; l->xc_ok = 0; l->fx_ok = 0;
  return 1;
}
/* a header to change: the layer's, or a fresh one */
static void x_head(const Layer *l, LayerX *h)
{
  const LayerX *x = layerx_get(l);
  if (x) *h = *x; else { memset(h, 0, sizeof *h); }
}
static int x_put_same(Doc *d, int i, const LayerX *h)
{
  Layer *l = d->lay[i];
  const LayerX *x = layerx_get(l);
  return x_put(d, i, h, h->vm_has ? vm_svg(l) : NULL, x ? pat_px(l) : NULL, x ? x->pat_w : 0, x ? x->pat_h : 0);
}
/* nothing left in the block: dropped */
static void x_tidy(Doc *d, int i)
{
  Layer *l = d->lay[i];
  const LayerX *x = layerx_get(l);
  if (!x || x->vm_has || x->pat_off || x->so_on || x->fr_kind || x->po_on) return;
  doc_note_x(d, i);
  free(l->x); l->x = NULL; l->xlen = 0; l->xc_ok = 0;
}

/* ------------------------------------------------------------------ vector masks */
int layer_has_vmask(const Layer *l) { return vm_svg(l) != NULL; }
struct Path *layer_vmask_path(const Layer *l) { const char *s = vm_svg(l); return s ? path_from_svg(s) : NULL; }
/* the xc cache: w, h, then the vector mask's own coverage and the combined plane */
typedef struct { i32 w, h; u8 *vm, *comb; } XCache;
static void xc_drop(Layer *l)
{
  XCache *c = (XCache *)l->xc;
  if (c) { free(c->vm); free(c->comb); free(c); }
  l->xc = NULL; l->xc_ok = 0;
}
static void box3(u8 *m, i32 w, i32 h, int r)
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
static XCache *vm_make(Doc *d, Layer *l)
{
  const LayerX *x = layerx_get(l);
  XCache *c = (XCache *)l->xc;
  long k, n = (long)d->w * d->h;
  if (c && (c->w != d->w || c->h != d->h)) { xc_drop(l); c = NULL; }
  if (c && l->xc_ok) return c;
  if (!c) {
    if (!(c = (XCache *)calloc(1, sizeof *c))) return NULL;
    l->xc = c;
  }
  c->w = d->w; c->h = d->h;
  if (!c->vm && !(c->vm = (u8 *)malloc((size_t)n))) return NULL;
  memset(c->vm, 0, (size_t)n);
  {
    Path *p = layer_vmask_path(l);
    Poly pl;
    poly_init(&pl);
    if (p && path_flatten(p, 0.25, &pl) && pl.n) poly_raster(&pl, p->evenodd, doc_box(d), c->vm, d->w);
    poly_free(&pl); path_free(p);
  }
  if (x->vm_invert) for (k = 0; k < n; k++) c->vm[k] = (u8)(255 - c->vm[k]);
  if (x->vm_feather) { int pass, r = (x->vm_feather + 1) / 2; for (pass = 0; pass < 3; pass++) box3(c->vm, d->w, d->h, r > 1 ? r / 2 + 1 : 1); }
  if (x->vm_density && x->vm_density < 100) {
    int f = 255 - x->vm_density * 255 / 100;
    for (k = 0; k < n; k++) c->vm[k] = (u8)(255 - mul255(255 - c->vm[k], 255 - f));
  }
  free(c->comb); c->comb = NULL;
  if (l->mask && l->mask_on) {
    if ((c->comb = (u8 *)malloc((size_t)n)) != NULL) {
      /* (the layer mask's own feather is left to the vector mask's) */
      for (k = 0; k < n; k++) c->comb[k] = (u8)mul255(c->vm[k], l->mask[k]);
    }
  }
  l->xc_ok = 1;
  return c;
}
static const u8 *hook_vmask(Doc *d, Layer *l)
{
  const LayerX *x = layerx_get(l);
  XCache *c;
  if (!x || !x->vm_has || !x->vm_on) return NULL;
  if (!(c = vm_make(d, l))) return NULL;
  return c->comb ? c->comb : c->vm;
}
int layer_vmask_at(Doc *d, Layer *l, i32 x, i32 y)
{
  const LayerX *lx = layerx_get(l);
  XCache *c;
  if (!lx || !lx->vm_has || x < 0 || y < 0 || x >= d->w || y >= d->h || !(c = vm_make(d, l))) return 255;
  return c->vm[(long)y * d->w + x];
}
int doc_vmask_add(Doc *d, int i, int how, const struct Path *p)
{
  LayerX h;
  char *svg;
  int ok;
  Layer *l;
  if (i < 0 || i >= d->nlay || (l = d->lay[i])->lock_all) return 0;
  if (how == VM_PATH && !p) return 0;
  x_head(l, &h);
  h.vm_has = 1; h.vm_on = 1; h.vm_linked = 1; h.vm_invert = how == VM_HIDE_ALL;
  if (how == VM_PATH) svg = path_to_svg(p);
  else svg = str_dup("");
  if (!svg) return 0;
  if (how == VM_REVEAL_ALL) h.vm_invert = 1;     /* an empty path, its outside shown: everything */
  if (how == VM_HIDE_ALL) h.vm_invert = 0;       /* an empty path, its inside shown: nothing */
  doc_begin(d, how == VM_PATH ? "Add Vector Mask" : how == VM_HIDE_ALL ? "Vector Mask Hide All" : "Vector Mask Reveal All");
  ok = x_put(d, i, &h, svg, pat_px(l), h.pat_w, h.pat_h);
  doc_end(d);
  free(svg);
  return ok;
}
int doc_vmask_delete(Doc *d, int i)
{
  LayerX h;
  if (i < 0 || i >= d->nlay || !layer_has_vmask(d->lay[i])) return 0;
  x_head(d->lay[i], &h);
  h.vm_has = 0;
  doc_begin(d, "Delete Vector Mask");
  x_put(d, i, &h, NULL, pat_px(d->lay[i]), h.pat_w, h.pat_h);
  x_tidy(d, i);
  doc_end(d);
  return 1;
}
int doc_vmask_set(Doc *d, int i, int on, int linked, int feather, int density)
{
  LayerX h;
  int ok;
  if (i < 0 || i >= d->nlay || !layer_has_vmask(d->lay[i])) return 0;
  x_head(d->lay[i], &h);
  if (on >= 0) h.vm_on = (u8)(on != 0);
  if (linked >= 0) h.vm_linked = (u8)(linked != 0);
  if (feather >= 0) h.vm_feather = (u16)(feather > 1000 ? 1000 : feather);
  if (density >= 0) h.vm_density = (u8)(density > 100 ? 100 : density);
  doc_begin(d, "Vector Mask Properties");
  ok = x_put_same(d, i, &h);
  doc_end(d);
  return ok;
}
int doc_vmask_rasterize(Doc *d, int i)
{
  Layer *l;
  XCache *c;
  long k, n = (long)d->w * d->h;
  if (i < 0 || i >= d->nlay || !layer_has_vmask(l = d->lay[i])) return 0;
  if (!(c = vm_make(d, l))) return 0;
  doc_begin(d, "Rasterize Vector Mask");
  if (!l->mask) {
    doc_note_all(d);
    if (!(l->mask = (u8 *)malloc((size_t)n))) { doc_end(d); return 0; }
    memcpy(l->mask, c->vm, (size_t)n);
    l->mask_on = 1; l->mask_linked = 1;
  } else {
    doc_note_mask(d, i, doc_box(d));
    for (k = 0; k < n; k++) l->mask[k] = (u8)mul255(l->mask[k], c->vm[k]);
  }
  layer_mask_changed(l);
  doc_vmask_delete(d, i);
  doc_end(d);
  return 1;
}

/* ------------------------------------------------------------------ linked layers */
static int is_picked(const Doc *d, int i) { return i == d->active || d->lay[i]->picked; }
int doc_link_layers(Doc *d)
{
  int i, n = 0, id = 0;
  for (i = 0; i < d->nlay; i++) { if (d->lay[i]->link > id) id = d->lay[i]->link; if (is_picked(d, i)) n++; }
  if (n < 2) return 0;
  doc_begin(d, "Link Layers");
  for (i = 0; i < d->nlay; i++) if (is_picked(d, i)) d->lay[i]->link = (u16)(id + 1);
  doc_end(d);
  return 1;
}
int doc_unlink_layers(Doc *d)
{
  int i, n = 0;
  for (i = 0; i < d->nlay; i++) if (is_picked(d, i) && d->lay[i]->link) n++;
  if (!n) return 0;
  doc_begin(d, "Unlink Layers");
  for (i = 0; i < d->nlay; i++) if (is_picked(d, i)) d->lay[i]->link = 0;
  /* a link of one is no link */
  for (i = 0; i < d->nlay; i++) if (d->lay[i]->link) {
    int k, m = 0;
    for (k = 0; k < d->nlay; k++) if (d->lay[k]->link == d->lay[i]->link) m++;
    if (m < 2) d->lay[i]->link = 0;
  }
  doc_end(d);
  return 1;
}
int doc_select_linked(Doc *d)
{
  int i, n = 0, id;
  if (d->active < 0 || d->active >= d->nlay || !(id = d->lay[d->active]->link)) return 0;
  for (i = 0; i < d->nlay; i++) { d->lay[i]->picked = d->lay[i]->link == id; n += d->lay[i]->picked; }
  return n;
}
/* layer i's pixels, mask and live settings moved (no selection), inside the open step */
static Box shift_one(Doc *d, int i, i32 dx, i32 dy)
{
  Layer *l = d->lay[i];
  i32 x, y;
  if (l->lock_pos || l->lock_all) return box_make(0, 0, 0, 0);
  if (l->x && layerx_move_hook) layerx_move_hook(d, i, dx, dy);
  if (!l->px && l->live) {
    double m[6] = { 1, 0, 0, 0, 1, 0 };
    m[2] = dx; m[5] = dy;
    doc_note_live(d, i);
    live_transform(l, m);
    return doc_box(d);
  }
  if (l->px) {
    u8 *n = (u8 *)calloc((size_t)d->w * d->h, 4);
    if (!n) return box_make(0, 0, 0, 0);
    doc_note_layer(d, i, doc_box(d));
    for (y = 0; y < d->h; y++) for (x = 0; x < d->w; x++) {
      i32 sx = x - dx, sy = y - dy;
      if (sx >= 0 && sy >= 0 && sx < d->w && sy < d->h) memcpy(n + ((long)y * d->w + x) * 4, l->px + ((long)sy * d->w + sx) * 4, 4);
    }
    memcpy(l->px, n, (size_t)d->w * d->h * 4);
    free(n);
  }
  if (l->mask && l->mask_linked) {
    u8 *m = (u8 *)malloc((size_t)d->w * d->h);
    if (m) {
      doc_note_mask(d, i, doc_box(d));
      memset(m, 255, (size_t)d->w * d->h);
      for (y = 0; y < d->h; y++) for (x = 0; x < d->w; x++) {
        i32 sx = x - dx, sy = y - dy;
        if (sx >= 0 && sy >= 0 && sx < d->w && sy < d->h) m[(long)y * d->w + x] = l->mask[(long)sy * d->w + sx];
      }
      memcpy(l->mask, m, (size_t)d->w * d->h); free(m);
      layer_mask_changed(l);
    }
  }
  return doc_box(d);
}
Box doc_move_linked(Doc *d, i32 dx, i32 dy)
{
  Box r = box_make(0, 0, 0, 0);
  int i, id;
  if (d->active < 0 || d->active >= d->nlay || (!dx && !dy)) return r;
  id = d->lay[d->active]->link;
  doc_begin(d, "Move");
  if (d->lay[d->active]->px && d->lay[d->active]->kind == LK_PIXEL) r = move_layer(d, dx, dy);
  else r = shift_one(d, d->active, dx, dy);
  if (id && !d->sel)
    for (i = 0; i < d->nlay; i++) if (i != d->active && d->lay[i]->link == id) r = box_union(r, shift_one(d, i, dx, dy));
  doc_end(d);
  return r;
}

/* ------------------------------------------------------------------ patterns */
const u8 *layerx_pattern_at(const Layer *l, i32 x, i32 y)
{
  const LayerX *h = layerx_get(l);
  const u8 *p = pat_px(l);
  int sc;
  i32 u, v;
  if (!p) return NULL;
  sc = h->pat_scale ? h->pat_scale : 100;
  u = (i32)floor((x - h->pat_ox) * 100.0 / sc); v = (i32)floor((y - h->pat_oy) * 100.0 / sc);
  u %= h->pat_w; if (u < 0) u += h->pat_w;
  v %= h->pat_h; if (v < 0) v += h->pat_h;
  return p + ((long)v * h->pat_w + u) * 4;
}
static void hook_fill_row(Doc *d, Layer *l, i32 y, i32 x0, i32 n, u8 *out)
{
  i32 x;
  (void)d;
  for (x = 0; x < n; x++) {
    const u8 *p = layerx_pattern_at(l, x0 + x, y);
    if (p) memcpy(out + x * 4, p, 4); else memset(out + x * 4, 0, 4);
  }
}
static void hook_overlay_row(Doc *d, Layer *l, i32 y, i32 x0, i32 n, u8 *acc, const u8 *shape, int keep)
{
  const LayerX *h = layerx_get(l);
  i32 x;
  if (!h || !h->po_on || !h->po_opacity || l->fx.hidden) return;
  for (x = 0; x < n; x++) {
    const u8 *p;
    u8 *q = acc + x * 4, a = q[3];
    int op;
    if (!shape[x] || !(p = layerx_pattern_at(l, x0 + x, y))) continue;
    op = mul255(mul255(shape[x], h->po_opacity), p[3]);
    if (!op) continue;
    blend_pixel_at(q, p, op, h->po_blend, x0 + x, y);
    if (keep) q[3] = a;
  }
  (void)d;
}
int doc_new_pattern_fill(Doc *d, const struct Pattern *p, int scale, const char *name)
{
  Layer *l;
  LayerX h;
  int at, ok;
  if (!p || !p->px || p->w < 1 || p->h < 1 || d->nlay >= MAXLAYERS) return -1;
  if (!(l = (Layer *)calloc(1, sizeof *l))) return -1;
  strncpy(l->name, name ? name : "Pattern Fill 1", sizeof l->name - 1);
  l->opacity = 255; l->visible = 1; l->kind = LK_FILL; l->fillp.kind = 2;
  at = d->active >= 0 && d->active < d->nlay ? d->active + 1 : d->nlay;
  l->depth = d->active >= 0 && d->active < d->nlay ? d->lay[d->active]->depth : 0;
  doc_begin(d, "New Fill Layer");
  if (!doc_add_layer(d, l, at)) { doc_end(d); return -1; }
  memset(&h, 0, sizeof h);
  h.pat_scale = (u16)(scale < 1 ? 100 : scale > 1000 ? 1000 : scale);
  ok = x_put(d, at, &h, NULL, p->px, p->w, p->h);
  doc_end(d);
  return ok ? at : -1;
}
int doc_set_pattern_fill(Doc *d, int i, const struct Pattern *p, int scale)
{
  LayerX h;
  Layer *l;
  int ok;
  if (i < 0 || i >= d->nlay || (l = d->lay[i])->kind != LK_FILL) return 0;
  x_head(l, &h);
  if (scale > 0) h.pat_scale = (u16)(scale > 1000 ? 1000 : scale);
  doc_begin(d, "Fill Layer Settings");
  l->fillp.kind = 2;
  if (p && p->px) ok = x_put(d, i, &h, h.vm_has ? vm_svg(l) : NULL, p->px, p->w, p->h);
  else ok = x_put_same(d, i, &h);
  doc_end(d);
  return ok;
}
int doc_set_pattern_overlay(Doc *d, int i, int on, const struct Pattern *p, int blend, int opacity, int scale)
{
  LayerX h;
  Layer *l;
  int ok;
  if (i < 0 || i >= d->nlay || (l = d->lay[i])->kind == LK_GROUP || l->kind == LK_ADJUST) return 0;
  x_head(l, &h);
  if (!p && !pat_px(l) && on) return 0;
  h.po_on = (u8)(on != 0);
  if (blend >= 0 && blend < BM_N) h.po_blend = (u8)blend;
  if (opacity >= 0) h.po_opacity = (u8)(opacity > 255 ? 255 : opacity);
  if (scale > 0) h.pat_scale = (u16)(scale > 1000 ? 1000 : scale);
  doc_begin(d, "Pattern Overlay");
  if (p && p->px) ok = x_put(d, i, &h, h.vm_has ? vm_svg(l) : NULL, p->px, p->w, p->h);
  else ok = x_put_same(d, i, &h);
  if (!on && l->kind != LK_FILL) {
    /* off: the pattern goes too, unless something else uses it */
    x_head(l, &h);
    ok = x_put(d, i, &h, h.vm_has ? vm_svg(l) : NULL, NULL, 0, 0) && ok;
    x_tidy(d, i);
  }
  doc_end(d);
  return ok;
}

/* ------------------------------------------------------------------ smart objects */
char smart_drawer[256];
int layer_is_smart(const Layer *l) { const LayerX *x = layerx_get(l); return x && x->so_on; }
const char *layer_smart_path(const Layer *l) { const LayerX *x = layerx_get(l); return x && x->so_on ? x->so_path : ""; }
static void join_path(char *out, size_t cap, const char *dir, const char *name)
{
  size_t n = strlen(dir);
  if (!n) snprintf(out, cap, "%s", name);
  else if (dir[n - 1] == ':' || dir[n - 1] == '/') snprintf(out, cap, "%s%s", dir, name);
  else snprintf(out, cap, "%s/%s", dir, name);
}
static unsigned long smart_serial;
/* a new file name in the drawer, not used yet */
static void smart_name(char *out, size_t cap)
{
  int tries;
  for (tries = 0; tries < 10000; tries++) {
    char nm[48];
    FILE *f;
    sprintf(nm, "EaselSmart%04lu.png", ++smart_serial);
    join_path(out, cap, smart_drawer, nm);
    if (!(f = fopen(out, "rb"))) return;
    fclose(f);
  }
}
static int write_png(const char *path, const u8 *px, i32 w, i32 h)
{
  Rgba im;
  size_t len;
  u8 *png;
  int ok;
  im.w = w; im.h = h; im.px = (u8 *)px;
  if (!(png = png_encode(&im, 72, &len))) return 0;
  ok = file_write(path, png, len);
  free(png);
  return ok;
}
static int read_pic(const char *path, Rgba *im)
{
  size_t n;
  u8 *data = file_read(path, &n);
  char err[80];
  int ok;
  memset(im, 0, sizeof *im);
  if (!data) return 0;
  ok = codec_decode(data, n, im, err, sizeof err);
  free(data);
  return ok;
}
/* the layer's pixels made again from its source through so_m (bilinear, the inverse map) */
static int smart_render(Doc *d, int i)
{
  Layer *l = d->lay[i];
  const LayerX *h = layerx_get(l);
  const double *m;
  double det, a, b, c, e, tx, ty;
  Rgba im;
  i32 x, y;
  if (!h || !h->so_on || !l->px) return 0;
  if (!read_pic(h->so_path, &im)) return 0;
  m = h->so_m;
  det = m[0] * m[4] - m[1] * m[3];
  if (fabs(det) < 1e-12) { rgba_free(&im); return 0; }
  a = m[4] / det; b = -m[1] / det; c = -m[3] / det; e = m[0] / det;
  tx = -(a * m[2] + b * m[5]); ty = -(c * m[2] + e * m[5]);
  doc_note_layer(d, i, doc_box(d));
  for (y = 0; y < d->h && !progress(y, d->h); y++)
    for (x = 0; x < d->w; x++) {
      double px = x + 0.5, py = y + 0.5, sx = a * px + b * py + tx, sy = c * px + e * py + ty;
      u8 *o = l->px + ((long)y * d->w + x) * 4;
      if (sx < -0.5 || sy < -0.5 || sx > im.w + 0.5 || sy > im.h + 0.5) { memset(o, 0, 4); continue; }
      sample_bilinear(im.px, im.w, im.h, sx, sy, o);
      if (sx < 0 || sy < 0 || sx > im.w || sy > im.h) {
        /* the half pixel round the source's edge fades out */
        double fx = sx < 0 ? 1 + sx * 2 : sx > im.w ? 1 - (sx - im.w) * 2 : 1, fy = sy < 0 ? 1 + sy * 2 : sy > im.h ? 1 - (sy - im.h) * 2 : 1;
        double f = fx < fy ? fx : fy;
        o[3] = (u8)(o[3] * (f < 0 ? 0 : f));
      }
    }
  rgba_free(&im);
  l->fx_ok = 0;
  return 1;
}
int doc_smart_convert(Doc *d, int i)
{
  Layer *l;
  LayerX h;
  Box b;
  u8 *crop;
  i32 y;
  char path[256];
  if (i < 0 || i >= d->nlay || !(l = d->lay[i])->px || l->kind != LK_PIXEL || layer_is_smart(l)) return 0;
  if (!layer_content_box(d, i, &b)) b = doc_box(d);
  if (!(crop = (u8 *)malloc((size_t)(b.x1 - b.x0) * (b.y1 - b.y0) * 4))) return 0;
  for (y = b.y0; y < b.y1; y++) memcpy(crop + (size_t)(y - b.y0) * (b.x1 - b.x0) * 4, l->px + ((long)y * d->w + b.x0) * 4, (size_t)(b.x1 - b.x0) * 4);
  smart_name(path, sizeof path);
  if (!write_png(path, crop, b.x1 - b.x0, b.y1 - b.y0)) { free(crop); return 0; }
  free(crop);
  x_head(l, &h);
  h.so_on = 1; h.so_linked = 0; h.so_w = b.x1 - b.x0; h.so_h = b.y1 - b.y0;
  h.so_m[0] = 1; h.so_m[1] = 0; h.so_m[2] = b.x0; h.so_m[3] = 0; h.so_m[4] = 1; h.so_m[5] = b.y0;
  strncpy(h.so_path, path, sizeof h.so_path - 1);
  strncpy(h.so_name, l->name, sizeof h.so_name - 1);
  doc_begin(d, "Convert to Smart Object");
  if (!x_put_same(d, i, &h)) { doc_end(d); remove(path); return 0; }
  doc_end(d);
  return 1;
}
int doc_smart_copy(Doc *d, int i)
{
  Layer *c;
  LayerX h;
  char path[256];
  Rgba im;
  int at;
  if (i < 0 || i >= d->nlay || !layer_is_smart(d->lay[i])) return -1;
  x_head(d->lay[i], &h);
  if (!read_pic(h.so_path, &im)) return -1;
  smart_name(path, sizeof path);
  if (!write_png(path, im.px, im.w, im.h)) { rgba_free(&im); return -1; }
  rgba_free(&im);
  if (!(c = layer_copy(d->lay[i], d->w, d->h))) { remove(path); return -1; }
  free(c->x); c->x = NULL; c->xlen = 0;
  c->picked = 0;
  snprintf(c->name, sizeof c->name, "%.58s copy", d->lay[i]->name);
  at = i + 1;
  doc_begin(d, "New Smart Object via Copy");
  if (!doc_add_layer(d, c, at)) { doc_end(d); remove(path); return -1; }
  h.so_linked = 0;
  strncpy(h.so_path, path, sizeof h.so_path - 1);
  x_put(d, at, &h, h.vm_has ? vm_svg(d->lay[i]) : NULL, pat_px(d->lay[i]), h.pat_w, h.pat_h);
  doc_end(d);
  return at;
}
int doc_smart_transform(Doc *d, int i, const double *m)
{
  LayerX h;
  double n[6];
  const double *o;
  if (i < 0 || i >= d->nlay || !layer_is_smart(d->lay[i]) || d->lay[i]->lock_pos || d->lay[i]->lock_all) return 0;
  x_head(d->lay[i], &h);
  o = h.so_m;
  n[0] = m[0] * o[0] + m[1] * o[3]; n[1] = m[0] * o[1] + m[1] * o[4]; n[2] = m[0] * o[2] + m[1] * o[5] + m[2];
  n[3] = m[3] * o[0] + m[4] * o[3]; n[4] = m[3] * o[1] + m[4] * o[4]; n[5] = m[3] * o[2] + m[4] * o[5] + m[5];
  memcpy(h.so_m, n, sizeof n);
  doc_begin(d, "Transform Smart Object");
  if (!x_put_same(d, i, &h) || !smart_render(d, i)) { doc_end(d); doc_undo(d); return 0; }
  doc_end(d);
  return 1;
}
int doc_smart_reset(Doc *d, int i)
{
  LayerX h;
  double cx, cy, ox, oy;
  if (i < 0 || i >= d->nlay || !layer_is_smart(d->lay[i])) return 0;
  x_head(d->lay[i], &h);
  /* back to 100 %, unturned, its centre kept where it is */
  cx = h.so_m[0] * h.so_w / 2 + h.so_m[1] * h.so_h / 2 + h.so_m[2];
  cy = h.so_m[3] * h.so_w / 2 + h.so_m[4] * h.so_h / 2 + h.so_m[5];
  ox = floor(cx - h.so_w / 2.0 + 0.5); oy = floor(cy - h.so_h / 2.0 + 0.5);
  h.so_m[0] = 1; h.so_m[1] = 0; h.so_m[2] = ox; h.so_m[3] = 0; h.so_m[4] = 1; h.so_m[5] = oy;
  doc_begin(d, "Reset Transform");
  if (!x_put_same(d, i, &h) || !smart_render(d, i)) { doc_end(d); doc_undo(d); return 0; }
  doc_end(d);
  return 1;
}
/* a new source (copied into the drawer unless linked), the transform scaled to keep its size */
static int smart_source(Doc *d, int i, const char *file, int linked, const char *what)
{
  LayerX h;
  Rgba im;
  char path[256];
  double sx, sy;
  if (i < 0 || i >= d->nlay || !layer_is_smart(d->lay[i])) return 0;
  x_head(d->lay[i], &h);
  if (!read_pic(file, &im)) return 0;
  if (linked) strncpy(path, file, sizeof path - 1), path[sizeof path - 1] = 0;
  else {
    smart_name(path, sizeof path);
    if (!write_png(path, im.px, im.w, im.h)) { rgba_free(&im); return 0; }
  }
  sx = (double)h.so_w / im.w; sy = (double)h.so_h / im.h;
  if (!what) sx = sy = 1;            /* (an update keeps the scale the layer has) */
  h.so_m[0] *= sx; h.so_m[3] *= sx; h.so_m[1] *= sy; h.so_m[4] *= sy;
  h.so_w = im.w; h.so_h = im.h;
  rgba_free(&im);
  if (!h.so_linked && strcmp(h.so_path, path)) remove(h.so_path);
  h.so_linked = (u8)linked;
  strncpy(h.so_path, path, sizeof h.so_path - 1);
  doc_begin(d, what ? what : "Update Modified Content");
  if (!x_put_same(d, i, &h) || !smart_render(d, i)) { doc_end(d); return 0; }
  doc_end(d);
  return 1;
}
int doc_smart_replace(Doc *d, int i, const char *file) { return smart_source(d, i, file, 0, "Replace Contents"); }
int doc_smart_relink(Doc *d, int i, const char *file) { return smart_source(d, i, file, 1, "Relink to File"); }
int doc_smart_update(Doc *d, int i)
{
  if (i < 0 || i >= d->nlay || !layer_is_smart(d->lay[i])) return 0;
  doc_begin(d, "Update Modified Content");
  if (!smart_render(d, i)) { doc_end(d); return 0; }
  doc_end(d);
  return 1;
}
int doc_smart_update_all(Doc *d)
{
  int i, n = 0;
  doc_begin(d, "Update All Modified Content");
  for (i = 0; i < d->nlay; i++) if (layer_is_smart(d->lay[i]) && layerx_get(d->lay[i])->so_linked) n += smart_render(d, i);
  doc_end(d);
  return n;
}
int doc_smart_export(Doc *d, int i, const char *file)
{
  Rgba im;
  int ok;
  if (i < 0 || i >= d->nlay || !layer_is_smart(d->lay[i])) return 0;
  if (!read_pic(layerx_get(d->lay[i])->so_path, &im)) return 0;
  ok = write_png(file, im.px, im.w, im.h);
  rgba_free(&im);
  return ok;
}
int doc_smart_embed(Doc *d, int i)
{
  LayerX h;
  Rgba im;
  char path[256];
  if (i < 0 || i >= d->nlay || !layer_is_smart(d->lay[i]) || !layerx_get(d->lay[i])->so_linked) return 0;
  x_head(d->lay[i], &h);
  if (!read_pic(h.so_path, &im)) return 0;
  smart_name(path, sizeof path);
  if (!write_png(path, im.px, im.w, im.h)) { rgba_free(&im); return 0; }
  rgba_free(&im);
  h.so_linked = 0; strncpy(h.so_path, path, sizeof h.so_path - 1);
  doc_begin(d, "Convert to Embedded");
  x_put_same(d, i, &h);
  doc_end(d);
  return 1;
}
int doc_smart_link(Doc *d, int i, const char *file)
{
  LayerX h;
  if (i < 0 || i >= d->nlay || !layer_is_smart(d->lay[i])) return 0;
  if (!doc_smart_export(d, i, file)) return 0;
  x_head(d->lay[i], &h);
  if (!h.so_linked) remove(h.so_path);
  h.so_linked = 1; strncpy(h.so_path, file, sizeof h.so_path - 1); h.so_path[sizeof h.so_path - 1] = 0;
  doc_begin(d, "Convert to Linked");
  x_put_same(d, i, &h);
  doc_end(d);
  return 1;
}
int doc_smart_rasterize(Doc *d, int i)
{
  LayerX h;
  if (i < 0 || i >= d->nlay || !layer_is_smart(d->lay[i])) return 0;
  x_head(d->lay[i], &h);
  /* (an embedded source's file stays while undo may want it; the scratch drawer is cleared at quit) */
  h.so_on = 0; h.so_path[0] = 0;
  doc_begin(d, "Rasterize Smart Object");
  x_put_same(d, i, &h);
  x_tidy(d, i);
  doc_end(d);
  return 1;
}
int doc_smart_to_layers(Doc *d, int i)
{
  const LayerX *h;
  Rgba im;
  Layer *l;
  i32 x, y, ox, oy;
  int at;
  if (i < 0 || i >= d->nlay || !layer_is_smart(d->lay[i])) return 0;
  h = layerx_get(d->lay[i]);
  if (!read_pic(h->so_path, &im)) return 0;
  ox = (i32)floor(h->so_m[2] + 0.5); oy = (i32)floor(h->so_m[5] + 0.5);
  if (!(l = layer_new(d, h->so_name[0] ? h->so_name : "Layer"))) { rgba_free(&im); return 0; }
  for (y = 0; y < im.h; y++) for (x = 0; x < im.w; x++) {
    i32 cx = x + ox, cy = y + oy;
    if (cx >= 0 && cy >= 0 && cx < d->w && cy < d->h) memcpy(l->px + ((long)cy * d->w + cx) * 4, im.px + ((long)y * im.w + x) * 4, 4);
  }
  rgba_free(&im);
  l->depth = d->lay[i]->depth;
  at = i + 1;
  doc_begin(d, "Convert to Layers");
  if (!doc_add_layer(d, l, at)) { doc_end(d); return 0; }
  d->lay[i]->visible = 0;
  doc_end(d);
  return 1;
}
Doc *smart_open(const char *file, char *err, int errcap)
{
  Rgba im;
  Doc *d;
  if (!read_pic(file, &im)) { if (err && errcap > 0) snprintf(err, (size_t)errcap, "%s", "The picture could not be read."); return NULL; }
  if (!(d = doc_adopt(im.w, im.h, im.px))) { rgba_free(&im); return NULL; }
  {
    const char *b = file + strlen(file);
    while (b > file && b[-1] != '/' && b[-1] != ':') b--;
    strncpy(d->lay[0]->name, b, sizeof d->lay[0]->name - 1);
  }
  if (!doc_smart_convert(d, 0)) { doc_free(d); if (err && errcap > 0) snprintf(err, (size_t)errcap, "%s", "The smart object's source could not be written."); return NULL; }
  doc_forget_undo(d);
  return d;
}

/* ------------------------------------------------------------------ artboards and frames */
/* a group (bare, of depth) inserted at at inside the open step; its index */
static Layer *bare(const char *name, int kind)
{
  Layer *l = (Layer *)calloc(1, sizeof *l);
  if (!l) return NULL;
  strncpy(l->name, name, sizeof l->name - 1);
  l->opacity = 255; l->visible = 1; l->kind = (u8)kind;
  if (kind == LK_GROUP) l->blend = BM_PASS;
  return l;
}
int doc_new_artboard(Doc *d, Box b, long bg, const char *name)
{
  Layer *g;
  int at;
  b = box_clip(b, doc_box(d));
  if (box_empty(b) || d->nlay >= MAXLAYERS) return -1;
  if (!(g = bare(name ? name : "Artboard 1", LK_GROUP))) return -1;
  g->artboard = (u8)(bg >= 0 ? 2 : 1); g->abox = b; g->fillp.c0 = bg >= 0 ? (u32)bg & 0xFFFFFF : 0xFFFFFF;
  g->blend = BM_NORMAL;
  at = d->active >= 0 && d->active < d->nlay ? d->active + 1 : d->nlay;
  g->depth = 0;
  /* (artboards are top level: one goes above the active layer's top-level block) */
  while (at < d->nlay && d->lay[at]->depth > 0) at++;
  doc_begin(d, "New Artboard");
  if (!doc_add_layer(d, g, at)) { doc_end(d); return -1; }
  d->lay[at]->depth = 0;
  doc_end(d);
  return at;
}
int doc_artboard_from_group(Doc *d, int g, Box b, long bg)
{
  Layer *l;
  if (g < 0 || g >= d->nlay || (l = d->lay[g])->kind != LK_GROUP) return 0;
  b = box_clip(b, doc_box(d));
  if (box_empty(b)) return 0;
  doc_begin(d, "Artboard from Group");
  l->artboard = (u8)(bg >= 0 ? 2 : 1); l->abox = b; if (bg >= 0) l->fillp.c0 = (u32)bg & 0xFFFFFF;
  if (l->blend == BM_PASS) l->blend = BM_NORMAL;
  doc_end(d);
  return 1;
}
int doc_artboard_set(Doc *d, int g, Box b, long bg)
{
  Layer *l;
  if (g < 0 || g >= d->nlay || !(l = d->lay[g])->artboard) return 0;
  b = box_clip(b, doc_box(d));
  if (box_empty(b)) return 0;
  doc_begin(d, "Artboard");
  l->abox = b;
  if (bg >= 0) { l->artboard = 2; l->fillp.c0 = (u32)bg & 0xFFFFFF; }
  else if (bg == -1) l->artboard = 1;
  doc_end(d);
  return 1;
}
int doc_artboard_at(const Doc *d, i32 x, i32 y)
{
  int i;
  for (i = d->nlay - 1; i >= 0; i--) {
    const Layer *l = d->lay[i];
    if (l->artboard && x >= l->abox.x0 && x < l->abox.x1 && y >= l->abox.y0 && y < l->abox.y1) return i;
  }
  return -1;
}
int layer_is_frame(const Layer *l) { const LayerX *x = layerx_get(l); return x && x->fr_kind; }
/* the frame's shape as a path */
static Path *frame_path(int kind, Box b)
{
  Path *p = path_new();
  if (!p) return NULL;
  if (kind == FR_ELLIPSE) {
    double cx = (b.x0 + b.x1) / 2.0, cy = (b.y0 + b.y1) / 2.0, rx = (b.x1 - b.x0) / 2.0, ry = (b.y1 - b.y0) / 2.0, k = 0.5522847498;
    path_move_to(p, cx + rx, cy);
    path_curve_to(p, cx + rx, cy + ry * k, cx + rx * k, cy + ry, cx, cy + ry);
    path_curve_to(p, cx - rx * k, cy + ry, cx - rx, cy + ry * k, cx - rx, cy);
    path_curve_to(p, cx - rx, cy - ry * k, cx - rx * k, cy - ry, cx, cy - ry);
    path_curve_to(p, cx + rx * k, cy - ry, cx + rx, cy - ry * k, cx + rx, cy);
  } else {
    path_move_to(p, b.x0, b.y0); path_line_to(p, b.x1, b.y0); path_line_to(p, b.x1, b.y1); path_line_to(p, b.x0, b.y1);
  }
  path_close(p);
  return p;
}
/* the frame's stroke laid on its placeholder or content: a ring of width w along the shape's edge, inside */
static void frame_stroke_on(Doc *d, Layer *l, int kind, Box b, int w, u32 col)
{
  Path *p;
  Poly pl, st;
  StrokeStyle ss;
  u8 *cov;
  i32 x, y;
  if (w < 1 || !l->px) return;
  if (!(p = frame_path(kind, b))) return;
  poly_init(&pl); poly_init(&st);
  memset(&ss, 0, sizeof ss);
  ss.width = w * 2; ss.join = JOIN_MITER;
  if ((cov = (u8 *)calloc((size_t)d->w * d->h, 1)) != NULL && path_flatten(p, 0.25, &pl) && poly_stroke(&pl, &ss, &st)) {
    poly_raster(&st, 0, doc_box(d), cov, d->w);
    for (y = b.y0 < 0 ? 0 : b.y0; y < b.y1 && y < d->h; y++) for (x = b.x0 < 0 ? 0 : b.x0; x < b.x1 && x < d->w; x++) {
      int a = cov[(long)y * d->w + x];
      u8 c[4];
      if (!a) continue;
      c[0] = (u8)(col >> 16); c[1] = (u8)(col >> 8); c[2] = (u8)col; c[3] = 255;
      blend_pixel(l->px + ((long)y * d->w + x) * 4, c, a, BM_NORMAL);
    }
  }
  free(cov); poly_free(&pl); poly_free(&st); path_free(p);
}
int doc_new_frame(Doc *d, Box b, int kind, int stroke, u32 color)
{
  Layer *g, *ph;
  LayerX h;
  Path *p;
  char *svg;
  int at, depth, gi;
  i32 x, y;
  if (box_empty(box_clip(b, doc_box(d))) || d->nlay + 2 > MAXLAYERS || (kind != FR_RECT && kind != FR_ELLIPSE)) return -1;
  if (!(p = frame_path(kind, b))) return -1;
  svg = path_to_svg(p); path_free(p);
  if (!svg) return -1;
  g = bare("Frame 1", LK_GROUP);
  ph = layer_new(d, "Placeholder");
  if (!g || !ph) { free(g); layer_free(ph); free(svg); return -1; }
  {
    int n = 0, i;
    for (i = 0; i < d->nlay; i++) if (layer_is_frame(d->lay[i])) n++;
    snprintf(g->name, sizeof g->name, "Frame %d", n + 1);
  }
  /* the placeholder: light grey with a cross corner to corner */
  for (y = b.y0 < 0 ? 0 : b.y0; y < b.y1 && y < d->h; y++) for (x = b.x0 < 0 ? 0 : b.x0; x < b.x1 && x < d->w; x++) {
    u8 *q = ph->px + ((long)y * d->w + x) * 4;
    double u = (double)(x - b.x0) / (b.x1 - b.x0), v = (double)(y - b.y0) / (b.y1 - b.y0);
    int on = fabs(u - v) * (b.x1 - b.x0) < 0.75 || fabs(u + v - 1) * (b.x1 - b.x0) < 0.75;
    q[0] = q[1] = q[2] = (u8)(on ? 160 : 224); q[3] = 255;
  }
  at = d->active >= 0 && d->active < d->nlay ? d->active + 1 : d->nlay;
  depth = d->active >= 0 && d->active < d->nlay ? d->lay[d->active]->depth : 0;
  if (depth > 60) depth = 60;
  doc_begin(d, "Frame Tool");
  doc_note_all(d);
  memmove(&d->lay[at + 2], &d->lay[at], (size_t)(d->nlay - at) * sizeof(Layer *));
  d->lay[at] = ph; d->lay[at + 1] = g; d->nlay += 2;
  ph->depth = (u8)(depth + 1); g->depth = (u8)depth;
  g->blend = BM_NORMAL;
  gi = at + 1;
  memset(&h, 0, sizeof h);
  h.vm_has = 1; h.vm_on = 1; h.vm_linked = 1;
  h.fr_kind = (u8)kind; h.fr_stroke = (u16)(stroke < 0 ? 0 : stroke > 500 ? 500 : stroke); h.fr_color = color & 0xFFFFFF; h.fr_box = b;
  x_put(d, gi, &h, svg, NULL, 0, 0);
  frame_stroke_on(d, ph, kind, b, h.fr_stroke, h.fr_color);
  d->active = gi;
  doc_end(d);
  free(svg);
  return gi;
}
int doc_frame_place(Doc *d, int f, int i, int fit)
{
  const LayerX *h;
  Layer *l, *n;
  Box cb, fb;
  double s, sx, sy, ox, oy;
  i32 x, y;
  int k, lo;
  if (f < 0 || f >= d->nlay || !(h = layerx_get(d->lay[f])) || !h->fr_kind || i < 0 || i >= d->nlay || i == f) return -1;
  l = d->lay[i];
  if (!l->px || l->kind != LK_PIXEL || !layer_content_box(d, i, &cb)) return -1;
  fb = h->fr_box;
  sx = (double)(fb.x1 - fb.x0) / (cb.x1 - cb.x0); sy = (double)(fb.y1 - fb.y0) / (cb.y1 - cb.y0);
  s = fit ? (sx < sy ? sx : sy) : (sx > sy ? sx : sy);
  ox = (fb.x0 + fb.x1) / 2.0 - (cb.x0 + cb.x1) / 2.0 * s; oy = (fb.y0 + fb.y1) / 2.0 - (cb.y0 + cb.y1) / 2.0 * s;
  if (!(n = layer_new(d, l->name))) return -1;
  for (y = 0; y < d->h; y++) for (x = 0; x < d->w; x++) {
    double u = (x + 0.5 - ox) / s, v = (y + 0.5 - oy) / s;
    if (u < cb.x0 || v < cb.y0 || u >= cb.x1 || v >= cb.y1) continue;
    sample_bilinear(l->px, d->w, d->h, u, v, n->px + ((long)y * d->w + x) * 4);
  }
  frame_stroke_on(d, n, h->fr_kind, fb, h->fr_stroke, h->fr_color);
  doc_begin(d, "Place in Frame");
  doc_note_all(d);
  /* the frame's members (the placeholder or what was placed before) and the layer go; the new one goes in */
  lo = block_start(d, f);
  n->depth = (u8)(d->lay[f]->depth + 1);
  {
    Layer *out[MAXLAYERS];
    int m = 0, nf = -1;
    for (k = 0; k < d->nlay; k++) {
      if ((k >= lo && k < f) || k == i) { layer_free(d->lay[k]); continue; }
      if (k == f) { out[m++] = n; nf = m; }
      out[m++] = d->lay[k];
    }
    memcpy(d->lay, out, (size_t)m * sizeof(Layer *));
    d->nlay = m;
    f = nf;
  }
  d->active = f;
  doc_end(d);
  return f;
}

/* ------------------------------------------------------------------ the compositor's hooks */
static void hook_free(Layer *l) { xc_drop(l); }
static void hook_move(Doc *d, int i, i32 dx, i32 dy)
{
  Layer *l = d->lay[i];
  const LayerX *x = layerx_get(l);
  LayerX h;
  if (!x) return;
  h = *x;
  if (h.so_on) { h.so_m[2] += dx; h.so_m[5] += dy; }
  if (h.fr_kind) { h.fr_box.x0 += dx; h.fr_box.x1 += dx; h.fr_box.y0 += dy; h.fr_box.y1 += dy; }
  h.pat_ox = (i16)(h.pat_ox + (h.po_on ? dx : 0)); h.pat_oy = (i16)(h.pat_oy + (h.po_on ? dy : 0));
  if (h.vm_has && h.vm_linked) {
    Path *p = layer_vmask_path(l);
    double m[6] = { 1, 0, 0, 0, 1, 0 };
    char *svg;
    m[2] = dx; m[5] = dy;
    if (!p) return;
    path_transform(p, m);
    svg = path_to_svg(p); path_free(p);
    if (svg) x_put(d, i, &h, svg, pat_px(l), h.pat_w, h.pat_h);
    free(svg);
  } else x_put_same(d, i, &h);
}
static void hook_remap(Doc *d, const double *m)
{
  int i;
  for (i = 0; i < d->nlay; i++) {
    Layer *l = d->lay[i];
    const LayerX *x = layerx_get(l);
    if (l->artboard) {
      double ax = m[0] * l->abox.x0 + m[1] * l->abox.y0 + m[2], ay = m[3] * l->abox.x0 + m[4] * l->abox.y0 + m[5];
      double bx = m[0] * l->abox.x1 + m[1] * l->abox.y1 + m[2], by = m[3] * l->abox.x1 + m[4] * l->abox.y1 + m[5];
      l->abox = box_make((i32)floor((ax < bx ? ax : bx) + 0.5), (i32)floor((ay < by ? ay : by) + 0.5), (i32)floor((ax > bx ? ax : bx) + 0.5), (i32)floor((ay > by ? ay : by) + 0.5));
    }
    if (x) {
      LayerX h = *x;
      Path *p = h.vm_has ? layer_vmask_path(l) : NULL;
      char *svg = NULL;
      if (h.so_on) {
        double n[6], *o = h.so_m;
        n[0] = m[0] * o[0] + m[1] * o[3]; n[1] = m[0] * o[1] + m[1] * o[4]; n[2] = m[0] * o[2] + m[1] * o[5] + m[2];
        n[3] = m[3] * o[0] + m[4] * o[3]; n[4] = m[3] * o[1] + m[4] * o[4]; n[5] = m[3] * o[2] + m[4] * o[5] + m[5];
        memcpy(h.so_m, n, sizeof n);
      }
      if (h.fr_kind) {
        double ax = m[0] * h.fr_box.x0 + m[1] * h.fr_box.y0 + m[2], ay = m[3] * h.fr_box.x0 + m[4] * h.fr_box.y0 + m[5];
        double bx = m[0] * h.fr_box.x1 + m[1] * h.fr_box.y1 + m[2], by = m[3] * h.fr_box.x1 + m[4] * h.fr_box.y1 + m[5];
        h.fr_box = box_make((i32)floor((ax < bx ? ax : bx) + 0.5), (i32)floor((ay < by ? ay : by) + 0.5), (i32)floor((ax > bx ? ax : bx) + 0.5), (i32)floor((ay > by ? ay : by) + 0.5));
      }
      if (p) { path_transform(p, m); svg = path_to_svg(p); path_free(p); }
      x_put(d, i, &h, svg ? svg : h.vm_has ? vm_svg(l) : NULL, pat_px(l), h.pat_w, h.pat_h);
      free(svg);
    }
  }
}
void layerx_init(void)
{
  layerx_vmask = hook_vmask;
  layerx_overlay_row = hook_overlay_row;
  layerx_fill_row = hook_fill_row;
  layerx_free_hook = hook_free;
  layerx_move_hook = hook_move;
  layerx_remap_hook = hook_remap;
}

/* ------------------------------------------------------------------ saving */
int layerx_to_text(const Layer *l, LayerXText *t)
{
  const LayerX *h = layerx_get(l);
  char *b;
  memset(t, 0, sizeof *t);
  if (!h) return 0;
  if (!(b = (char *)malloc(600))) return 0;
  sprintf(b, "1 %d %d %d %d %d %d  %d %d %d  %d %d %d  %d %d %d %d %.6f %.6f %.6f %.6f %.6f %.6f  %d %d %lu %ld %ld %ld %ld",
          h->vm_has, h->vm_on, h->vm_linked, h->vm_invert, h->vm_feather, h->vm_density,
          h->pat_scale, h->pat_ox, h->pat_oy, h->po_on, h->po_blend, h->po_opacity,
          h->so_on, h->so_linked, (int)h->so_w, (int)h->so_h, h->so_m[0], h->so_m[1], h->so_m[2], h->so_m[3], h->so_m[4], h->so_m[5],
          h->fr_kind, h->fr_stroke, (unsigned long)h->fr_color, (long)h->fr_box.x0, (long)h->fr_box.y0, (long)h->fr_box.x1, (long)h->fr_box.y1);
  t->nums = b;
  t->vpath = str_dup(h->vm_has && vm_svg(l) ? vm_svg(l) : "");
  t->spath = str_dup(h->so_on ? h->so_path : "");
  t->pat = pat_px(l); t->pw = h->pat_w; t->ph = h->pat_h;
  if (!t->vpath || !t->spath) { layerx_text_free(t); return 0; }
  return 1;
}
void layerx_text_free(LayerXText *t) { free(t->nums); free(t->vpath); free(t->spath); memset(t, 0, sizeof *t); }
int layerx_from_text(Layer *l, const char *nums, const char *vpath, const char *spath, const u8 *pat, i32 pw, i32 ph)
{
  LayerX h;
  long v[16];
  double m[6];
  unsigned long col;
  int ver, k;
  size_t sl, pl, n;
  u8 *b;
  if (!nums) return 0;
  memset(&h, 0, sizeof h);
  if (sscanf(nums, "%d %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld", &ver, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9], &v[10], &v[11]) != 13 || ver != 1) return 0;
  h.vm_has = (u8)(v[0] != 0); h.vm_on = (u8)(v[1] != 0); h.vm_linked = (u8)(v[2] != 0); h.vm_invert = (u8)(v[3] != 0);
  h.vm_feather = (u16)(v[4] < 0 ? 0 : v[4] > 1000 ? 1000 : v[4]); h.vm_density = (u8)(v[5] < 0 ? 0 : v[5] > 100 ? 100 : v[5]);
  h.pat_scale = (u16)(v[6] < 1 ? 100 : v[6] > 1000 ? 1000 : v[6]); h.pat_ox = (i16)v[7]; h.pat_oy = (i16)v[8];
  h.po_on = (u8)(v[9] != 0); h.po_blend = (u8)(v[10] >= 0 && v[10] < BM_N ? v[10] : 0); h.po_opacity = (u8)(v[11] < 0 ? 0 : v[11] > 255 ? 255 : v[11]);
  {
    /* the rest: after the first 13 numbers */
    const char *s = nums;
    int skip = 13, sw, sh, son, slk, fk, fs;
    long b0, b1, b2, b3;
    while (skip-- && *s) { while (*s == ' ') s++; while (*s && *s != ' ') s++; }
    if (sscanf(s, "%d %d %d %d %lf %lf %lf %lf %lf %lf %d %d %lu %ld %ld %ld %ld", &son, &slk, &sw, &sh, &m[0], &m[1], &m[2], &m[3], &m[4], &m[5], &fk, &fs, &col, &b0, &b1, &b2, &b3) == 17) {
      h.so_on = (u8)(son != 0 && spath && *spath); h.so_linked = (u8)(slk != 0); h.so_w = sw; h.so_h = sh;
      for (k = 0; k < 6; k++) h.so_m[k] = m[k] == m[k] && fabs(m[k]) < 1e7 ? m[k] : (k == 0 || k == 4);
      if (spath) strncpy(h.so_path, spath, sizeof h.so_path - 1);
      h.fr_kind = (u8)(fk == FR_RECT || fk == FR_ELLIPSE ? fk : 0); h.fr_stroke = (u16)(fs < 0 ? 0 : fs > 500 ? 500 : fs);
      h.fr_color = (u32)col & 0xFFFFFF; h.fr_box = box_make((i32)b0, (i32)b1, (i32)b2, (i32)b3);
    }
  }
  if (!h.vm_has) vpath = NULL;
  if (pat && (pw < 1 || ph < 1)) pat = NULL;
  sl = vpath ? strlen(vpath) + 1 : 0; pl = pat ? (size_t)pw * ph * 4 : 0; n = sizeof(LayerX) + sl + pl;
  if (!(b = (u8 *)calloc(1, n))) return 0;
  h.magic = LAYERX_MAGIC; h.len = (u32)n;
  if (vpath) { h.vm_off = sizeof(LayerX); h.vm_len = (u32)sl; memcpy(b + h.vm_off, vpath, sl); } else h.vm_has = 0;
  if (pat) { h.pat_off = (u32)(sizeof(LayerX) + sl); h.pat_w = pw; h.pat_h = ph; memcpy(b + h.pat_off, pat, pl); }
  memcpy(b, &h, sizeof h);
  free(l->x); l->x = b; l->xlen = (u32)n; l->xc_ok = 0;
  return 1;
}
