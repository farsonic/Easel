/*
 * layertest.c - live layers (0.3): groups (normal and pass-through), masks (density, feather), clipping,
 * adjustment and fill layers, fill opacity, Blend If, and the stack operations, each checked
 * against a composite worked out another way; plus undo and redo of every operation.
 * Run under the address and undefined-behaviour sanitizers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/doc.h"
#include "core/layers.h"
#include "core/adjust.h"
#include "core/paint.h"
#include "core/codec.h"

static int fails, tests;
#define CHECK(c, msg) do { tests++; if (!(c)) { fails++; printf("FAIL %s (line %d)\n", msg, __LINE__); } } while (0)

static unsigned rnd = 12345;
static unsigned rn(void) { rnd = rnd * 1103515245u + 12345u; return (rnd >> 16) & 0x7fff; }

/* a pixel layer of random colours with random alpha */
static Layer *rand_layer(Doc *d, const char *name, int opaque)
{
  Layer *l = layer_new(d, name);
  long k;
  for (k = 0; k < (long)d->w * d->h * 4; k++) l->px[k] = (u8)rn();
  if (opaque) for (k = 0; k < (long)d->w * d->h; k++) l->px[k * 4 + 3] = 255;
  return l;
}
static u8 *composite(Doc *d)
{
  u8 *o = (u8 *)malloc((size_t)d->w * d->h * 4);
  doc_composite(d, doc_box(d), o, (long)d->w * 4);
  return o;
}
static int maxdiff(const u8 *a, const u8 *b, long n)
{
  long k;
  int m = 0;
  for (k = 0; k < n; k++) { int v = abs(a[k] - b[k]); if (v > m) m = v; }
  return m;
}
/* the composite of plain layers worked out directly (the reference) */
static void ref_blend(u8 *out, const Layer *const *ls, int n, long px)
{
  long k;
  int i;
  memset(out, 0, (size_t)px * 4);
  for (i = 0; i < n; i++)
    for (k = 0; k < px; k++) if (ls[i]->visible && ls[i]->px[k * 4 + 3]) blend_pixel(out + k * 4, ls[i]->px + k * 4, ls[i]->opacity, ls[i]->blend);
}

static void test_groups(void)
{
  Doc *d = doc_new(23, 17, 0x336699, 0);
  Layer *a, *b;
  u8 *c0, *c1, ref[23 * 17 * 4], tmp[23 * 17 * 4];
  long n = 23 * 17;
  int g;
  a = rand_layer(d, "A", 0); a->blend = BM_MULTIPLY; doc_add_layer(d, a, 1);
  b = rand_layer(d, "B", 0); b->blend = BM_SCREEN; b->opacity = 180; doc_add_layer(d, b, 2);
  c0 = composite(d);
  /* a pass-through group at full opacity looks the same as no group */
  doc_pick_layer(d, 1, 0); doc_pick_layer(d, 2, 1);
  CHECK(doc_group_layers(d), "group layers");
  CHECK(d->nlay == 4 && d->lay[3]->kind == LK_GROUP && d->lay[1]->depth == 1 && d->lay[2]->depth == 1, "group shape");
  g = 3;
  c1 = composite(d);
  CHECK(maxdiff(c0, c1, n * 4) == 0, "pass-through group = ungrouped");
  free(c1);
  /* an isolated (Normal) group: A and B composited alone, then over the background */
  d->lay[g]->blend = BM_NORMAL; d->lay[g]->opacity = 200;
  c1 = composite(d);
  {
    const Layer *ls[2];
    long k;
    ls[0] = d->lay[1]; ls[1] = d->lay[2];
    ref_blend(tmp, ls, 2, n);
    memcpy(ref, d->lay[0]->px, (size_t)n * 4);
    for (k = 0; k < n; k++) if (tmp[k * 4 + 3]) blend_pixel(ref + k * 4, tmp + k * 4, 200, BM_NORMAL);
  }
  CHECK(maxdiff(ref, c1, n * 4) <= 1, "isolated group");
  free(c1);
  /* pass-through at opacity: a mix of with and without */
  d->lay[g]->blend = BM_PASS; d->lay[g]->opacity = 128;
  c1 = composite(d);
  {
    long k;
    int ch;
    for (k = 0; k < n * 4; k++) { ch = d->lay[0]->px[k]; ref[k] = (u8)(ch + ((c0[k] - ch) * 128 + (c0[k] >= ch ? 127 : -127)) / 255); }
  }
  CHECK(maxdiff(ref, c1, n * 4) <= 1, "pass-through group at half opacity");
  free(c1);
  /* a hidden group hides its members */
  d->lay[g]->visible = 0;
  c1 = composite(d);
  CHECK(maxdiff(d->lay[0]->px, c1, n * 4) == 0, "hidden group");
  free(c1);
  d->lay[g]->visible = 1; d->lay[g]->opacity = 255; d->lay[g]->blend = BM_NORMAL;
  /* merging an isolated group gives one layer that looks the same (a pass-through one whose members
     blend with what's below can't, as in Photoshop) */
  c1 = composite(d);
  CHECK(doc_merge_group(d, g) && d->nlay == 2 && d->lay[1]->kind == LK_PIXEL && d->lay[1]->depth == 0, "merge group");
  {
    u8 *c2 = composite(d);
    CHECK(maxdiff(c1, c2, n * 4) <= 1, "merged group looks the same");
    free(c2);
  }
  /* undo brings the group back */
  CHECK(doc_undo(d) && d->nlay == 4 && d->lay[3]->kind == LK_GROUP, "undo merge group");
  CHECK(doc_redo(d) && d->nlay == 2, "redo merge group");
  CHECK(doc_undo(d), "undo again");
  /* ungroup */
  CHECK(doc_ungroup(d, 3) && d->nlay == 3 && !d->lay[1]->depth && !d->lay[2]->depth, "ungroup");
  {
    u8 *c2 = composite(d);
    CHECK(maxdiff(c0, c2, n * 4) == 0, "ungrouped looks the same");
    free(c2);
  }
  CHECK(doc_undo(d) && d->nlay == 4, "undo ungroup");
  /* duplicate and delete a whole group */
  CHECK(doc_duplicate_layer(d, 3) && d->nlay == 7 && d->lay[6]->kind == LK_GROUP && d->lay[4]->depth == 1, "duplicate group");
  CHECK(doc_delete_layer(d, 6) && d->nlay == 4, "delete group with members");
  free(c0); free(c1);
  doc_free(d);
}

static void test_masks(void)
{
  Doc *d = doc_new(31, 9, 0x000000, 0);
  Layer *a = rand_layer(d, "A", 1);
  u8 *c;
  long n = 31 * 9, k;
  int ok;
  doc_add_layer(d, a, 1);
  /* hide all: only the background shows */
  CHECK(doc_mask_add(d, 1, MK_HIDE_ALL), "mask hide all");
  c = composite(d);
  CHECK(maxdiff(c, d->lay[0]->px, n * 4) == 0, "hidden by mask");
  free(c);
  /* a gradient mask: alpha follows it */
  for (k = 0; k < n; k++) d->lay[1]->mask[k] = (u8)(k % 31 * 255 / 30);
  layer_mask_changed(d->lay[1]);
  c = composite(d);
  ok = 1;
  for (k = 0; k < n; k++) {
    u8 want[4];
    memcpy(want, d->lay[0]->px + k * 4, 4);
    blend_pixel(want, d->lay[1]->px + k * 4, d->lay[1]->mask[k], BM_NORMAL);
    if (maxdiff(want, c + k * 4, 4) > 1) ok = 0;
  }
  CHECK(ok, "gradient mask");
  free(c);
  /* density 0 (fade 255): the mask has no effect */
  doc_mask_set(d, 1, -1, -1, 255, -1);
  c = composite(d);
  {
    u8 ref[31 * 9 * 4];
    memcpy(ref, d->lay[0]->px, (size_t)n * 4);
    for (k = 0; k < n; k++) blend_pixel(ref + k * 4, d->lay[1]->px + k * 4, 255, BM_NORMAL);
    CHECK(maxdiff(ref, c, n * 4) == 0, "mask density 0");
  }
  free(c);
  doc_mask_set(d, 1, -1, -1, 0, -1);
  /* disabled: no effect */
  doc_mask_set(d, 1, 0, -1, -1, -1);
  c = composite(d);
  CHECK(maxdiff(c, d->lay[1]->px, n * 4) == 0, "mask disabled");
  free(c);
  doc_mask_set(d, 1, 1, -1, -1, -1);
  /* feather softens: a hard edge becomes a ramp */
  for (k = 0; k < n; k++) d->lay[1]->mask[k] = (u8)(k % 31 < 15 ? 0 : 255);
  layer_mask_changed(d->lay[1]);
  doc_mask_set(d, 1, -1, -1, -1, 6);
  {
    const u8 *m = layer_mask_used(d, d->lay[1]);
    CHECK(m && m[4 * 31 + 14] > 0 && m[4 * 31 + 14] < 255 && m[4 * 31 + 2] < 40 && m[4 * 31 + 28] > 215, "feathered mask");
  }
  doc_mask_set(d, 1, -1, -1, -1, 0);
  /* undo of mask painting (tiles of the mask) */
  doc_begin(d, "Paint mask");
  doc_note_mask(d, 1, box_make(0, 0, 31, 9));
  memset(d->lay[1]->mask, 77, (size_t)n);
  doc_end(d);
  CHECK(doc_undo(d) && d->lay[1]->mask[0] == 0 && d->lay[1]->mask[20] == 255, "undo mask painting");
  CHECK(doc_redo(d) && d->lay[1]->mask[0] == 77, "redo mask painting");
  /* apply: the mask goes into alpha */
  memset(d->lay[1]->mask, 100, (size_t)n);
  layer_mask_changed(d->lay[1]);
  c = composite(d);
  CHECK(doc_mask_delete(d, 1, 1) && !d->lay[1]->mask, "apply mask");
  {
    u8 *c2 = composite(d);
    CHECK(maxdiff(c, c2, n * 4) <= 1, "applied mask looks the same");
    free(c2);
  }
  free(c);
  CHECK(doc_undo(d) && d->lay[1]->mask, "undo apply mask");
  /* selection from the mask */
  doc_sel_from_mask(d, 1, SEL_REPLACE);
  CHECK(d->sel && d->sel[5] == 100, "selection from mask");
  doc_free(d);
}

static void test_clipping(void)
{
  Doc *d = doc_new(19, 13, 0xFFFFFF, 0);
  Layer *base = rand_layer(d, "Base", 0), *top = rand_layer(d, "Top", 1);
  u8 *c, ref[19 * 13 * 4];
  long n = 19 * 13, k;
  base->opacity = 210;
  top->blend = BM_MULTIPLY;
  doc_add_layer(d, base, 1); doc_add_layer(d, top, 2);
  CHECK(doc_set_clip(d, 2, 1), "create clipping mask");
  CHECK(!doc_set_clip(d, 0, 1), "nothing to clip the bottom layer to");
  c = composite(d);
  /* reference: base alone, top on it keeping its alpha, then over the background by base's opacity */
  memcpy(ref, d->lay[0]->px, (size_t)n * 4);
  for (k = 0; k < n; k++) {
    u8 t[4];
    memcpy(t, base->px + k * 4, 4);
    if (t[3]) { u8 a = t[3]; blend_pixel(t, top->px + k * 4, 255, BM_MULTIPLY); t[3] = a; }
    if (t[3]) blend_pixel(ref + k * 4, t, 210, BM_NORMAL);
  }
  CHECK(maxdiff(ref, c, n * 4) <= 1, "clipping group");
  free(c);
  CHECK(doc_undo(d) && !d->lay[2]->clip, "undo clipping");
  doc_free(d);
}

static void test_adjust_fill(void)
{
  Doc *d = doc_new(17, 11, 0, 0);
  Layer *a = rand_layer(d, "A", 1);
  AdjParams p;
  u8 *c, *ref;
  long n = 17 * 11;
  int i, kinds[] = { AK_BRIGHTNESS, AK_LEVELS, AK_CURVES, AK_EXPOSURE, AK_INVERT, AK_POSTERIZE, AK_HUESAT, AK_VIBRANCE,
                     AK_COLORBALANCE, AK_BLACKWHITE, AK_PHOTOFILTER, AK_CHANNELMIXER, AK_THRESHOLD, AK_GRADIENTMAP, AK_SELECTIVECOLOR };
  adjlayer_init();
  memcpy(d->lay[0]->px, a->px, (size_t)n * 4);
  layer_free(a);
  for (i = 0; i < (int)(sizeof kinds / sizeof kinds[0]); i++) {
    int li, maxd;
    Doc *e;
    adj_defaults(kinds[i], &p);
    switch (kinds[i]) {
    case AK_BRIGHTNESS: p.v[0] = 40; p.v[1] = 30; break;
    case AK_HUESAT: p.v[0] = 60; p.v[1] = 30; p.v[2] = -10; break;
    case AK_VIBRANCE: p.v[0] = 50; break;
    case AK_COLORBALANCE: p.v[3] = 30; p.v[5] = -20; break;
    case AK_EXPOSURE: p.v[0] = 50; break;
    case AK_CHANNELMIXER: p.v[1] = 20; p.v[4] = 10; break;
    case AK_SELECTIVECOLOR: p.pts[0] = 30; p.pts[13] = -20; break;
    default: break;
    }
    li = doc_new_adjust(d, &p, adj_names[kinds[i]]);
    CHECK(li == 1 && d->lay[1]->kind == LK_ADJUST, "new adjustment layer");
    c = composite(d);
    /* reference: the same adjustment applied destructively to a copy */
    e = doc_new(17, 11, 0, 0);
    memcpy(e->lay[0]->px, d->lay[0]->px, (size_t)n * 4);
    adj_apply(e, &p, "x");
    ref = (u8 *)malloc((size_t)n * 4);
    memcpy(ref, e->lay[0]->px, (size_t)n * 4);
    maxd = maxdiff(ref, c, n * 4);
    {
      char msg[80];
      sprintf(msg, "adjustment layer %s (max %d)", adj_names[kinds[i]], maxd);
      CHECK(maxd <= 1, msg);
    }
    free(c);
    doc_free(e);
    /* at half opacity: between */
    d->lay[1]->opacity = 128;
    c = composite(d);
    {
      long k;
      int ok = 1;
      for (k = 0; k < n * 4; k++) if ((k & 3) != 3) {
        int lo = d->lay[0]->px[k] < ref[k] ? d->lay[0]->px[k] : ref[k], hi = d->lay[0]->px[k] < ref[k] ? ref[k] : d->lay[0]->px[k];
        if (c[k] < lo - 1 || c[k] > hi + 1) ok = 0;
      }
      CHECK(ok, "adjustment at half opacity");
    }
    free(c);
    free(ref);
    CHECK(doc_delete_layer(d, 1), "delete adjustment layer");
  }
  /* settings changes are undoable and recomputed */
  adj_defaults(AK_INVERT, &p);
  doc_new_adjust(d, &p, "Invert");
  c = composite(d);
  CHECK(c[0] == 255 - d->lay[0]->px[0], "invert layer");
  free(c);
  adj_defaults(AK_POSTERIZE, &p); p.kind = AK_POSTERIZE; p.v[0] = 2;
  doc_set_adjust(d, 1, &p);
  c = composite(d);
  CHECK(c[0] == 0 || c[0] == 255, "changed to posterize");
  free(c);
  CHECK(doc_undo(d) && d->lay[1]->adj.kind == AK_INVERT, "undo adjustment settings");
  c = composite(d);
  CHECK(c[0] == 255 - d->lay[0]->px[0], "undo recomputes the tables");
  free(c);
  doc_delete_layer(d, 1);
  /* fill layers */
  {
    FillParams f;
    memset(&f, 0, sizeof f);
    f.kind = 0; f.c0 = 0x123456;
    doc_new_fill(d, &f, "Solid");
    c = composite(d);
    CHECK(c[0] == 0x12 && c[1] == 0x34 && c[2] == 0x56 && c[3] == 255, "solid fill layer");
    free(c);
    f.kind = 1; f.c0 = 0x000000; f.c1 = 0xFFFFFF; f.gkind = 0; f.angle = 0; f.scale = 100;
    doc_set_fill(d, 1, &f);
    c = composite(d);
    CHECK(c[0] < 40 && c[(16) * 4] > 215, "gradient fill layer left to right");
    CHECK(doc_rasterize(d, 1) && d->lay[1]->kind == LK_PIXEL, "rasterize fill");
    {
      u8 *c2 = composite(d);
      CHECK(maxdiff(c, c2, n * 4) == 0, "rasterized looks the same");
      free(c2);
    }
    free(c);
  }
  doc_free(d);
}

static void test_fill_blendif(void)
{
  Doc *d = doc_new(16, 16, 0x808080, 0);
  Layer *a = layer_new(d, "A");
  u8 *c;
  long k, n = 256;
  for (k = 0; k < n; k++) { a->px[k * 4] = a->px[k * 4 + 1] = a->px[k * 4 + 2] = (u8)k; a->px[k * 4 + 3] = 255; }
  doc_add_layer(d, a, 1);
  /* fill opacity 0: nothing */
  d->lay[1]->fill_cut = 255;
  c = composite(d);
  CHECK(c[0] == 0x80 && c[255 * 4] == 0x80, "fill opacity 0");
  free(c);
  d->lay[1]->fill_cut = 0;
  /* Blend If: this layer's darks (below 100) and lights (above 200) drop out */
  d->lay[1]->blendif[0] = 100; d->lay[1]->blendif[1] = 55;
  c = composite(d);
  CHECK(c[50 * 4] == 0x80 && c[150 * 4] == 150 && c[230 * 4] == 0x80, "Blend If on this layer");
  free(c);
  doc_free(d);
}

static void test_moves(void)
{
  Doc *d = doc_new(8, 8, 0, 0);
  int i;
  for (i = 0; i < 5; i++) { char nm[16]; sprintf(nm, "L%d", i + 1); doc_add_layer(d, rand_layer(d, nm, 0), d->nlay); }
  /* 0 Background, 1-5 L1..L5: group L2, L3 */
  doc_pick_layer(d, 2, 0); doc_pick_layer(d, 3, 1);
  doc_group_layers(d);
  CHECK(d->nlay == 7 && d->lay[4]->kind == LK_GROUP && !strcmp(d->lay[2]->name, "L2") && !strcmp(d->lay[3]->name, "L3"), "group of two");
  /* arrange the group forward: above L4 */
  CHECK(doc_arrange(d, 4, 1) && d->lay[5]->kind == LK_GROUP && !strcmp(d->lay[2]->name, "L4"), "group forward");
  /* backward again */
  CHECK(doc_arrange(d, 5, 2) && d->lay[4]->kind == LK_GROUP && !strcmp(d->lay[5]->name, "L4"), "group backward");
  /* L5 moved into the group, as its top member */
  CHECK(doc_move_block(d, 6, 3, 1) && d->lay[4]->depth == 1 && !strcmp(d->lay[4]->name, "L5") && d->lay[5]->kind == LK_GROUP, "into a group");
  /* the group to the back */
  CHECK(doc_arrange(d, 5, 3) && d->lay[3]->kind == LK_GROUP && d->lay[0]->depth == 1, "group to the back");
  for (i = 0; i < 5; i++) doc_undo(d);                  /* the group and four moves */
  if (d->nlay != 6) { int k; printf("nlay %d:", d->nlay); for (k = 0; k < d->nlay; k++) printf(" %s/%d", d->lay[k]->name, d->lay[k]->depth); printf("\n"); }
  CHECK(d->nlay == 6, "undo all moves");
  /* nested group inside a group */
  doc_pick_layer(d, 1, 0); doc_pick_layer(d, 2, 1); doc_group_layers(d);     /* G1 = L1, L2 at 3 */
  doc_pick_layer(d, 3, 0); doc_pick_layer(d, 4, 1); doc_group_layers(d);     /* G2 = G1 + L3 */
  CHECK(d->lay[5]->kind == LK_GROUP && d->lay[3]->kind == LK_GROUP && d->lay[3]->depth == 1 && d->lay[1]->depth == 2, "nested groups");
  {
    u8 *c = composite(d), *c2;
    doc_merge_group(d, 5);
    c2 = composite(d);
    CHECK(maxdiff(c, c2, 8 * 8 * 4) <= 2, "merge nested");
    free(c); free(c2);
  }
  /* align: a dot moved to the left edge */
  {
    Doc *e = doc_new(10, 10, 0, 1);
    Box b;
    memset(e->lay[0]->px, 0, 400);
    e->lay[0]->px[(5 * 10 + 6) * 4 + 3] = 255;
    CHECK(doc_align(e, AL_LEFT) && layer_content_box(e, 0, &b) && b.x0 == 0 && b.y0 == 5, "align left");
    doc_free(e);
  }
  doc_free(d);
}

/* everything saved as OpenRaster and read back: the same stack, the same picture */
static void test_ora(void)
{
  Doc *d = doc_new(21, 15, 0x204060, 0), *e;
  AdjParams p;
  FillParams f;
  u8 *png, *c0, *c1;
  size_t len;
  char err[100];
  int i, ok;
  adjlayer_init();
  doc_add_layer(d, rand_layer(d, "One", 0), 1);
  doc_add_layer(d, rand_layer(d, "Two", 0), 2);
  doc_add_layer(d, rand_layer(d, "Three", 1), 3);
  d->lay[3]->blend = BM_OVERLAY; d->lay[3]->opacity = 150;
  doc_pick_layer(d, 1, 0); doc_pick_layer(d, 2, 1);
  doc_group_layers(d);                                   /* 0 bg, 1 One, 2 Two, 3 Group, 4 Three */
  d->lay[3]->blend = BM_NORMAL; d->lay[3]->opacity = 200; d->lay[3]->collapsed = 1;
  doc_mask_add(d, 2, MK_REVEAL_ALL);
  for (i = 0; i < 21 * 15; i++) d->lay[2]->mask[i] = (u8)(i * 7);
  doc_mask_set(d, 2, 1, 0, 40, 3);
  doc_set_clip(d, 4, 1);
  d->lay[4]->fill_cut = 30; d->lay[4]->blendif[0] = 20; d->lay[4]->blendif[3] = 10; d->lay[4]->lock_pos = 1;
  adj_defaults(AK_CURVES, &p); p.npts = 3; p.pts[2] = 128; p.pts[3] = 170; p.pts[4] = 255; p.pts[5] = 255;
  d->active = 4; doc_new_adjust(d, &p, "Curves 1");
  memset(&f, 0, sizeof f); f.kind = 1; f.c0 = 0xFF0000; f.c1 = 0x0000FF; f.gkind = 1; f.scale = 80;
  doc_new_fill(d, &f, "Gradient Fill 1");
  d->lay[d->active]->opacity = 90; d->lay[d->active]->blend = BM_MULTIPLY;
  c0 = composite(d);
  png = ora_write(d, &len);
  CHECK(png != NULL, "ora written");
  e = ora_read(png, len, err, sizeof err);
  CHECK(e != NULL, "ora read back");
  if (e) {
    CHECK(e->nlay == d->nlay, "same number of layers");
    ok = e->nlay == d->nlay;
    for (i = 0; ok && i < d->nlay; i++) {
      Layer *a = d->lay[i], *b = e->lay[i];
      if (a->kind != b->kind || a->depth != b->depth || a->clip != b->clip || a->blend != b->blend || a->fill_cut != b->fill_cut ||
          a->lock_pos != b->lock_pos || memcmp(a->blendif, b->blendif, 4) || strcmp(a->name, b->name) || (!a->mask) != (!b->mask) ||
          a->collapsed != b->collapsed || abs(a->opacity - b->opacity) > 0) { ok = 0; printf("layer %d differs\n", i); }
      if (a->mask && b->mask && (memcmp(a->mask, b->mask, 21 * 15) || a->mask_fade != b->mask_fade || a->mask_feather != b->mask_feather ||
          a->mask_linked != b->mask_linked)) { ok = 0; printf("mask %d differs\n", i); }
      if (a->kind == LK_ADJUST && (a->adj.kind != b->adj.kind || a->adj.npts != b->adj.npts || memcmp(a->adj.pts, b->adj.pts, 6 * sizeof(int)))) { ok = 0; printf("adjust %d differs\n", i); }
      if (a->kind == LK_FILL && memcmp(&a->fillp, &b->fillp, sizeof a->fillp)) { ok = 0; printf("fill %d differs\n", i); }
    }
    CHECK(ok, "every layer's settings come back");
    c1 = composite(e);
    CHECK(maxdiff(c0, c1, 21 * 15 * 4) <= 1, "the same picture after OpenRaster");
    free(c1);
    doc_free(e);
  }
  free(c0); free(png);
  doc_free(d);
}

int main(void)
{
  test_groups();
  test_masks();
  test_clipping();
  test_adjust_fill();
  test_fill_blendif();
  test_moves();
  test_ora();
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
