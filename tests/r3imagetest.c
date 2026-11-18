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
#include "core/selr3.h"
#include "core/type.h"
#include "core/fonts.h"
#include "core/hyphen.h"
#include "core/adjust.h"
#include "core/adjust10.h"
#include "core/colr3.h"
#include "core/gsub.h"
#include "core/ttraster.h"

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

/* ------------------------------------------------------------------ selections */
static u32 rng_state = 12345;
static int rnd8(int amp) { rng_state = rng_state * 1103515245u + 12345u; return (int)((rng_state >> 16) % (2 * amp + 1)) - amp; }
static void put(u8 *p, int r, int g, int b) { p[0] = (u8)(r < 0 ? 0 : r > 255 ? 255 : r); p[1] = (u8)(g < 0 ? 0 : g > 255 ? 255 : g); p[2] = (u8)(b < 0 ? 0 : b > 255 ? 255 : b); p[3] = 255; }
static void test_sky(void)
{
  i32 w = 200, h = 150, x, y;
  Doc *d = doc_new(w, h, 0xFFFFFF, 0);
  long sky_in = 0, sky_n = 0, ground_in = 0, ground_n = 0, tower_in = 0;
  /* a blue sky (lighter at the horizon) over textured ground, a dark tower into the sky */
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      u8 *p = d->lay[0]->px + ((long)y * w + x) * 4;
      if (x >= 130 && x < 150 && y >= 20) put(p, 60 + rnd8(25), 55 + rnd8(25), 50 + rnd8(25));
      else if (y < 70) put(p, 70 + y, 130 + y / 2, 235 - y / 4);
      else put(p, 90 + rnd8(40), 120 + rnd8(40), 50 + rnd8(30));
    }
  CHECK(sel_sky(d, SEL_REPLACE, 50, 50), "Select Sky found a sky");
  if (d->sel)
    for (y = 0; y < h; y++)
      for (x = 0; x < w; x++) {
        int s = d->sel[(long)y * w + x] >= 128, tower = x >= 130 && x < 150 && y >= 20;
        if (tower) { tower_in += s; continue; }
        if (y < 66) { sky_n++; sky_in += s; }
        else if (y > 74) { ground_n++; ground_in += s; }
      }
  CHECK(sky_in > sky_n * 9 / 10, "most of the sky selected");
  CHECK(ground_in < ground_n / 50, "the ground not");
  CHECK(tower_in < 60, "nor the tower standing in it");
  doc_undo(d);
  CHECK(!d->sel || !d->sel[0], "undo");
  /* no sky: nothing found */
  for (y = 0; y < h; y++) for (x = 0; x < w; x++) put(d->lay[0]->px + ((long)y * w + x) * 4, 90 + rnd8(40), 120 + rnd8(40), 50 + rnd8(30));
  CHECK(!sel_sky(d, SEL_REPLACE, 50, 50), "a field without sky: none");
  doc_free(d);
}
static void test_subject(void)
{
  i32 w = 160, h = 120, x, y;
  Doc *d = doc_new(w, h, 0xFFFFFF, 0);
  Box b;
  long in = 0, in_n = 0, out = 0, out_n = 0;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      u8 *p = d->lay[0]->px + ((long)y * w + x) * 4;
      int dx = x - 90, dy = y - 55;
      if (dx * dx * 4 + dy * dy * 3 < 30 * 30 * 3) put(p, 200 + rnd8(15), 40 + rnd8(15), 40 + rnd8(15));
      else put(p, 190 + rnd8(12), 185 + rnd8(12), 170 + rnd8(12));
    }
  CHECK(subject_box(d, &b), "something stands out");
  CHECK(b.x0 <= 70 && b.x1 >= 110 && b.y0 <= 30 && b.y1 >= 80 && b.x0 > 40 && b.x1 < 140, "the box round the subject");
  CHECK(sel_subject(d, SEL_REPLACE), "Select Subject");
  if (d->sel)
    for (y = 0; y < h; y++)
      for (x = 0; x < w; x++) {
        int dx = x - 90, dy = y - 55, e = dx * dx * 4 + dy * dy * 3, s = d->sel[(long)y * w + x] >= 128;
        if (e < 25 * 25 * 3) { in_n++; in += s; }
        else if (e > 36 * 36 * 3) { out_n++; out += s; }
      }
  CHECK(in > in_n * 9 / 10 && out < out_n / 30, "the subject selected, the background not");
  /* a flat picture: nothing stands out */
  for (y = 0; y < h; y++) for (x = 0; x < w; x++) put(d->lay[0]->px + ((long)y * w + x) * 4, 128, 128, 128);
  CHECK(!subject_box(d, &b), "flat: no subject");
  doc_free(d);
}
static void test_decontaminate(void)
{
  i32 w = 60, h = 40, x, y;
  Doc *d = doc_new(w, h, 0xFFFFFF, 0);
  Layer *l = d->lay[0];
  u8 before[4], after[4], inside[4];
  /* a red square photographed on blue: its edge pixels are purple */
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      u8 *p = l->px + ((long)y * w + x) * 4;
      int t = x < 28 ? 255 : x >= 32 ? 0 : (32 - x) * 255 / 4;
      put(p, t, 0, 255 - t);
    }
  CHECK(doc_mask_add(d, 0, MK_REVEAL_ALL), "a mask");
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      int t = x < 28 ? 255 : x >= 32 ? 0 : (32 - x) * 255 / 4;
      l->mask[(long)y * w + x] = (u8)t;
    }
  layer_mask_changed(l);
  memcpy(before, l->px + (20L * w + 30) * 4, 4);
  memcpy(inside, l->px + (20L * w + 10) * 4, 4);
  CHECK(doc_decontaminate(d, 0, 100), "decontaminate");
  memcpy(after, l->px + (20L * w + 30) * 4, 4);
  CHECK(after[2] < before[2] / 2 && after[0] > before[0], "the edge's blue taken out, red put in");
  CHECK(!memcmp(inside, l->px + (20L * w + 10) * 4, 4), "fully shown pixels untouched");
  doc_undo(d);
  CHECK(!memcmp(before, l->px + (20L * w + 30) * 4, 4), "undo");
  doc_free(d);
}

/* ------------------------------------------------------------------ type */
static const char *find_font(const char *name)
{
  static char path[512];
  static const char *const dirs[] = { "/usr/share/fonts/truetype/crosextra", "/usr/share/fonts/truetype/carlito", "../quill/fonts", "/home/user/quill/fonts", NULL };
  int k;
  for (k = 0; dirs[k]; k++) {
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dirs[k], name);
    if ((f = fopen(path, "rb"))) { fclose(f); return path; }
  }
  return NULL;
}
static TextLayout *lay_t(TextParams *tp, const char *text, u16 ot, double size)
{
  text_params_init(tp, "Carlito", size, 0, 10, 100);
  text_set_string(tp, text);
  tp->run[0].ot = ot;
  return text_layout(tp);
}
static void gsub_note(FILE *f, const char *path, const char *tag, const char *text, const TextLayout *tl)
{
  i32 i;
  if (!f || !tl) return;
  fprintf(f, "%s\t%s\t%s\t", path, tag, text);
  for (i = 0; i < tl->ng; i++) fprintf(f, " %d", tl->g[i].dead ? -1 : (int)tl->g[i].glyph);
  fprintf(f, "\n");
}
static long ink(const u8 *px, Box b)
{
  long k, s = 0;
  for (k = 0; k < (long)(b.x1 - b.x0) * (b.y1 - b.y0); k++) s += px[k * 4 + 3];
  return s;
}
static void test_type(void)
{
  const char *carlito = find_font("Carlito-Regular.ttf");
  static TextParams tp;
  FontSet *fs;
  TextLayout *a, *b;
  Face *c;
  FILE *gf = NULL;
  char path[512];
  /* hyphenation: Liang's own example patterns */
  static const char pats[] = "UTF-8\nLEFTHYPHENMIN 2\nRIGHTHYPHENMIN 2\n.hy3ph\nhe2n\nhena4\nhen5at\n1na\nn2at\n1tio\n2io\no2n\n";
  u8 brk[16];
  CHECK(!hyphen_ready(), "no patterns at first");
  CHECK(hyphen_load_mem(pats, sizeof pats - 1) && hyphen_ready(), "patterns loaded");
  CHECK(hyphen_word("hyphenation", 11, brk) == 2 && brk[2] && brk[6], "hy-phen-ation, as Liang's paper has it");
  CHECK(hyphen_word("Hyphenation", 11, brk) >= 2 && brk[2], "capitals too");
  CHECK(hyphen_word("hy-ph", 5, brk) == 0, "not across punctuation");
  if (!carlito) { printf("Carlito isn't installed: the type tests that need a font are skipped\n"); hyphen_free(); return; }
  fs = (FontSet *)calloc(1, sizeof *fs);
  fonts_add(fs, carlito, "Carlito", 0, 0);
  text_fonts = fs;
  c = fonts_face(fs, "Carlito", 0, 0);
  CHECK(c && ttf_load(c), "Carlito loads");
  /* horizontal and vertical scale */
  a = lay_t(&tp, "Wide", 0, 40);
  tp.run[0].hscale = 150;
  b = text_layout(&tp);
  CHECK(a && b && fabs((b->g[3].x + b->g[3].adv - b->g[0].x) - 1.5 * (a->g[3].x + a->g[3].adv - a->g[0].x)) < 0.5, "150% horizontal scale: half as wide again");
  text_layout_free(a); text_layout_free(b);
  {
    u8 *p0 = NULL, *p1 = NULL;
    Box b0, b1;
    text_layout_free(lay_t(&tp, "H", 0, 60));
    text_render(&tp, 300, 300, &p0, &b0);
    tp.run[0].vscale = 200;
    text_render(&tp, 300, 300, &p1, &b1);
    CHECK(p0 && p1 && fabs((b1.y1 - b1.y0) - 2.0 * (b0.y1 - b0.y0)) < 4 && abs((b1.x1 - b1.x0) - (b0.x1 - b0.x0)) <= 2, "200% vertical scale: twice as tall, as wide");
    free(p0); free(p1);
    {
      /* kept in OpenRaster's text */
      Layer l;
      char *sv;
      memset(&l, 0, sizeof l);
      l.kind = LK_TEXT; l.live = &tp; l.livelen = sizeof tp;
      sv = live_save(&l);
      l.live = NULL;
      CHECK(sv && strstr(sv, "rhv=0,0,200"), "scales saved");
      CHECK(sv && live_load(&l, LK_TEXT, sv) && layer_text(&l) && layer_text(&l)->run[0].vscale == 200, "and read");
      free(sv); free(l.live);
    }
    tp.run[0].vscale = 0;
    /* Crisp and Strong: Strong has more ink, Crisp the same shape with fewer half-covered pixels */
    {
      u8 *ps = NULL, *pc = NULL, *pg = NULL;
      Box bs, bc, bg;
      long half_s = 0, half_c = 0, k;
      text_set_string(&tp, "a e");
      tp.aa = TAA_SMOOTH; text_render(&tp, 300, 300, &ps, &bs);
      tp.aa = TAA_CRISP; text_render(&tp, 300, 300, &pc, &bc);
      tp.aa = TAA_STRONG; text_render(&tp, 300, 300, &pg, &bg);
      CHECK(ps && pc && pg && ink(pg, bg) > ink(ps, bs) * 105 / 100, "Strong is heavier");
      if (ps && pc && bs.x0 == bc.x0 && bs.y0 == bc.y0 && bs.x1 == bc.x1 && bs.y1 == bc.y1)
        for (k = 0; k < (long)(bs.x1 - bs.x0) * (bs.y1 - bs.y0); k++) { half_s += ps[k * 4 + 3] > 40 && ps[k * 4 + 3] < 215; half_c += pc[k * 4 + 3] > 40 && pc[k * 4 + 3] < 215; }
      CHECK(half_c < half_s, "Crisp has fewer grey edge pixels");
      free(ps); free(pc); free(pg);
      tp.aa = TAA_SMOOTH;
    }
  }
  /* hyphenation in paragraph text: a narrow box breaks the long word with a hyphen */
  text_params_init(&tp, "Carlito", 30, 0, 10, 10);
  text_set_string(&tp, "hyphenation hyphenation");
  tp.box = 1; tp.bw = 140; tp.bh = 400; tp.hyphenate = 1;
  a = text_layout(&tp);
  {
    int hy = 0, ok = 1;
    i32 i;
    if (a) for (i = 0; i < a->ng; i++) hy += a->g[i].hyph;
    if (a) for (i = 0; i < a->nl; i++) if (a->ln[i].x1 - a->ln[i].x0 > tp.bw + 0.5) ok = 0;
    CHECK(hy >= 1, "a word broken with a hyphen");
    CHECK(ok, "every line fits the box (with its hyphen)");
    tp.hyphenate = 0;
    b = text_layout(&tp);
    if (b) for (i = 0, hy = 0; i < b->ng; i++) hy += b->g[i].hyph;
    CHECK(b && hy == 0, "hyphenation off: none");
    text_layout_free(b);
  }
  text_layout_free(a);
  hyphen_free();
  /* contextual alternates and fractions (GSUB lookups 6), for check_r3.py to redo with fontTools */
  snprintf(path, sizeof path, "%s/r3_gsub.txt", tmpdir);
  gf = fopen(path, "w");
  a = lay_t(&tp, "1/2", TO_FRAC, 40);
  CHECK(a && a->g[0].glyph != ttf_glyph(c, '1') && a->g[1].glyph != ttf_glyph(c, '/') && a->g[2].glyph != ttf_glyph(c, '2'), "1/2: numerator, fraction slash, denominator");
  gsub_note(gf, carlito, "frac", "1/2", a); text_layout_free(a);
  a = lay_t(&tp, "12/345 6", TO_FRAC, 40);
  CHECK(a && a->g[7].glyph == ttf_glyph(c, '6'), "a figure away from the fraction stays as it is");
  gsub_note(gf, carlito, "frac", "12/345 6", a); text_layout_free(a);
  a = lay_t(&tp, "\xce\x86\xce\x93", TO_CALT, 40);   /* Greek capital alpha with tonos, then gamma */
  b = lay_t(&tp, "\xce\x86\xce\x93", 0, 40);
  CHECK(a && b && a->g[0].glyph != b->g[0].glyph, "contextual alternate before a gamma");
  gsub_note(gf, carlito, "calt", "\xce\x86\xce\x93", a); text_layout_free(a); text_layout_free(b);
  a = lay_t(&tp, "\xce\x86\xce\x91", TO_CALT, 40);
  b = lay_t(&tp, "\xce\x86\xce\x91", 0, 40);
  gsub_note(gf, carlito, "calt", "\xce\x86\xce\x91", a); text_layout_free(a); text_layout_free(b);
  a = lay_t(&tp, "\xce\x93\xce\x86", TO_CALT, 40);
  gsub_note(gf, carlito, "calt", "\xce\x93\xce\x86", a); text_layout_free(a);
  a = lay_t(&tp, "\xce\x86 \xce\x93", TO_CALT, 40);
  gsub_note(gf, carlito, "calt", "\xce\x86 \xce\x93", a); text_layout_free(a);
  if (gf) fclose(gf);
  text_fonts = NULL;
  text_cache_flush();
  fonts_free(fs); free(fs);
}

/* ------------------------------------------------------------------ adjustments */
static void test_replace_color(void)
{
  Doc *d = doc_new(30, 10, 0xFFFFFF, 0);
  u8 *px = d->lay[0]->px;
  u32 add[1] = { 0xFF8000 }, sub[1] = { 0xFF0400 };
  i32 x, y;
  /* red, orange and blue stripes */
  for (y = 0; y < 10; y++)
    for (x = 0; x < 30; x++) {
      u8 *p = px + ((long)y * 30 + x) * 4;
      if (x < 10) put(p, 255, 0, 0); else if (x < 20) put(p, 255, 128, 0); else put(p, 0, 0, 255);
    }
  CHECK(adj_replace_color(d, 0xFF0000, 40, 120, 0, 0), "replace red");
  CHECK(px[1] > 100 && px[15 * 4] == 255 && px[15 * 4 + 1] == 128, "red turned, orange not");
  doc_undo(d);
  CHECK(adj_replace_color_ex(d, 0xFF0000, 40, 120, 0, 0, add, 1, NULL, 0), "add orange to the sample");
  CHECK(px[1] > 100 && !(px[15 * 4] == 255 && px[15 * 4 + 1] == 128) && px[25 * 4 + 2] == 255 && px[25 * 4] == 0, "red and orange turned, blue not");
  doc_undo(d);
  CHECK(adj_replace_color_ex(d, 0xFF0000, 60, 120, 0, 0, NULL, 0, sub, 1), "subtract a red-orange");
  CHECK(px[1] < 60, "red near the subtracted colour: barely changed");
  doc_undo(d);
  {
    AdjParams p;
    adj_defaults(AK_REPLACECOLOR, &p);
    CHECK(replace_color_sample(&p, 0x00FF00, 0) && replace_color_sample(&p, 0x0000FF, 1) && p.npts == 2 && p.pts[3] == -1, "samples held");
  }
  doc_free(d);
}
static void test_acv(void)
{
  AdjParams p, q;
  /* a Photoshop .acv as its documentation lays it out: version 4, two curves */
  static const u8 acv[] = { 0, 4, 0, 2,
    0, 3, 0, 0, 0, 0, 0, 160, 0, 128, 0, 255, 0, 255,      /* composite: (0,0) (128,160) (255,255) as output, input */
    0, 2, 0, 20, 0, 0, 0, 235, 0, 255 };                   /* red: (0,20) (255,235) */
  size_t n;
  u8 *w, lut[768];
  CHECK(curves_acv_read(acv, sizeof acv, &p), "an .acv read");
  CHECK(p.kind == AK_CURVES && p.v[1] == 1 && p.v[2] == 3 && p.pts[2] == 128 && p.pts[3] == 160 && p.v[3] == 2 && p.pts[17] == 20, "its curves");
  CHECK(p.v[4] == 2 && p.v[5] == 2, "green and blue straight");
  adj10_curves_lut(&p, lut);
  CHECK(lut[128 + 256] > 150 && lut[0] >= 20 && lut[128 + 512] > 150, "the composite lifts the middle, red the shadows");
  CHECK((w = curves_acv_write(&p, &n)) != NULL, "written");
  CHECK(w && curves_acv_read(w, n, &q) && !memcmp(q.v, p.v, 6 * sizeof(int)) && !memcmp(q.pts, p.pts, 64 * sizeof(int)), "read back the same");
  free(w);
  CHECK(!curves_acv_read(acv, 6, &q), "cut short: refused");
  {
    static const u8 bad[] = { 0, 4, 0, 1, 0, 2, 0, 0, 0, 200, 0, 255, 0, 100 };   /* (inputs falling) */
    CHECK(!curves_acv_read(bad, sizeof bad, &q), "inputs out of order: refused");
  }
}

static void test_picker_colours(void)
{
  int c, m, y, k;
  rgb_to_cmyk100(0xFF0000, &c, &m, &y, &k);
  CHECK(c == 0 && m == 100 && y == 100 && k == 0, "red: M100 Y100");
  rgb_to_cmyk100(0x000000, &c, &m, &y, &k);
  CHECK(c == 0 && m == 0 && y == 0 && k == 100, "black: K100");
  rgb_to_cmyk100(0x808080, &c, &m, &y, &k);
  CHECK(c == 0 && m == 0 && y == 0 && k == 50, "grey: K50");
  CHECK(cmyk100_to_rgb(0, 100, 100, 0) == 0xFF0000 && cmyk100_to_rgb(0, 0, 0, 100) == 0 && cmyk100_to_rgb(100, 0, 0, 0) == 0x00FFFF, "CMYK to RGB");
  {
    u32 col;
    int bad = 0;
    for (col = 0; col < 0x1000000; col += 0x10307) {
      rgb_to_cmyk100(col, &c, &m, &y, &k);
      { u32 back = cmyk100_to_rgb(c, m, y, k); int s; for (s = 0; s < 24; s += 8) if (abs((int)((back >> s) & 255) - (int)((col >> s) & 255)) > 4) bad++; }
    }
    CHECK(!bad, "CMYK there and back within rounding");
  }
  CHECK(web_safe(0x123456) == 0x003366 && web_safe(0xFFFFFF) == 0xFFFFFF && web_safe(0x1A1A1A) == 0x333333, "nearest web-safe colours");
  CHECK(is_web_safe(0x33CC99) && !is_web_safe(0x34CC99), "web-safe or not");
}

static void test_path_symmetry(void)
{
  BrushDynamics dy;
  Poly pl;
  double ox[SYM_MAXCOPIES], oy[SYM_MAXCOPIES], rot[SYM_MAXCOPIES];
  int mir[SYM_MAXCOPIES], n;
  memset(&dy, 0, sizeof dy);
  dy.sym_mode = SYM_PATH;
  brush_sym_path = NULL;
  CHECK(brush_symmetry(&dy, 100, 100, 30, 40, ox, oy, rot, mir) == 1, "no path: no copies");
  /* a vertical line at x = 50: the same as Vertical symmetry */
  poly_init(&pl);
  poly_add(&pl, 50, 0); poly_add(&pl, 50, 100); poly_end(&pl, 0);
  brush_sym_path = &pl;
  n = brush_symmetry(&dy, 100, 100, 30, 40, ox, oy, rot, mir);
  CHECK(n == 2 && fabs(ox[1] - 70) < 1e-9 && fabs(oy[1] - 40) < 1e-9 && mir[1], "mirrored across a straight path");
  poly_free(&pl);
  /* a bent path (an L): each side mirrored across its nearest leg */
  poly_init(&pl);
  poly_add(&pl, 10, 10); poly_add(&pl, 10, 90); poly_add(&pl, 90, 90); poly_end(&pl, 0);
  n = brush_symmetry(&dy, 100, 100, 20, 30, ox, oy, rot, mir);
  CHECK(n == 2 && fabs(ox[1] - 0) < 1e-9 && fabs(oy[1] - 30) < 1e-9, "near the upright leg: across it");
  n = brush_symmetry(&dy, 100, 100, 60, 80, ox, oy, rot, mir);
  CHECK(n == 2 && fabs(ox[1] - 60) < 1e-9 && fabs(oy[1] - 100) < 1e-9, "near the bottom leg: across that");
  poly_free(&pl);
  brush_sym_path = NULL;
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
  test_sky();
  test_subject();
  test_decontaminate();
  test_type();
  test_replace_color();
  test_acv();
  test_picker_colours();
  test_path_symmetry();
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
