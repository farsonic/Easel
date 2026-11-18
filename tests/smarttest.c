/*
 * smarttest.c - Easel 1.2's content-aware fill (core/cafill.c: PatchMatch, Edit > Content-Aware Fill,
 * the Remove tool's and Spot Healing's Content-Aware type, the Patch tool's Destination and
 * Content-Aware modes, Content-Aware Move) and the Quick Selection and Object Selection tools
 * (core/selsmart.c), on pictures made here. Run under ASan/UBSan.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core/doc.h"
#include "core/paint.h"
#include "core/cafill.h"
#include "core/brushx.h"
#include "core/selsmart.h"

static int fails, tests;
#define CHECK(c, msg) do { tests++; if (!(c)) { fails++; printf("FAIL %s (line %d)\n", msg, __LINE__); } } while (0)

/* vertical stripes 4 pixels wide, dark and light */
static void stripes(u8 *px, i32 w, i32 h)
{
  i32 x, y;
  for (y = 0; y < h; y++) for (x = 0; x < w; x++) { u8 *p = px + ((long)y * w + x) * 4; p[0] = p[1] = p[2] = (u8)((x / 4) % 2 ? 220 : 40); p[3] = 255; }
}
static double err_in(const u8 *a, const u8 *b, i32 w, Box r)
{
  double e = 0;
  long n = 0;
  i32 x, y;
  for (y = r.y0; y < r.y1; y++) for (x = r.x0; x < r.x1; x++) { e += abs(a[((long)y * w + x) * 4] - b[((long)y * w + x) * 4]); n++; }
  return n ? e / n : 0;
}
static int aborter(long done, long total) { (void)total; return done > 3; }

static void test_fill(void)
{
  i32 w = 80, h = 60, x, y;
  u8 *truth = (u8 *)malloc((size_t)w * h * 4), *img = (u8 *)malloc((size_t)w * h * 4), *hole = (u8 *)calloc((size_t)w * h, 1);
  Box hb = box_make(30, 20, 46, 36);
  CaOpts o;
  double e, emean;
  stripes(truth, w, h);
  memcpy(img, truth, (size_t)w * h * 4);
  for (y = hb.y0; y < hb.y1; y++) for (x = hb.x0; x < hb.x1; x++) { hole[(long)y * w + x] = 255; img[((long)y * w + x) * 4] = img[((long)y * w + x) * 4 + 1] = img[((long)y * w + x) * 4 + 2] = 255; }
  memset(&o, 0, sizeof o); o.seed = 1;
  CHECK(ca_fill_rgba(img, w, h, hole, NULL, &o), "fill");
  e = err_in(img, truth, w, hb);
  emean = 90;                                               /* (a flat grey would be off by 90 on average) */
  CHECK(e < emean / 3, "the stripes go on through the hole");
  {
    /* outside the hole nothing changed */
    int same = 1;
    long k;
    for (k = 0; k < (long)w * h; k++) if (!hole[k] && memcmp(img + k * 4, truth + k * 4, 4)) same = 0;
    CHECK(same, "only the hole changes");
  }
  /* allowed area: only from the right side (all light there) */
  {
    u8 *allow = (u8 *)calloc((size_t)w * h, 1);
    long k;
    int light = 1;
    for (y = 0; y < h; y++) for (x = 60; x < w; x++) allow[(long)y * w + x] = 255;
    for (y = 0; y < h; y++) for (x = 60; x < w; x++) { u8 *p = truth + ((long)y * w + x) * 4; p[0] = p[1] = p[2] = 200; }
    memcpy(img, truth, (size_t)w * h * 4);
    CHECK(ca_fill_rgba(img, w, h, hole, allow, &o), "fill from an allowed area");
    for (k = 0; k < (long)w * h; k++) if (hole[k] && img[k * 4] != 200) light = 0;
    CHECK(light, "taken only from there");
    free(allow);
  }
  /* Abort */
  progress_hook = aborter;
  memcpy(img, truth, (size_t)w * h * 4);
  CHECK(!ca_fill_rgba(img, w, h, hole, NULL, &o), "aborted");
  progress_hook = NULL; progress_reset();
  /* nothing to take from */
  memset(hole, 255, (size_t)w * h);
  CHECK(!ca_fill_rgba(img, w, h, hole, NULL, &o), "all hole: refused");
  free(truth); free(img); free(hole);
}

static void test_doc_tools(void)
{
  Doc *d = doc_new(80, 60, 0, 0);
  u8 *truth = (u8 *)malloc((size_t)80 * 60 * 4);
  Box r;
  CaOpts o;
  i32 x, y;
  stripes(d->lay[0]->px, 80, 60);
  memcpy(truth, d->lay[0]->px, (size_t)80 * 60 * 4);
  /* a blot, selected, filled */
  for (y = 20; y < 30; y++) for (x = 30; x < 40; x++) { u8 *p = d->lay[0]->px + ((long)y * 80 + x) * 4; p[0] = 255; p[1] = 0; p[2] = 0; }
  sel_rect(d, 29, 19, 41, 31, SEL_REPLACE, 0);
  memset(&o, 0, sizeof o);
  r = doc_ca_fill(d, &o);
  CHECK(!box_empty(r) && err_in(d->lay[0]->px, truth, 80, box_make(30, 20, 40, 30)) < 30, "Content-Aware Fill");
  CHECK(!strcmp(doc_undo_name(d), "Content-Aware Fill"), "its step");
  doc_undo(d);
  CHECK(d->lay[0]->px[((long)25 * 80 + 35) * 4 + 1] == 0, "undo");
  /* the Remove tool / Spot Healing Content-Aware: a stroke over the blot */
  cafill_init();
  sel_none(d);
  {
    Brush b;
    Stroke *s;
    memset(&b, 0, sizeof b);
    b.tool = PT_SPOTHEAL; b.size = 14; b.hardness = 100; b.opacity = 100; b.flow = 100; b.spacing = 25; b.spot_type = SPOT_CONTENT;
    s = stroke_begin(d, &b, 0, 35, 22, NULL);
    stroke_to(s, 35, 28, NULL);
    stroke_end(s);
    CHECK(err_in(d->lay[0]->px, truth, 80, box_make(31, 21, 39, 29)) < 40, "Remove: the blot gone");
    doc_undo(d);
  }
  /* Patch, Destination: the blot copied 20 to the right, the selection with it */
  sel_rect(d, 30, 20, 40, 30, SEL_REPLACE, 0);
  r = patch_dest(d, 20, 0, 0);
  CHECK(!box_empty(r) && d->lay[0]->px[((long)25 * 80 + 55) * 4] > 150 && d->lay[0]->px[((long)25 * 80 + 55) * 4 + 1] < 150, "patch destination: the blot healed in there");
  CHECK(sel_at(d, 55, 25) == 255 && sel_at(d, 35, 25) == 0, "the selection went with it");
  doc_undo(d);
  /* Patch, Transparent: only the texture */
  sel_rect(d, 30, 20, 40, 30, SEL_REPLACE, 0);
  patch_dest(d, 20, 0, 1);
  doc_undo(d);
  /* Patch, Content-Aware: the blot filled from the stripes 20 to the left */
  sel_rect(d, 29, 19, 41, 31, SEL_REPLACE, 0);
  r = patch_content(d, -20, 0, 4, 3);
  CHECK(!box_empty(r) && err_in(d->lay[0]->px, truth, 80, box_make(30, 20, 40, 30)) < 40, "patch content-aware");
  doc_undo(d);
  /* Content-Aware Move: the blot to the right, its place filled */
  sel_rect(d, 30, 20, 40, 30, SEL_REPLACE, 0);
  r = doc_ca_move(d, 24, 0, 0, 4, 0);
  CHECK(!box_empty(r), "content-aware move");
  CHECK(d->lay[0]->px[((long)25 * 80 + 58) * 4 + 1] < 60 && d->lay[0]->px[((long)25 * 80 + 58) * 4] > 200, "the blot moved");
  CHECK(err_in(d->lay[0]->px, truth, 80, box_make(31, 21, 39, 29)) < 40, "its place filled");
  CHECK(sel_at(d, 58, 25) == 255 && sel_at(d, 35, 25) == 0, "the selection is what moved");
  doc_undo(d);
  /* Extend: copied, the original kept */
  sel_rect(d, 30, 20, 40, 30, SEL_REPLACE, 0);
  doc_ca_move(d, 24, 0, 1, 4, 5);
  CHECK(d->lay[0]->px[((long)25 * 80 + 35) * 4 + 1] == 0 && d->lay[0]->px[((long)25 * 80 + 59) * 4 + 1] < 80, "extend keeps the original");
  free(truth);
  doc_free(d);
}

/* a red disc (radius 15 at 40, 30) on a blue to cyan ground */
static Doc *disc_doc(void)
{
  Doc *d = doc_new(80, 60, 0, 0);
  i32 x, y;
  for (y = 0; y < 60; y++) for (x = 0; x < 80; x++) {
    u8 *p = d->lay[0]->px + ((long)y * 80 + x) * 4;
    double dd = sqrt((x + 0.5 - 40) * (x + 0.5 - 40) + (y + 0.5 - 30) * (y + 0.5 - 30));
    if (dd < 15) { p[0] = (u8)(200 + (x % 3) * 10); p[1] = 30; p[2] = 30; }
    else { p[0] = 20; p[1] = (u8)(80 + x); p[2] = 200; }
    p[3] = 255;
  }
  return d;
}
static void sel_score(Doc *d, int *in, int *out)
{
  i32 x, y;
  *in = *out = 0;
  for (y = 0; y < 60; y++) for (x = 0; x < 80; x++) {
    double dd = sqrt((x + 0.5 - 40) * (x + 0.5 - 40) + (y + 0.5 - 30) * (y + 0.5 - 30));
    int s = sel_at(d, x, y) > 127;
    if (dd < 14 && s) (*in)++;
    if (dd > 16 && s) (*out)++;
  }
}
static void test_selections(void)
{
  Doc *d = disc_doc();
  double pts[6] = { 36, 30, 40, 28, 44, 31 };
  int in, out, total = 0;
  i32 x, y;
  for (y = 0; y < 60; y++) for (x = 0; x < 80; x++) if ((x + 0.5 - 40) * (x + 0.5 - 40) + (y + 0.5 - 30) * (y + 0.5 - 30) < 14 * 14) total++;
  quick_select(d, pts, 3, 3, SEL_REPLACE, 0, 0);
  sel_score(d, &in, &out);
  CHECK(in > total * 9 / 10 && out < 10, "Quick Selection finds the disc");
  CHECK(!strcmp(doc_undo_name(d), "Quick Selection"), "its step");
  /* taking away: a stroke in the ground leaves the disc */
  sel_all(d);
  {
    double p2[4] = { 8, 8, 70, 50 };
    quick_select(d, p2, 2, 4, SEL_SUBTRACT, 0, 1);
    CHECK(sel_at(d, 8, 8) < 128 && sel_at(d, 40, 30) == 255, "Quick Selection subtracts the ground");
  }
  sel_none(d);
  CHECK(object_select(d, box_make(20, 10, 60, 50), SEL_REPLACE, 0, 1), "Object Selection");
  sel_score(d, &in, &out);
  CHECK(in > total * 9 / 10 && out < 15, "the disc found in the box");
  CHECK(sel_at(d, 40, 30) == 255 && sel_at(d, 22, 12) == 0, "middle in, corner out");
  /* the soft edge */
  sel_none(d);
  object_select(d, box_make(20, 10, 60, 50), SEL_REPLACE, 0, 0);
  {
    int soft = 0;
    for (x = 0; x < 80; x++) if (sel_at(d, x, 30) > 0 && sel_at(d, x, 30) < 255) soft++;
    CHECK(soft >= 2, "soft edge without Hard Edge");
  }
  /* on a transparent layer: the object is what is opaque */
  {
    Doc *e = doc_new(60, 60, 0, 1);
    for (y = 20; y < 40; y++) for (x = 20; x < 40; x++) { u8 *p = e->lay[0]->px + ((long)y * 60 + x) * 4; p[0] = 10; p[1] = 200; p[2] = 10; p[3] = 255; }
    CHECK(object_select(e, box_make(10, 10, 50, 50), SEL_REPLACE, 0, 1) && sel_at(e, 30, 30) == 255 && sel_at(e, 15, 15) == 0, "object on a transparent layer");
    doc_free(e);
  }
  doc_free(d);
}

int main(void)
{
  test_fill();
  test_doc_tools();
  test_selections();
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
