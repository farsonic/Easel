/*
 * r3imagetest.c - round 3's picture work: shape layers filled with gradients and patterns and with a
 * radius per corner, Combine Shapes, Fill Path with a pattern or history, the built-in custom shapes;
 * the new filters (Field Blur, Flame, Picture Frame, Tree, HDR Toning, Liquify, Lens Correction's
 * perspective and Auto Scale, Oil Paint's lighting); Replace Color's sampled colours, Curves .acv;
 * Select Sky and Subject, Color Range's localized clusters, Decontaminate Colors; type scaled
 * horizontally and vertically, hyphenation, contextual alternates, type on a path. Run under
 * ASan/UBSan.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core/doc.h"
#include "core/layers.h"
#include "core/layerx.h"
#include "core/path.h"
#include "core/pattern.h"
#include "core/gradient.h"
#include "core/paint.h"
#include "core/live.h"
#include "core/shape.h"
#include "core/vecx.h"
#include "core/pathops.h"
#include "core/brush.h"

static int fails, tests;
#define CHECK(c, msg) do { tests++; if (!(c)) { fails++; printf("FAIL %s (line %d)\n", msg, __LINE__); } } while (0)
static const char *tmpdir = "/tmp";

static void comp_px(Doc *d, i32 x, i32 y, u8 *out) { doc_composite(d, box_make(x, y, x + 1, y + 1), out, 4); }

/* ------------------------------------------------------------------ shapes */
static void test_shape_fills(void)
{
  Doc *d = doc_new(100, 60, 0xFFFFFF, 0);
  ShapeParams sp;
  Gradient g;
  Pattern *pat;
  int i;
  u8 a[4], b[4], c[4];
  char *saved;
  Layer tmp;
  shape_defaults(&sp, SH_RECT, 10, 10, 90, 50, 0xFF0000);
  i = doc_new_shape(d, &sp, NULL, NULL);
  CHECK(i == 1, "shape layer");
  /* a black-to-white linear gradient across the shape, left to right */
  gradient_init(&g, "bw", 0x000000, 0xFFFFFF);
  CHECK(doc_set_shape_fill(d, i, SFILL_GRADIENT, &g, GR_LINEAR, 0, 100, 0, NULL), "gradient fill");
  comp_px(d, 11, 30, a); comp_px(d, 50, 30, b); comp_px(d, 88, 30, c);
  CHECK(a[0] < 20 && c[0] > 235 && abs(b[0] - 128) < 12 && b[0] == b[1] && b[1] == b[2], "linear: black at the left, grey in the middle, white at the right");
  comp_px(d, 5, 30, a);
  CHECK(a[0] == 255 && a[1] == 255, "outside the shape the background");
  /* reversed, then 90 degrees (bottom to top) */
  CHECK(doc_set_shape_fill(d, i, SFILL_GRADIENT, &g, GR_LINEAR, 0, 100, 1, NULL), "reversed");
  comp_px(d, 11, 30, a);
  CHECK(a[0] > 235, "reversed: white at the left");
  CHECK(doc_set_shape_fill(d, i, SFILL_GRADIENT, &g, GR_LINEAR, 90, 100, 0, NULL), "angle 90");
  comp_px(d, 50, 11, a); comp_px(d, 50, 49, b);
  CHECK(a[0] > 220 && b[0] < 35, "90 degrees: black at the bottom, white at the top");
  /* radial: black in the middle */
  CHECK(doc_set_shape_fill(d, i, SFILL_GRADIENT, &g, GR_RADIAL, 0, 100, 0, NULL), "radial");
  comp_px(d, 50, 30, a); comp_px(d, 11, 11, b);
  CHECK(a[0] < 10 && b[0] > 200, "radial: black at the centre, light at the corner");
  /* kept through OpenRaster's text */
  saved = live_save(d->lay[i]);
  CHECK(saved && strstr(saved, "fk=1") && strstr(saved, "gc="), "saved");
  memset(&tmp, 0, sizeof tmp);
  CHECK(saved && live_load(&tmp, LK_SHAPE, saved), "loaded");
  if (layer_shape(&tmp)) {
    const ShapeParams *q = layer_shape(&tmp);
    CHECK(q->fill_kind == SFILL_GRADIENT && q->grad_style == GR_RADIAL && q->grad_nc == 2 && q->grad_rgb[1] == 0xFFFFFF, "gradient read back");
  }
  free(tmp.live); free(saved);
  /* undo: the gradient goes */
  doc_undo(d);
  comp_px(d, 50, 30, a);
  CHECK(layer_shape(d->lay[i])->grad_style == GR_LINEAR && layer_shape(d->lay[i])->grad_angle == 90 && abs(a[0] - 128) < 16, "undo the radial fill");
  /* a pattern: a 2 x 2 checker of red and blue */
  pat = pattern_new("check", 2, 2);
  {
    int k;
    for (k = 0; k < 4; k++) { u8 *p = pat->px + k * 4; int red = k == 0 || k == 3; p[0] = red ? 255 : 0; p[1] = 0; p[2] = red ? 0 : 255; p[3] = 255; }
  }
  CHECK(doc_set_shape_fill(d, i, SFILL_PATTERN, NULL, 0, 0, 100, 0, pat), "pattern fill");
  comp_px(d, 20, 20, a); comp_px(d, 21, 20, b);
  CHECK(a[1] == 0 && b[1] == 0 && ((a[0] == 255 && b[2] == 255) || (a[2] == 255 && b[0] == 255)), "the pattern shows, alternating");
  CHECK(layerx_get(d->lay[i]) && !layerx_get(d->lay[i])->po_on, "no Pattern Overlay turned on by it");
  /* scale 200: pairs of pixels */
  CHECK(doc_set_shape_fill(d, i, SFILL_PATTERN, NULL, 0, 0, 200, 0, NULL), "pattern scaled");
  comp_px(d, 20, 20, a); comp_px(d, 21, 20, b);
  CHECK(a[0] == b[0] && a[2] == b[2], "scaled 200%: two pixels a cell");
  CHECK(doc_set_shape_fill(d, i, SFILL_COLOR, NULL, 0, 0, 0, 0, NULL), "back to a colour");
  comp_px(d, 20, 20, a);
  CHECK(a[0] == 255 && a[1] == 0 && a[2] == 0, "solid red again");
  pattern_free(pat);
  doc_free(d);
}
static void test_corner_radii(void)
{
  Doc *d = doc_new(100, 100, 0xFFFFFF, 0);
  ShapeParams sp;
  int i;
  u8 a[4];
  Path *p = path_new();
  double r[4] = { 0, 30, 0, 10 };
  shape_defaults(&sp, SH_RECT, 10, 10, 90, 90, 0x000000);
  sp.radii_on = 1; memcpy(sp.radii, r, sizeof r);
  i = doc_new_shape(d, &sp, NULL, NULL);
  comp_px(d, 11, 11, a); CHECK(a[0] < 30, "top left square");
  comp_px(d, 88, 11, a); CHECK(a[0] > 225, "top right rounded (30)");
  comp_px(d, 88, 88, a); CHECK(a[0] < 30, "bottom right square");
  comp_px(d, 11, 88, a); CHECK(a[0] > 128, "bottom left rounded (10)");
  comp_px(d, 14, 86, a); CHECK(a[0] < 60, "inside the small radius filled");
  /* radii too big for the side are scaled down together */
  r[0] = r[1] = r[2] = r[3] = 500;
  CHECK(shape_rect_path4(p, 0, 0, 40, 20, r), "huge radii");
  CHECK(p->na == 8, "eight anchors");
  {
    i32 k;
    double minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
    for (k = 0; k < p->na; k++) {
      minx = fmin(minx, p->a[k].x); maxx = fmax(maxx, p->a[k].x);
      miny = fmin(miny, p->a[k].y); maxy = fmax(maxy, p->a[k].y);
    }
    CHECK(minx >= -1e-9 && maxx <= 40 + 1e-9 && miny >= -1e-9 && maxy <= 20 + 1e-9, "stays in its box");
  }
  {
    char *s = live_save(d->lay[i]);
    Layer t;
    memset(&t, 0, sizeof t);
    CHECK(s && strstr(s, "radii=0,30,0,10"), "radii saved");
    CHECK(s && live_load(&t, LK_SHAPE, s) && layer_shape(&t)->radii_on && layer_shape(&t)->radii[1] == 30, "radii read");
    free(t.live); free(s);
  }
  path_free(p);
  doc_free(d);
}

static int rect_layer(Doc *d, double x0, double y0, double x1, double y1, u32 c)
{
  ShapeParams sp;
  shape_defaults(&sp, SH_RECT, x0, y0, x1, y1, c);
  return doc_new_shape(d, &sp, NULL, NULL);
}
static int dark(Doc *d, i32 x, i32 y) { u8 c[4]; comp_px(d, x, y, c); return c[0] < 64; }
static void test_combine(void)
{
  static const int ops[4] = { CS_UNITE, CS_SUBTRACT, CS_INTERSECT, CS_EXCLUDE };
  /* expected at (15,15) only in A, (25,25) both, (35,35) only in B */
  static const int want[4][3] = { { 1, 1, 1 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 0, 1 } };
  int k;
  for (k = 0; k < 4; k++) {
    Doc *d = doc_new(60, 60, 0xFFFFFF, 0);
    int a = rect_layer(d, 10, 10, 30, 30, 0), b, r;
    b = rect_layer(d, 20, 20, 40, 40, 0);
    d->lay[a]->picked = 1; d->active = b;
    r = doc_combine_shapes(d, ops[k]);
    CHECK(r == 1 && d->nlay == 2, "combined into one layer");
    CHECK(dark(d, 15, 15) == want[k][0] && dark(d, 25, 25) == want[k][1] && dark(d, 35, 35) == want[k][2], "the boolean's inside");
    CHECK(!dark(d, 5, 5) && !dark(d, 50, 50), "outside both");
    if (layer_shape(d->lay[1])) {
      Path p = shape_path(layer_shape(d->lay[1]));
      double x0, y0, x1, y1;
      CHECK(layer_shape(d->lay[1])->kind == SH_CUSTOM, "a custom shape");
      if (k == CS_INTERSECT && path_bounds(&p, &x0, &y0, &x1, &y1))
        CHECK(fabs(x0 - 20) < 0.3 && fabs(y0 - 20) < 0.3 && fabs(x1 - 30) < 0.3 && fabs(y1 - 30) < 0.3, "intersection 20..30 to a third of a pixel");
    }
    doc_undo(d);
    if (d->nlay != 3 || !layer_shape(d->lay[1]) || layer_shape(d->lay[1])->kind != SH_RECT) printf("nlay %d kind %d\n", d->nlay, layer_shape(d->lay[1]) ? layer_shape(d->lay[1])->kind : -1);
    CHECK(d->nlay == 3 && layer_shape(d->lay[1])->kind == SH_RECT, "undo brings both back");
    doc_free(d);
  }
  {
    /* one shape: not enough for Unite; Merge Shape Components resolves its subpaths */
    Doc *d = doc_new(60, 60, 0xFFFFFF, 0);
    Path *p = path_new();
    ShapeParams sp;
    int i;
    shape_rect_path(p, 10, 10, 30, 30, 0); shape_rect_path(p, 20, 20, 40, 40, 0);
    shape_defaults(&sp, SH_CUSTOM, 10, 10, 40, 40, 0);
    sp.evenodd = 1;                              /* the overlap a hole */
    i = doc_new_shape(d, &sp, p, NULL);
    CHECK(doc_combine_shapes(d, CS_UNITE) < 0, "Unite needs two layers");
    CHECK(!dark(d, 25, 25) && dark(d, 15, 15), "even-odd: the overlap empty");
    CHECK(doc_combine_shapes(d, CS_MERGE) == i, "merge components");
    CHECK(!dark(d, 25, 25) && dark(d, 15, 15) && dark(d, 35, 35), "the same look after merging");
    CHECK(shape_path(layer_shape(d->lay[i])).ns >= 2, "outlines traced");
    path_free(p);
    doc_free(d);
  }
}
static void test_fill_path_sources(void)
{
  Doc *d = doc_new(40, 40, 0xFFFFFF, 0);
  Path *p = path_new();
  Pattern *pat = pattern_new("p", 2, 1);
  Snapshot *snap;
  u8 *px = d->lay[0]->px;
  shape_rect_path(p, 10, 10, 30, 30, 0);
  pat->px[0] = 255; pat->px[1] = 0; pat->px[2] = 0; pat->px[3] = 255;
  pat->px[4] = 0; pat->px[5] = 255; pat->px[6] = 0; pat->px[7] = 255;
  CHECK(!box_empty(path_fill_source(d, p, PF_PATTERN, 0, pat, NULL, 100, BM_NORMAL, 1)), "fill path with a pattern");
  CHECK(px[(20 * 40 + 20) * 4] == 255 && px[(20 * 40 + 21) * 4 + 1] == 255 && px[(20 * 40 + 21) * 4] == 0, "the pattern's tiles from the canvas's corner");
  CHECK(px[(5 * 40 + 5) * 4 + 1] == 255 && px[(5 * 40 + 5) * 4 + 2] == 255, "outside untouched");
  /* history: a snapshot of the white picture painted back into the path */
  doc_undo(d);
  snap = snapshot_take(d);
  CHECK(snap != NULL, "snapshot");
  memset(px, 0, (size_t)40 * 40 * 4);
  CHECK(!box_empty(path_fill_source(d, p, PF_HISTORY, 0, NULL, snap, 100, BM_NORMAL, 1)), "fill path with history");
  CHECK(px[(20 * 40 + 20) * 4] == 255 && px[(20 * 40 + 20) * 4 + 3] == 255 && px[(5 * 40 + 5) * 4 + 3] == 0, "the snapshot inside, the rest as it was");
  CHECK(box_empty(path_fill_source(d, p, PF_PATTERN, 0, NULL, NULL, 100, BM_NORMAL, 1)), "no pattern: nothing");
  snapshot_free(snap); pattern_free(pat); path_free(p);
  doc_free(d);
}
static void test_shape_library(void)
{
  int k, n = shape_library_count();
  CHECK(n >= 12, "a dozen built-in shapes");
  for (k = 0; k < n; k++) {
    Path *p = path_new();
    double x0, y0, x1, y1;
    int ok = shape_library_path(k, p, 10, 20, 210, 120);
    CHECK(ok && p->na >= 2 && path_bounds(p, &x0, &y0, &x1, &y1) && x0 >= 9 && x1 <= 211 && y0 >= 19 && y1 <= 121 && x1 - x0 > 100 && y1 - y0 > 50, shape_library_name(k));
    path_free(p);
  }
  {
    /* the ring has a hole */
    Doc *d = doc_new(100, 100, 0xFFFFFF, 0);
    Path *p = path_new();
    ShapeParams sp;
    for (k = 0; k < n && strcmp(shape_library_name(k), "Ring"); k++) continue;
    shape_library_path(k, p, 0, 0, 100, 100);
    shape_defaults(&sp, SH_CUSTOM, 0, 0, 100, 100, 0); sp.evenodd = p->evenodd;
    doc_new_shape(d, &sp, p, NULL);
    CHECK(dark(d, 50, 10) && !dark(d, 50, 50), "ring: filled band, empty middle");
    path_free(p); doc_free(d);
  }
}

int main(int argc, char **argv)
{
  if (argc > 1) tmpdir = argv[1];
  layerx_init();
  test_shape_fills();
  test_corner_radii();
  test_combine();
  test_fill_path_sources();
  test_shape_library();
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
