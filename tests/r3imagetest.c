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
#include "core/filtr3.h"
#include "core/filters10.h"
#include "core/codec.h"
#include "core/util.h"

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

/* ------------------------------------------------------------------ filters */
/* a test picture: a gradient with a few hard shapes, opaque */
static Doc *test_picture(i32 w, i32 h)
{
  Doc *d = doc_new(w, h, 0xFFFFFF, 0);
  u8 *px = d->lay[0]->px;
  i32 x, y;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      u8 *p = px + ((long)y * w + x) * 4;
      int sq = (x / 12 + y / 12) & 1, disc = (x - w / 2) * (x - w / 2) + (y - h / 2) * (y - h / 2) < (w / 5) * (w / 5);
      p[0] = (u8)(disc ? 230 : sq ? 40 : x * 255 / w); p[1] = (u8)(disc ? 40 : y * 255 / h); p[2] = (u8)(disc ? 60 : sq ? 200 : 128); p[3] = 255;
    }
  return d;
}
static void save_png(const Doc *d, const char *name)
{
  Rgba im;
  size_t len;
  u8 *data;
  char path[512];
  im.w = d->w; im.h = d->h; im.px = d->lay[0]->px;
  if (!(data = png_encode(&im, 72, &len))) return;
  snprintf(path, sizeof path, "%s/%s", tmpdir, name);
  file_write(path, data, len);
  free(data);
}
static double diff_mean(const u8 *a, const u8 *b, long n)
{
  double s = 0;
  long k;
  for (k = 0; k < n; k++) s += abs(a[k] - b[k]);
  return s / n;
}
static void test_field_blur(void)
{
  Doc *d = test_picture(120, 90);
  long n = 120L * 90 * 4;
  u8 *orig = (u8 *)malloc((size_t)n);
  BlurPin pins[2];
  memcpy(orig, d->lay[0]->px, (size_t)n);
  save_png(d, "r3_field_src.png");
  /* one pin: the same blur everywhere (checked against Pillow's Gaussian blur by check_r3.py) */
  pins[0].x = 60; pins[0].y = 45; pins[0].blur = 8;
  CHECK(fabs(field_blur_radius(pins, 1, 3, 3) - 8) < 1e-9, "one pin: its radius everywhere");
  CHECK(flt_field_blur(d, pins, 1) == 1, "field blur");
  save_png(d, "r3_field_one.png");
  CHECK(diff_mean(orig, d->lay[0]->px, n) > 3, "blurred");
  doc_undo(d);
  CHECK(diff_mean(orig, d->lay[0]->px, n) == 0, "undo");
  /* two pins: sharp at the left, blurred at the right */
  pins[0].x = 5; pins[0].y = 45; pins[0].blur = 0;
  pins[1].x = 115; pins[1].y = 45; pins[1].blur = 12;
  CHECK(field_blur_radius(pins, 2, 60, 45) > 5 && field_blur_radius(pins, 2, 60, 45) < 7, "half way: about half the blur");
  CHECK(flt_field_blur(d, pins, 2) == 1, "two pins");
  {
    long kl = (45L * 120 + 5) * 4, kr = (45L * 120 + 112) * 4;
    int dl = abs(d->lay[0]->px[kl] - orig[kl]) + abs(d->lay[0]->px[kl + 2] - orig[kl + 2]);
    double edge_l = 0, edge_r = 0;
    i32 y;
    for (y = 2; y < 88; y++) {
      edge_l += abs(d->lay[0]->px[(y * 120L + 11) * 4 + 2] - d->lay[0]->px[(y * 120L + 12) * 4 + 2]);
      edge_r += abs(d->lay[0]->px[(y * 120L + 107) * 4 + 2] - d->lay[0]->px[(y * 120L + 108) * 4 + 2]);
    }
    CHECK(dl <= 2 && kr > 0, "the sharp pin leaves its pixel");
    CHECK(edge_l > 3 * edge_r, "edges kept at the left, softened at the right");
  }
  /* through a selection: only the selected half changes */
  doc_undo(d);
  sel_rect(d, 0, 0, 60, 90, SEL_REPLACE, 0);
  pins[0].x = 60; pins[0].y = 45; pins[0].blur = 8;
  CHECK(flt_field_blur(d, pins, 1) == 1, "inside a selection");
  CHECK(!memcmp(d->lay[0]->px + 80 * 4, orig + 80 * 4, 4) && memcmp(d->lay[0]->px + (45L * 120 + 12) * 4, orig + (45L * 120 + 12) * 4, 4), "outside the selection untouched");
  free(orig);
  doc_free(d);
}
static int aborter(long done, long total) { (void)total; return done > 0; }
static void test_hdr_oil(void)
{
  Doc *d = test_picture(100, 80);
  long n = 100L * 80 * 4;
  u8 *orig = (u8 *)malloc((size_t)n);
  HdrParams hp;
  OilParams op;
  memcpy(orig, d->lay[0]->px, (size_t)n);
  hdr_defaults(&hp);
  CHECK(flt_hdr_toning(d, &hp) == 1, "HDR Toning");
  CHECK(diff_mean(orig, d->lay[0]->px, n) > 0.5 && diff_mean(orig, d->lay[0]->px, n) < 40, "changed, not wrecked");
  doc_undo(d);
  /* neutral settings leave it nearly as it was */
  memset(&hp, 0, sizeof hp); hp.radius = 10; hp.strength = 5; hp.gamma = 100;
  CHECK(flt_hdr_toning(d, &hp) == 1, "neutral HDR Toning");
  CHECK(diff_mean(orig, d->lay[0]->px, n) < 1.5, "neutral: nearly the same");
  doc_undo(d);
  /* more detail: more local contrast */
  hp.detail = 200;
  flt_hdr_toning(d, &hp);
  {
    double c0 = 0, c1 = 0;
    i32 x;
    for (x = 1; x < 99; x++) { c0 += abs(orig[(40L * 100 + x) * 4 + 1] - orig[(40L * 100 + x - 1) * 4 + 1]); c1 += abs(d->lay[0]->px[(40L * 100 + x) * 4 + 1] - d->lay[0]->px[(40L * 100 + x - 1) * 4 + 1]); }
    CHECK(c1 > c0 * 1.1, "Detail 200%: the edges stronger");
  }
  doc_undo(d);
  /* exposure +1 stop: brighter */
  memset(&hp, 0, sizeof hp); hp.radius = 10; hp.strength = 5; hp.gamma = 100; hp.exposure = 100;
  flt_hdr_toning(d, &hp);
  CHECK(d->lay[0]->px[(10L * 100 + 50) * 4 + 1] > orig[(10L * 100 + 50) * 4 + 1], "exposure brightens");
  doc_undo(d);
  oil_defaults(&op);
  CHECK(flt_oil_paint_ex(d, &op) == 1, "oil paint");
  CHECK(diff_mean(orig, d->lay[0]->px, n) > 1, "oil paint changes the picture");
  {
    /* bands don't show: the same result in one band and in many (a picture taller than a band) */
    Doc *e = test_picture(40, 200), *f = test_picture(40, 200);
    OilParams q = op;
    q.lighting = 1; q.bristle = 50;
    flt_oil_paint_ex(e, &q);
    sel_rect(f, 0, 0, 40, 200, SEL_REPLACE, 0);               /* (the same area, through a selection) */
    flt_oil_paint_ex(f, &q);
    CHECK(!memcmp(e->lay[0]->px, f->lay[0]->px, 40L * 200 * 4), "the same with and without a full selection");
    doc_free(e); doc_free(f);
  }
  doc_undo(d);
  /* aborted: the step is unfinished, the picture as the job leaves it (jobs take the step back) */
  progress_hook = aborter;
  flt_oil_paint_ex(d, &op);
  CHECK(progress_aborted, "abort seen");
  progress_hook = NULL; progress_reset();
  doc_undo(d);
  CHECK(diff_mean(orig, d->lay[0]->px, n) == 0, "aborted step undone");
  free(orig);
  doc_free(d);
}
static long count_changed(const Doc *d, const u8 *orig)
{
  long k, c = 0;
  for (k = 0; k < (long)d->w * d->h; k++) if (memcmp(d->lay[0]->px + k * 4, orig + k * 4, 4)) c++;
  return c;
}
static void test_render(void)
{
  Doc *d = doc_new(200, 150, 0x000000, 0);
  long n = 200L * 150 * 4;
  u8 *orig = (u8 *)malloc((size_t)n), *first = (u8 *)malloc((size_t)n);
  FlameParams fp;
  TreeParams tp;
  FrameParams pf;
  Path *p = path_new();
  int k;
  memcpy(orig, d->lay[0]->px, (size_t)n);
  flame_defaults(&fp);
  CHECK(flt_flame(d, NULL, &fp) == 1, "flames along the default line");
  {
    /* bright and warm above the line, nothing far below it */
    u8 c[4];
    long warm = 0, below = 0;
    i32 x, y;
    for (y = 0; y < 150; y++) for (x = 0; x < 200; x++) {
      const u8 *q = d->lay[0]->px + ((long)y * 200 + x) * 4;
      if (q[0] > 120 && q[0] >= q[2]) warm++;
      if (y > 140 && q[0] > 30) below++;
    }
    comp_px(d, 100, 100, c);
    CHECK(warm > 400, "warm flame colours");
    CHECK(below == 0, "nothing below the line");
  }
  memcpy(first, d->lay[0]->px, (size_t)n);
  doc_undo(d);
  CHECK(flt_flame(d, NULL, &fp) == 1 && !memcmp(first, d->lay[0]->px, (size_t)n), "the same seed, the same flames");
  doc_undo(d);
  fp.seed = 2;
  flt_flame(d, NULL, &fp);
  CHECK(memcmp(first, d->lay[0]->px, (size_t)n) != 0, "another seed, other flames");
  doc_undo(d);
  /* along a path, the other types */
  path_move_to(p, 20, 75); path_line_to(p, 180, 75);
  for (k = 0; k < FLAME_N; k++) {
    fp.type = k;
    CHECK(flt_flame(d, p, &fp) == 1 && count_changed(d, orig) > 200, "a flame type along a path");
    doc_undo(d);
  }
  fp.custom_color = 1; fp.color = 0x2040FF; fp.type = FLAME_MANY_UP;
  flt_flame(d, p, &fp);
  {
    long blue = 0, k2;
    for (k2 = 0; k2 < 200L * 150; k2++) if (d->lay[0]->px[k2 * 4 + 2] > 100 && d->lay[0]->px[k2 * 4] < 100) blue++;
    CHECK(blue > 100, "a custom colour");
  }
  doc_undo(d);
  /* trees: every type draws, standing on the bottom edge */
  tree_defaults(&tp);
  for (k = 0; k < TREE_N; k++) {
    Doc *w = doc_new(160, 160, 0xFFFFFF, 0);
    u8 c[4];
    tp.type = k;
    CHECK(flt_tree(w, &tp) == 1, tree_names[k]);
    comp_px(w, 80, 158, c);
    CHECK(c[0] < 250 || c[1] < 250 || c[2] < 250, "the trunk at the foot");
    comp_px(w, 3, 3, c);
    CHECK(c[0] == 255 && c[1] == 255 && c[2] == 255, "the corner left alone");
    doc_free(w);
  }
  /* picture frames: drawn near the edge, the middle left alone */
  for (k = 0; k < PF_N; k++) {
    Doc *w = doc_new(200, 150, 0xFFFFFF, 0);
    u8 c[4];
    long edge = 0, k2;
    i32 x, y;
    frame_defaults(&pf, 200, 150);
    pf.kind = k;
    CHECK(flt_picture_frame(w, &pf) == 1, frame_names[k]);
    for (y = 0; y < 150; y++) for (x = 0; x < 200; x++) {
      const u8 *q = w->lay[0]->px + ((long)y * 200 + x) * 4;
      if ((x < 40 || x >= 160 || y < 40 || y >= 110) && (q[0] < 250 || q[1] < 250 || q[2] < 250)) edge++;
    }
    comp_px(w, 100, 75, c);
    CHECK(edge > 150, "ornaments near the edge");
    CHECK(c[0] == 255 && c[1] == 255 && c[2] == 255, "the middle untouched");
    k2 = 0; (void)k2;
    doc_free(w);
  }
  path_free(p);
  free(orig); free(first);
  doc_free(d);
}
static void test_lens(void)
{
  Doc *d = test_picture(120, 90);
  LensParams lp;
  long n = 120L * 90 * 4;
  u8 *orig = (u8 *)malloc((size_t)n);
  double sx, sy, s;
  FILE *f;
  char path[512];
  memcpy(orig, d->lay[0]->px, (size_t)n);
  lens_defaults(&lp);
  lens_map(&lp, 120, 90, 1, 30.5, 20.5, &sx, &sy);
  CHECK(fabs(sx - 30.5) < 1e-9 && fabs(sy - 20.5) < 1e-9, "no change: the identity");
  CHECK(fabs(lens_auto_scale(&lp, 120, 90) - 1) < 0.01, "auto scale of nothing: 100%");
  lp.vperspective = -40; lp.hperspective = 25; lp.angle = 3;
  CHECK(flt_lens_correction_ex(d, &lp) == 1, "perspective");
  save_png(d, "r3_lens_persp.png");
  /* the map's four corners for check_r3.py, which redoes it with Pillow's perspective transform */
  snprintf(path, sizeof path, "%s/r3_lens_persp.txt", tmpdir);
  if ((f = fopen(path, "w"))) {
    static const double pts[4][2] = { { 0, 0 }, { 120, 0 }, { 120, 90 }, { 0, 90 } };
    int k;
    for (k = 0; k < 4; k++) { lens_map(&lp, 120, 90, 1, pts[k][0], pts[k][1], &sx, &sy); fprintf(f, "%.9f %.9f %.9f %.9f\n", pts[k][0], pts[k][1], sx, sy); }
    fclose(f);
  }
  {
    /* the top widened: a source point near the top comes from nearer the middle */
    double ax, ay, bx, by;
    LensParams q;
    lens_defaults(&q); q.vperspective = -50;
    lens_map(&q, 120, 90, 1, 10, 5, &ax, &ay); lens_map(&q, 120, 90, 1, 10, 85, &bx, &by);
    CHECK(ax > 10 && bx < 10, "vertical perspective -50: the top spread, the bottom drawn in");
  }
  CHECK(d->lay[0]->px[3] == 0 || d->lay[0]->px[(89L * 120 + 119) * 4 + 3] == 0, "an edge uncovered without Auto Scale");
  doc_undo(d);
  lp.auto_scale = 1;
  s = lens_auto_scale(&lp, 120, 90);
  CHECK(s > 1, "auto scale zooms in");
  flt_lens_correction_ex(d, &lp);
  {
    long k, clear = 0;
    for (k = 0; k < 120L * 90; k++) if (d->lay[0]->px[k * 4 + 3] < 255) clear++;
    CHECK(clear == 0, "Auto Scale: no edge shows");
  }
  doc_undo(d);
  /* distortion alone matches 1.0's Lens Correction */
  {
    Doc *e = test_picture(120, 90);
    lens_defaults(&lp); lp.distortion = 40; lp.vig_amount = -30;
    flt_lens_correction_ex(d, &lp);
    flt_lens_correction(e, 40, 0, 0, -30, 50, 100);
    CHECK(diff_mean(d->lay[0]->px, e->lay[0]->px, n) < 0.5, "the same distortion and vignette as before");
    doc_free(e);
  }
  free(orig);
  doc_free(d);
}
static void test_liquify(void)
{
  Doc *d = test_picture(100, 100);
  LiquifyMesh *m = liquify_new(100, 100);
  long n = 100L * 100 * 4;
  u8 *orig = (u8 *)malloc((size_t)n), *prev = (u8 *)malloc((size_t)n);
  double sx, sy;
  int t;
  memcpy(orig, d->lay[0]->px, (size_t)n);
  CHECK(m != NULL, "mesh");
  liquify_at(m, 33.3, 44.4, &sx, &sy);
  CHECK(fabs(sx - 33.3) < 1e-9 && fabs(sy - 44.4) < 1e-9, "an empty mesh: the identity");
  /* forward warp: what was at 40,50 is pushed to 50,50 */
  liquify_stroke(m, LQ_WARP, 40, 50, 50, 50, 40, 50, 100, 50);
  liquify_at(m, 50, 50, &sx, &sy);
  CHECK(sx < 45 && fabs(sy - 50) < 1, "forward warp: the stroke's end shows what was behind it");
  liquify_at(m, 5, 5, &sx, &sy);
  CHECK(fabs(sx - 5) < 1e-6 && fabs(sy - 5) < 1e-6, "outside the brush: unchanged");
  /* reconstruct takes it back */
  for (t = 0; t < 30; t++) liquify_stroke(m, LQ_RECONSTRUCT, 45, 50, 45, 50, 60, 50, 100, 100);
  liquify_at(m, 50, 50, &sx, &sy);
  CHECK(fabs(sx - 50) < 0.5, "reconstruct restores it");
  /* pucker pulls in: the middle shows a wider area; bloat the opposite; twirls turn */
  liquify_reset(m);
  liquify_stroke(m, LQ_PUCKER, 50, 50, 50, 50, 60, 50, 100, 100);
  liquify_at(m, 60, 50, &sx, &sy);
  CHECK(sx > 60.5, "pucker: a point takes from further out");
  liquify_reset(m);
  liquify_stroke(m, LQ_BLOAT, 50, 50, 50, 50, 60, 50, 100, 100);
  liquify_at(m, 60, 50, &sx, &sy);
  CHECK(sx < 59.5, "bloat: from nearer in");
  liquify_reset(m);
  liquify_stroke(m, LQ_TWIRL_CW, 50, 50, 50, 50, 60, 50, 100, 100);
  liquify_at(m, 60, 50, &sx, &sy);
  CHECK(fabs(sy - 50) > 0.5, "twirl turns");
  liquify_reset(m);
  liquify_stroke(m, LQ_PUSH_LEFT, 50, 70, 50, 30, 30, 50, 100, 50);
  liquify_at(m, 50, 40, &sx, &sy);
  CHECK(fabs(sx - 50) > 1, "push left moves across the stroke");
  /* absurd strokes don't overflow */
  liquify_stroke(m, LQ_WARP, -1e9, 0, 1e9, 0, 1e9, 50, 100, 50);
  liquify_stroke(m, LQ_WARP, 0, 0, 0.0 / 0.0, 0, 10, 50, 100, 50);
  /* applied: the picture changes in the brushed area only */
  liquify_reset(m);
  liquify_stroke(m, LQ_WARP, 30, 15, 60, 15, 30, 50, 100, 50);
  CHECK(flt_liquify(d, m) == 1, "liquify applied");
  CHECK(!memcmp(d->lay[0]->px + (90L * 100 + 90) * 4, orig + (90L * 100 + 90) * 4, 4) && memcmp(d->lay[0]->px + (15L * 100 + 55) * 4, orig + (15L * 100 + 55) * 4, 4), "far away the same, the stroke moved");
  liquify_apply_rgba(m, orig, prev, 100, 100);
  CHECK(!memcmp(prev, d->lay[0]->px, (size_t)n), "the preview is what applying gives");
  doc_undo(d);
  CHECK(!memcmp(d->lay[0]->px, orig, (size_t)n), "undo");
  liquify_free(m); free(orig); free(prev);
  doc_free(d);
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
  test_field_blur();
  test_hdr_oil();
  test_render();
  test_lens();
  test_liquify();
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
