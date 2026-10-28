/*
 * layers.c - live layers (0.3): groups, masks, clipping, adjustment and fill layers, and moving,
 * merging and aligning with them (see layers.h).
 *
 * The stack is flat, bottom first; a group's members are the layers just below its header with a
 * greater depth. A "block" is a layer, or a group header with all its members: what moves,
 * duplicates or deletes as one. Changes to the stack's shape keep the whole document for undo
 * (doc_note_all); changes to settings only are caught by doc_begin/doc_end comparing them.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "layers.h"
#include "paint.h"

/* ------------------------------------------------------------------ the stack's shape */
int layer_paintable(const Doc *d, int i)
{
  const Layer *l;
  if (i < 0 || i >= d->nlay) return 0;
  l = d->lay[i];
  return l->kind == LK_PIXEL && l->px && !l->locked && !l->lock_all && layer_shown(d, i);
}
int block_start(const Doc *d, int i)
{
  int j;
  if (i < 0 || i >= d->nlay || d->lay[i]->kind != LK_GROUP) return i;
  for (j = i; j > 0 && d->lay[j - 1]->depth > d->lay[i]->depth; j--) continue;
  return j;
}
int layer_parent(const Doc *d, int i)
{
  int j;
  if (i < 0 || i >= d->nlay || !d->lay[i]->depth) return -1;
  for (j = i + 1; j < d->nlay; j++)
    if (d->lay[j]->depth < d->lay[i]->depth) return d->lay[j]->kind == LK_GROUP ? j : -1;
  return -1;
}
int layer_shown(const Doc *d, int i)
{
  for (; i >= 0; i = layer_parent(d, i)) if (!d->lay[i]->visible) return 0;
  return 1;
}
int layer_hidden_by_collapse(const Doc *d, int i)
{
  int p;
  for (p = layer_parent(d, i); p >= 0; p = layer_parent(d, p)) if (d->lay[p]->collapsed) return 1;
  return 0;
}

/* a new layer object of a kind with no pixels (group, adjustment, fill) */
static Layer *bare_layer(const char *name, int kind)
{
  Layer *l = (Layer *)calloc(1, sizeof *l);
  if (!l) return NULL;
  strncpy(l->name, name, sizeof l->name - 1);
  l->opacity = 255; l->visible = 1; l->kind = (u8)kind;
  if (kind == LK_GROUP) l->blend = BM_PASS;
  return l;
}
/* layer l put into the stack at index at with depth (no undo of its own: inside a step) */
static int insert_at(Doc *d, Layer *l, int at, int depth)
{
  if (d->nlay >= MAXLAYERS) { layer_free(l); return -1; }
  if (at < 0) at = 0;
  if (at > d->nlay) at = d->nlay;
  memmove(&d->lay[at + 1], &d->lay[at], (size_t)(d->nlay - at) * sizeof(Layer *));
  d->lay[at] = l; d->nlay++;
  l->depth = (u8)depth;
  return at;
}
/* where a new layer goes: just above the active one's block, at its depth (inside a group header's
   group when the active layer is an open group) */
static void new_place(Doc *d, int *at, int *depth)
{
  if (d->active < 0 || d->active >= d->nlay) { *at = d->nlay; *depth = 0; return; }
  *at = d->active + 1; *depth = d->lay[d->active]->depth;
}

/* ------------------------------------------------------------------ groups */
int doc_new_group(Doc *d, const char *name)
{
  Layer *g = bare_layer(name ? name : "Group 1", LK_GROUP);
  int at, depth, i;
  if (!g) return -1;
  new_place(d, &at, &depth);
  doc_begin(d, "New Group");
  doc_note_all(d);
  i = insert_at(d, g, at, depth);
  if (i >= 0) d->active = i;
  doc_end(d);
  return i;
}
/* the block of i taken out of the stack into out (bottom first); its count */
static int take_block(Doc *d, int i, Layer **out)
{
  int s = block_start(d, i), n = i - s + 1;
  memcpy(out, &d->lay[s], (size_t)n * sizeof(Layer *));
  memmove(&d->lay[s], &d->lay[i + 1], (size_t)(d->nlay - i - 1) * sizeof(Layer *));
  d->nlay -= n;
  return n;
}
/* a block put back at index at, its top at depth (members keep their nesting relative to it) */
static void put_block(Doc *d, Layer **b, int n, int at, int depth)
{
  int k, base = b[n - 1]->depth;
  if (at < 0) at = 0;
  if (at > d->nlay) at = d->nlay;
  memmove(&d->lay[at + n], &d->lay[at], (size_t)(d->nlay - at) * sizeof(Layer *));
  for (k = 0; k < n; k++) {
    int nd = b[k]->depth - base + depth;
    b[k]->depth = (u8)(nd < 0 ? 0 : nd > 250 ? 250 : nd);
    d->lay[at + k] = b[k];
  }
  d->nlay += n;
}
int doc_group_layers(Doc *d)
{
  Layer *moved[MAXLAYERS], *g;
  int n = 0, i, top = -1, depth, at;
  if (d->nlay >= MAXLAYERS) return 0;
  if (!doc_npicked(d) && d->active >= 0 && d->active < d->nlay) d->lay[d->active]->picked = 1;
  /* the topmost picked layer decides where the group goes */
  for (i = d->nlay - 1; i >= 0; i--) if (d->lay[i]->picked) { top = i; break; }
  if (top < 0 || !(g = bare_layer("Group 1", LK_GROUP))) return 0;
  {
    /* "Group N", the next free number */
    int k, num = 1;
    for (k = 0; k < d->nlay; k++) if (d->lay[k]->kind == LK_GROUP && !strncmp(d->lay[k]->name, "Group ", 6)) { int v = atoi(d->lay[k]->name + 6); if (v >= num) num = v + 1; }
    sprintf(g->name, "Group %d", num);
  }
  depth = d->lay[top]->depth;
  doc_begin(d, "Group Layers");
  doc_note_all(d);
  /* each picked block taken out, from the top down (a block inside a picked group goes with it) */
  for (i = d->nlay - 1; i >= 0; i--) {
    int p, inside = 0;
    if (!d->lay[i]->picked) continue;
    for (p = layer_parent(d, i); p >= 0; p = layer_parent(d, p)) if (d->lay[p]->picked) inside = 1;
    if (inside) continue;
    {
      Layer *b[MAXLAYERS];
      int s = block_start(d, i), m = take_block(d, i, b), k;
      /* prepended: moved stays bottom first */
      memmove(moved + m, moved, (size_t)n * sizeof(Layer *));
      for (k = 0; k < m; k++) { int nd = b[k]->depth - b[m - 1]->depth + depth + 1; b[k]->depth = (u8)nd; moved[k] = b[k]; }
      n += m;
      if (s <= top) top -= m;
      i = s;
    }
  }
  at = top + 1;
  if (at > d->nlay) at = d->nlay;
  memmove(&d->lay[at + n + 1], &d->lay[at], (size_t)(d->nlay - at) * sizeof(Layer *));
  memcpy(&d->lay[at], moved, (size_t)n * sizeof(Layer *));
  d->nlay += n;
  g->depth = (u8)depth;
  d->lay[at + n] = g; d->nlay++;
  for (i = 0; i < d->nlay; i++) d->lay[i]->picked = 0;
  d->active = at + n;
  doc_end(d);
  return 1;
}
int doc_ungroup(Doc *d, int g)
{
  int s, k;
  if (g < 0 || g >= d->nlay || d->lay[g]->kind != LK_GROUP) return 0;
  s = block_start(d, g);
  doc_begin(d, "Ungroup Layers");
  doc_note_all(d);
  for (k = s; k < g; k++) if (d->lay[k]->depth) d->lay[k]->depth--;
  layer_free(d->lay[g]);
  memmove(&d->lay[g], &d->lay[g + 1], (size_t)(d->nlay - g - 1) * sizeof(Layer *));
  d->nlay--;
  if (!d->nlay) { Layer *l = layer_new(d, "Layer 1"); if (l) d->lay[d->nlay++] = l; }
  d->active = g - 1 >= 0 ? g - 1 : 0;
  doc_end(d);
  return 1;
}
int doc_move_block(Doc *d, int i, int below, int depth)
{
  Layer *b[MAXLAYERS];
  int s, n, at;
  if (i < 0 || i >= d->nlay) return 0;
  s = block_start(d, i);
  if (below >= s && below <= i) return 0;                /* onto itself */
  doc_begin(d, "Move Layer");
  doc_note_all(d);
  n = take_block(d, i, b);
  if (below > i) below -= n;
  at = below + 1;
  put_block(d, b, n, at, depth);
  d->active = at + n - 1;
  doc_end(d);
  return 1;
}
int doc_arrange(Doc *d, int i, int how)
{
  int s, depth, j, below;
  if (i < 0 || i >= d->nlay) return 0;
  s = block_start(d, i); depth = d->lay[i]->depth;
  switch (how) {
  case 1:                                                /* forward: above the next block up at this level */
    for (j = i + 1; j < d->nlay && d->lay[j]->depth > depth; j++) continue;
    if (j >= d->nlay || d->lay[j]->depth < depth) return 0;
    return doc_move_block(d, i, j, depth);
  case 2:                                                /* backward: below the next block down */
    if (s == 0 || d->lay[s - 1]->depth < depth) return 0;
    below = block_start(d, s - 1) - 1;
    return doc_move_block(d, i, below, depth);
  case 0:                                                /* to the front of its group (or the stack) */
    for (j = i + 1; j < d->nlay && d->lay[j]->depth >= depth; j++) continue;
    if (j - 1 == i) return 0;
    return doc_move_block(d, i, j - 1, depth);
  default:                                               /* to the back */
    for (j = s - 1; j >= 0 && d->lay[j]->depth >= depth; j--) continue;
    if (j + 1 == s) return 0;
    return doc_move_block(d, i, j, depth);
  }
}

/* ------------------------------------------------------------------ masks */
void layer_mask_changed(Layer *l) { l->maskfx_ok = 0; }
int doc_mask_add(Doc *d, int i, int how)
{
  Layer *l;
  long k, n = (long)d->w * d->h;
  if (i < 0 || i >= d->nlay) return 0;
  l = d->lay[i];
  if (l->mask) return 0;
  doc_begin(d, "Add Layer Mask");
  doc_note_all(d);
  if (!(l->mask = (u8 *)malloc((size_t)n))) { doc_end(d); return 0; }
  for (k = 0; k < n; k++) {
    int v;
    switch (how) {
    case MK_HIDE_ALL: v = 0; break;
    case MK_REVEAL_SEL: v = d->sel ? d->sel[k] : 255; break;
    case MK_HIDE_SEL: v = d->sel ? 255 - d->sel[k] : 0; break;
    case MK_FROM_ALPHA: v = l->px ? l->px[k * 4 + 3] : 255; break;
    default: v = 255; break;
    }
    l->mask[k] = (u8)v;
  }
  if (how == MK_FROM_ALPHA && l->px) for (k = 0; k < n; k++) l->px[k * 4 + 3] = l->px[k * 4 + 3] ? 255 : 0;
  l->mask_on = 1; l->mask_linked = 1;
  l->maskfx_ok = 0;
  doc_end(d);
  return 1;
}
int doc_mask_delete(Doc *d, int i, int apply)
{
  Layer *l;
  long k, n = (long)d->w * d->h;
  if (i < 0 || i >= d->nlay || !d->lay[i]->mask) return 0;
  l = d->lay[i];
  doc_begin(d, apply ? "Apply Layer Mask" : "Delete Layer Mask");
  doc_note_all(d);
  if (apply && l->px) {
    const u8 *m = layer_mask_used(d, l);
    if (m) for (k = 0; k < n; k++) {
      int v = 255 - (((255 - m[k]) * (255 - l->mask_fade) + 127) / 255);
      l->px[k * 4 + 3] = (u8)((l->px[k * 4 + 3] * v + 127) / 255);
    }
  }
  free(l->mask); l->mask = NULL;
  free(l->maskfx); l->maskfx = NULL; l->maskfx_ok = 0;
  l->edit_mask = 0;
  doc_end(d);
  return 1;
}
int doc_mask_set(Doc *d, int i, int on, int linked, int fade, int feather)
{
  Layer *l;
  if (i < 0 || i >= d->nlay || !d->lay[i]->mask) return 0;
  l = d->lay[i];
  doc_begin(d, on >= 0 ? (on ? "Enable Layer Mask" : "Disable Layer Mask") : linked >= 0 ? "Link Layer Mask" : "Mask Properties");
  if (on >= 0) l->mask_on = (u8)(on != 0);
  if (linked >= 0) l->mask_linked = (u8)(linked != 0);
  if (fade >= 0) l->mask_fade = (u8)(fade > 255 ? 255 : fade);
  if (feather >= 0 && feather != l->mask_feather) { l->mask_feather = (u16)(feather > 1000 ? 1000 : feather); l->maskfx_ok = 0; }
  doc_end(d);
  return 1;
}
int doc_mask_invert(Doc *d, int i)
{
  Layer *l;
  long k, n = (long)d->w * d->h;
  if (i < 0 || i >= d->nlay || !d->lay[i]->mask) return 0;
  l = d->lay[i];
  doc_begin(d, "Invert Mask");
  doc_note_mask(d, i, doc_box(d));
  for (k = 0; k < n; k++) l->mask[k] = (u8)(255 - l->mask[k]);
  l->maskfx_ok = 0;
  doc_end(d);
  return 1;
}
void doc_sel_from_mask(Doc *d, int i, int op)
{
  Layer *l;
  long k, n = (long)d->w * d->h;
  u8 *m;
  if (i < 0 || i >= d->nlay) return;
  l = d->lay[i];
  if (!l->mask && !l->px) return;
  if (!(m = (u8 *)malloc((size_t)n))) return;
  for (k = 0; k < n; k++) m[k] = l->mask ? l->mask[k] : l->px[k * 4 + 3];
  doc_begin(d, "Load Selection");
  doc_note_sel(d);
  if (op == SEL_REPLACE || !d->sel) {
    if (op == SEL_SUBTRACT) for (k = 0; k < n; k++) m[k] = (u8)(255 - m[k]);
    else if (op == SEL_INTERSECT && !d->sel) { /* everything intersected with m is m */ }
    free(d->sel); d->sel = m;
  } else {
    for (k = 0; k < n; k++) {
      int a = d->sel[k], b = m[k];
      d->sel[k] = (u8)(op == SEL_ADD ? (a > b ? a : b) : op == SEL_SUBTRACT ? (a * (255 - b) + 127) / 255 : (a * b + 127) / 255);
    }
    free(m);
  }
  /* the selection's box (and none at all if it came out empty) */
  {
    i32 x, y, x0 = d->w, y0 = d->h, x1 = 0, y1 = 0;
    for (y = 0; y < d->h; y++) for (x = 0; x < d->w; x++) if (d->sel[(long)y * d->w + x]) {
      if (x < x0) x0 = x;
      if (y < y0) y0 = y;
      if (x >= x1) x1 = x + 1;
      if (y >= y1) y1 = y + 1;
    }
    if (x1 <= x0) { free(d->sel); d->sel = NULL; d->selbox = doc_box(d); }
    else d->selbox = box_make(x0, y0, x1, y1);
  }
  doc_end(d);
}

/* ------------------------------------------------------------------ clipping */
int doc_set_clip(Doc *d, int i, int on)
{
  if (i < 0 || i >= d->nlay || d->lay[i]->kind == LK_GROUP) return 0;
  if (on && (i == 0 || d->lay[i - 1]->depth < d->lay[i]->depth)) return 0;   /* nothing below to clip to */
  doc_begin(d, on ? "Create Clipping Mask" : "Release Clipping Mask");
  d->lay[i]->clip = (u8)(on != 0);
  doc_end(d);
  return 1;
}

/* ------------------------------------------------------------------ adjustment and fill layers */
int doc_new_adjust(Doc *d, const AdjParams *p, const char *name)
{
  Layer *l = bare_layer(name ? name : "Adjustment", LK_ADJUST);
  int at, depth, i;
  if (!l) return -1;
  l->adj = *p;
  new_place(d, &at, &depth);
  doc_begin(d, "New Adjustment Layer");
  doc_note_all(d);
  i = insert_at(d, l, at, depth);
  if (i >= 0) d->active = i;
  doc_end(d);
  return i;
}
int doc_set_adjust(Doc *d, int i, const AdjParams *p)
{
  if (i < 0 || i >= d->nlay || d->lay[i]->kind != LK_ADJUST) return 0;
  doc_begin(d, "Adjustment Settings");
  d->lay[i]->adj = *p;
  d->lay[i]->adjprog_ok = 0;
  doc_end(d);
  return 1;
}
int doc_new_fill(Doc *d, const FillParams *f, const char *name)
{
  Layer *l = bare_layer(name ? name : "Fill", LK_FILL);
  int at, depth, i;
  if (!l) return -1;
  l->fillp = *f;
  new_place(d, &at, &depth);
  doc_begin(d, "New Fill Layer");
  doc_note_all(d);
  i = insert_at(d, l, at, depth);
  if (i >= 0) d->active = i;
  doc_end(d);
  return i;
}
int doc_set_fill(Doc *d, int i, const FillParams *f)
{
  if (i < 0 || i >= d->nlay || d->lay[i]->kind != LK_FILL) return 0;
  doc_begin(d, "Fill Layer Settings");
  d->lay[i]->fillp = *f;
  doc_end(d);
  return 1;
}
int doc_rasterize(Doc *d, int i)
{
  Layer *l;
  if (i < 0 || i >= d->nlay) return 0;
  l = d->lay[i];
  if (l->kind == LK_GROUP) return doc_merge_group(d, i);
  if (l->kind != LK_FILL && l->kind != LK_TEXT && l->kind != LK_SHAPE) return 0;   /* (0.9: type and shape layers too) */
  {
    u8 *px = (u8 *)malloc((size_t)d->w * d->h * 4);
    u8 op = l->opacity, bl = l->blend, fc = l->fill_cut, vis = l->visible, mon = l->mask_on, clip = l->clip;
    u8 bi[4];
    if (!px) return 0;
    memcpy(bi, l->blendif, 4);
    /* the fill's own pixels: composited alone, at full strength, unmasked */
    l->opacity = 255; l->blend = BM_NORMAL; l->fill_cut = 0; l->visible = 1; l->mask_on = 0; l->clip = 0; memset(l->blendif, 0, 4);
    doc_composite_range(d, i, i + 1, l->depth, doc_box(d), px, (long)d->w * 4, 0);
    l->opacity = op; l->blend = bl; l->fill_cut = fc; l->visible = vis; l->mask_on = mon; l->clip = clip; memcpy(l->blendif, bi, 4);
    doc_begin(d, l->kind == LK_TEXT ? "Rasterize Type" : l->kind == LK_SHAPE ? "Rasterize Shape" : "Rasterize Layer");
    doc_note_all(d);
    l->px = px; l->kind = LK_PIXEL;
    free(l->live); l->live = NULL; l->livelen = 0;           /* 0.9 */
    free(l->livepx); l->livepx = NULL; l->live_ok = 0;
    doc_end(d);
  }
  return 1;
}

/* ------------------------------------------------------------------ merging */
/* layers s..e (a run of whole blocks at one level) composited into one pixel layer in their place */
static int merge_run(Doc *d, int s, int e, const char *name)
{
  Layer *m;
  int depth = d->lay[e]->depth, k;
  if (!(m = layer_new(d, d->lay[e]->name))) return 0;
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
int doc_merge_group(Doc *d, int g)
{
  if (g < 0 || g >= d->nlay || d->lay[g]->kind != LK_GROUP) return 0;
  return merge_run(d, block_start(d, g), g, "Merge Group");
}
int doc_merge_picked(Doc *d)
{
  int lo = -1, hi = -1, i, depth;
  for (i = 0; i < d->nlay; i++) if (d->lay[i]->picked) { if (lo < 0) lo = i; hi = i; }
  if (lo < 0 || lo == hi) return 0;
  /* the run from the lowest picked block to the highest, at the highest's level */
  depth = d->lay[hi]->depth;
  lo = block_start(d, lo);
  for (i = lo; i <= hi; i++) if (d->lay[i]->depth < depth) depth = d->lay[i]->depth;
  for (i = 0; i < d->nlay; i++) d->lay[i]->picked = 0;
  (void)depth;
  return merge_run(d, lo, hi, "Merge Layers");
}

/* ------------------------------------------------------------------ the picked layers */
void doc_pick_layer(Doc *d, int i, int how)
{
  int k;
  if (i < 0 || i >= d->nlay) return;
  if (how == 0) { for (k = 0; k < d->nlay; k++) d->lay[k]->picked = 0; d->lay[i]->picked = 1; d->active = i; }
  else if (how == 1) {
    if (!doc_npicked(d) && d->active >= 0 && d->active < d->nlay) d->lay[d->active]->picked = 1;
    d->lay[i]->picked = (u8)!d->lay[i]->picked;
    if (d->lay[i]->picked) d->active = i;
  } else {
    int a = d->active < i ? d->active : i, b = d->active < i ? i : d->active;
    for (k = 0; k < d->nlay; k++) d->lay[k]->picked = (u8)(k >= a && k <= b);
    d->active = i;
  }
}
int doc_npicked(const Doc *d)
{
  int k, n = 0;
  for (k = 0; k < d->nlay; k++) n += d->lay[k]->picked;
  return n;
}

/* ------------------------------------------------------------------ aligning */
int layer_content_box(const Doc *d, int i, Box *b)
{
  const Layer *l;
  i32 x, y, x0 = d->w, y0 = d->h, x1 = 0, y1 = 0;
  if (i < 0 || i >= d->nlay || !(l = d->lay[i])->px) return 0;
  for (y = 0; y < d->h; y++) {
    const u8 *p = l->px + (long)y * d->w * 4 + 3;
    for (x = 0; x < d->w; x++, p += 4) if (*p) {
      if (x < x0) x0 = x;
      if (x >= x1) x1 = x + 1;
      if (y < y0) y0 = y;
      y1 = y + 1;
    }
  }
  if (x1 <= x0) return 0;
  *b = box_make(x0, y0, x1, y1);
  return 1;
}
/* layer i's pixels (and its mask when linked) shifted by dx, dy */
static void shift_layer(Doc *d, int i, i32 dx, i32 dy)
{
  Layer *l = d->lay[i];
  u8 *n;
  i32 x, y;
  if (!l->px || (!dx && !dy) || l->lock_pos || l->lock_all) return;
  if ((n = (u8 *)calloc((size_t)d->w * d->h, 4)) == NULL) return;
  for (y = 0; y < d->h; y++) for (x = 0; x < d->w; x++) {
    i32 sx = x - dx, sy = y - dy;
    if (sx >= 0 && sy >= 0 && sx < d->w && sy < d->h) memcpy(n + ((long)y * d->w + x) * 4, l->px + ((long)sy * d->w + sx) * 4, 4);
  }
  free(l->px); l->px = n;
  if (l->mask && l->mask_linked) {
    u8 *m = (u8 *)malloc((size_t)d->w * d->h);
    if (m) {
      memset(m, 255, (size_t)d->w * d->h);
      for (y = 0; y < d->h; y++) for (x = 0; x < d->w; x++) {
        i32 sx = x - dx, sy = y - dy;
        if (sx >= 0 && sy >= 0 && sx < d->w && sy < d->h) m[(long)y * d->w + x] = l->mask[(long)sy * d->w + sx];
      }
      free(l->mask); l->mask = m; l->maskfx_ok = 0;
    }
  }
}
int doc_align(Doc *d, int how)
{
  Box ref, bb[MAXLAYERS];
  int idx[MAXLAYERS], n = 0, i, k;
  ref = d->sel ? d->selbox : doc_box(d);
  for (i = 0; i < d->nlay; i++)
    if ((d->lay[i]->picked || (i == d->active && !doc_npicked(d))) && layer_content_box(d, i, &bb[n])) idx[n++] = i;
  if (!n || ((how == AL_DIST_H || how == AL_DIST_V) && n < 3)) return 0;
  doc_begin(d, how >= AL_DIST_H ? "Distribute" : "Align");
  doc_note_all(d);
  if (how == AL_DIST_H || how == AL_DIST_V) {
    /* centres spread evenly between the outermost two */
    int order[MAXLAYERS];
    double c0, c1;
    for (k = 0; k < n; k++) order[k] = k;
    for (k = 1; k < n; k++) {
      int t = order[k], j = k;
      double ct = how == AL_DIST_H ? bb[t].x0 + bb[t].x1 : bb[t].y0 + bb[t].y1;
      while (j > 0 && (how == AL_DIST_H ? bb[order[j - 1]].x0 + bb[order[j - 1]].x1 : bb[order[j - 1]].y0 + bb[order[j - 1]].y1) > ct) { order[j] = order[j - 1]; j--; }
      order[j] = t;
    }
    c0 = how == AL_DIST_H ? (bb[order[0]].x0 + bb[order[0]].x1) / 2.0 : (bb[order[0]].y0 + bb[order[0]].y1) / 2.0;
    c1 = how == AL_DIST_H ? (bb[order[n - 1]].x0 + bb[order[n - 1]].x1) / 2.0 : (bb[order[n - 1]].y0 + bb[order[n - 1]].y1) / 2.0;
    for (k = 1; k < n - 1; k++) {
      int t = order[k];
      double want = c0 + (c1 - c0) * k / (n - 1), have = how == AL_DIST_H ? (bb[t].x0 + bb[t].x1) / 2.0 : (bb[t].y0 + bb[t].y1) / 2.0;
      i32 dlt = (i32)(want - have + (want >= have ? 0.5 : -0.5));
      shift_layer(d, idx[t], how == AL_DIST_H ? dlt : 0, how == AL_DIST_V ? dlt : 0);
    }
  } else {
    for (k = 0; k < n; k++) {
      Box b = bb[k];
      i32 dx = 0, dy = 0;
      switch (how) {
      case AL_LEFT: dx = ref.x0 - b.x0; break;
      case AL_RIGHT: dx = ref.x1 - b.x1; break;
      case AL_HCENTER: dx = (ref.x0 + ref.x1) / 2 - (b.x0 + b.x1) / 2; break;
      case AL_TOP: dy = ref.y0 - b.y0; break;
      case AL_BOTTOM: dy = ref.y1 - b.y1; break;
      default: dy = (ref.y0 + ref.y1) / 2 - (b.y0 + b.y1) / 2; break;
      }
      shift_layer(d, idx[k], dx, dy);
    }
  }
  doc_end(d);
  return 1;
}

/* ------------------------------------------------------------------ moving a group */
/* every pixel layer in group g (and the group's mask, if linked) moved by dx, dy; the area changed */
Box doc_move_group(Doc *d, int g, i32 dx, i32 dy)
{
  int s, k;
  if (g < 0 || g >= d->nlay || d->lay[g]->kind != LK_GROUP || (!dx && !dy) || d->lay[g]->lock_pos || d->lay[g]->lock_all) return box_make(0, 0, 0, 0);
  s = block_start(d, g);
  doc_begin(d, "Move");
  doc_note_all(d);
  for (k = s; k <= g; k++) shift_layer(d, k, dx, dy);
  doc_end(d);
  return doc_box(d);
}
