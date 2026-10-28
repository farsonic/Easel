/*
 * selxtest.c - Easel 0.7's selections and transforms: alpha channels (save, load, the Channels
 * panel's commands, undo and redo, OpenRaster), Quick Mask, projective transforms (identity, quarter
 * turns, perspective corners, scaling there and back, skew, undo), Transform Selection and Again,
 * Single Row and Column, anti-aliasing of the wand and lasso, Color Range, the Magnetic Lasso, crop
 * presets, rotated crop, straightening, the Ruler, Defringe and the mattes, and Alt-drag copying.
 * Run under the address and undefined-behaviour sanitizers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core/doc.h"
#include "core/paint.h"
#include "core/codec.h"
#include "core/channels.h"
#include "core/xform.h"
#include "core/selx.h"
#include "core/maglasso.h"
#include "core/crop.h"
#include "core/matte.h"
#include "core/layers.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fails, tests;
#define CHECK(c, msg) do { tests++; if (!(c)) { fails++; printf("FAIL %s (line %d)\n", msg, __LINE__); } } while (0)

static unsigned rnd = 777;
static unsigned rn(void) { rnd = rnd * 1103515245u + 12345u; return (rnd >> 16) & 0x7fff; }

static void fill_random(Doc *d, int li, int opaque)
{
  long k, n = (long)d->w * d->h;
  for (k = 0; k < n * 4; k++) d->lay[li]->px[k] = (u8)rn();
  if (opaque) for (k = 0; k < n; k++) d->lay[li]->px[k * 4 + 3] = 255;
}
static u8 *dup(const u8 *p, size_t n) { u8 *q = (u8 *)malloc(n); memcpy(q, p, n); return q; }
static int maxdiff(const u8 *a, const u8 *b, long n)
{
  long k;
  int m = 0;
  for (k = 0; k < n; k++) { int v = abs(a[k] - b[k]); if (v > m) m = v; }
  return m;
}
static void put(Doc *d, int li, i32 x, i32 y, u32 c, int a)
{
  u8 *p = d->lay[li]->px + ((long)y * d->w + x) * 4;
  p[0] = (u8)(c >> 16); p[1] = (u8)(c >> 8); p[2] = (u8)c; p[3] = (u8)a;
}
static void fill_box(Doc *d, int li, Box b, u32 c, int a)
{
  i32 x, y;
  for (y = b.y0; y < b.y1; y++) for (x = b.x0; x < b.x1; x++) put(d, li, x, y, c, a);
}
static const u8 *px_at(Doc *d, int li, i32 x, i32 y) { return d->lay[li]->px + ((long)y * d->w + x) * 4; }
static int sel_v(Doc *d, i32 x, i32 y) { return sel_at(d, x, y); }

/* ------------------------------------------------------------------ channels */
static void test_channels(void)
{
  Doc *d = doc_new(40, 30, 0x808080, 0), *e;
  long n = 40 * 30, k;
  int c0, c1, c2, ok;
  u8 *png;
  size_t len;
  char err[128];
  sel_rect(d, 5, 5, 20, 15, SEL_REPLACE, 0);
  c0 = chan_save_selection(d, -1, SEL_REPLACE, NULL);
  CHECK(c0 == 0 && d->nchan == 1 && !strcmp(d->chan_name[0], "Alpha 1"), "selection saved as Alpha 1");
  CHECK(d->chan[0][10 * 40 + 10] == 255 && d->chan[0][2 * 40 + 2] == 0, "the channel holds the selection");
  sel_rect(d, 15, 10, 30, 25, SEL_REPLACE, 0);
  CHECK(chan_save_selection(d, 0, SEL_ADD, NULL) == 0 && d->chan[0][20 * 40 + 25] == 255 && d->chan[0][10 * 40 + 10] == 255, "saved into it: Add");
  sel_rect(d, 0, 0, 12, 30, SEL_REPLACE, 0);
  chan_save_selection(d, 0, SEL_SUBTRACT, NULL);
  CHECK(d->chan[0][10 * 40 + 10] == 0 && d->chan[0][10 * 40 + 15] == 255, "Subtract");
  sel_rect(d, 0, 0, 40, 12, SEL_REPLACE, 0);
  chan_save_selection(d, 0, SEL_INTERSECT, NULL);
  CHECK(d->chan[0][10 * 40 + 15] == 255 && d->chan[0][13 * 40 + 15] == 0 && d->chan[0][20 * 40 + 25] == 0, "Intersect");
  doc_undo(d);
  CHECK(d->chan[0][20 * 40 + 25] == 255, "undo of Intersect");
  doc_redo(d);
  CHECK(d->chan[0][20 * 40 + 25] == 0, "redo of Intersect");
  /* load */
  sel_none(d);
  CHECK(chan_load_selection(d, 0, SEL_REPLACE, 0) && d->sel && d->sel[10 * 40 + 15] == 255 && d->sel[10 * 40 + 5] == 0, "Load Selection");
  CHECK(d->selbox.x0 == 12 && d->selbox.y0 == 5 && d->selbox.x1 == 30 && d->selbox.y1 == 12, "its box");
  chan_load_selection(d, 0, SEL_REPLACE, 1);
  CHECK(d->sel && d->sel[10 * 40 + 15] == 0 && d->sel[0] == 255, "Load inverted");
  sel_rect(d, 0, 0, 14, 30, SEL_REPLACE, 0);
  chan_load_selection(d, 0, SEL_INTERSECT, 0);
  CHECK(d->sel && d->sel[10 * 40 + 13] == 255 && d->sel[10 * 40 + 15] == 0, "Load intersecting");
  doc_undo(d);
  CHECK(d->sel && d->sel[10 * 40 + 2] == 255, "undo of Load");
  /* the panel */
  c1 = chan_duplicate(d, 0, NULL);
  CHECK(c1 == 1 && d->nchan == 2 && !strcmp(d->chan_name[1], "Alpha 1 copy") && !memcmp(d->chan[0], d->chan[1], (size_t)n), "Duplicate Channel");
  CHECK(chan_rename(d, 1, "Mask B") && !strcmp(d->chan_name[1], "Mask B"), "rename");
  doc_undo(d);
  CHECK(!strcmp(d->chan_name[1], "Alpha 1 copy"), "undo of rename");
  doc_redo(d);
  c2 = chan_from_layer(d, 0, CH_RED, NULL);
  CHECK(c2 == 2 && d->chan[2][0] == 0x80 && !strcmp(d->chan_name[2], "Red copy"), "a channel from the red channel");
  put(d, 0, 3, 3, 0x204060, 255);
  c2 = chan_from_layer(d, 0, CH_LUMINOSITY, "Lum");
  CHECK(c2 == 3 && abs(d->chan[3][3 * 40 + 3] - (0x20 * 77 + 0x40 * 150 + 0x60 * 29) / 256) <= 1, "luminosity");
  CHECK(chan_invert(d, 3) && d->chan[3][0] == 255 - 0x80, "invert a channel");
  doc_undo(d);
  CHECK(d->chan[3][0] == 0x80, "undo of invert");
  CHECK(chan_move(d, 3, 0) && !strcmp(d->chan_name[0], "Lum") && !strcmp(d->chan_name[1], "Alpha 1"), "move a channel");
  doc_undo(d);
  CHECK(!strcmp(d->chan_name[3], "Lum"), "undo of move");
  CHECK(chan_delete(d, 1) && d->nchan == 3 && !strcmp(d->chan_name[1], "Red copy"), "Delete Channel");
  doc_undo(d);
  CHECK(d->nchan == 4 && !strcmp(d->chan_name[1], "Mask B") && !memcmp(d->chan[0], d->chan[1], (size_t)n), "undo of Delete");
  doc_redo(d);
  CHECK(d->nchan == 3, "redo of Delete");
  doc_undo(d);
  /* a channel painted with a brush, undone */
  {
    PaintTarget t;
    Brush b;
    Stroke *s;
    u8 *before;
    sel_none(d);                                           /* (a stroke keeps to the selection) */
    before = dup(d->chan[1], (size_t)n);
    memset(&b, 0, sizeof b);
    b.tool = PT_BRUSH; b.size = 5; b.hardness = 100; b.opacity = 100; b.flow = 100; b.spacing = 25;
    CHECK(chan_target(d, 1, &t), "a channel as a brush target");
    s = stroke_begin_target(d, &t, &b, 0xFFFFFF, 30, 25, 1.0, NULL);
    stroke_to(s, 35, 25, NULL);
    stroke_end(s);
    CHECK(d->chan[1][25 * 40 + 32] == 255 && before[25 * 40 + 32] == 0, "painted white");
    doc_undo(d);
    CHECK(!memcmp(d->chan[1], before, (size_t)n), "undo of the stroke");
    free(before);
  }
  /* channels go with a crop, and come back with its undo */
  {
    u8 *before = dup(d->chan[0], (size_t)n);
    CHECK(image_crop(d, box_make(10, 4, 30, 20)) && d->w == 20 && d->h == 16, "crop");
    CHECK(d->nchan == 4 && d->chan[0][(10 - 4) * 20 + (15 - 10)] == before[10 * 40 + 15], "channels cropped too");
    if (d->nchan != 4 || d->chan[0][(10 - 4) * 20 + (15 - 10)] != before[10 * 40 + 15]) printf("nchan %d, %d vs %d\n", d->nchan, d->chan[0][(10 - 4) * 20 + (15 - 10)], before[10 * 40 + 15]);
    doc_undo(d);
    CHECK(d->w == 40 && d->nchan == 4 && !memcmp(d->chan[0], before, (size_t)n), "undo of the crop");
    free(before);
  }
  /* OpenRaster */
  png = ora_write(d, &len);
  CHECK(png != NULL, "ora with channels written");
  e = png ? ora_read(png, len, err, sizeof err) : NULL;
  CHECK(e && e->nchan == d->nchan, "ora read back with its channels");
  for (ok = e != NULL, k = 0; e && k < d->nchan && k < e->nchan; k++)
    if (strcmp(e->chan_name[k], d->chan_name[k]) || memcmp(e->chan[k], d->chan[k], (size_t)n)) ok = 0;
  CHECK(ok, "the same channels, names and pixels");
  doc_free(e); free(png);
  doc_free(d);
}

/* ------------------------------------------------------------------ Quick Mask */
static void test_quickmask(void)
{
  Doc *d = doc_new(32, 24, 0xFFFFFF, 0);
  long n = 32 * 24;
  u8 *s0;
  PaintTarget t;
  Brush b;
  Stroke *s;
  sel_ellipse(d, 4, 4, 28, 20, SEL_REPLACE, 0);
  s0 = dup(d->sel, (size_t)n);
  CHECK(quickmask_enter(d) && quickmask_active(d) && !d->sel && !memcmp(d->qmask, s0, (size_t)n), "into Quick Mask: the selection as a plane");
  CHECK(!quickmask_enter(d), "not twice");
  CHECK(quickmask_leave(d) && !quickmask_active(d) && d->sel && !memcmp(d->sel, s0, (size_t)n), "out again: the same selection");
  doc_undo(d);
  CHECK(quickmask_active(d) && !d->sel, "undo of leaving");
  doc_undo(d);
  CHECK(!quickmask_active(d) && d->sel && !memcmp(d->sel, s0, (size_t)n), "undo of entering");
  doc_redo(d);
  CHECK(quickmask_active(d), "redo");
  /* paint black: taken out of the selection */
  memset(&b, 0, sizeof b);
  b.tool = PT_BRUSH; b.size = 4; b.hardness = 100; b.opacity = 100; b.flow = 100; b.spacing = 25;
  CHECK(quickmask_target(d, &t), "the plane as a brush target");
  s = stroke_begin_target(d, &t, &b, 0x000000, 16, 12, 1.0, NULL);
  stroke_end(s);
  CHECK(d->qmask[12 * 32 + 16] == 0, "painted black");
  quickmask_leave(d);
  CHECK(d->sel && d->sel[12 * 32 + 16] == 0 && d->sel[12 * 32 + 8] == 255, "the painted part isn't selected");
  doc_undo(d); doc_undo(d);
  CHECK(quickmask_active(d) && !memcmp(d->qmask, s0, (size_t)n), "undo of the stroke on the plane");
  quickmask_leave(d);
  /* no selection: everything */
  sel_none(d);
  quickmask_enter(d);
  CHECK(d->qmask[0] == 255 && d->qmask[n - 1] == 255, "no selection is everything");
  quickmask_leave(d);
  CHECK(!d->sel, "and back to no selection");
  free(s0);
  doc_free(d);
}

/* ------------------------------------------------------------------ transforms */
static void test_transforms(void)
{
  Doc *d = doc_new(32, 32, 0, 0), *r;
  long n = 32 * 32 * 4;
  u8 *orig, *after;
  XForm t;
  double h[9], x, y;
  int c, ok;
  fill_random(d, 0, 1);
  orig = dup(d->lay[0]->px, (size_t)n);
  /* identity: exact */
  xform_init(&t, doc_box(d));
  CHECK(xform_valid(&t), "the identity is valid");
  xform_layer(d, 0, &t, RS_BICUBIC);
  CHECK(!memcmp(d->lay[0]->px, orig, (size_t)n), "identity is exact");
  /* a quarter turn through the homography matches image_rotate exactly */
  r = doc_new(32, 32, 0, 0);
  memcpy(r->lay[0]->px, orig, (size_t)n);
  image_rotate(r, 1);
  xform_init(&t, doc_box(d));
  xform_rotate_about(&t, 90, 16, 16);
  xform_layer(d, 0, &t, RS_BILINEAR);
  CHECK(!memcmp(d->lay[0]->px, r->lay[0]->px, (size_t)n), "90 degrees by homography = image_rotate");
  /* and nearly so resampled (the map nudged off whole pixels) */
  doc_undo(d);
  CHECK(!memcmp(d->lay[0]->px, orig, (size_t)n), "undo restores exactly");
  doc_redo(d);
  CHECK(!memcmp(d->lay[0]->px, r->lay[0]->px, (size_t)n), "redo");
  doc_undo(d);
  xform_init(&t, doc_box(d));
  xform_rotate_about(&t, 90, 16, 16);
  t.q[0] += 1e-6;
  xform_layer(d, 0, &t, RS_BILINEAR);
  {
    /* inside (the edge pixels are anti-aliased against transparency) */
    i32 xx, yy;
    int m = 0;
    for (yy = 1; yy < 31; yy++) for (xx = 1; xx < 31; xx++) {
      int k, v;
      for (k = 0; k < 4; k++) { v = abs(px_at(d, 0, xx, yy)[k] - px_at(r, 0, xx, yy)[k]); if (v > m) m = v; }
    }
    CHECK(m <= 2, "90 degrees resampled is within 2");
  }
  doc_undo(d);
  doc_free(r);
  /* perspective: the corners land where asked */
  {
    Doc *p = doc_new(100, 100, 0, 1);
    static const u32 cols[4] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00 };
    Box src = box_make(20, 20, 60, 50);
    double q[8] = { 10, 15, 85, 25, 70, 90, 25, 70 };
    fill_box(p, 0, src, 0x808080, 255);
    fill_box(p, 0, box_make(20, 20, 24, 24), cols[0], 255);
    fill_box(p, 0, box_make(56, 20, 60, 24), cols[1], 255);
    fill_box(p, 0, box_make(56, 46, 60, 50), cols[2], 255);
    fill_box(p, 0, box_make(20, 46, 24, 50), cols[3], 255);
    xform_init(&t, src);
    for (c = 0; c < 4; c++) xform_distort(&t, c, q[2 * c], q[2 * c + 1]);
    CHECK(xform_valid(&t) && xform_matrix(&t, h), "a perspective quad is valid");
    for (ok = 1, c = 0; c < 4; c++) {
      double sx = c == 0 || c == 3 ? src.x0 : src.x1, sy = c < 2 ? src.y0 : src.y1;
      xform_point(h, sx, sy, &x, &y);
      if (fabs(x - q[2 * c]) > 1e-6 || fabs(y - q[2 * c + 1]) > 1e-6) ok = 0;
    }
    CHECK(ok, "the matrix takes the box's corners to the quad's");
    xform_layer(p, 0, &t, RS_BILINEAR);
    for (ok = 1, c = 0; c < 4; c++) {
      /* a little in from each corner, towards the middle */
      double mx = (q[0] + q[2] + q[4] + q[6]) / 4, my = (q[1] + q[3] + q[5] + q[7]) / 4, dx = mx - q[2 * c], dy = my - q[2 * c + 1], l = sqrt(dx * dx + dy * dy);
      i32 xx = (i32)floor(q[2 * c] + dx / l * 2.5), yy = (i32)floor(q[2 * c + 1] + dy / l * 2.5);
      const u8 *v = px_at(p, 0, xx, yy);
      u32 got = (u32)v[0] << 16 | (u32)v[1] << 8 | v[2];
      if (v[3] < 200 || abs(v[0] - (int)(cols[c] >> 16)) > 60 || abs(v[1] - (int)((cols[c] >> 8) & 255)) > 60 || abs(v[2] - (int)(cols[c] & 255)) > 60) {
        ok = 0; printf("corner %d at %d,%d is %06lx alpha %d\n", c, (int)xx, (int)yy, (unsigned long)got, v[3]);
      }
    }
    CHECK(ok, "each corner's colour lands at its new corner");
    CHECK(px_at(p, 0, 12, 80)[3] == 0 && px_at(p, 0, 50, 55)[3] == 255 && px_at(p, 0, 80, 80)[3] == 0, "the middle filled, outside the quad clear");
    {
      /* the edge is anti-aliased: some partly covered pixels */
      long k, part = 0;
      for (k = 0; k < 100 * 100; k++) if (p->lay[0]->px[k * 4 + 3] > 0 && p->lay[0]->px[k * 4 + 3] < 255) part++;
      CHECK(part > 20, "anti-aliased edges");
    }
    doc_free(p);
  }
  /* scaled up two times and back down: near enough the same */
  {
    Doc *s = doc_new(96, 96, 0, 1);
    i32 xx, yy;
    long sum = 0, cnt = 0;
    Box b = box_make(32, 32, 64, 64);
    u8 *o;
    for (yy = 32; yy < 64; yy++) for (xx = 32; xx < 64; xx++)
      put(s, 0, xx, yy, (u32)((xx - 32) * 8) << 16 | (u32)((yy - 32) * 8) << 8 | (u32)(128 + 100 * sin(xx * 0.3) * cos(yy * 0.2)), 255);
    o = dup(s->lay[0]->px, 96 * 96 * 4);
    xform_set_affine(&t, b, 48, 48, 2, 2, 0, 0, 0, 0, 0);
    xform_layer(s, 0, &t, RS_BICUBIC);
    CHECK(px_at(s, 0, 20, 20)[3] == 255 && px_at(s, 0, 15, 15)[3] == 0, "scaled up twice about the centre");
    xform_set_affine(&t, box_make(16, 16, 80, 80), 48, 48, 0.5, 0.5, 0, 0, 0, 0, 0);
    xform_layer(s, 0, &t, RS_BILINEAR);
    for (yy = 34; yy < 62; yy++) for (xx = 34; xx < 62; xx++) {
      int k;
      for (k = 0; k < 3; k++) sum += abs(px_at(s, 0, xx, yy)[k] - o[((long)yy * 96 + xx) * 4 + k]);
      cnt += 3;
    }
    CHECK(sum / cnt <= 6, "and halved: back to within a few levels");
    CHECK(px_at(s, 0, 20, 20)[3] == 0, "the outside is clear again");
    doc_undo(s); doc_undo(s);
    CHECK(!memcmp(s->lay[0]->px, o, 96 * 96 * 4), "two undos: exactly as before");
    free(o);
    doc_free(s);
  }
  /* skew: 45 degrees about the top edge moves the bottom row along by the height */
  {
    Doc *s = doc_new(60, 40, 0, 1);
    Box b = box_make(10, 10, 30, 30);
    fill_box(s, 0, b, 0x3366CC, 255);
    put(s, 0, 10, 29, 0xFF0000, 255);
    xform_set_affine(&t, b, 10, 10, 1, 1, 0, 45, 0, 0, 0);
    CHECK(fabs(t.q[6] - 30) < 1e-9 && fabs(t.q[7] - 30) < 1e-9 && fabs(t.q[0] - 10) < 1e-9, "skew moves the bottom corners by the height");
    xform_layer(s, 0, &t, RS_BILINEAR);
    {
      const u8 *v = px_at(s, 0, 30, 29);
      CHECK(v[3] > 128 && v[0] > 120 && v[2] < 200, "the bottom-left pixel moved along the bottom");
      CHECK(px_at(s, 0, 12, 28)[3] == 0 && px_at(s, 0, 12, 11)[3] == 255, "a parallelogram");
    }
    doc_free(s);
  }
  /* Transform Again: a quarter turn again is a half turn */
  {
    Doc *s = doc_new(32, 32, 0, 0), *q2 = doc_new(32, 32, 0, 0);
    fill_random(s, 0, 1);
    memcpy(q2->lay[0]->px, s->lay[0]->px, (size_t)n);
    image_rotate(q2, 2);
    xform_init(&t, doc_box(s));
    xform_rotate_about(&t, 90, 16, 16);
    xform_layer(s, 0, &t, RS_BILINEAR);
    CHECK(!box_empty(xform_again(s, 0, RS_BILINEAR)), "Transform Again");
    CHECK(!memcmp(s->lay[0]->px, q2->lay[0]->px, (size_t)n) && !strcmp(doc_undo_name(s), "Transform Again"), "twice a quarter turn = a half turn");
    doc_free(s); doc_free(q2);
  }
  /* the selected pixels float: only they move, and the selection goes with them */
  {
    Doc *s = doc_new(40, 40, 0xFFFFFF, 0);
    fill_box(s, 0, box_make(5, 5, 15, 15), 0xFF0000, 255);
    sel_rect(s, 5, 5, 15, 15, SEL_REPLACE, 0);
    xform_init(&t, s->selbox);
    xform_translate(&t, 20, 10);
    after = NULL;
    xform_layer(s, 0, &t, RS_BICUBIC);
    CHECK(px_at(s, 0, 30, 20)[0] == 255 && px_at(s, 0, 30, 20)[1] == 0 && px_at(s, 0, 7, 7)[3] == 0, "the selection moved, its old place transparent");
    CHECK(s->sel && s->selbox.x0 == 25 && s->selbox.y0 == 15 && s->selbox.x1 == 35 && s->selbox.y1 == 25, "the selection went with it");
    /* flips and quarter turns of the selected region */
    fill_box(s, 0, box_make(25, 15, 27, 25), 0x00FF00, 255);
    xform_flip(s, 0, 0);
    CHECK(px_at(s, 0, 34, 20)[1] == 255 && px_at(s, 0, 25, 20)[0] == 255 && px_at(s, 0, 25, 20)[1] == 0, "flipped within the selection");
    sel_rect(s, 25, 15, 35, 19, SEL_REPLACE, 0);
    xform_rotate90(s, 0, 1);
    CHECK(s->selbox.x1 - s->selbox.x0 == 4 && s->selbox.y1 - s->selbox.y0 == 10, "a quarter turn of a 10 x 4 selection is 4 x 10");
    free(after);
    doc_free(s);
  }
  free(orig);
  doc_free(d);
}
static void test_transform_selection(void)
{
  Doc *d = doc_new(50, 50, 0x445566, 0);
  u8 *px = dup(d->lay[0]->px, 50 * 50 * 4);
  XForm t;
  sel_rect(d, 5, 5, 25, 15, SEL_REPLACE, 0);
  xform_init(&t, d->selbox);
  xform_translate(&t, 10, 20);
  CHECK(xform_selection(d, &t), "Transform Selection");
  CHECK(d->selbox.x0 == 15 && d->selbox.y0 == 25 && d->selbox.x1 == 35 && d->selbox.y1 == 35, "the selection moved");
  CHECK(!memcmp(d->lay[0]->px, px, 50 * 50 * 4), "the pixels did not");
  CHECK(!strcmp(doc_undo_name(d), "Transform Selection"), "its undo step");
  CHECK(sel_rotate90(d, 1) && d->selbox.x1 - d->selbox.x0 == 10 && d->selbox.y1 - d->selbox.y0 == 20, "the selection turned");
  CHECK(sel_flip(d, 1) && d->selbox.y1 - d->selbox.y0 == 20, "flipped");
  xform_init(&t, d->selbox);
  xform_scale_about(&t, 1.5, 1.5, (d->selbox.x0 + d->selbox.x1) / 2.0, (d->selbox.y0 + d->selbox.y1) / 2.0);
  xform_selection(d, &t);
  CHECK(d->selbox.x1 - d->selbox.x0 >= 15 && d->selbox.x1 - d->selbox.x0 <= 16 && d->selbox.y1 - d->selbox.y0 == 30, "scaled one and a half times");
  doc_undo(d); doc_undo(d); doc_undo(d); doc_undo(d);
  CHECK(d->selbox.x0 == 5 && d->selbox.y0 == 5 && d->selbox.x1 == 25 && d->selbox.y1 == 15, "undone");
  free(px);
  doc_free(d);
}

/* ------------------------------------------------------------------ selection tools */
static int fractional(Doc *d)
{
  long k, n = (long)d->w * d->h, c = 0;
  if (!d->sel) return 0;
  for (k = 0; k < n; k++) if (d->sel[k] > 0 && d->sel[k] < 255) c++;
  return (int)(c > 1000000 ? 1000000 : c);
}
static void test_seltools(void)
{
  Doc *d = doc_new(60, 50, 0xFFFFFF, 0);
  double tri[6] = { 5, 5, 50, 12, 20, 45 };
  i32 x, y;
  Box b;
  /* marquee styles */
  b = marquee_box(MQ_NORMAL, 0, 0, 10, 10, 30, 20, 0, 0);
  CHECK(b.x0 == 10 && b.y0 == 10 && b.x1 == 30 && b.y1 == 20, "normal marquee");
  b = marquee_box(MQ_NORMAL, 0, 0, 10, 10, 4, 30, 1, 0);
  CHECK(b.x0 == -10 && b.x1 == 10 && b.y0 == 10 && b.y1 == 30, "square, to the left");
  b = marquee_box(MQ_FIXED_RATIO, 16, 9, 0, 0, 32, 5, 0, 0);
  CHECK(b.x1 == 32 && b.y1 == 18, "fixed ratio 16:9");
  b = marquee_box(MQ_FIXED_SIZE, 64, 48, 100, 100, 50, 150, 0, 0);
  CHECK(b.x0 == 36 && b.x1 == 100 && b.y0 == 100 && b.y1 == 148, "fixed size on the pointer's side");
  b = marquee_box(MQ_FIXED_SIZE, 64, 48, 100, 100, 0, 0, 0, 1);
  CHECK(b.x0 == 68 && b.y0 == 76 && b.x1 == 132 && b.y1 == 124, "fixed size from the centre");
  /* single row and column */
  sel_single_row(d, 7, SEL_REPLACE);
  CHECK(d->sel && d->selbox.y0 == 7 && d->selbox.y1 == 8 && d->selbox.x0 == 0 && d->selbox.x1 == 60, "Single Row");
  sel_single_col(d, 9, SEL_ADD);
  CHECK(d->selbox.x0 == 0 && d->selbox.y1 == 50 && sel_v(d, 9, 40) == 255 && sel_v(d, 10, 40) == 0, "Single Column added");
  /* the lasso, with and without anti-aliasing */
  sel_polygon_ex(d, tri, 3, SEL_REPLACE, 0, 1);
  CHECK(fractional(d) > 20, "anti-aliased lasso: fractional edges");
  sel_polygon_ex(d, tri, 3, SEL_REPLACE, 0, 0);
  CHECK(d->sel && fractional(d) == 0, "without: hard edges");
  sel_ellipse_ex(d, 5, 5, 40, 30, SEL_REPLACE, 0, 0);
  CHECK(d->sel && fractional(d) == 0, "hard elliptical marquee");
  sel_ellipse_ex(d, 5, 5, 40, 30, SEL_REPLACE, 0, 1);
  CHECK(fractional(d) > 20, "anti-aliased elliptical marquee");
  /* the wand on a hard-edged disc */
  for (y = 0; y < 50; y++) for (x = 0; x < 60; x++) if ((x - 30) * (x - 30) + (y - 25) * (y - 25) < 225) put(d, 0, x, y, 0x2040A0, 255);
  sel_wand_ex(d, 30, 25, 10, 1, 0, SEL_REPLACE, 1);
  CHECK(d->sel && fractional(d) > 20 && sel_v(d, 30, 25) == 255 && sel_v(d, 2, 2) == 0, "anti-aliased wand");
  sel_wand_ex(d, 30, 25, 10, 1, 0, SEL_REPLACE, 0);
  CHECK(d->sel && fractional(d) == 0 && sel_v(d, 30, 25) == 255 && sel_v(d, 44, 25) == 255 && sel_v(d, 45, 25) == 0, "wand without: hard");
  doc_free(d);
}
static void test_color_range(void)
{
  static const u32 cols[11] = { 0xE01010, 0xE0E010, 0x10C010, 0x10D0D0, 0x1010E0, 0xD010D0, 0xFFFFFF, 0x808080, 0x000000, 0xE0AC8C, 0x7F7F7F };
  /* where each preset should be near full: indices into cols */
  static const int want[CR_N] = { -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  Doc *d = doc_new(110, 4, 0, 0);
  ColorRange cr;
  int p, i, ok;
  u8 *m;
  for (i = 0; i < 11; i++) fill_box(d, 0, box_make(i * 10, 0, i * 10 + 10, 4), cols[i], 255);
  memset(&cr, 0, sizeof cr);
  for (p = CR_REDS; p < CR_N; p++) {
    cr.preset = p;
    m = color_range_mask(d, &cr);
    for (ok = 1, i = 0; i < 11; i++) {
      int v = m[i * 10 + 5];
      /* hue ranges against the colours (skin is orange-red, so partly red and yellow); tone
         ranges against the greys (colours have tones too) */
      if (p <= CR_MAGENTAS && i >= 9) continue;
      if (p >= CR_HIGHLIGHTS && p <= CR_SHADOWS && (i < 6 || i == 9)) continue;
      if (p == CR_MIDTONES && i == 10) continue;
      if (i == want[p] ? v < 200 : v > 60) { ok = 0; printf("%s: stripe %d is %d\n", color_range_names[p], i, v); }
    }
    CHECK(ok, color_range_names[p]);
    free(m);
  }
  /* sampled colours, fuzziness, minus, invert */
  cr.preset = CR_SAMPLED; cr.ncolors = 1; cr.colors[0] = 0x808080; cr.fuzziness = 40;
  m = color_range_mask(d, &cr);
  CHECK(m[75] == 255 && m[105] > 200 && m[65] == 0 && m[5] == 0, "sampled grey with fuzziness");
  free(m);
  cr.nminus = 1; cr.minus[0] = 0x7F7F7F; cr.fuzziness = 4;
  m = color_range_mask(d, &cr);
  CHECK(m[75] < 200 && m[105] == 0, "minus a colour");
  free(m);
  cr.nminus = 0; cr.invert = 1;
  m = color_range_mask(d, &cr);
  CHECK(m[75] == 0 && m[5] == 255, "inverted");
  free(m);
  cr.invert = 0; cr.preset = CR_BLUES;
  sel_color_range_ex(d, &cr, SEL_REPLACE);
  CHECK(d->sel && d->selbox.x0 == 40 && d->selbox.x1 == 50, "Color Range made the selection");
  doc_free(d);
}

/* ------------------------------------------------------------------ the Magnetic Lasso */
/* how far the farthest of the points is from the circle */
static double off_circle(const double *p, int n, double cx, double cy, double r)
{
  double m = 0;
  int i;
  for (i = 0; i < n; i++) { double dd = fabs(sqrt((p[2 * i] - cx) * (p[2 * i] - cx) + (p[2 * i + 1] - cy) * (p[2 * i + 1] - cy)) - r); if (dd > m) m = dd; }
  return m;
}
static void test_maglasso(void)
{
  Doc *d = doc_new(200, 150, 0xFFFFFF, 0);
  MagLasso *ml;
  double *pts, *poly, cx = 100, cy = 75, R = 40, worst = 0;
  int n, i, laid = 0;
  i32 x, y;
  long area = 0, k;
  for (y = 0; y < 150; y++) for (x = 0; x < 200; x++) if ((x + 0.5 - cx) * (x + 0.5 - cx) + (y + 0.5 - cy) * (y + 0.5 - cy) <= R * R) put(d, 0, x, y, 0x101010, 255);
  /* a little noise, so it isn't too easy */
  for (k = 0; k < 200 * 150; k++) { u8 *p = d->lay[0]->px + k * 4; int v = (int)(rn() % 21) - 10; p[0] = (u8)(p[0] + v < 0 ? 0 : p[0] + v > 255 ? 255 : p[0] + v); }
  ml = maglasso_new(d, 6, 10, 30, 0);
  CHECK(ml != NULL, "a magnetic lasso");
  maglasso_anchor(ml, cx + R + 3, cy);
  for (i = 1; i <= 72; i++) {
    /* the pointer wanders 2-4 pixels off the edge, inside and out */
    double a = i * 5 * M_PI / 180, rr = R + (i % 3 == 0 ? -3 : 3);
    laid += maglasso_track(ml, cx + rr * cos(a), cy + rr * sin(a), &pts, &n);
    if (n > 0) { double o = off_circle(pts, n, cx, cy, R); if (o > worst) worst = o; }
    if (i % 18 == 0 && i < 72) maglasso_anchor(ml, cx + rr * cos(a), cy + rr * sin(a));
  }
  CHECK(laid > 3, "anchors laid automatically");
  CHECK(worst <= 2.0, "the live path keeps to the edge");
  maglasso_points(ml, &pts, &n);
  CHECK(n > 100 && off_circle(pts, n, cx, cy, R) <= 2.0, "the committed path keeps within 2 pixels of the circle");
  if (off_circle(pts, n, cx, cy, R) > 2.0) printf("worst %.2f\n", off_circle(pts, n, cx, cy, R));
  i = maglasso_anchors(ml);
  CHECK(maglasso_unanchor(ml) && maglasso_anchors(ml) == i - 1, "Backspace takes an anchor away");
  poly = maglasso_close(ml, &n);
  CHECK(poly && n > 100 && off_circle(poly, n, cx, cy, R) <= 2.0, "closed along the edge");
  sel_polygon(d, poly, n, SEL_REPLACE, 0);
  for (k = 0; d->sel && k < 200 * 150; k++) area += d->sel[k];
  area /= 255;
  CHECK(fabs(area - M_PI * R * R) < 0.06 * M_PI * R * R, "the selection is the disc");
  free(poly);
  maglasso_free(ml);
  /* a square, all layers, frequency off */
  {
    Doc *s = doc_new(120, 100, 0xF0F0F0, 0);
    double sq[5][2] = { { 30, 20 }, { 90, 20 }, { 90, 80 }, { 30, 80 }, { 30, 20 } };
    int seg, ok = 1;
    fill_box(s, 0, box_make(30, 20, 90, 80), 0x203040, 255);
    ml = maglasso_new(s, 5, 20, 0, 1);
    maglasso_anchor(ml, 31, 23);
    for (seg = 0; seg < 4; seg++) {
      int st;
      for (st = 1; st <= 10; st++) {
        double px = sq[seg][0] + (sq[seg + 1][0] - sq[seg][0]) * st / 10.0 + (seg == 0 ? 0 : seg == 2 ? 0 : (seg == 1 ? 3 : -3)),
               py = sq[seg][1] + (sq[seg + 1][1] - sq[seg][1]) * st / 10.0 + (seg == 0 ? -3 : seg == 2 ? 3 : 0);
        maglasso_track(ml, px, py, &pts, &n);
        if (st == 10 && seg < 3) maglasso_anchor(ml, px, py);
      }
    }
    poly = maglasso_close(ml, &n);
    for (i = 0; poly && i < n; i++) {
      double px = poly[2 * i], py = poly[2 * i + 1];
      double dd = fabs(px - 30), e2 = fabs(px - 90), e3 = fabs(py - 20), e4 = fabs(py - 80);
      if (e2 < dd) dd = e2;
      if (e3 < dd) dd = e3;
      if (e4 < dd) dd = e4;
      if (dd > 1.5) ok = 0;
    }
    CHECK(poly && n > 100 && ok, "the path follows the square's edges within 1.5 pixels");
    free(poly);
    maglasso_free(ml);
    doc_free(s);
  }
  doc_free(d);
}

/* ------------------------------------------------------------------ crop, straighten, ruler */
static void test_crop(void)
{
  Doc *d = doc_new(80, 60, 0xFFFFFF, 0);
  RulerInfo ri;
  Box b;
  u8 *o;
  i32 x, y;
  fill_random(d, 0, 1);
  o = dup(d->lay[0]->px, 80 * 60 * 4);
  /* presets */
  CHECK(ncrop_presets > 10 && fabs(crop_preset_ratio(d, &crop_presets[1]) - 80.0 / 60) < 1e-9, "Original Ratio");
  b = crop_preset_box(d, &crop_presets[7], 0, 0, 0, 32, 4, 0);
  CHECK(b.x1 == 32 && b.y1 == 18, "16:9 crop box");
  b = crop_preset_box(d, &crop_presets[7], 1, 0, 0, 9, 4, 0);
  CHECK(b.x1 == 9 && b.y1 == 16, "swapped to 9:16");
  CHECK(image_crop_preset(d, box_make(10, 10, 50, 42), &crop_presets[9], 0, RS_BILINEAR) && d->w == 320 && d->h == 256, "crop to 320 x 256");
  doc_undo(d);
  CHECK(d->w == 80 && d->h == 60 && !memcmp(d->lay[0]->px, o, 80 * 60 * 4), "one undo");
  /* a rotated crop at no angle is a plain crop */
  CHECK(image_crop_rotated(d, 40, 30, 20, 10, 0, RS_BILINEAR) && d->w == 20 && d->h == 10, "rotated crop");
  {
    int m = 0, k;
    for (y = 0; y < 10; y++) for (x = 0; x < 20; x++) for (k = 0; k < 4; k++) {
      int v = abs(px_at(d, 0, x, y)[k] - o[((long)(y + 25) * 80 + x + 30) * 4 + k]);
      if (v > m) m = v;
    }
    CHECK(m <= 1, "at 0 degrees it is the plain crop");
  }
  doc_undo(d);
  /* 90 degrees */
  CHECK(image_crop_rotated(d, 40, 30, 20, 10, 90, RS_BILINEAR) && d->w == 20 && d->h == 10, "rotated crop at 90");
  {
    /* new (x, y) comes from old centre + R(90)(x - 10, y - 5): old x = 40 - (y + .5 - 5), old y = 30 + (x + .5 - 10) */
    const u8 *v = px_at(d, 0, 3, 2);
    const u8 *w = o + ((long)(30 + 3 - 10) * 80 + (40 - 2 + 5 - 1)) * 4;
    CHECK(maxdiff(v, w, 4) <= 1, "turned the right way");
  }
  doc_undo(d);
  /* straightening a line tilted 8 degrees */
  {
    Doc *s = doc_new(200, 140, 0xFFFFFF, 0);
    double a = 8 * M_PI / 180, x0 = 20, y0 = 50, x1 = 180, y1 = 50 + 160 * tan(a);
    int ok;
    shape_line(s, x0, y0, x1, y1, 4, 0x000000, 100);
    CHECK(fabs(straighten_angle(x0, y0, x1, y1) - 8) < 1e-9 && fabs(straighten_angle(0, 0, -3, 40) - straighten_angle(0, 0, -3, 40)) < 1e-9, "the angle to straighten");
    CHECK(fabs(straighten_angle(0, 0, 1, 10) - (atan2(10, 1) * 180 / M_PI - 90)) < 1e-9, "nearer vertical: to upright");
    ok = image_straighten(s, x0, y0, x1, y1);
    CHECK(ok && s->w < 200 && s->h < 140, "straightened and cropped");
    {
      /* the line's middle row at a quarter and three quarters across */
      double cyl = 0, cyr = 0, wl = 0, wr = 0;
      i32 xl = s->w / 4, xr = s->w * 3 / 4;
      for (y = 0; y < s->h; y++) {
        double dl = 255 - px_at(s, 0, xl, y)[0], dr = 255 - px_at(s, 0, xr, y)[0];
        cyl += dl * y; wl += dl; cyr += dr * y; wr += dr;
      }
      CHECK(wl > 0 && wr > 0 && fabs(cyl / wl - cyr / wr) < 0.75, "the line is level");
    }
    {
      /* no empty corners */
      CHECK(px_at(s, 0, 0, 0)[3] == 255 && px_at(s, 0, s->w - 1, s->h - 1)[3] == 255, "no empty corners");
    }
    doc_undo(s);
    CHECK(s->w == 200 && s->h == 140, "undo of Straighten");
    CHECK(layer_straighten(s, x0, y0, x1, y1, RS_BILINEAR) && s->w == 200 && !strcmp(doc_undo_name(s), "Straighten Layer"), "Straighten Layer keeps the canvas");
    doc_free(s);
  }
  /* a layer mask and a group go through crops and canvas changes (their buffers the new size) */
  {
    Doc *s = doc_new(30, 20, 0x336699, 0);
    u8 *c;
    Layer *l = layer_new(s, "masked");
    fill_box(s, 0, box_make(0, 0, 30, 20), 0x336699, 255);
    doc_add_layer(s, l, 1);
    fill_box(s, 1, box_make(5, 5, 25, 15), 0xFF0000, 255);
    s->active = 1;
    doc_mask_add(s, 1, MK_REVEAL_ALL);
    s->lay[1]->mask[7 * 30 + 10] = 0;
    doc_new_group(s, "group");
    CHECK(canvas_resize(s, 50, 40, 4, 0xFFFFFF) && image_crop(s, box_make(5, 5, 45, 35)), "canvas grown and cropped with a mask and a group");
    CHECK(s->lay[1]->mask && s->lay[1]->mask[(7 + 10 - 5) * 40 + (10 + 10 - 5)] == 0 && s->lay[1]->mask[0] == 255, "the mask went with them");
    CHECK(image_rotate_angle(s, 15) && image_resize(s, 33, 27, RS_BICUBIC), "turned and resized");
    c = (u8 *)malloc((size_t)s->w * s->h * 4);
    doc_composite(s, doc_box(s), c, (long)s->w * 4);
    free(c);
    doc_undo(s); doc_undo(s); doc_undo(s); doc_undo(s);
    CHECK(s->w == 30 && s->h == 20 && s->lay[1]->mask[7 * 30 + 10] == 0, "all undone");
    doc_free(s);
  }
  /* the Ruler */
  ruler_measure(10, 10, 40, -20, &ri);
  CHECK(fabs(ri.length - sqrt(1800.0)) < 1e-9 && fabs(ri.angle - 45) < 1e-9 && ri.w == 30 && ri.h == -30, "ruler");
  CHECK(fabs(ruler_protractor(0, 0, 10, 0, 0, 10) - 90) < 1e-9, "protractor");
  free(o);
  doc_free(d);
}

/* ------------------------------------------------------------------ matting, Alt-drag */
static void test_matte(void)
{
  Doc *d = doc_new(40, 40, 0, 1);
  i32 x, y;
  int ok = 1, before = 0;
  Box b;
  for (y = 0; y < 40; y++) for (x = 0; x < 40; x++) {
    double r = sqrt((x + 0.5 - 20) * (x + 0.5 - 20) + (y + 0.5 - 20) * (y + 0.5 - 20));
    if (r < 12) put(d, 0, x, y, r < 10 ? 0xE00000 : 0x00E000, 255);
  }
  for (y = 0; y < 40; y++) for (x = 0; x < 40; x++) if (px_at(d, 0, x, y)[1] == 0xE0) before++;
  b = layer_defringe(d, 2);
  CHECK(!box_empty(b) && before > 20, "Defringe");
  for (y = 0; y < 40; y++) for (x = 0; x < 40; x++) {
    const u8 *p = px_at(d, 0, x, y);
    if (p[3] && (p[0] < 180 || p[1] > 60)) ok = 0;
  }
  CHECK(ok, "the green halo is gone (red to the edge)");
  doc_undo(d);
  CHECK(px_at(d, 0, 20, 31)[1] == 0xE0, "undo of Defringe");
  /* mattes */
  put(d, 0, 1, 1, 0x800000, 128);
  layer_remove_black_matte(d);
  CHECK(px_at(d, 0, 1, 1)[0] == 255, "Remove Black Matte");
  put(d, 0, 2, 1, 0xFF7F7F, 128);
  layer_remove_white_matte(d);
  CHECK(px_at(d, 0, 2, 1)[0] == 255 && px_at(d, 0, 2, 1)[1] <= 1, "Remove White Matte");
  doc_free(d);
}
static void test_move_copy(void)
{
  Doc *d = doc_new(40, 30, 0xFFFFFF, 0);
  u8 *o;
  fill_box(d, 0, box_make(5, 5, 10, 10), 0x0000FF, 255);
  o = dup(d->lay[0]->px, 40 * 30 * 4);
  sel_rect(d, 5, 5, 10, 10, SEL_REPLACE, 0);
  move_selection_copy(d, 15, 3);
  CHECK(px_at(d, 0, 7, 7)[2] == 255 && px_at(d, 0, 7, 7)[0] == 0, "the original stays");
  CHECK(px_at(d, 0, 22, 10)[2] == 255 && px_at(d, 0, 22, 10)[0] == 0 && px_at(d, 0, 26, 12)[0] == 255, "the copy is there");
  CHECK(d->selbox.x0 == 20 && d->selbox.y0 == 8, "the selection is on the copy");
  doc_undo(d);
  CHECK(!memcmp(d->lay[0]->px, o, 40 * 30 * 4) && d->selbox.x0 == 5, "undone");
  sel_none(d);
  move_selection_copy(d, 4, 0);
  CHECK(d->nlay == 2 && d->active == 1 && px_at(d, 1, 10, 7)[2] == 255 && px_at(d, 0, 7, 7)[2] == 255, "no selection: the layer duplicated and moved");
  doc_undo(d);
  CHECK(d->nlay == 1, "one undo");
  free(o);
  doc_free(d);
}

int main(void)
{
  test_channels();
  test_quickmask();
  test_transforms();
  test_transform_selection();
  test_seltools();
  test_color_range();
  test_maglasso();
  test_crop();
  test_matte();
  test_move_copy();
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
