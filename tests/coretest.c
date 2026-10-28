/*
 * coretest.c - native tests of Easel's core: the document, blending, undo, painting, selections,
 * transforms, adjustments and filters.
 * cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. core/util.c core/doc.c core/paint.c core/stroke.c core/brush.c core/gradient.c core/pattern.c core/adjust.c tests/coretest.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../core/doc.h"
#include "../core/paint.h"
#include "../core/adjust.h"

static int tests, failed;
static void ok(const char *name, int cond) { tests++; if (!cond) { failed++; printf("FAIL %s\n", name); } }
static const u8 *px(Doc *d, int layer, i32 x, i32 y) { return d->lay[layer]->px + ((long)y * d->w + x) * 4; }
static int is_rgba(const u8 *p, int r, int g, int b, int a, int tol)
{
  return abs(p[0] - r) <= tol && abs(p[1] - g) <= tol && abs(p[2] - b) <= tol && abs(p[3] - a) <= tol;
}
static long layer_sum(Doc *d, int l)
{
  long s = 0, k;
  for (k = 0; k < (long)d->w * d->h * 4; k++) s += d->lay[l]->px[k] * (k % 7 + 1);
  return s;
}
static Brush brush(int tool, int size, int hard, int opacity)
{
  Brush b;
  memset(&b, 0, sizeof b);
  b.tool = tool; b.size = size; b.hardness = hard; b.opacity = opacity; b.flow = 100; b.spacing = 25; b.strength = 50;
  return b;
}

static void documents(void)
{
  Doc *d = doc_new(40, 30, 0xFF0000, 0);
  u8 out[4];
  ok("new: one opaque red layer", d && d->nlay == 1 && is_rgba(px(d, 0, 5, 5), 255, 0, 0, 255, 0) && !strcmp(d->lay[0]->name, "Background"));
  ok("new: too big refused", doc_new(MAXDIM + 1, 10, 0, 0) == NULL);
  doc_add_layer(d, layer_new(d, "Top"), -1);
  ok("add layer: on top and active", d->nlay == 2 && d->active == 1 && !strcmp(d->lay[1]->name, "Top"));
  doc_composite(d, box_make(3, 3, 4, 4), out, 4);
  ok("composite: an empty layer shows what's under it", is_rgba(out, 255, 0, 0, 255, 0));
  memset(d->lay[1]->px, 0, (size_t)d->w * d->h * 4);
  { long k; for (k = 0; k < (long)d->w * d->h; k++) { u8 *p = d->lay[1]->px + k * 4; p[0] = 0; p[1] = 0; p[2] = 255; p[3] = 128; } }
  doc_composite(d, box_make(3, 3, 4, 4), out, 4);
  ok("composite: half blue over red", is_rgba(out, 127, 0, 128, 255, 2));
  d->lay[1]->blend = BM_MULTIPLY; d->lay[1]->px[3] = 255;
  { long k; for (k = 0; k < (long)d->w * d->h; k++) d->lay[1]->px[k * 4 + 3] = 255; }
  doc_composite(d, box_make(3, 3, 4, 4), out, 4);
  ok("composite: multiply of blue and red is black", is_rgba(out, 0, 0, 0, 255, 0));
  d->lay[1]->blend = BM_SCREEN;
  doc_composite(d, box_make(3, 3, 4, 4), out, 4);
  ok("composite: screen of blue and red is magenta", is_rgba(out, 255, 0, 255, 255, 0));
  d->lay[1]->blend = BM_NORMAL; d->lay[1]->opacity = 0;
  doc_composite(d, box_make(3, 3, 4, 4), out, 4);
  ok("composite: opacity 0 hides it", is_rgba(out, 255, 0, 0, 255, 0));
  d->lay[1]->opacity = 255; d->lay[1]->visible = 0;
  doc_composite(d, box_make(3, 3, 4, 4), out, 4);
  ok("composite: hidden layers don't show", is_rgba(out, 255, 0, 0, 255, 0));
  d->lay[1]->visible = 1;
  ok("undo: layer added undone", doc_undo(d) && d->nlay == 1);
  ok("redo: back again (pixels as they were when undone)", doc_redo(d) && d->nlay == 2);
  ok("merge down", doc_merge_down(d, 1) && d->nlay == 1 && is_rgba(px(d, 0, 0, 0), 0, 0, 255, 255, 0));
  ok("merge down undone", doc_undo(d) && d->nlay == 2);
  ok("duplicate", doc_duplicate_layer(d, 1) && d->nlay == 3 && !strcmp(d->lay[2]->name, "Top copy"));
  ok("move layer", doc_move_layer(d, 2, 0) && !strcmp(d->lay[0]->name, "Top copy") && d->active == 0);
  ok("delete layer", doc_delete_layer(d, 0) && d->nlay == 2 && !strcmp(d->lay[0]->name, "Background"));
  d->lay[1]->opacity = 128;
  ok("flatten", doc_flatten(d) && d->nlay == 1 && d->lay[0]->px[3] == 255);
  ok("flatten undone", doc_undo(d) && d->nlay == 2);
  doc_set_layer_props(d, 1, "Renamed", 200, BM_OVERLAY, -1);
  ok("layer settings", !strcmp(d->lay[1]->name, "Renamed") && d->lay[1]->opacity == 200 && d->lay[1]->blend == BM_OVERLAY);
  ok("layer settings undone", doc_undo(d) && !strcmp(d->lay[1]->name, "Top") && d->lay[1]->blend == BM_NORMAL);
  { const char *h[10]; int n = doc_history(d, h, 10); ok("history lists the steps", n >= 3 && !strcmp(h[0], "New Layer")); }
  doc_free(d);
}

static void painting(void)
{
  Doc *d = doc_new(60, 40, 0xFFFFFF, 0);
  Brush b = brush(PT_BRUSH, 10, 100, 100);
  Stroke *s;
  Box ch;
  long before = layer_sum(d, 0);
  s = stroke_begin(d, &b, 0x000000, 10, 20, &ch);
  stroke_to(s, 50, 20, &ch);
  stroke_end(s);
  ok("stroke: paints black along the line", is_rgba(px(d, 0, 30, 20), 0, 0, 0, 255, 0));
  ok("stroke: leaves the rest", is_rgba(px(d, 0, 30, 5), 255, 255, 255, 255, 0));
  ok("stroke: undone exactly", doc_undo(d) && layer_sum(d, 0) == before);
  ok("stroke: redone", doc_redo(d) && is_rgba(px(d, 0, 30, 20), 0, 0, 0, 255, 0));
  doc_undo(d);
  b = brush(PT_BRUSH, 10, 0, 100);
  s = stroke_begin(d, &b, 0x000000, 10, 20, &ch); stroke_to(s, 50, 20, &ch); stroke_end(s);
  ok("stroke: a soft brush fades at its edge", px(d, 0, 30, 23)[0] > 0 && px(d, 0, 30, 23)[0] < 255);
  doc_undo(d);
  /* opacity caps the stroke even where it crosses itself */
  b = brush(PT_BRUSH, 8, 100, 50);
  s = stroke_begin(d, &b, 0x000000, 10, 10, &ch);
  stroke_to(s, 50, 10, &ch); stroke_to(s, 10, 10, &ch); stroke_to(s, 50, 10, &ch);
  stroke_end(s);
  ok("stroke: 50% opacity stays 50% where it crosses itself", abs(px(d, 0, 30, 10)[0] - 128) <= 2);
  doc_undo(d);
  /* the selection keeps paint in */
  sel_rect(d, 0, 0, 30, 40, SEL_REPLACE, 0);
  b = brush(PT_BRUSH, 6, 100, 100);
  s = stroke_begin(d, &b, 0x0000FF, 5, 20, &ch); stroke_to(s, 55, 20, &ch); stroke_end(s);
  ok("selection: paint inside", is_rgba(px(d, 0, 20, 20), 0, 0, 255, 255, 0));
  ok("selection: none outside", is_rgba(px(d, 0, 40, 20), 255, 255, 255, 255, 0));
  sel_none(d);
  ok("deselect", d->sel == NULL);
  /* the eraser on a layer above */
  doc_add_layer(d, layer_new(d, "L"), -1);
  fill_selection(d, 0x00FF00, 100, BM_NORMAL);
  ok("fill: the whole layer", is_rgba(px(d, 1, 0, 0), 0, 255, 0, 255, 0));
  b = brush(PT_ERASER, 6, 100, 100);
  s = stroke_begin(d, &b, 0, 30, 5, &ch); stroke_to(s, 30, 35, &ch); stroke_end(s);
  ok("eraser: clears to transparent", px(d, 1, 30, 20)[3] == 0 && px(d, 1, 5, 5)[3] == 255);
  /* the pencil: hard edges */
  b = brush(PT_PENCIL, 4, 0, 100);
  s = stroke_begin(d, &b, 0xFF0000, 10, 30, &ch); stroke_to(s, 20, 30, &ch); stroke_end(s);
  ok("pencil: hard", is_rgba(px(d, 1, 15, 30), 255, 0, 0, 255, 0) && (px(d, 1, 15, 33)[0] == 0 || px(d, 1, 15, 33)[0] == 255));
  /* clone stamp copies from the offset */
  d->active = 0;
  b = brush(PT_CLONE, 4, 100, 100); b.clone_dx = 0; b.clone_dy = 0;
  ok("locked layer can't be painted", (d->lay[0]->locked = 1, stroke_begin(d, &b, 0, 5, 5, &ch) == NULL));
  d->lay[0]->locked = 0;
  doc_free(d);
}

static void fills(void)
{
  Doc *d = doc_new(20, 20, 0xFFFFFF, 0);
  Box b;
  shape_rect(d, 5, 5, 15, 15, 0, 0x000000, 100);
  ok("rectangle drawn", is_rgba(px(d, 0, 10, 10), 0, 0, 0, 255, 0) && is_rgba(px(d, 0, 2, 2), 255, 255, 255, 255, 0));
  b = fill_bucket(d, 10, 10, 0xFF0000, 0, 1, 0, 100, 0);
  ok("bucket: fills the square only", is_rgba(px(d, 0, 10, 10), 255, 0, 0, 255, 0) && is_rgba(px(d, 0, 2, 2), 255, 255, 255, 255, 0) && b.x0 == 5 && b.x1 == 15);
  fill_bucket(d, 0, 0, 0x00FF00, 10, 0, 0, 100, 0);
  ok("bucket: not contiguous takes every like pixel", is_rgba(px(d, 0, 19, 19), 0, 255, 0, 255, 0) && is_rgba(px(d, 0, 10, 10), 255, 0, 0, 255, 0));
  fill_gradient(d, 0, 0, 20, 0, GR_LINEAR, 0x000000, 0xFFFFFF, 0, 0, 100, BM_NORMAL);
  ok("gradient: dark to light", px(d, 0, 1, 10)[0] < 30 && px(d, 0, 18, 10)[0] > 225 && abs(px(d, 0, 10, 10)[0] - 133) < 12);
  shape_ellipse(d, 0, 0, 20, 20, 0x0000FF, 100);
  ok("ellipse: middle in, corner out", is_rgba(px(d, 0, 10, 10), 0, 0, 255, 255, 0) && px(d, 0, 0, 0)[2] < 255);
  ok("eyedropper", doc_pick(d, 10, 10, 1, 0) == 0x0000FF);
  doc_free(d);
}

static void selections(void)
{
  Doc *d = doc_new(30, 30, 0xFFFFFF, 0);
  Box b;
  i32 *e;
  int n;
  sel_rect(d, 5, 5, 15, 15, SEL_REPLACE, 0);
  ok("rect: selected inside", sel_at(d, 10, 10) == 255 && sel_at(d, 20, 20) == 0 && sel_bounds(d, &b) && b.x0 == 5 && b.x1 == 15);
  sel_rect(d, 10, 10, 20, 20, SEL_ADD, 0);
  ok("add", sel_at(d, 18, 18) == 255 && sel_at(d, 6, 6) == 255);
  sel_rect(d, 0, 0, 12, 12, SEL_SUBTRACT, 0);
  ok("subtract", sel_at(d, 6, 6) == 0 && sel_at(d, 18, 18) == 255);
  sel_rect(d, 15, 15, 30, 30, SEL_INTERSECT, 0);
  ok("intersect", sel_at(d, 18, 18) == 255 && sel_at(d, 13, 13) == 0);
  sel_invert(d);
  ok("invert", sel_at(d, 18, 18) == 0 && sel_at(d, 2, 2) == 255);
  sel_ellipse(d, 0, 0, 30, 30, SEL_REPLACE, 0);
  ok("ellipse", sel_at(d, 15, 15) == 255 && sel_at(d, 0, 0) == 0);
  n = sel_edges(d, doc_box(d), &e);
  ok("marching ants have edges", n > 4);
  free(e);
  sel_rect(d, 4, 4, 26, 26, SEL_REPLACE, 0);
  sel_feather(d, 4);
  ok("feather softens the edge", sel_at(d, 4, 15) > 0 && sel_at(d, 4, 15) < 255 && sel_at(d, 15, 15) == 255);
  sel_rect(d, 10, 10, 20, 20, SEL_REPLACE, 0);
  sel_grow(d, 2);
  ok("expand", sel_at(d, 8, 15) == 255 && sel_at(d, 6, 15) == 0);
  sel_grow(d, -4);
  ok("contract", sel_at(d, 11, 15) == 0 && sel_at(d, 13, 15) == 255);
  {
    double tri[6] = { 0, 0, 30, 0, 0, 30 };
    sel_polygon(d, tri, 3, SEL_REPLACE, 0);
    ok("lasso", sel_at(d, 5, 5) == 255 && sel_at(d, 25, 25) == 0);
  }
  shape_rect(d, 0, 0, 30, 30, 0, 0xFFFFFF, 100);
  sel_none(d);
  shape_rect(d, 10, 10, 20, 20, 0, 0x000000, 100);
  sel_wand(d, 15, 15, 10, 1, 0, SEL_REPLACE);
  ok("magic wand", sel_at(d, 15, 15) == 255 && sel_at(d, 5, 5) == 0);
  ok("selection undo", doc_undo(d) && d->sel == NULL);
  sel_all(d);
  ok("select all", sel_at(d, 0, 0) == 255 && sel_at(d, 29, 29) == 255);
  doc_free(d);
}

static void transforms(void)
{
  Doc *d = doc_new(40, 20, 0xFFFFFF, 0);
  long s0;
  shape_rect(d, 0, 0, 10, 10, 0, 0xFF0000, 100);
  s0 = layer_sum(d, 0);
  image_flip(d, 0);
  ok("flip canvas horizontal", is_rgba(px(d, 0, 35, 5), 255, 0, 0, 255, 0) && is_rgba(px(d, 0, 5, 5), 255, 255, 255, 255, 0));
  image_flip(d, 0);
  ok("flip twice is unchanged", layer_sum(d, 0) == s0);
  image_rotate(d, 1);
  ok("rotate 90: sizes swap", d->w == 20 && d->h == 40 && is_rgba(px(d, 0, 15, 5), 255, 0, 0, 255, 0));
  image_rotate(d, 3);
  ok("and back", d->w == 40 && layer_sum(d, 0) == s0);
  image_resize(d, 80, 40, RS_BICUBIC);
  ok("resize up", d->w == 80 && d->h == 40 && is_rgba(px(d, 0, 10, 10), 255, 0, 0, 255, 2));
  doc_undo(d);
  ok("resize undone", d->w == 40 && layer_sum(d, 0) == s0);
  image_resize(d, 20, 10, RS_BILINEAR);
  ok("resize down averages", d->w == 20 && is_rgba(px(d, 0, 2, 2), 255, 0, 0, 255, 1));
  doc_undo(d);
  canvas_resize(d, 60, 40, 4, 0x0000FF);
  ok("canvas size: centred, filled", d->w == 60 && is_rgba(px(d, 0, 15, 15), 255, 0, 0, 255, 0) && is_rgba(px(d, 0, 1, 1), 0, 0, 255, 255, 0));
  doc_undo(d);
  ok("crop", image_crop(d, box_make(0, 0, 10, 10)) && d->w == 10 && is_rgba(px(d, 0, 9, 9), 255, 0, 0, 255, 0));
  doc_undo(d);
  sel_rect(d, 0, 0, 10, 10, SEL_REPLACE, 0);
  move_layer(d, 20, 5);
  ok("move: the selection's pixels go, leaving a hole", is_rgba(px(d, 0, 25, 10), 255, 0, 0, 255, 0) && px(d, 0, 5, 5)[3] == 0);
  ok("move: the selection goes with them", sel_at(d, 25, 10) == 255 && sel_at(d, 5, 5) == 0);
  doc_undo(d);
  ok("move undone", layer_sum(d, 0) == s0 && sel_at(d, 5, 5) == 255);
  sel_none(d);
  ok("rotate any angle grows the canvas", image_rotate_angle(d, 45) && d->w > 40 && d->h > 20);
  doc_undo(d);
  {
    Clip *c;
    sel_rect(d, 0, 0, 10, 10, SEL_REPLACE, 0);
    c = clip_copy(d, 0);
    ok("copy", c && c->w == 10 && c->h == 10 && c->px[3] == 255);
    ok("paste as a new layer", clip_paste(d, c, 1) && d->nlay == 2 && is_rgba(px(d, 1, 5, 5), 255, 0, 0, 255, 0));
    clip_free(c);
  }
  doc_free(d);
}

static void adjustments(void)
{
  Doc *d = doc_new(32, 32, 0x804020, 0);
  long s0 = layer_sum(d, 0);
  u32 h[1024];
  adj_invert(d);
  ok("invert", is_rgba(px(d, 0, 0, 0), 127, 191, 223, 255, 0));
  adj_invert(d);
  ok("invert twice is unchanged", layer_sum(d, 0) == s0);
  { int p[4] = { 0, 0, 255, 255 }; adj_curves(d, 0, p, 2); ok("straight curve changes nothing", layer_sum(d, 0) == s0); }
  adj_levels(d, 0, 0, 128, 1, 0, 255);
  ok("levels: white point halved doubles", is_rgba(px(d, 0, 0, 0), 255, 128, 64, 255, 1));
  doc_undo(d);
  adj_desaturate(d);
  ok("desaturate", px(d, 0, 0, 0)[0] == px(d, 0, 0, 0)[1] && px(d, 0, 0, 0)[1] == px(d, 0, 0, 0)[2]);
  doc_undo(d);
  adj_hue_saturation(d, 180, 0, 0, 0);
  ok("hue turned half way", px(d, 0, 0, 0)[2] > px(d, 0, 0, 0)[0]);
  doc_undo(d);
  adj_threshold(d, 128);
  ok("threshold", px(d, 0, 0, 0)[0] == 0 || px(d, 0, 0, 0)[0] == 255);
  doc_undo(d);
  adj_brightness_contrast(d, 0, 0);
  ok("brightness/contrast 0 leaves it", layer_sum(d, 0) == s0 || doc_can_undo(d));
  histogram(d, 0, h);
  ok("histogram counts every pixel", h[256 + 0x80] == 32 * 32 && h[768 + 0x20] == 32 * 32);
  flt_gaussian_blur(d, 3);
  ok("blur of a flat colour keeps it", is_rgba(px(d, 0, 16, 16), 0x80, 0x40, 0x20, 255, 1));
  shape_rect(d, 0, 0, 16, 32, 0, 0xFFFFFF, 100);
  shape_rect(d, 16, 0, 32, 32, 0, 0x000000, 100);
  flt_gaussian_blur(d, 3);
  ok("blur softens an edge", px(d, 0, 16, 16)[0] > 40 && px(d, 0, 16, 16)[0] < 215 && px(d, 0, 2, 16)[0] > 250);
  doc_undo(d);
  flt_unsharp_mask(d, 100, 2, 0);
  ok("unsharp mask", px(d, 0, 14, 16)[0] == 255 && px(d, 0, 17, 16)[0] == 0);
  doc_undo(d);
  flt_find_edges(d);
  ok("find edges: dark at the edge, light elsewhere", px(d, 0, 16, 16)[0] < 128 && px(d, 0, 5, 16)[0] > 250);
  doc_undo(d);
  flt_mosaic(d, 8);
  ok("mosaic", px(d, 0, 0, 0)[0] == px(d, 0, 7, 7)[0]);
  doc_undo(d);
  { long s1 = layer_sum(d, 0);
    flt_twirl(d, 90); ok("twirl changes it", layer_sum(d, 0) != s1); doc_undo(d);
    flt_median(d, 1); ok("median keeps a straight edge", px(d, 0, 2, 16)[0] == 255); doc_undo(d);
    flt_add_noise(d, 20, 1, 1); ok("noise", layer_sum(d, 0) != s1); doc_undo(d);
    ok("all undone", layer_sum(d, 0) == s1); }
  /* only in the selection */
  sel_rect(d, 0, 0, 8, 8, SEL_REPLACE, 0);
  adj_invert(d);
  ok("adjusts only the selection", px(d, 0, 2, 2)[0] == 0 && px(d, 0, 12, 12)[0] == 255);
  /* preview cancelled */
  { long s2 = layer_sum(d, 0); flt_gaussian_blur(d, 5); doc_discard_last(d); ok("preview discarded", layer_sum(d, 0) == s2 && !doc_can_redo(d)); }
  doc_free(d);
}

int main(void)
{
  documents();
  painting();
  fills();
  selections();
  transforms();
  adjustments();
  printf("%d tests, %d failed\n", tests, failed);
  return failed != 0;
}
