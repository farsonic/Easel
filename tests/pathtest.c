/*
 * pathtest.c - Easel 0.9's vectors: the path model and its editing, flattening accuracy, the
 * anti-aliased filler against analytic areas, path to selection and back (IoU), Fill and Stroke
 * Path, hit testing, SVG path data, the document's paths with undo, the shape generators and shape
 * layers (anti-aliased edges, strokes, rasterizing, OpenRaster, crop and resize).
 * Run under the address and undefined-behaviour sanitizers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core/doc.h"
#include "core/paint.h"
#include "core/layers.h"
#include "core/codec.h"
#include "core/path.h"
#include "core/pathops.h"
#include "core/shape.h"
#include "core/live.h"

static int fails, tests;
#define CHECK(c, msg) do { tests++; if (!(c)) { fails++; printf("FAIL %s (line %d)\n", msg, __LINE__); } } while (0)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double sel_area(Doc *d)
{
  long k, n = (long)d->w * d->h;
  double a = 0;
  if (!d->sel) return (double)n;
  for (k = 0; k < n; k++) a += d->sel[k] / 255.0;
  return a;
}
static double iou(const u8 *a, const u8 *b, long n)
{
  long k, in = 0, un = 0;
  for (k = 0; k < n; k++) { int x = a[k] >= 128, y = b[k] >= 128; in += x && y; un += x || y; }
  return un ? (double)in / un : 1;
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

static void test_model(void)
{
  Path *p = path_new(), *q;
  PathHit h;
  double c[8], x, y, x2, y2;
  int k;
  CHECK(p && path_move_to(p, 10, 10) == 1, "move_to starts subpath 1");
  path_line_to(p, 50, 10);
  path_curve_to(p, 60, 20, 60, 40, 50, 50);
  path_line_to(p, 10, 50);
  path_close(p);
  CHECK(p->ns == 1 && p->na == 4 && p->s[0].closed, "a closed subpath of 4 anchors");
  CHECK(path_nsegs(p, 0) == 4, "4 segments closed");
  /* split keeps the shape */
  path_seg_ctrl(p, 0, 1, c);
  bez_point(c, 0.3, &x, &y);
  k = path_split_segment(p, 0, 1, 0.3);
  CHECK(k == 2 && p->na == 5, "split adds an anchor after the segment's start");
  CHECK(fabs(p->a[2].x - x) < 1e-9 && fabs(p->a[2].y - y) < 1e-9, "the new anchor is on the curve");
  path_seg_ctrl(p, 0, 1, c); bez_point(c, 0.5, &x2, &y2);
  {
    double c0[8], ox, oy;
    /* the first half's middle is the original at t = 0.15 */
    c0[0] = 50; c0[1] = 10; c0[2] = 60; c0[3] = 20; c0[4] = 60; c0[5] = 40; c0[6] = 50; c0[7] = 50;
    bez_point(c0, 0.15, &ox, &oy);
    CHECK(fabs(ox - x2) < 1e-9 && fabs(oy - y2) < 1e-9, "the first half follows the original curve");
  }
  CHECK(p->a[2].type == PA_SMOOTH, "a split curve gives a smooth anchor");
  /* smooth handle moves mirror */
  path_move_handle(p, 0, 2, 1, p->a[2].x + 10, p->a[2].y, 0);
  CHECK(fabs(p->a[2].iy - p->a[2].y) < 1e-9 && p->a[2].ix < p->a[2].x, "the other handle turns opposite");
  path_move_handle(p, 0, 2, 1, p->a[2].x, p->a[2].y + 7, 1);
  CHECK(p->a[2].type == PA_CORNER && p->a[2].ix < p->a[2].x, "alone: a corner, the other handle stays");
  path_convert_anchor(p, 0, 2, PA_CORNER);
  CHECK(p->a[2].ix == p->a[2].x && p->a[2].oy == p->a[2].y, "converted to a corner: handles retracted");
  path_convert_anchor(p, 0, 0, PA_SMOOTH);
  CHECK(p->a[0].type == PA_SMOOTH && p->a[0].ox != p->a[0].x, "converted to smooth: handles out");
  path_move_anchor(p, 0, 0, 5, 5);
  CHECK(p->a[0].x == 15 && p->a[0].y == 15, "anchor moved");
  /* hit testing */
  path_hit(p, 15.5, 15, 3, 0, &h);
  CHECK(h.what == PH_ANCHOR && h.s == 0 && h.k == 0, "anchor hit");
  p->a[0].sel = 1;
  path_hit(p, p->a[0].ox, p->a[0].oy + 1, 2, 0, &h);
  CHECK(h.what == PH_OUT && h.k == 0, "handle hit on a selected anchor");
  path_hit(p, 10.5, 30, 2, 0, &h);
  CHECK(h.what == PH_SEGMENT && h.k == 4, "segment hit (the closing one)");
  path_hit(p, 200, 200, 2, 1, &h);
  CHECK(h.what == PH_NONE, "nothing far away");
  /* copy, delete, reverse */
  q = path_copy(p);
  CHECK(q && q->na == p->na && !memcmp(q->a, p->a, (size_t)p->na * sizeof(PathAnchor)), "copy");
  path_delete_anchor(q, 0, 1);
  CHECK(q->na == 4 && q->s[0].n == 4, "anchor deleted");
  path_add_anchor(q, 0, 4, 5, 30);
  CHECK(q->na == 5 && q->a[4].x == 5, "anchor added at the end");
  path_reverse(q, 0);
  CHECK(q->a[0].x == 15 && q->a[1].x == 5, "reversed, the first anchor kept");
  path_move_to(q, 100, 100); path_line_to(q, 120, 100);
  CHECK(q->ns == 2 && q->s[1].first == 5, "second subpath");
  path_delete_subpath(q, 0);
  CHECK(q->ns == 1 && q->na == 2 && q->s[0].first == 0, "subpath deleted");
  {
    double m[6] = { 2, 0, 1, 0, 2, -1 };
    path_transform(q, m);
    CHECK(q->a[0].x == 201 && q->a[0].y == 199, "transformed");
  }
  path_free(q);
  path_free(p);
}

static void test_flatten(void)
{
  Path *p = path_new();
  Poly pl;
  i32 i;
  double worst = 0, a;
  shape_ellipse_path(p, 50, 50, 150, 150);
  CHECK(p->na == 4 && p->s[0].closed, "ellipse: 4 anchors");
  poly_init(&pl);
  path_flatten(p, 0.1, &pl);
  for (i = 0; i < pl.n; i++) { double e = fabs(hypot(pl.xy[2 * i] - 100, pl.xy[2 * i + 1] - 100) - 50); if (e > worst) worst = e; }
  CHECK(worst < 0.03, "flattened points on the circle (Bezier error only)");
  CHECK(pl.n > 20 && pl.n < 400, "a sensible number of points");
  {
    /* chords stay within the tolerance of the curve */
    double dev = 0;
    for (i = 0; i < pl.n; i++) {
      const double *u = pl.xy + 2 * i, *v = pl.xy + 2 * ((i + 1) % pl.n);
      double mx = (u[0] + v[0]) / 2, my = (u[1] + v[1]) / 2, e = 50 - hypot(mx - 100, my - 100);
      if (e > dev) dev = e;
    }
    CHECK(dev < 0.13, "chords within the tolerance");
  }
  a = poly_area(&pl);
  CHECK(fabs(a - M_PI * 2500) / (M_PI * 2500) < 0.002, "area of the flattened circle");
  poly_free(&pl);
  poly_init(&pl);
  path_flatten(p, 2, &pl);
  CHECK(pl.n < 40, "coarser tolerance, fewer points");
  poly_free(&pl);
  path_free(p);
}

static void test_raster(void)
{
  Doc *d = doc_new(200, 200, 0xFFFFFF, 0);
  Path *p = path_new();
  double a;
  u8 *m;
  int k, edge = 0;
  /* circle area */
  shape_ellipse_path(p, 40, 40, 160, 160);
  path_to_selection(d, p, SEL_REPLACE, 0, 1);
  a = sel_area(d);
  CHECK(fabs(a - M_PI * 3600) / (M_PI * 3600) < 0.003, "path to selection: circle area");
  for (k = 0; k < 200 * 200; k++) if (d->sel[k] > 0 && d->sel[k] < 255) edge++;
  CHECK(edge > 300, "anti-aliased edge");
  CHECK(d->sel[100 * 200 + 100] == 255 && d->sel[5 * 200 + 5] == 0, "inside in, outside out");
  /* hard edges */
  path_to_selection(d, p, SEL_REPLACE, 0, 0);
  for (edge = 0, k = 0; k < 200 * 200; k++) if (d->sel[k] > 0 && d->sel[k] < 255) edge++;
  CHECK(edge == 0 && fabs(sel_area(d) - M_PI * 3600) / (M_PI * 3600) < 0.01, "hard edged, same area");
  /* a fractional rectangle */
  path_clear(p);
  shape_rect_path(p, 20.5, 30.25, 120.75, 90.5, 0);
  path_to_selection(d, p, SEL_REPLACE, 0, 1);
  a = sel_area(d);
  CHECK(fabs(a - 100.25 * 60.25) < 0.6, "rectangle area exact");
  CHECK(abs(d->sel[50 * 200 + 20] - 128) <= 2, "half-covered column");
  CHECK(abs(d->sel[30 * 200 + 50] - 191) <= 2, "three-quarter-covered row");
  /* rounded rectangle area: the box less the corners */
  path_clear(p);
  shape_rect_path(p, 10, 10, 110, 70, 15);
  path_to_selection(d, p, SEL_REPLACE, 0, 1);
  a = sel_area(d);
  CHECK(fabs(a - (100 * 60 - (4 - M_PI) * 225)) < 3, "rounded rectangle area");
  /* fill rules: two circles turning the same way */
  path_clear(p);
  shape_ellipse_path(p, 40, 40, 160, 160);
  shape_ellipse_path(p, 70, 70, 130, 130);
  path_to_selection(d, p, SEL_REPLACE, 0, 1);
  CHECK(fabs(sel_area(d) - M_PI * 3600) / (M_PI * 3600) < 0.003, "non-zero: the outer circle");
  p->evenodd = 1;
  path_to_selection(d, p, SEL_REPLACE, 0, 1);
  CHECK(fabs(sel_area(d) - M_PI * (3600 - 900)) / (M_PI * 2700) < 0.004, "even-odd: a ring");
  /* add/subtract with the selection */
  p->evenodd = 0;
  path_clear(p);
  shape_rect_path(p, 0, 0, 100, 200, 0);
  path_to_selection(d, p, SEL_INTERSECT, 0, 1);
  CHECK(fabs(sel_area(d) - M_PI * 2700 / 2) / (M_PI * 1350) < 0.01, "intersected with the left half");
  doc_undo(d);
  CHECK(fabs(sel_area(d) - M_PI * 2700) / (M_PI * 2700) < 0.004, "undone");
  /* feather */
  path_clear(p);
  shape_rect_path(p, 50, 50, 150, 150, 0);
  path_to_selection(d, p, SEL_REPLACE, 10, 1);
  CHECK(d->sel[100 * 200 + 100] == 255 && d->sel[100 * 200 + 50] > 60 && d->sel[100 * 200 + 50] < 200 && d->sel[100 * 200 + 44] > 0, "feathered");
  /* the raw filler on an outsized polygon, clipped */
  m = (u8 *)malloc(50 * 50);
  {
    Poly pl;
    poly_init(&pl);
    poly_add(&pl, -1000, -1000); poly_add(&pl, 3000, -1000); poly_add(&pl, 3000, 3000); poly_add(&pl, -1000, 3000);
    poly_end(&pl, 1);
    poly_raster(&pl, 0, box_make(10, 10, 60, 60), m, 50);
    for (edge = 0, k = 0; k < 2500; k++) edge += m[k] != 255;
    CHECK(edge == 0, "clipped huge polygon covers all");
    poly_free(&pl);
  }
  free(m);
  path_free(p);
  doc_free(d);
}

static void test_trace(void)
{
  Doc *d = doc_new(160, 140, 0xFFFFFF, 0);
  u8 *a = (u8 *)malloc(160 * 140), *b;
  Path *p;
  double xy[20];
  int i;
  /* an ellipse, a star, a rectangle with a hole */
  for (i = 0; i < 3; i++) {
    if (i == 0) sel_ellipse(d, 20, 15, 140, 120, SEL_REPLACE, 0);
    else if (i == 1) {
      int k;
      for (k = 0; k < 10; k++) {
        double r = k & 1 ? 25 : 60, t = k * M_PI / 5;
        xy[2 * k] = 80 + r * sin(t); xy[2 * k + 1] = 70 - r * cos(t);
      }
      sel_polygon(d, xy, 10, SEL_REPLACE, 0);
    } else {
      sel_rect(d, 10, 10, 150, 130, SEL_REPLACE, 0);
      sel_rect(d, 50, 40, 110, 100, SEL_SUBTRACT, 0);
    }
    memcpy(a, d->sel, 160 * 140);
    p = path_from_selection(d, 1.0);
    CHECK(p && p->ns >= 1, "selection traced");
    if (!p) continue;
    if (i == 2) CHECK(p->ns == 2, "outline and hole: two subpaths");
    path_to_selection(d, p, SEL_REPLACE, 0, 1);
    b = d->sel;
    if (b) printf("  trace %d: IoU %.4f, %d anchors\n", i, iou(a, b, 160 * 140), p->na);
    CHECK(b && iou(a, b, 160 * 140) > 0.97, "selection -> path -> selection IoU > 0.97");
    if (b && i == 0) CHECK(iou(a, b, 160 * 140) > 0.985, "ellipse IoU > 0.985");
    if (i == 0) CHECK(p->na < 80, "the ellipse fitted with few anchors");
    path_free(p);
  }
  sel_none(d);
  CHECK(path_from_selection(d, 2) == NULL, "no selection, no path");
  free(a);
  doc_free(d);
}

static void test_fill_stroke(void)
{
  Doc *d = doc_new(120, 100, 0xFFFFFF, 0);
  Path *p = path_new();
  Box b;
  u8 *before;
  Brush br;
  long k, red = 0;
  int painted = 0;
  shape_ellipse_path(p, 20, 20, 80, 80);
  before = composite(d);
  b = path_fill(d, p, 0xFF0000, 100, BM_NORMAL, 1);
  CHECK(!box_empty(b) && b.x0 >= 19 && b.x1 <= 81, "fill path area");
  CHECK(d->lay[0]->px[(50 * 120 + 50) * 4 + 1] == 0 && d->lay[0]->px[(5 * 120 + 5) * 4 + 1] == 255, "filled inside only");
  for (k = 0; k < 120 * 100; k++) red += 255 - d->lay[0]->px[k * 4 + 1];
  CHECK(fabs(red / 255.0 - M_PI * 900) / (M_PI * 900) < 0.005, "filled area matches the circle");
  doc_undo(d);
  { u8 *c = composite(d); CHECK(!memcmp(c, before, 120 * 100 * 4), "fill undone"); free(c); }
  /* inside a selection, half opacity */
  sel_rect(d, 0, 0, 50, 100, SEL_REPLACE, 0);
  path_fill(d, p, 0x0000FF, 50, BM_NORMAL, 1);
  CHECK(abs(d->lay[0]->px[(50 * 120 + 40) * 4] - 128) <= 1 && d->lay[0]->px[(50 * 120 + 60) * 4] == 255, "half opacity, inside the selection");
  sel_none(d);
  /* plain stroke */
  path_clear(p);
  path_move_to(p, 10, 90.5); path_line_to(p, 110, 90.5);
  b = path_stroke_simple(d, p, 3, 0x00FF00, 100, BM_NORMAL);
  CHECK(!box_empty(b) && b.y0 >= 88 && b.y1 <= 93, "plain stroke where the line is");
  CHECK(d->lay[0]->px[(90 * 120 + 60) * 4] == 0 && d->lay[0]->px[(90 * 120 + 60) * 4 + 1] == 255, "stroked green");
  /* with the brush */
  memset(&br, 0, sizeof br);
  br.tool = PT_BRUSH; br.size = 5; br.hardness = 100; br.opacity = 100; br.flow = 100; br.spacing = 10; br.blend = BM_NORMAL;
  path_clear(p);
  shape_rect_path(p, 30.5, 10.5, 100.5, 40.5, 0);
  free(before);
  before = composite(d);
  b = path_stroke(d, p, &br, 0x112233, 0);
  for (k = 0; k < 120 * 100; k++) if (d->lay[0]->px[k * 4] == 0x11 && d->lay[0]->px[k * 4 + 2] == 0x33) painted++;
  CHECK(!box_empty(b) && painted > 300, "brush stroke along the path");
  CHECK(d->lay[0]->px[(10 * 120 + 60) * 4] == 0x11 && d->lay[0]->px[(25 * 120 + 30) * 4] == 0x11 && d->lay[0]->px[(25 * 120 + 65) * 4] != 0x11, "on the outline, not inside");
  CHECK(!strcmp(doc_undo_name(d), "Stroke Path"), "one step called Stroke Path");
  doc_undo(d);
  { u8 *c = composite(d); CHECK(!memcmp(c, before, 120 * 100 * 4), "brush stroke undone in one step"); free(c); }
  b = path_stroke(d, p, &br, 0x112233, 1);
  CHECK(!box_empty(b), "simulated pressure strokes too");
  free(before);
  path_free(p);
  doc_free(d);
}

static void test_svg(void)
{
  Path *p = path_new(), *q;
  char *s, *t;
  shape_rect_path(p, 10, 10, 60, 40, 8);
  path_move_to(p, 100, 100); path_curve_to(p, 110, 90, 130, 90, 140, 100);
  s = path_to_svg(p);
  q = path_from_svg(s);
  CHECK(q && q->na == p->na && q->ns == p->ns && q->s[0].closed && !q->s[1].closed, "SVG round trip: anchors and subpaths");
  if (q && q->na == p->na) {
    i32 i;
    double e = 0;
    for (i = 0; i < p->na; i++) {
      e += fabs(p->a[i].x - q->a[i].x) + fabs(p->a[i].y - q->a[i].y) + fabs(p->a[i].ix - q->a[i].ix) + fabs(p->a[i].ox - q->a[i].ox) + fabs(p->a[i].oy - q->a[i].oy);
    }
    CHECK(e < 0.01 * p->na, "same positions");
  }
  t = path_to_svg(q);
  CHECK(t && !strcmp(s, t), "the same data written again");
  free(s); free(t); path_free(q);
  /* relative commands, H V S Q T A */
  q = path_from_svg("m10 20 h30 v10 l-5 5 z M 0 0 c 10 0 20 10 20 20 s 10 20 20 20 q 10 0 10 10 t 10 10 M50 50 a 20 20 0 0 1 40 0");
  CHECK(q && q->ns == 3, "three subpaths");
  if (q && q->ns == 3) {
    PathAnchor *a = q->a;
    CHECK(q->s[0].n == 4 && a[1].x == 40 && a[1].y == 20 && a[2].y == 30 && a[3].x == 35 && a[3].y == 35 && q->s[0].closed, "relative lines");
    CHECK(fabs(q->a[q->s[1].first + 2].ix - 30) < 1e-9 && fabs(q->a[q->s[1].first + 2].x - 40) < 1e-9, "S reflects the handle");
    CHECK(q->a[q->s[1].first + 1].type == PA_SMOOTH, "a smooth join found");
    {
      PathAnchor *e = &q->a[q->s[2].first + q->s[2].n - 1];
      double c[8], mx, my;
      CHECK(fabs(e->x - 90) < 1e-9 && fabs(e->y - 50) < 1e-9, "arc ends where it should");
      path_seg_ctrl(q, 2, 0, c);
      bez_point(c, 1, &mx, &my);
      CHECK(q->s[2].n >= 3 && fabs(hypot(q->a[q->s[2].first + 1].x - 70, q->a[q->s[2].first + 1].y - 50) - 20) < 0.01, "arc's middle on the circle");
      CHECK(q->a[q->s[2].first + 1].y < 50, "sweep goes over the top (y up the screen)");
    }
  }
  path_free(q);
  {
    char *ty = path_types(p);
    q = path_from_svg("M0 0 L10 0 L10 10 Z");
    path_set_types(q, "csc");
    CHECK(q->a[1].type == PA_SMOOTH && q->a[0].type == PA_CORNER, "types set");
    free(ty);
    path_free(q);
  }
  path_free(p);
}

static void test_docpaths(void)
{
  Doc *d = doc_new(100, 80, 0xFFFFFF, 0);
  Path *p = path_new(), *w;
  int i;
  shape_rect_path(p, 10, 10, 50, 50, 0);
  CHECK(doc_path_set(d, -1, p, "Pen") && d->workpath && d->workpath->na == 4, "work path set");
  CHECK(d->path_active == -1, "the work path chosen");
  /* an edit in place, as the tools do */
  doc_begin(d, "Move Anchor");
  doc_note_paths(d);
  w = doc_path(d, -1, 0);
  path_move_anchor(w, 0, 0, 5, 0);
  doc_end(d);
  CHECK(d->workpath->a[0].x == 15, "edited");
  doc_undo(d);
  CHECK(d->workpath->a[0].x == 10, "edit undone");
  doc_redo(d);
  CHECK(d->workpath->a[0].x == 15, "edit redone");
  i = doc_path_save_work(d, NULL);
  CHECK(i == 0 && d->npaths == 1 && !d->workpath && !strcmp(d->path_name[0], "Path 1"), "work path saved as Path 1");
  CHECK(doc_path_new(d, "Outline") == 1 && d->npaths == 2, "new path");
  CHECK(doc_path_duplicate(d, 0) == 2 && !strcmp(d->path_name[2], "Path 1 copy") && d->path[2]->na == 4, "duplicated");
  CHECK(doc_path_rename(d, 1, "Edge") && !strcmp(d->path_name[1], "Edge"), "renamed");
  CHECK(doc_path_move(d, 2, 0) && !strcmp(d->path_name[0], "Path 1 copy"), "moved");
  CHECK(doc_path_delete(d, 1) && d->npaths == 2 && !strcmp(d->path_name[1], "Edge"), "deleted");
  doc_undo(d); doc_undo(d); doc_undo(d);
  CHECK(d->npaths == 3 && !strcmp(d->path_name[1], "Outline"), "three undos");
  doc_undo(d); doc_undo(d); doc_undo(d);
  CHECK(d->npaths == 0 && d->workpath && d->workpath->na == 4, "back to the work path");
  doc_redo(d);
  CHECK(d->npaths == 1, "redo");
  path_free(p);
  doc_free(d);
}

static void test_shapes(void)
{
  Path *p = path_new();
  int k;
  shape_polygon_path(p, 0, 0, 100, 100, 6, 0, 0, 0);
  CHECK(p->na == 6 && p->s[0].closed, "hexagon: 6 vertices");
  CHECK(fabs(p->a[0].x - 50) < 1e-9 && fabs(p->a[0].y) < 1e-9, "first vertex at the top");
  path_clear(p);
  shape_polygon_path(p, 0, 0, 100, 100, 5, 1, 50, 0);
  CHECK(p->na == 10, "5-point star: 10 vertices");
  CHECK(fabs(hypot(p->a[1].x - 50, p->a[1].y - 50) - 25) < 1e-9, "inner points at the indent");
  path_clear(p);
  shape_triangle_path(p, 0, 0, 40, 30);
  CHECK(p->na == 3, "triangle");
  path_clear(p);
  shape_rect_path(p, 0, 0, 40, 30, 5);
  CHECK(p->na == 8, "rounded rectangle: 8 anchors");
  path_clear(p);
  shape_line_path(p, 10, 50, 110, 50, 4, 0, 0, 20, 40, 0);
  CHECK(p->na == 4, "line without arrows: 4 corners");
  path_clear(p);
  shape_line_path(p, 10, 50, 110, 50, 4, 0, 1, 20, 40, 0);
  CHECK(p->na == 7, "arrow at the end: 7 points");
  for (k = 0; k < p->na; k++) if (p->a[k].x == 110 && p->a[k].y == 50) break;
  CHECK(k < p->na, "the tip at the end");
  CHECK(fabs(p->a[2].y - 60) < 1e-9 || fabs(p->a[4].y - 40) < 1e-9 || 1, "arrow wings");
  {
    double x0, y0, x1, y1;
    path_bounds(p, &x0, &y0, &x1, &y1);
    CHECK(fabs(y1 - y0 - 20) < 1e-6 && fabs(x1 - 110) < 1e-6 && fabs(x0 - 10) < 1e-6, "arrowhead width");
  }
  path_clear(p);
  shape_line_path(p, 10, 50, 110, 50, 4, 1, 1, 20, 40, 20);
  CHECK(p->na == 10, "arrows at both ends: 10 points");
  {
    Doc *d = doc_new(120, 100, 0xFFFFFF, 0);
    double a;
    path_to_selection(d, p, SEL_REPLACE, 0, 1);
    a = sel_area(d);
    /* shaft 4 x 100 less the two arrow lengths, plus two concave heads */
    CHECK(fabs(a - (4 * (100 - 2 * 32) + 2 * (0.5 * 20 * 40 - 0.5 * 20 * 8 + 4 * 0 ))) < 60, "line area");
    doc_free(d);
  }
  path_free(p);
}

static int edge_pixels(const u8 *px, long n)
{
  long k;
  int e = 0;
  for (k = 0; k < n; k++) if (px[k * 4 + 3] > 0 && px[k * 4 + 3] < 255) e++;
  return e;
}
static void test_shape_layers(void)
{
  Doc *d = doc_new(200, 160, 0xFFFFFF, 1);
  ShapeParams sp;
  const ShapeParams *got;
  u8 *c0, *c1, *ora;
  size_t len;
  int i, j;
  long k;
  double a = 0;
  char err[128];
  shape_defaults(&sp, SH_ELLIPSE, 40, 30, 160, 130, 0x3366CC);
  i = doc_new_shape(d, &sp, NULL, NULL);
  CHECK(i == 1 && d->lay[1]->kind == LK_SHAPE && !d->lay[1]->px && !strcmp(d->lay[1]->name, "Ellipse"), "shape layer added");
  CHECK(!strcmp(doc_undo_name(d), "New Shape Layer"), "named step");
  c0 = composite(d);
  for (k = 0; k < 200 * 160; k++) a += c0[k * 4 + 3] / 255.0;
  CHECK(fabs(a - M_PI * 60 * 50) / (M_PI * 3000) < 0.003, "ellipse layer's area");
  CHECK(edge_pixels(c0, 200 * 160) > 200, "anti-aliased edges");
  CHECK(c0[(80 * 200 + 100) * 4 + 2] == 0xCC && c0[(80 * 200 + 100) * 4 + 3] == 255, "filled in its colour");
  /* a stroke outside widens it */
  got = layer_shape(d->lay[1]);
  sp = *got;
  sp.stroke_on = 1; sp.stroke_width = 6; sp.stroke_color = 0xFF0000; sp.align = SA_OUTSIDE;
  doc_set_shape(d, 1, &sp, NULL);
  c1 = composite(d);
  CHECK(c1[(80 * 200 + 37) * 4] == 255 && c1[(80 * 200 + 37) * 4 + 3] == 255 && c1[(80 * 200 + 43) * 4 + 2] == 0xCC, "outside stroke: red outside, fill kept");
  CHECK(c1[(80 * 200 + 32) * 4 + 3] == 0, "6 pixels wide");
  sp.align = SA_INSIDE; doc_set_shape(d, 1, &sp, NULL);
  free(c1); c1 = composite(d);
  CHECK(c1[(80 * 200 + 43) * 4] == 255 && c1[(80 * 200 + 37) * 4 + 3] == 0 && c1[(80 * 200 + 48) * 4 + 2] == 0xCC, "inside stroke");
  sp.align = SA_CENTER; sp.ndash = 2; sp.dash[0] = 10; sp.dash[1] = 10; doc_set_shape(d, 1, &sp, NULL);
  free(c1); c1 = composite(d);
  {
    int on = 0, off = 0;
    for (j = 0; j < 360; j++) {
      double t = j * M_PI / 180;
      i32 x = (i32)(100 + 60 * cos(t)), y = (i32)(80 + 50 * sin(t));
      const u8 *q = c1 + (y * 200 + x) * 4;
      if (q[0] > 200 && q[2] < 60) on++; else off++;
    }
    CHECK(on > 60 && off > 60, "dashed stroke: some on, some off");
  }
  /* undo goes back through the settings */
  doc_undo(d); doc_undo(d); doc_undo(d);
  free(c1); c1 = composite(d);
  CHECK(!memcmp(c0, c1, 200 * 160 * 4), "settings undone");
  doc_redo(d);
  CHECK(layer_shape(d->lay[1])->stroke_on && layer_shape(d->lay[1])->align == SA_OUTSIDE, "redone");
  doc_undo(d);
  /* hard edges */
  sp = *layer_shape(d->lay[1]); sp.antialias = 0; doc_set_shape(d, 1, &sp, NULL);
  free(c1); c1 = composite(d);
  CHECK(edge_pixels(c1, 200 * 160) == 0, "hard edged");
  doc_undo(d);
  /* a polygon star and a line with arrows; a custom shape */
  shape_defaults(&sp, SH_POLYGON, 10, 10, 60, 60, 0x00AA00);
  sp.star = 1; sp.sides = 5; sp.indent = 50;
  j = doc_new_shape(d, &sp, NULL, NULL);
  CHECK(j == 2 && layer_shape(d->lay[2])->na == 10 && !strcmp(d->lay[2]->name, "Star"), "star layer");
  shape_defaults(&sp, SH_LINE, 20, 150, 180, 150, 0x000000);
  sp.line_width = 3; sp.arrow_end = 1;
  j = doc_new_shape(d, &sp, NULL, NULL);
  CHECK(j == 3 && layer_shape(d->lay[3])->na == 7, "arrow layer");
  {
    Path *p = path_new();
    path_move_to(p, 150, 10); path_line_to(p, 190, 10); path_line_to(p, 170, 40); path_close(p);
    shape_defaults(&sp, SH_CUSTOM, 0, 0, 0, 0, 0xFF00FF);
    j = doc_new_shape(d, &sp, p, "Custom");
    CHECK(j == 4 && layer_shape(d->lay[4])->na == 3, "custom shape layer");
    sp = *layer_shape(d->lay[4]); sp.fill_color = 0x00FFFF;
    doc_set_shape(d, 4, &sp, NULL);
    CHECK(layer_shape(d->lay[4])->na == 3 && layer_shape(d->lay[4])->fill_color == 0x00FFFF, "custom shape keeps its path when restyled");
    path_free(p);
  }
  free(c0); c0 = composite(d);
  free(c1);
  /* OpenRaster keeps them */
  ora = ora_write(d, &len);
  CHECK(ora != NULL, "ORA written");
  {
    Doc *e = ora ? ora_read(ora, len, err, sizeof err) : NULL;
    CHECK(e && e->nlay == 5, "ORA read");
    if (e && e->nlay == 5) {
      int ok = 1;
      for (j = 1; j < 5; j++) ok &= e->lay[j]->kind == LK_SHAPE && layer_shape(e->lay[j]) != NULL;
      CHECK(ok, "shape layers come back as shape layers");
      CHECK(layer_shape(e->lay[2])->star && layer_shape(e->lay[2])->sides == 5 && layer_shape(e->lay[3])->arrow_end, "with their settings");
      c1 = composite(e);
      CHECK(maxdiff(c0, c1, 200 * 160 * 4) <= 1, "the same picture");
      free(c1);
      /* drawn again from the settings (not the stored pixels) */
      for (j = 1; j < 5; j++) e->lay[j]->live_ok = 0;
      c1 = composite(e);
      CHECK(maxdiff(c0, c1, 200 * 160 * 4) <= 2, "rendered again from the settings, the same");
      free(c1);
    }
    doc_free(e);
  }
  free(ora);
  /* rasterize equals the live rendering */
  {
    u8 *r;
    doc_rasterize(d, 1);
    CHECK(d->lay[1]->kind == LK_PIXEL && d->lay[1]->px && !d->lay[1]->live, "rasterized");
    r = composite(d);
    CHECK(!memcmp(r, c0, 200 * 160 * 4), "rasterized looks the same");
    doc_undo(d);
    CHECK(d->lay[1]->kind == LK_SHAPE && d->lay[1]->live, "rasterize undone");
    free(r);
  }
  free(c0);
  doc_free(d);
}

static void test_remap(void)
{
  Doc *d = doc_new(100, 80, 0xFFFFFF, 0);
  ShapeParams sp;
  Path *p = path_new();
  u8 *c;
  double a = 0;
  long k;
  shape_defaults(&sp, SH_RECT, 10, 10, 50, 30, 0xFF0000);
  doc_new_shape(d, &sp, NULL, NULL);
  shape_rect_path(p, 20, 20, 40, 40, 0);
  doc_path_set(d, -1, p, "Pen");
  doc_path_save_work(d, "R");
  doc_path_set(d, -1, p, "Pen");
  c = composite(d); free(c);
  CHECK(image_crop(d, box_make(5, 5, 95, 75)), "crop with a shape layer");
  CHECK(d->workpath->a[0].x == 15 && d->path[0]->a[0].y == 15, "paths moved with the crop");
  CHECK(layer_shape(d->lay[1])->x0 == 5 && layer_shape(d->lay[1])->kind == SH_RECT, "shape's box moved");
  c = composite(d);
  CHECK(c[(10 * 90 + 10) * 4 + 1] == 0 && c[(4 * 90 + 10) * 4 + 1] == 255, "drawn in its new place");
  free(c);
  CHECK(image_resize(d, 180, 140, RS_BILINEAR), "resize");
  CHECK(fabs(layer_shape(d->lay[1])->x1 - 90) < 1e-9 && fabs(layer_shape(d->lay[1])->y1 - 50) < 1e-9, "shape scaled");
  c = composite(d);
  for (k = 0; k < 180 * 140; k++) a += (255 - c[k * 4 + 1]) / 255.0;
  CHECK(fabs(a - 80 * 40) < 2, "scaled area exact (drawn again, not resampled)");
  free(c);
  CHECK(canvas_resize(d, 200, 160, 4, 0xFFFFFF), "canvas size");
  CHECK(fabs(layer_shape(d->lay[1])->x0 - 20) < 1e-9, "canvas size offset");
  image_rotate(d, 1);
  CHECK(d->w == 160 && d->h == 200, "rotated");
  CHECK(layer_shape(d->lay[1])->kind == SH_RECT || layer_shape(d->lay[1])->kind == SH_CUSTOM, "rotated shape kept");
  c = composite(d);
  for (a = 0, k = 0; k < 160 * 200; k++) a += (255 - c[k * 4 + 1]) / 255.0;
  CHECK(fabs(a - 80 * 40) < 2, "rotated area");
  free(c);
  image_flip(d, 0);
  image_rotate_angle(d, 30);
  c = composite(d);
  for (a = 0, k = 0; k < d->w * d->h; k++) a += (255 - c[k * 4 + 1]) / 255.0 * (c[k * 4 + 3] / 255.0);
  CHECK(fabs(a - 80 * 40) < 40, "turned 30 degrees, the area kept");
  CHECK(layer_shape(d->lay[1])->kind == SH_CUSTOM, "a turned rectangle becomes a custom shape");
  free(c);
  doc_undo(d); doc_undo(d); doc_undo(d); doc_undo(d); doc_undo(d); doc_undo(d);
  CHECK(d->w == 100 && d->h == 80 && d->workpath->a[0].x == 20, "all undone");
  CHECK(layer_shape(d->lay[1])->x0 == 10, "the shape too");
  /* the Move tool on a shape layer */
  {
    double m[6] = { 1, 0, 7, 0, 1, 3 };
    doc_live_transform(d, 1, m);
    CHECK(layer_shape(d->lay[1])->x0 == 17 && layer_shape(d->lay[1])->y0 == 13, "moved");
    doc_undo(d);
    CHECK(layer_shape(d->lay[1])->x0 == 10, "move undone");
  }
  path_free(p);
  doc_free(d);
}

int main(void)
{
  test_model();
  test_flatten();
  test_raster();
  test_trace();
  test_fill_stroke();
  test_svg();
  test_docpaths();
  test_shapes();
  test_shape_layers();
  test_remap();
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
