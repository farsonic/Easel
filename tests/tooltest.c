/*
 * tooltest.c - native tests of Easel 1.1's tool options (core/toolopts.h and the stroke engine):
 * Dodge and Burn's ranges and Protect Tones, the Sponge's Saturate and Vibrance, the Clone Stamp's
 * Mode and Current & Below sampling, the Eyedropper's sizes and layers, Erase to History, Finger
 * Painting, Sharpen's Protect Detail, the retouching tools' Mode, the Crop overlays and the Move
 * tool's Auto-Select Group.
 *
 * Built and run by tools/test.sh (linked with the whole core); run from the repository root.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../core/paint.h"
#include "../core/layers.h"
#include "../core/toolopts.h"
#include "../core/gradient.h"

static int tests, failed;
static void ok(const char *name, int cond) { tests++; if (!cond) { failed++; printf("FAIL %s\n", name); } }
static u8 *px(Doc *d, int layer, i32 x, i32 y) { return d->lay[layer]->px + ((long)y * d->w + x) * 4; }
static Brush brush(int tool, int size, int hard, int opacity)
{
  Brush b;
  memset(&b, 0, sizeof b);
  b.tool = tool; b.size = size; b.hardness = hard; b.opacity = opacity; b.flow = 100; b.spacing = 25; b.strength = 50; b.bg = 0xFFFFFF;
  return b;
}
static void line(Doc *d, const Brush *b, u32 c, double x0, double y0, double x1, double y1)
{
  Box ch;
  Stroke *s = stroke_begin(d, b, c, x0, y0, &ch);
  if (s) { stroke_to(s, x1, y1, &ch); stroke_end(s); }
}
static void fill_layer(Doc *d, int l, u32 c, int a)
{
  long k;
  for (k = 0; k < (long)d->w * d->h; k++) {
    u8 *p = d->lay[l]->px + k * 4;
    p[0] = (u8)(c >> 16); p[1] = (u8)(c >> 8); p[2] = (u8)c; p[3] = (u8)a;
  }
}
static int chroma(const u8 *p)
{
  int lo = p[0], hi = p[0], k;
  for (k = 1; k < 3; k++) { if (p[k] < lo) lo = p[k]; if (p[k] > hi) hi = p[k]; }
  return hi - lo;
}

/* ------------------------------------------------------------------ the curves */
static void curves(void)
{
  u8 lut[256], all[256];
  u8 p[3], q[3];
  int v, mono;
  tone_lut(lut, TR_MIDTONES, 0, 50);
  ok("midtones dodge keeps black and white", lut[0] == 0 && lut[255] == 255);
  ok("midtones dodge lifts the middle most", lut[128] - 128 > lut[32] - 32 && lut[128] - 128 > lut[224] - 224 && lut[128] > 140);
  for (mono = 1, v = 1; v < 256; v++) if (lut[v] < lut[v - 1]) mono = 0;
  ok("dodge curve rises", mono);
  tone_lut(lut, TR_MIDTONES, 1, 50);
  ok("midtones burn darkens the middle", lut[128] < 118 && lut[0] == 0 && lut[255] == 255);
  tone_lut(lut, TR_SHADOWS, 0, 50);
  ok("shadows dodge lifts the darks most", lut[30] - 30 > lut[200] - 200 && lut[0] > 0);
  tone_lut(lut, TR_HIGHLIGHTS, 1, 50);
  ok("highlights burn lowers the lights most", 220 - lut[220] > 40 - lut[40] && lut[255] < 255);
  tone_lut(lut, TR_SHADOWS, 1, 0);
  for (mono = 1, v = 0; v < 256; v++) if (lut[v] != v) mono = 0;
  ok("exposure 0 changes nothing", mono);
  /* TR_ALL is Easel 1.0's even change */
  tone_lut(all, TR_ALL, 0, 100);
  ok("TR_ALL dodge as 1.0", abs(all[100] - (100 + (155 * 255) / 400)) <= 1);
  /* Protect Tones keeps the hue: the channels keep their order and roughly their ratio */
  tone_lut(lut, TR_MIDTONES, 0, 80);
  p[0] = 160; p[1] = 80; p[2] = 40;
  memcpy(q, p, 3); tone_apply(q, lut, 255, 1);
  ok("protect tones lightens", q[0] + q[1] + q[2] > 280);
  ok("protect tones keeps the hue", q[0] > q[1] && q[1] > q[2] && fabs((double)q[0] / q[1] - 2.0) < 0.35);
  memcpy(q, p, 3); tone_apply(q, lut, 0, 1);
  ok("no coverage, no change", !memcmp(p, q, 3));
  p[0] = 250; p[1] = 120; p[2] = 10;
  memcpy(q, p, 3); tone_apply(q, lut, 255, 1);
  ok("protect tones compresses rather than clips", q[0] > q[1] && q[1] > q[2]);
  /* the sponge */
  p[0] = 150; p[1] = 110; p[2] = 100;
  memcpy(q, p, 3); tone_sponge(q, 128, 1, 0);
  ok("saturate adds colour", chroma(q) > chroma(p));
  memcpy(q, p, 3); tone_sponge(q, 128, 0, 0);
  ok("desaturate takes it away", chroma(q) < chroma(p));
  p[0] = p[1] = p[2] = 90;
  memcpy(q, p, 3); tone_sponge(q, 255, 1, 1);
  ok("grey stays grey", q[0] == 90 && q[1] == 90 && q[2] == 90);
  {
    u8 dull[3] = { 140, 120, 110 }, vivid[3] = { 230, 40, 30 }, a[3], b[3];
    int gd, gv;
    memcpy(a, dull, 3); tone_sponge(a, 100, 1, 1);
    memcpy(b, vivid, 3); tone_sponge(b, 100, 1, 1);
    gd = chroma(a) - chroma(dull); gv = chroma(b) - chroma(vivid);
    ok("vibrance saturates dull colours more than vivid ones", gd * chroma(vivid) > gv * chroma(dull));
    memcpy(a, vivid, 3); tone_sponge(a, 255, 1, 0);
    ok("saturating never leaves the range", chroma(a) >= chroma(vivid));
  }
}

/* ------------------------------------------------------------------ strokes */
/* a 64 x 16 document whose background goes from black to white across */
static Doc *ramp(void)
{
  Doc *d = doc_new(64, 16, 0, 0);
  i32 x, y;
  for (y = 0; y < 16; y++) for (x = 0; x < 64; x++) { u8 *p = px(d, 0, x, y); p[0] = p[1] = p[2] = (u8)(x * 4); p[3] = 255; }
  doc_forget_undo(d);
  return d;
}
static void tone_strokes(void)
{
  Doc *d = ramp();
  Brush b = brush(PT_DODGE, 100, 100, 100);
  int dark, light;
  b.tone_range = TR_SHADOWS;
  line(d, &b, 0, 0, 8, 63, 8);
  dark = px(d, 0, 8, 8)[0] - 32; light = px(d, 0, 56, 8)[0] - 224;
  ok("dodge shadows stroke: the darks move most", dark > light && dark > 4);
  doc_undo(d);
  ok("undone", px(d, 0, 8, 8)[0] == 32);
  b.tool = PT_BURN; b.tone_range = TR_HIGHLIGHTS;
  line(d, &b, 0, 0, 8, 63, 8);
  ok("burn highlights stroke: the lights move most", 224 - px(d, 0, 56, 8)[0] > 32 - px(d, 0, 8, 8)[0] && px(d, 0, 56, 8)[0] < 220);
  doc_free(d);

  d = doc_new(32, 32, 0x967064, 0);
  doc_forget_undo(d);
  b = brush(PT_SPONGE, 60, 100, 100);
  b.sponge_saturate = 1;
  line(d, &b, 0, 0, 16, 31, 16);
  ok("sponge saturate stroke", chroma(px(d, 0, 16, 16)) > 0x96 - 0x64);
  b.sponge_saturate = 0;
  line(d, &b, 0, 0, 16, 31, 16); line(d, &b, 0, 0, 16, 31, 16); line(d, &b, 0, 0, 16, 31, 16);
  ok("sponge desaturate stroke", chroma(px(d, 0, 16, 16)) < 0x96 - 0x64 + 10);
  doc_free(d);
}
static void clone_tools(void)
{
  /* the Clone Stamp's Mode: Darken copies only what is darker */
  Doc *d = doc_new(40, 20, 0x000000, 0);
  Brush b = brush(PT_CLONE, 8, 100, 100);
  i32 x, y;
  int top;
  for (y = 0; y < 20; y++) for (x = 0; x < 20; x++) { u8 *p = px(d, 0, x, y); p[0] = p[1] = p[2] = 255; }
  doc_forget_undo(d);
  b.clone_dx = -20; b.clone_dy = 0;
  b.blend = BM_DARKEN;
  line(d, &b, 0, 30, 10, 31, 10);
  ok("clone in Darken leaves black black", px(d, 0, 30, 10)[0] == 0);
  b.blend = BM_NORMAL;
  line(d, &b, 0, 30, 10, 31, 10);
  ok("clone in Normal copies white", px(d, 0, 30, 10)[0] == 255);
  doc_free(d);

  /* Current & Below: a red bottom layer, the empty active one, a blue one on top */
  d = doc_new(40, 20, 0xFF0000, 0);
  doc_add_layer(d, layer_new(d, "Paint"), -1);
  doc_add_layer(d, layer_new(d, "Top"), -1);
  top = d->nlay - 1;
  fill_layer(d, top, 0x0000FF, 255);
  d->active = 1;
  doc_forget_undo(d);
  b = brush(PT_CLONE, 8, 100, 100);
  b.clone_dx = -20;
  b.sample_all = SAMPLE_BELOW;
  line(d, &b, 0, 30, 10, 31, 10);
  ok("clone current & below takes the red", px(d, 1, 30, 10)[0] == 255 && px(d, 1, 30, 10)[2] == 0 && px(d, 1, 30, 10)[3] == 255);
  b.sample_all = SAMPLE_ALL;
  line(d, &b, 0, 30, 4, 31, 4);
  ok("clone all layers takes the blue", px(d, 1, 30, 4)[2] == 255 && px(d, 1, 30, 4)[0] == 0);
  ok("the top layer is shown again", d->lay[top]->visible);
  /* the eyedropper */
  ok("pick all layers", doc_pick_ex(d, 5, 5, SAMPLE_ALL, 1) == 0x0000FF);
  ok("pick current & below", doc_pick_ex(d, 5, 5, SAMPLE_BELOW, 1) == 0xFF0000);
  ok("pick the empty current layer", doc_pick_ex(d, 5, 5, SAMPLE_CURRENT, 1) == 0xFFFFFFFFUL);
  ok("pick the painted current layer", doc_pick_ex(d, 30, 10, SAMPLE_CURRENT, 1) == 0xFF0000);
  doc_free(d);

  /* sample sizes: a black and white checkerboard averages to grey; 101 at a corner is clipped */
  d = doc_new(120, 120, 0, 0);
  for (y = 0; y < 120; y++) for (x = 0; x < 120; x++) { u8 *p = px(d, 0, x, y); p[0] = p[1] = p[2] = (x + y) & 1 ? 255 : 0; }
  ok("point sample", doc_pick_ex(d, 10, 10, SAMPLE_CURRENT, 1) == 0);
  {
    int k, good = 1;
    for (k = 1; k < NPICK_SIZES; k++) {
      u32 c = doc_pick_ex(d, 60, 60, SAMPLE_CURRENT, pick_sizes[k]);
      int g = (int)(c & 0xFF), n = pick_sizes[k] * pick_sizes[k], want = (int)(255L * (n / 2) / n);
      if (abs(g - want) > 1 || (c >> 16) != (c & 0xFF)) good = 0;
    }
    ok("every sample size averages", good);
  }
  ok("a big sample at the corner", doc_pick_ex(d, 0, 0, SAMPLE_ALL, 101) != 0xFFFFFFFFUL);
  ok("outside the picture", doc_pick_ex(d, -200, -200, SAMPLE_ALL, 3) == 0xFFFFFFFFUL);
  doc_free(d);

  /* Current & Below keeps the groups the active layer is in */
  d = doc_new(20, 20, 0xFF0000, 0);
  doc_add_layer(d, layer_new(d, "In group"), -1);
  fill_layer(d, 1, 0x00FF00, 255);
  d->active = 1;
  doc_group_layers(d);
  {
    int i, g = -1, m = -1;
    for (i = 0; i < d->nlay; i++) { if (d->lay[i]->kind == LK_GROUP) g = i; else if (i > 0) m = i; }
    ok("grouped", g >= 0 && m >= 0);
    if (g >= 0 && m >= 0) {
      d->active = m;
      ok("pick below inside a group", doc_pick_ex(d, 3, 3, SAMPLE_BELOW, 1) == 0x00FF00);
      ok("auto-select layer", auto_select_target(d, m, 0) == m);
      ok("auto-select group", auto_select_target(d, m, 1) == g);
      ok("auto-select a layer in no group", auto_select_target(d, 0, 1) == 0);
    }
  }
  doc_free(d);
}
static void eraser_smudge(void)
{
  Doc *d = doc_new(32, 32, 0xFFFFFF, 0);
  Brush b;
  Snapshot *snap;
  doc_add_layer(d, layer_new(d, "Paint"), -1);
  fill_layer(d, 1, 0x3366CC, 255);
  doc_forget_undo(d);
  snap = snapshot_take(d);
  ok("snapshot", snap != NULL);
  b = brush(PT_BRUSH, 40, 100, 100);
  line(d, &b, 0xFF0000, 0, 16, 31, 16);
  ok("painted over", px(d, 1, 16, 16)[0] == 255);
  b = brush(PT_ERASER, 40, 100, 100);
  b.erase_history = 1; b.snapshot = snap;
  line(d, &b, 0, 0, 16, 31, 16);
  ok("erase to history paints the snapshot back", px(d, 1, 16, 16)[0] == 0x33 && px(d, 1, 16, 16)[2] == 0xCC && px(d, 1, 16, 16)[3] == 255);
  b.snapshot = NULL;
  line(d, &b, 0, 0, 16, 31, 16);
  ok("erase to history without a snapshot erases", px(d, 1, 16, 16)[3] == 0);
  snapshot_free(snap);
  doc_free(d);

  /* Finger Painting starts each smudge with the foreground colour */
  d = doc_new(40, 20, 0xFFFFFF, 0);
  doc_forget_undo(d);
  b = brush(PT_SMUDGE, 8, 100, 100);
  b.strength = 80;
  line(d, &b, 0xFF0000, 4, 10, 30, 10);
  ok("smudge alone leaves white white", px(d, 0, 10, 10)[1] == 255);
  b.finger_painting = 1;
  line(d, &b, 0xFF0000, 4, 10, 30, 10);
  ok("finger painting smears the foreground", px(d, 0, 6, 10)[1] < 200 && px(d, 0, 6, 10)[0] > 200);
  doc_free(d);

  /* Sharpen's Protect Detail is gentler; Blur in Darken only darkens */
  {
    Doc *a = ramp(), *c = ramp();
    i32 x, y;
    int maxa = 0, maxc = 0, lighter = 0;
    for (y = 0; y < 16; y++) for (x = 0; x < 64; x++) if ((x / 4 + y / 4) & 1) { px(a, 0, x, y)[0] = px(c, 0, x, y)[0] = 255; }
    b = brush(PT_SHARPEN, 30, 100, 100);
    b.strength = 100;
    line(a, &b, 0, 0, 8, 63, 8);
    b.protect_detail = 1;
    line(c, &b, 0, 0, 8, 63, 8);
    for (x = 0; x < 64; x++) {
      int want = (x / 4 + 2) & 1 ? 255 : x * 4;
      if (abs(px(a, 0, x, 8)[0] - want) > maxa) maxa = abs(px(a, 0, x, 8)[0] - want);
      if (abs(px(c, 0, x, 8)[0] - want) > maxc) maxc = abs(px(c, 0, x, 8)[0] - want);
    }
    ok("protect detail sharpens less", maxc < maxa && maxc > 0);
    doc_free(a); doc_free(c);
    a = ramp();
    for (y = 0; y < 16; y++) for (x = 0; x < 64; x++) { u8 *p = px(a, 0, x, y); p[0] = p[1] = p[2] = (x / 3) & 1 ? 255 : 0; }
    c = ramp();
    memcpy(c->lay[0]->px, a->lay[0]->px, (size_t)64 * 16 * 4);
    b = brush(PT_BLUR, 30, 100, 100);
    b.strength = 100; b.blend = BM_DARKEN;
    line(a, &b, 0, 0, 8, 63, 8);
    for (y = 0; y < 16; y++) for (x = 0; x < 64; x++) if (px(a, 0, x, y)[0] > px(c, 0, x, y)[0]) lighter++;
    ok("blur in Darken never lightens", !lighter && memcmp(a->lay[0]->px, c->lay[0]->px, (size_t)64 * 16 * 4));
    doc_free(a); doc_free(c);
  }
}

/* ------------------------------------------------------------------ the crop overlay */
static void overlays(void)
{
  double s[400];
  int n, k, good;
  n = crop_overlay_lines(CO_THIRDS, 0, 0, 300, 90, 0, s, 100);
  ok("thirds: four lines", n == 4 && fabs(s[0] - 100) < 1e-9 && fabs(s[4] - 200) < 1e-9 && fabs(s[9] - 30) < 1e-9 && fabs(s[13] - 60) < 1e-9);
  n = crop_overlay_lines(CO_GOLDEN, 100, 0, 0, 100, 0, s, 100);
  ok("golden ratio (box given backwards)", n == 4 && fabs(s[0] - 38.1966) < 0.01 && fabs(s[4] - 61.8034) < 0.01);
  n = crop_overlay_lines(CO_GRID, 0, 0, 100, 64, 32, s, 100);
  ok("grid lines", n == 3 + 1);
  ok("grid limited", crop_overlay_lines(CO_GRID, 0, 0, 10000, 10000, 1, s, 100) == 100);
  n = crop_overlay_lines(CO_DIAGONAL, 0, 0, 200, 100, 0, s, 100);
  for (good = n == 4, k = 0; k < n; k++) if (fabs(fabs(s[k * 4 + 2] - s[k * 4]) - fabs(s[k * 4 + 3] - s[k * 4 + 1])) > 1e-9) good = 0;
  ok("diagonals at 45 degrees", good);
  n = crop_overlay_lines(CO_TRIANGLE, 0, 0, 200, 100, 0, s, 100);
  good = n == 3;
  for (k = 1; good && k < 3; k++) {
    double dx = s[k * 4 + 2] - s[k * 4], dy = s[k * 4 + 3] - s[k * 4 + 1];
    if (fabs(dx * 200 + dy * -100) > 1e-6) good = 0;
  }
  ok("triangle: perpendiculars to the diagonal", good);
  ok("no overlay", crop_overlay_lines(CO_NONE, 0, 0, 10, 10, 0, s, 100) == 0);
  ok("empty box", crop_overlay_lines(CO_THIRDS, 5, 5, 5, 9, 0, s, 100) == 0);
}

static void grid_snap(void)
{
  Doc *d = doc_new(100, 100, 0, 0);
  double x = 21, y = 7.9;
  grid_snap_paint(d, &x, &y);
  ok("grid snap, default 16", x == 16.5 && y == 0.5);
  d->guides.grid_x = 10; d->guides.grid_y = 20; d->guides.grid_ox = 3; d->guides.grid_oy = -5;
  x = 21; y = 26; grid_snap_paint(d, &x, &y);
  ok("grid snap with spacing and offset", x == 23.5 && y == 35.5);
  doc_free(d);
}

static void delete_state(void)
{
  Doc *d = doc_new(20, 20, 0xFFFFFF, 0);
  Brush b = brush(PT_BRUSH, 6, 100, 100);
  const char *names[8];
  doc_forget_undo(d);
  line(d, &b, 0xFF0000, 2, 2, 18, 2);
  line(d, &b, 0x00FF00, 2, 10, 18, 10);
  line(d, &b, 0x0000FF, 2, 17, 18, 17);
  ok("three steps", doc_history(d, names, 8) == 3);
  doc_undo(d);
  ok("delete a step past the end refused", !doc_delete_state(d, 3) && !doc_delete_state(d, -5));
  ok("delete the second step", doc_delete_state(d, 1));
  ok("one step left, none to redo", doc_history(d, names, 8) == 1 && !doc_can_redo(d));
  ok("its change gone, the first kept", px(d, 0, 10, 10)[1] == 255 && px(d, 0, 10, 10)[0] == 255 && px(d, 0, 10, 2)[1] == 0);
  ok("delete the first (the last done)", doc_delete_state(d, -2) == 0 && doc_delete_state(d, -1) && !doc_can_undo(d) && px(d, 0, 10, 2)[1] == 255);
  doc_free(d);
}

static void dpaint(void)
{
  u8 pal[256 * 3], rgba[4];
  Gradient g;
  int k, lo, hi;
  u32 sc[256];
  Doc *d;
  Brush b;
  for (k = 0; k < 256; k++) { pal[k * 3] = (u8)k; pal[k * 3 + 1] = (u8)(255 - k); pal[k * 3 + 2] = 7; }
  ok("range gradient", gradient_from_palette(&g, pal, 256, 10, 14) && g.ncolor == 5);
  gradient_sample(&g, 0, rgba);
  ok("range starts at lo", rgba[0] == 10 && rgba[1] == 245 && rgba[3] == 255);
  gradient_sample(&g, 1, rgba);
  ok("range ends at hi", rgba[0] == 14);
  gradient_sample(&g, 0.5, rgba);
  ok("range middle", rgba[0] == 12);
  ok("long range sampled", gradient_from_palette(&g, pal, 256, 0, 255) && g.ncolor == GRAD_MAXSTOPS && g.color[GRAD_MAXSTOPS - 1].rgb >> 16 == 255);
  ok("range backwards and clipped", gradient_from_palette(&g, pal, 20, 30, 5) && (g.color[0].rgb >> 16) == 5 && (g.color[g.ncolor - 1].rgb >> 16) == 19);
  ok("one entry", gradient_from_palette(&g, pal, 256, 3, 3) && g.ncolor == 2);
  ok("no palette", !gradient_from_palette(&g, pal, 0, 0, 3));

  d = doc_new(40, 20, 0xFFFFFF, 0);
  d->npal = 16; d->ncycle = 1; d->cycle[0].lo = 4; d->cycle[0].hi = 9; d->cycle[0].active = 1;
  doc_fill_range(d, &lo, &hi);
  ok("fill range from the cycle range", lo == 4 && hi == 9);
  d->cycle[0].active = 0;
  doc_fill_range(d, &lo, &hi);
  ok("fill range: the whole palette", lo == 0 && hi == 15);
  /* the stencil: the background colour (white) is kept, the rest painted */
  {
    i32 x, y;
    for (y = 0; y < 20; y++) for (x = 20; x < 40; x++) { u8 *p = px(d, 0, x, y); p[0] = 0x80; p[1] = p[2] = 0; }
  }
  doc_forget_undo(d);
  d->bg = 0xFFFFFF;
  ok("stencil colours: background", stencil_colors(d, STC_BACKGROUND, sc) == 1 && sc[0] == 0xFFFFFF);
  ok("stencil colours: none locked outside indexed", stencil_colors(d, STC_LOCKED, sc) == 0);
  b = brush(PT_BRUSH, 10, 100, 100);
  b.stencil = sc; b.nstencil = 1;
  line(d, &b, 0x0000FF, 2, 10, 38, 10);
  ok("stencil keeps the background colour", px(d, 0, 10, 10)[0] == 255 && px(d, 0, 10, 10)[2] == 255);
  ok("stencil paints the rest", px(d, 0, 30, 10)[2] == 255 && px(d, 0, 30, 10)[0] == 0);
  doc_undo(d);
  b.stencil_invert = 1;
  line(d, &b, 0x0000FF, 2, 10, 38, 10);
  ok("inverted stencil paints only the background colour", px(d, 0, 10, 10)[0] == 0 && px(d, 0, 30, 10)[0] == 0x80);
  d->mode = MODE_INDEXED; d->pal[3] = 1; d->pal[4] = 2; d->pal[5] = 3; d->pal_lock[1] = 1;
  ok("stencil colours: locked entries", stencil_colors(d, STC_LOCKED, sc) == 1 && sc[0] == 0x010203);
  d->mode = MODE_RGB;
  doc_free(d);
}

int main(void)
{
  dpaint();
  delete_state();
  grid_snap();
  curves();
  tone_strokes();
  clone_tools();
  eraser_smudge();
  overlays();
  printf("%d tests, %d failed\n", tests, failed);
  return failed != 0;
}
