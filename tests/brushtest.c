/*
 * brushtest.c - native tests of Easel 0.5's brushes and painting: dab shapes, spacing, dynamics,
 * symmetry, DPaint brushes and their ILBM files, gradients and .ggr, patterns, the new tools
 * (Magic Eraser, Background Eraser, Color Replacement, History Brush, Pattern Stamp, the Eraser's
 * modes, Auto Erase, clone sampling all layers), presets (text and .abr), swatches, painting into
 * a mask, and randomised strokes for the sanitizers.
 *
 * cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. core/util.c core/zip.c core/xml.c core/image.c \
 *   core/doc.c core/paint.c core/stroke.c core/brush.c core/preset.c core/gradient.c core/pattern.c core/swatch.c \
 *   core/codec.c core/png.c core/jpegenc.c core/bmp.c core/ilbm.c core/gif.c core/ora.c core/avif.c core/tiff.c \
 *   core/psd.c core/rasters.c core/webp.c core/amigafmt.c tests/brushtest.c /tmp/easel_dav1d/libdav1d.a -Iav1/include -lm
 * (run from the repository root; it writes scratch files in /tmp)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../core/paint.h"
#include "../core/preset.h"
#include "../core/gradient.h"
#include "../core/pattern.h"
#include "../core/swatch.h"
#include "../core/codec.h"

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
/* a document with a white background and an empty layer on top (active) */
static Doc *doc2(i32 w, i32 h)
{
  Doc *d = doc_new(w, h, 0xFFFFFF, 0);
  doc_add_layer(d, layer_new(d, "Paint"), -1);
  doc_forget_undo(d);
  return d;
}
static void line(Doc *d, const Brush *b, u32 c, double x0, double y0, double x1, double y1)
{
  Box ch;
  Stroke *s = stroke_begin(d, b, c, x0, y0, &ch);
  if (s) { stroke_to(s, x1, y1, &ch); stroke_end(s); }
}
static long alpha_sum(Doc *d, int l)
{
  long s = 0, k;
  for (k = 0; k < (long)d->w * d->h; k++) s += d->lay[l]->px[k * 4 + 3];
  return s;
}
static int layers_equal(Doc *a, Doc *b, int l) { return !memcmp(a->lay[l]->px, b->lay[l]->px, (size_t)a->w * a->h * 4); }

/* ------------------------------------------------------------------ dab shapes and spacing */
static void extent(const u8 *a, int w, int h, int *ew, int *eh)
{
  int i, j, x0 = w, x1 = -1, y0 = h, y1 = -1;
  for (j = 0; j < h; j++) for (i = 0; i < w; i++) if (a[j * w + i] >= 128) { if (i < x0) x0 = i; if (i > x1) x1 = i; if (j < y0) y0 = j; if (j > y1) y1 = j; }
  *ew = x1 - x0 + 1; *eh = y1 - y0 + 1;
}
static void dab_shapes(void)
{
  Brush b = brush(PT_BRUSH, 40, 100, 100);
  u8 *a;
  int w, h, ew, eh, c;
  a = brush_dab_preview(&b, &w, &h);
  extent(a, w, h, &ew, &eh);
  ok("round tip: a 40 px disc", abs(ew - 40) <= 1 && abs(eh - 40) <= 1);
  free(a);
  b.roundness = 50;
  a = brush_dab_preview(&b, &w, &h);
  extent(a, w, h, &ew, &eh);
  ok("roundness 50: 40 wide, 20 high", abs(ew - 40) <= 1 && abs(eh - 20) <= 1);
  free(a);
  b.angle = 90;
  a = brush_dab_preview(&b, &w, &h);
  extent(a, w, h, &ew, &eh);
  ok("angle 90: 20 wide, 40 high", abs(ew - 20) <= 1 && abs(eh - 40) <= 1);
  free(a);
  b.angle = 45;
  a = brush_dab_preview(&b, &w, &h);
  c = w / 2;
  ok("angle 45 (anticlockwise): the long axis up and right", a[(c - 13) * w + c + 13] == 255 && a[(c + 13) * w + c + 13] == 0);
  {
    int i, j, good = w == h;
    for (j = 0; good && j < h; j++) for (i = 0; i < w; i++) if (abs(a[j * w + i] - a[(w - 1 - i) * w + (w - 1 - j)]) > 2) { good = 0; break; }
    ok("angle 45: symmetric across its long axis", good);
  }
  free(a);
  b = brush(PT_BRUSH, 30, 0, 100);
  a = brush_dab_preview(&b, &w, &h);
  c = w / 2;
  ok("hardness 0: soft fall-off", a[c * w + c] >= 250 && a[c * w + c + 8] > 60 && a[c * w + c + 8] < 255 && a[c * w + c + 14] < 40);
  free(a);
  b.hardness = 50;
  a = brush_dab_preview(&b, &w, &h);
  ok("hardness 50: full to half the radius", a[c * w + c + 7] == 255 && a[c * w + c + 12] < 255);
  free(a);
  /* a sampled tip: a bar 10x2 scaled to 40 */
  {
    Tip *t = tip_new(10, 2);
    memset(t->a, 255, 20);
    b = brush(PT_BRUSH, 40, 100, 100); b.tip = t;
    a = brush_dab_preview(&b, &w, &h);
    extent(a, w, h, &ew, &eh);
    ok("sampled tip scaled: its larger side the size", abs(ew - 40) <= 2 && abs(eh - 8) <= 2);
    free(a);
    b.angle = 90;
    a = brush_dab_preview(&b, &w, &h);
    extent(a, w, h, &ew, &eh);
    ok("sampled tip turned", abs(eh - 40) <= 2 && abs(ew - 8) <= 2);
    free(a);
    tip_free(t);
  }
  /* spacing: dab counts along a straight line */
  {
    Doc *d = doc2(200, 60);
    Box ch;
    Stroke *s;
    b = brush(PT_BRUSH, 20, 100, 100);
    s = stroke_begin(d, &b, 0, 10, 30, &ch); stroke_to(s, 110, 30, &ch);
    ok("spacing 25% of 20: a dab every 5 px, 21 dabs over 100", stroke_dabs(s) == 21);
    stroke_end(s);
    b.spacing = 100;
    s = stroke_begin(d, &b, 0, 10, 30, &ch); stroke_to(s, 60, 30, &ch); stroke_to(s, 110, 30, &ch);
    ok("spacing 100%: 6 dabs, carried over between segments", stroke_dabs(s) == 6);
    stroke_end(s);
    b.spacing = 25; b.dyn.pressure_size = 1;
    s = stroke_begin_p(d, &b, 0, 10, 30, 0.5, &ch); stroke_to_p(s, 110, 30, 0.5, &ch);
    ok("pressure 0.5 halves the size and the spacing: 41 dabs", stroke_dabs(s) == 41);
    stroke_end(s);
    b = brush(PT_PENCIL, 5, 100, 100);
    s = stroke_begin(d, &b, 0, 10, 30, &ch); stroke_to(s, 50, 30, &ch);
    ok("pencil: a dab every pixel", stroke_dabs(s) == 41);
    stroke_end(s);
    doc_free(d);
  }
}

/* ------------------------------------------------------------------ dynamics */
static void dynamics(void)
{
  Brush b = brush(PT_BRUSH, 12, 80, 100);
  Doc *d1 = doc2(120, 80), *d2 = doc2(120, 80), *d3 = doc2(120, 80);
  double mean = 0;
  int i, inrange = 1;
  for (i = 0; i < 10000; i++) { double r = brush_rand01(5, (u32)i, 3); mean += r; if (r < 0 || r >= 1) inrange = 0; }
  ok("random numbers: in [0,1), mean near a half", inrange && fabs(mean / 10000 - 0.5) < 0.02);
  ok("random numbers: a pure function", brush_rand01(1, 2, 3) == brush_rand01(1, 2, 3) && brush_rand01(1, 2, 3) != brush_rand01(1, 2, 4));
  b.dyn.shape_on = 1; b.dyn.size.jitter = 60; b.dyn.angle.jitter = 100; b.roundness = 40;
  b.dyn.scatter_on = 1; b.dyn.scatter.jitter = 200; b.dyn.both_axes = 1; b.dyn.count = 3; b.dyn.count_jitter.jitter = 50;
  b.dyn.color_on = 1; b.dyn.hue_jitter = 50; b.dyn.fgbg.jitter = 50; b.dyn.bri_jitter = 20;
  b.dyn.transfer_on = 1; b.dyn.opacity.jitter = 50; b.dyn.flow.jitter = 30;
  b.dyn.seed = 42;
  line(d1, &b, 0xFF0000, 10, 40, 110, 40);
  line(d2, &b, 0xFF0000, 10, 40, 110, 40);
  b.dyn.seed = 43;
  line(d3, &b, 0xFF0000, 10, 40, 110, 40);
  ok("dynamics: the same seed paints the same stroke", layers_equal(d1, d2, 1));
  ok("dynamics: another seed another stroke", !layers_equal(d1, d3, 1));
  /* chunked differently: the same dabs */
  {
    Doc *d4 = doc2(120, 80);
    Box ch;
    Stroke *s;
    b.dyn.seed = 42;
    s = stroke_begin(d4, &b, 0xFF0000, 10, 40, &ch);
    for (i = 1; i <= 20; i++) stroke_to(s, 10 + i * 5, 40, &ch);
    stroke_end(s);
    ok("dynamics: points fed in any chunks give the same stroke", layers_equal(d1, d4, 1));
    doc_free(d4);
  }
  /* colour dynamics painted more than one colour */
  {
    int k, distinct = 0;
    u32 first = 0xFFFFFFFF;
    for (k = 0; k < 120 * 80; k++) {
      u8 *p = d1->lay[1]->px + k * 4;
      if (p[3] > 0) { u32 c = ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2]; if (first == 0xFFFFFFFF) first = c; else if (c != first) { distinct = 1; break; } }
    }
    ok("color dynamics: the colour varies", distinct);
  }
  /* fade: size fades to nothing over 10 steps */
  {
    Doc *d = doc2(200, 40);
    Brush f = brush(PT_BRUSH, 20, 100, 100);
    f.dyn.shape_on = 1; f.dyn.size.control = CTL_FADE; f.dyn.size.fade_steps = 10; f.spacing = 50;
    line(d, &f, 0, 10, 20, 190, 20);
    ok("fade: wide at the start, gone by the end", px(d, 1, 10, 13)[3] == 255 && px(d, 1, 180, 20)[3] == 0);
    doc_free(d);
  }
  /* pressure opacity */
  {
    Doc *d = doc2(100, 40);
    Brush f = brush(PT_BRUSH, 10, 100, 100);
    Box ch;
    Stroke *s;
    f.dyn.pressure_opacity = 1;
    s = stroke_begin_p(d, &f, 0, 10, 20, 0.25, &ch); stroke_to_p(s, 90, 20, 0.25, &ch); stroke_end(s);
    ok("pressure opacity: a quarter", abs(px(d, 1, 50, 20)[3] - 64) <= 2);
    doc_free(d);
  }
  doc_free(d1); doc_free(d2); doc_free(d3);
}

/* ------------------------------------------------------------------ symmetry */
static int mirrored(Doc *d, int l, int vx, int hy)
{
  i32 x, y;
  for (y = 0; y < d->h; y++)
    for (x = 0; x < d->w; x++) {
      i32 mx = vx ? d->w - 1 - x : x, my = hy ? d->h - 1 - y : y;
      if (memcmp(px(d, l, x, y), px(d, l, mx, my), 4)) return 0;
    }
  return 1;
}
static void symmetry(void)
{
  int mode, i;
  double ox[SYM_MAXCOPIES], oy[SYM_MAXCOPIES], rot[SYM_MAXCOPIES];
  int mir[SYM_MAXCOPIES];
  BrushDynamics dy;
  for (mode = SYM_VERTICAL; mode <= SYM_BOTH; mode++) {
    Doc *d = doc2(64, 48);
    Brush b = brush(PT_BRUSH, 9, 30, 70);
    Box ch;
    Stroke *s;
    b.flow = 60; b.dyn.sym_mode = mode;
    s = stroke_begin(d, &b, 0x2050A0, 5.3, 7.7, &ch);
    stroke_to(s, 40.2, 30.1, &ch); stroke_to(s, 20.6, 44.9, &ch); stroke_to(s, 33.3, 3.4, &ch); stroke_to(s, 31.9, 24.2, &ch);
    stroke_end(s);
    ok(mode == SYM_VERTICAL ? "symmetry vertical: an exact mirror" : mode == SYM_HORIZONTAL ? "symmetry horizontal: an exact mirror" : "symmetry both: exact in both axes",
       alpha_sum(d, 1) > 1000 && mirrored(d, 1, mode != SYM_HORIZONTAL, mode == SYM_HORIZONTAL) && (mode != SYM_BOTH || mirrored(d, 1, 0, 1)));
    doc_free(d);
  }
  /* an odd-sized canvas, a sampled asymmetric tip and a DPaint brush */
  {
    Doc *d = doc2(61, 41);
    Brush b = brush(PT_BRUSH, 15, 100, 100);
    Tip *t = tip_procedural(PTIP_LEAF, 32, 1);
    b.tip = t; b.angle = 30; b.dyn.sym_mode = SYM_BOTH; b.dyn.shape_on = 1; b.dyn.angle.jitter = 50; b.dyn.seed = 9;
    line(d, &b, 0, 3, 3, 50, 35);
    ok("symmetry: asymmetric sampled tip mirrored exactly", alpha_sum(d, 1) > 1000 && mirrored(d, 1, 1, 0) && mirrored(d, 1, 0, 1));
    tip_free(t);
    doc_free(d);
  }
  memset(&dy, 0, sizeof dy);
  dy.sym_mode = SYM_RADIAL; dy.sym_segments = 6;
  ok("radial 6: six copies", brush_symmetry(&dy, 100, 100, 70, 50, ox, oy, rot, mir) == 6);
  ok("radial: copies on the circle", fabs(hypot(ox[3] - 50, oy[3] - 50) - 20) < 1e-9 && fabs(ox[3] - 30) < 1e-9 && fabs(oy[3] - 50) < 1e-9);
  ok("radial: 60 degrees anticlockwise (up on the screen)", oy[1] < 50 && fabs(rot[1] - 60) < 1e-9);
  dy.sym_mode = SYM_MANDALA;
  ok("mandala 6: twelve copies, six mirrored", brush_symmetry(&dy, 100, 100, 70, 40, ox, oy, rot, mir) == 12 && mir[6] && !mir[5]);
  {
    int allr = 1;
    for (i = 0; i < 12; i++) if (fabs(hypot(ox[i] - 50, oy[i] - 50) - hypot(20, 10)) > 1e-9) allr = 0;
    ok("mandala: every copy the same distance from the middle", allr);
  }
  dy.sym_mode = SYM_VERTICAL; dy.sym_angle = 90;
  brush_symmetry(&dy, 100, 100, 70, 40, ox, oy, rot, mir);
  ok("vertical axis turned 90: a horizontal mirror", fabs(ox[1] - 70) < 1e-9 && fabs(oy[1] - 60) < 1e-9);
  /* radial painting: on a square canvas a four-fold turn is exact too */
  {
    Doc *d = doc2(64, 64);
    Brush b = brush(PT_BRUSH, 7, 40, 80);
    i32 x, y;
    int good = 1;
    b.dyn.sym_mode = SYM_RADIAL; b.dyn.sym_segments = 4; b.flow = 70;
    line(d, &b, 0, 40.3, 20.6, 55.1, 28.2);
    line(d, &b, 0, 30, 30, 45.5, 34.7);
    for (y = 0; y < 64; y++) for (x = 0; x < 64; x++) if (memcmp(px(d, 1, x, y), px(d, 1, y, 63 - x), 4)) good = 0;
    ok("radial 4: an exact quarter turn", alpha_sum(d, 1) > 1000 && good);
    doc_free(d);
    d = doc2(64, 64);
    b.dyn.sym_mode = SYM_MANDALA; b.dyn.sym_segments = 4;
    line(d, &b, 0, 40.3, 20.6, 55.1, 28.2);
    for (y = 0; y < 64; y++) for (x = 0; x < 64; x++) if (memcmp(px(d, 1, x, y), px(d, 1, y, x), 4) || memcmp(px(d, 1, x, y), px(d, 1, 63 - x, y), 4)) good = 0;
    ok("mandala 4: exact mirrors across the diagonals and the middle", alpha_sum(d, 1) > 1000 && good);
    doc_free(d);
    d = doc2(64, 64);
    b.dyn.sym_mode = SYM_RADIAL; b.dyn.sym_segments = 7;
    line(d, &b, 0, 40.3, 20.6, 55.1, 28.2);
    ok("radial 7: painted, every copy", alpha_sum(d, 1) > 7 * 800);
    doc_free(d);
  }
}

/* ------------------------------------------------------------------ DPaint brushes */
static int cb_equal(const CustomBrush *a, const CustomBrush *b)
{
  return a && b && a->w == b->w && a->h == b->h && a->hx == b->hx && a->hy == b->hy && !memcmp(a->px, b->px, (size_t)a->w * a->h * 4);
}
static void custom_brushes(void)
{
  Doc *d = doc_new(40, 30, 0x000000, 0);
  CustomBrush *b, *t1, *t2;
  i32 x, y;
  int good;
  /* an asymmetric picture on black: an L of colours */
  for (y = 5; y < 15; y++) for (x = 8; x < 12; x++) { u8 *p = px(d, 0, x, y); p[0] = (u8)(x * 20); p[1] = (u8)(y * 10); p[2] = 200; }
  for (x = 12; x < 20; x++) { u8 *p = px(d, 0, x, 14); p[0] = 255; p[1] = 255; p[2] = 0; }
  b = cbrush_pickup(d, box_make(4, 2, 25, 20), 0, 0, 1, 0x000000);
  ok("pick up: trimmed to the shape, black transparent", b && b->w == 12 && b->h == 10 && b->px[3] == 255 && b->px[((long)0 * 12 + 5) * 4 + 3] == 0);
  ok("pick up: the colours kept", b && b->px[0] == 160 && b->px[1] == 50 && b->px[2] == 200);
  t1 = cbrush_flip(b, 0); t2 = cbrush_flip(t1, 0);
  ok("flip X twice: the same", cb_equal(b, t2) && !cb_equal(b, t1));
  cbrush_free(t1); cbrush_free(t2);
  t1 = cbrush_flip(b, 1); t2 = cbrush_flip(t1, 1);
  ok("flip Y twice: the same", cb_equal(b, t2));
  cbrush_free(t1); cbrush_free(t2);
  t1 = cbrush_rotate90(b, 1);
  ok("rotate 90: the sides swapped, a corner moved", t1 && t1->w == 10 && t1->h == 12 && !memcmp(t1->px + ((long)0 * 10 + 9) * 4, b->px, 4));
  t2 = cbrush_rotate90(t1, 3);
  ok("rotate 90 then 270: the same", cb_equal(b, t2));
  cbrush_free(t2);
  {
    CustomBrush *r = cbrush_copy(b);
    int k;
    for (k = 0; k < 4; k++) { CustomBrush *n = cbrush_rotate90(r, 1); cbrush_free(r); r = n; }
    ok("rotate 90 four times: the same", cb_equal(b, r));
    cbrush_free(r);
  }
  t2 = cbrush_rotate(b, 90.0);
  ok("rotate any angle by 90: as rotate 90", cb_equal(t1, t2));
  cbrush_free(t1); cbrush_free(t2);
  t1 = cbrush_rotate(b, 33); t2 = cbrush_rotate(t1, -33);
  ok("rotate 33 and back: the shape about the same", t2 && abs(t2->w - b->w) <= 2 && abs(t2->h - b->h) <= 2);
  cbrush_free(t1); cbrush_free(t2);
  t1 = cbrush_double(b); t2 = cbrush_half(t1);
  ok("double then half: the same", t1 && t1->w == 24 && t1->h == 20 && cb_equal(b, t2));
  cbrush_free(t1); cbrush_free(t2);
  t1 = cbrush_outline(b, 0xFF00FF);
  ok("outline: a pixel round, in its colour", t1 && t1->w == 14 && t1->h == 12 && t1->px[((long)1 * 14 + 0) * 4] == 255 && t1->px[((long)1 * 14 + 0) * 4 + 1] == 0 && t1->px[3] == 0);
  cbrush_free(t1);
  /* stamped in Matte, Color and Replace modes */
  {
    Doc *p = doc_new(40, 30, 0x808080, 0);
    Brush br = brush(PT_BRUSH, 1, 100, 100);
    Box ch;
    Stroke *s;
    br.custom = b; br.custom_mode = CB_MATTE;
    s = stroke_begin(p, &br, 0x00FF00, 20, 15, &ch); stroke_end(s);
    /* the handle (6, 5) lands on pixel 20, 15: brush pixel (0, 0) at 14, 10 */
    good = !memcmp(px(p, 0, 14, 10), b->px, 3) && px(p, 0, 19, 10)[0] == 128;
    ok("matte: the brush's colours, its background left alone", good);
    doc_free(p);
    p = doc_new(40, 30, 0x808080, 0);
    br.custom_mode = CB_COLOR;
    s = stroke_begin(p, &br, 0x00FF00, 20, 15, &ch); stroke_end(s);
    ok("color: the shape in the foreground colour", px(p, 0, 14, 10)[1] == 255 && px(p, 0, 14, 10)[0] == 0 && px(p, 0, 19, 10)[0] == 128);
    doc_free(p);
    p = doc_new(40, 30, 0x808080, 0);
    br.custom_mode = CB_REPLACE; br.bg = 0x0000FF;
    s = stroke_begin(p, &br, 0x00FF00, 20, 15, &ch); stroke_end(s);
    ok("replace: the background painted too, in the background colour", !memcmp(px(p, 0, 14, 10), b->px, 3) && px(p, 0, 19, 10)[2] == 255 && px(p, 0, 19, 10)[0] == 0);
    doc_free(p);
    p = doc_new(40, 30, 0x808080, 0);
    br.custom_mode = CB_CYCLE;
    line(p, &br, 0xFF0000, 5, 15, 35, 15);
    ok("cycle: more than one colour along the stroke", memcmp(px(p, 0, 9, 10), px(p, 0, 30, 10), 3) != 0);
    doc_free(p);
  }
  /* ILBM brush files */
  {
    size_t len;
    u8 *f = cbrush_write_ilbm(b, &len);
    char err[80];
    CustomBrush *r = f ? cbrush_read_ilbm(f, len, err, sizeof err) : NULL;
    Rgba im;
    ok("ILBM brush: GRAB and pixels round trip", cb_equal(b, r));
    ok("ILBM brush: a transparent colour (masking 2), 6 planes for 42 colours", f && f[28] == 6 && f[29] == 2);
    good = f && ilbm_read(f, len, &im, err, sizeof err) && im.w == b->w && im.h == b->h;
    if (good) {
      long k;
      for (k = 0; k < (long)im.w * im.h; k++) {
        const u8 *a = im.px + k * 4, *c = b->px + k * 4;
        if (c[3] != a[3] || (c[3] && memcmp(a, c, 3))) { good = 0; break; }
      }
      rgba_free(&im);
    }
    ok("ILBM brush: read by Easel's own ilbm_read", good);
    {
      const u8 *g = NULL;
      size_t o;
      for (o = 12; f && o + 8 <= len; o += 8 + ((((size_t)f[o + 4] << 24) | ((size_t)f[o + 5] << 16) | ((size_t)f[o + 6] << 8) | f[o + 7]) + 1) / 2 * 2)
        if (!memcmp(f + o, "GRAB", 4)) { g = f + o + 8; break; }
      ok("ILBM brush: GRAB holds the handle", g && g[1] == b->hx && g[3] == b->hy);
    }
    free(f); cbrush_free(r);
  }
  /* a many-coloured brush: 24 planes and a mask plane */
  {
    CustomBrush *m = cbrush_new(30, 20), *r;
    size_t len;
    u8 *f;
    char err[80];
    Rgba im;
    long k;
    for (k = 0; k < 600; k++) { u8 *p = m->px + k * 4; p[0] = (u8)k; p[1] = (u8)(k * 7); p[2] = (u8)(k >> 2); p[3] = (k % 7) ? 255 : 0; if (!p[3]) p[0] = p[1] = p[2] = 0; }
    m->hx = 3; m->hy = 17;
    f = cbrush_write_ilbm(m, &len);
    r = f ? cbrush_read_ilbm(f, len, err, sizeof err) : NULL;
    ok("ILBM brush, 600 colours: deep with a mask plane, round trip", f && f[28] == 24 && f[29] == 1 && cb_equal(m, r));
    good = f && ilbm_read(f, len, &im, err, sizeof err) && im.w == 30;
    if (good) { for (k = 0; k < 600; k++) if (m->px[k * 4 + 3] && memcmp(im.px + k * 4, m->px + k * 4, 4)) good = 0; rgba_free(&im); }
    ok("ILBM brush, deep: read by ilbm_read", good);
    ok("ILBM brush: save and load a file", cbrush_save(m, "/tmp/br05_test.brush") && (r ? (cbrush_free(r), 1) : 1) && (r = cbrush_load("/tmp/br05_test.brush", err, sizeof err)) && cb_equal(m, r));
    ok("ILBM brush: damaged files refused", !cbrush_read_ilbm(f, 40, err, sizeof err) && !cbrush_read_ilbm((const u8 *)"FORM\0\0\0\4ILBM", 12, err, sizeof err));
    free(f); cbrush_free(r); cbrush_free(m);
  }
  cbrush_free(b);
  doc_free(d);
}

/* ------------------------------------------------------------------ gradients */
static int sample_close(const Gradient *a, const Gradient *b, int tol)
{
  int i, k;
  for (i = 0; i <= 1000; i++) {
    u8 x[4], y[4];
    gradient_sample(a, i / 1000.0, x); gradient_sample(b, i / 1000.0, y);
    for (k = 0; k < 4; k++) if (abs(x[k] - y[k]) > tol) { printf("  t=%.3f %d %d\n", i / 1000.0, x[k], y[k]); return 0; }
  }
  return 1;
}
static void gradients(void)
{
  Gradient g, h, r;
  u8 c[4];
  size_t len;
  char *t;
  GradientList l, l2;
  gradient_init(&g, "Test", 0x000000, 0xFFFFFF);
  gradient_add_color(&g, 0.5, GS_RGB, 0xFF0000);
  g.color[1].mid = 0.25;
  gradient_sample(&g, 0.125, c);
  ok("gradient: a quarter of the way to red", abs(c[0] - 64) <= 1 && c[1] == 0 && c[3] == 255);
  gradient_sample(&g, 0.625, c);
  ok("gradient: midpoint 25%: half way to white a quarter in", abs(c[0] - 255) <= 1 && abs(c[1] - 128) <= 1 && abs(c[2] - 128) <= 1);
  gradient_sample(&g, 0.5, c);
  ok("gradient: the stop's own colour at its place", c[0] == 255 && c[1] == 0 && c[2] == 0);
  gradient_sample(&g, -1, c);
  ok("gradient: clamped below", c[0] == 0 && c[1] == 0);
  g.opacity[1].opacity = 0;
  gradient_sample(&g, 0.5, c);
  ok("gradient: opacity stops", abs(c[3] - 128) <= 1);
  g.color[0].kind = GS_FG; g.fg = 0x00FF00;
  gradient_sample(&g, 0, c);
  ok("gradient: a foreground stop takes fg", c[1] == 255 && c[0] == 0);
  h = g; gradient_reverse(&h);
  {
    int i, good = 1;
    for (i = 0; i <= 100; i++) {
      u8 a[4], b[4];
      int k;
      gradient_sample(&g, i / 100.0, a); gradient_sample(&h, 1 - i / 100.0, b);
      for (k = 0; k < 4; k++) if (abs(a[k] - b[k]) > 1) good = 0;
    }
    ok("gradient: reversed samples back to front", good);
  }
  /* .ggr round trip: midpoints, opacity at other places, fg/bg stops */
  g.bg = 0x0000FF; g.color[2].kind = GS_BG;
  gradient_add_opacity(&g, 0.3, 0.8);
  g.opacity[0].mid = 0.7;
  t = gradient_write_ggr(&g, &len);
  ok("ggr: written", t && !strncmp(t, "GIMP Gradient\nName: Test\n", 25));
  memset(&r, 0, sizeof r);
  ok("ggr: read back", t && gradient_read_ggr(t, len, &r));
  r.fg = g.fg; r.bg = g.bg;
  ok("ggr: round trip samples the same", sample_close(&g, &r, 1));
  ok("ggr: foreground and background stops kept", r.color[0].kind == GS_FG && r.color[r.ncolor - 1].kind == GS_BG);
  free(t);
  {
    static const char ggr[] = "GIMP Gradient\nName: Two\n2\n0.000000 0.125000 0.500000 1.000000 0.000000 0.000000 1.000000 0.000000 1.000000 0.000000 1.000000 0 0\n"
                              "0.500000 0.750000 1.000000 0.000000 0.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.000000 0 0\n";
    ok("ggr: GIMP's file read", gradient_read_ggr(ggr, sizeof ggr - 1, &r) && !strcmp(r.name, "Two"));
    gradient_sample(&r, 0.125, c);
    ok("ggr: a segment's middle at its midpoint", abs(c[0] - 128) <= 1 && abs(c[1] - 128) <= 1);
    gradient_sample(&r, 0.5 + 1e-6, c);
    ok("ggr: a jump between segments", c[2] == 255 && c[0] == 0);
    gradient_sample(&r, 1, c);
    ok("ggr: the end's alpha", c[3] == 0);
  }
  /* Easel's file with the presets */
  gradlist_init(&l); gradlist_init(&l2);
  ok("gradient presets", gradlist_builtin(&l) >= 15 && !strcmp(l.g[0].name, "Foreground to Background"));
  t = gradlist_write(&l, &len);
  ok("Easel gradients: round trip", t && gradlist_read(&l2, t, len) == l.n);
  {
    int i, good = l2.n == l.n;
    for (i = 0; good && i < l.n; i++) good = sample_close(&l.g[i], &l2.g[i], 1) && !strcmp(l.g[i].name, l2.g[i].name) && l.g[i].ncolor == l2.g[i].ncolor;
    ok("Easel gradients: every preset the same", good);
  }
  free(t);
  ok("gradients: save and load by extension", gradlist_save(&l, "/tmp/br05_test.ggr") && gradlist_load(&l2, "/tmp/br05_test.ggr") == 1);
  gradient_noise(&g, 7, 10, 40, 1);
  ok("noise gradient", g.ncolor == 10 && g.nopacity == 10);
  gradlist_free(&l); gradlist_free(&l2);
  /* the fill */
  {
    Doc *d = doc_new(101, 10, 0xFFFFFF, 0);
    gradient_init(&g, "BW", 0x000000, 0xFFFFFF);
    fill_gradient_ex(d, 0, 0, 101, 0, GR_LINEAR, &g, 0, 0, 100, BM_NORMAL);
    ok("fill_gradient_ex: dark to light", px(d, 0, 0, 5)[0] < 3 && px(d, 0, 100, 5)[0] > 252 && abs(px(d, 0, 50, 5)[0] - 128) <= 2);
    gradient_init(&g, "FG to transparent", 0, 0);
    g.color[0].kind = g.color[1].kind = GS_FG; g.opacity[1].opacity = 0;
    d->fg = 0xFF0000;
    fill_gradient_ex(d, 0, 0, 101, 0, GR_LINEAR, &g, 1, 1, 100, BM_NORMAL);
    ok("fill_gradient_ex: reversed, the foreground fading", px(d, 0, 98, 5)[0] > 250 && px(d, 0, 98, 5)[1] < 10 && px(d, 0, 2, 5)[0] < 30);
    doc_free(d);
  }
}

/* ------------------------------------------------------------------ patterns */
static void patterns(void)
{
  Pattern *p = pattern_new("RGB", 3, 2);
  Doc *d = doc_new(10, 7, 0, 0);
  i32 x, y;
  int good = 1, k;
  PatternList l, l2;
  size_t len;
  u8 *f;
  for (k = 0; k < 6; k++) { p->px[k * 4] = (u8)(k * 40); p->px[k * 4 + 1] = (u8)(200 - k * 10); p->px[k * 4 + 2] = 7; p->px[k * 4 + 3] = 255; }
  fill_pattern(d, p, 1, 1, 100, BM_NORMAL);
  for (y = 0; y < 7; y++) for (x = 0; x < 10; x++) {
    const u8 *q = p->px + ((((y - 1) % 2 + 2) % 2) * 3 + (((x - 1) % 3 + 3) % 3)) * 4;
    if (memcmp(px(d, 0, x, y), q, 4)) good = 0;
  }
  ok("pattern fill: tiled from its origin", good);
  sel_rect(d, 2, 2, 5, 5, SEL_REPLACE, 0);
  memset(d->lay[0]->px, 0, (size_t)70 * 4);
  { long q; for (q = 0; q < 70; q++) d->lay[0]->px[q * 4 + 3] = 255; }
  fill_pattern(d, p, 0, 0, 100, BM_NORMAL);
  ok("pattern fill: only the selection", px(d, 0, 1, 1)[0] == 0 && px(d, 0, 1, 1)[1] == 0 && !memcmp(px(d, 0, 3, 3), pattern_at(p, 3, 3, 0, 0), 4));
  sel_none(d);
  /* the bucket */
  {
    Doc *e = doc_new(20, 10, 0xFFFFFF, 0);
    for (y = 0; y < 10; y++) { u8 *q = px(e, 0, 10, y); q[0] = q[1] = q[2] = 0; }
    fill_bucket_pattern(e, 2, 2, p, 0, 1, 0, 100, 0);
    ok("pattern bucket: the area filled, the wall and beyond not", !memcmp(px(e, 0, 4, 3), pattern_at(p, 4, 3, 0, 0), 4) && px(e, 0, 10, 3)[0] == 0 && px(e, 0, 15, 3)[0] == 255);
    doc_free(e);
  }
  /* the Pattern Stamp, aligned */
  {
    Doc *e = doc2(30, 20);
    Brush b = brush(PT_PATTERN, 6, 100, 100);
    b.pattern = p; b.pattern_aligned = 1;
    line(e, &b, 0, 5, 10, 25, 10);
    ok("pattern stamp: paints the pattern where it is laid", !memcmp(px(e, 1, 12, 10), pattern_at(p, 12, 10, 0, 0), 4) && !memcmp(px(e, 1, 13, 10), pattern_at(p, 13, 10, 0, 0), 4));
    b.pattern_aligned = 0;
    line(e, &b, 0, 6, 15, 25, 15);
    ok("pattern stamp, not aligned: starts where the stroke does", !memcmp(px(e, 1, 12, 15), pattern_at(p, 12, 15, 6, 15), 4));
    doc_free(e);
  }
  /* Define Pattern and files */
  {
    Pattern *q;
    sel_rect(d, 1, 1, 4, 3, SEL_REPLACE, 0);
    q = pattern_from_doc(d, doc_box(d), 0);
    ok("define pattern: the selection's box", q && q->w == 3 && q->h == 2);
    sel_none(d);
    patlist_init(&l); patlist_init(&l2);
    patlist_builtin(&l);
    patlist_add(&l, q);
    patlist_add(&l, pattern_copy(p));
    f = patlist_write(&l, &len);
    ok("pattern set: an IFF CAT of ILBMs", f && !memcmp(f, "CAT ", 4) && patlist_read(&l2, f, len) == l.n);
    good = l2.n == l.n;
    for (k = 0; good && k < l.n; k++) good = l.p[k]->w == l2.p[k]->w && l.p[k]->h == l2.p[k]->h && !memcmp(l.p[k]->px, l2.p[k]->px, (size_t)l.p[k]->w * l.p[k]->h * 4) && !strcmp(l.p[k]->name, l2.p[k]->name);
    ok("pattern set: names and pixels round trip", good);
    ok("pattern: one ILBM saved and loaded", pattern_save_ilbm(p, "/tmp/br05_pat.iff") && patlist_load(&l2, "/tmp/br05_pat.iff") == 1 && !memcmp(l2.p[l2.n - 1]->px, p->px, 24));
    free(f);
    patlist_free(&l); patlist_free(&l2);
  }
  pattern_free(p);
  doc_free(d);
}

/* ------------------------------------------------------------------ the new tools */
static void tools(void)
{
  i32 x, y;
  /* Magic Eraser */
  {
    Doc *d = doc_new(30, 20, 0xFF0000, 0);
    for (y = 5; y < 10; y++) for (x = 5; x < 10; x++) { u8 *p = px(d, 0, x, y); p[0] = 0; p[2] = 255; }
    for (y = 5; y < 10; y++) for (x = 20; x < 25; x++) { u8 *p = px(d, 0, x, y); p[0] = 0; p[2] = 255; }
    magic_erase(d, 6, 6, 10, 1, 0, 100, 0);
    ok("magic eraser: the square erased", px(d, 0, 7, 7)[3] == 0 && px(d, 0, 22, 7)[3] == 255 && px(d, 0, 2, 2)[3] == 255);
    doc_undo(d);
    ok("magic eraser: one undo step", px(d, 0, 7, 7)[3] == 255);
    magic_erase(d, 6, 6, 10, 0, 0, 100, 0);
    ok("magic eraser, not contiguous: both squares", px(d, 0, 7, 7)[3] == 0 && px(d, 0, 22, 7)[3] == 0 && px(d, 0, 2, 2)[3] == 255);
    doc_undo(d);
    magic_erase(d, 6, 6, 10, 1, 0, 50, 0);
    ok("magic eraser at 50%", abs(px(d, 0, 7, 7)[3] - 128) <= 1);
    doc_free(d);
  }
  /* Background Eraser: white around a black disc */
  {
    Doc *d = doc_new(60, 40, 0xFFFFFF, 0);
    Brush b = brush(PT_BGERASE, 30, 100, 100);
    for (y = 0; y < 40; y++) for (x = 0; x < 60; x++) if ((x - 30) * (x - 30) + (y - 20) * (y - 20) < 36) { u8 *p = px(d, 0, x, y); p[0] = p[1] = p[2] = 0; }
    b.tolerance = 40; b.limits = LIM_DISCONTIGUOUS; b.sampling = SMP_ONCE;
    line(d, &b, 0, 20, 20, 40, 20);
    ok("background eraser: the white erased, the black kept", px(d, 0, 22, 20)[3] == 0 && px(d, 0, 30, 20)[3] == 255 && px(d, 0, 30, 20)[0] == 0 && px(d, 0, 2, 2)[3] == 255);
    doc_free(d);
    /* contiguous: white inside a black ring is joined to nothing */
    d = doc_new(60, 40, 0xFFFFFF, 0);
    for (y = 0; y < 40; y++) for (x = 0; x < 60; x++) { int r2 = (x - 30) * (x - 30) + (y - 20) * (y - 20); if (r2 < 100 && r2 >= 36) { u8 *p = px(d, 0, x, y); p[0] = p[1] = p[2] = 0; } }
    b.limits = LIM_CONTIGUOUS; b.size = 40; b.sampling = SMP_CONTINUOUS;
    line(d, &b, 0, 14, 20, 15, 20);
    ok("background eraser, contiguous: the white in the ring kept", px(d, 0, 17, 20)[3] == 0 && px(d, 0, 30, 20)[3] == 255);
    doc_free(d);
    /* protect foreground; the background swatch */
    d = doc_new(40, 20, 0xFFFFFF, 0);
    for (x = 20; x < 40; x++) for (y = 0; y < 20; y++) { u8 *p = px(d, 0, x, y); p[0] = 0; p[1] = 0; p[2] = 255; }
    b = brush(PT_BGERASE, 50, 100, 100);
    b.tolerance = 10; b.limits = LIM_DISCONTIGUOUS; b.sampling = SMP_SWATCH; b.bg = 0x0000FF;
    line(d, &b, 0, 10, 10, 30, 10);
    ok("background eraser, swatch: only the background colour", px(d, 0, 30, 10)[3] == 0 && px(d, 0, 10, 10)[3] == 255);
    doc_free(d);
    d = doc_new(40, 20, 0xFFFFFF, 0);
    b.sampling = SMP_CONTINUOUS; b.tolerance = 100; b.protect_fg = 1;
    for (x = 20; x < 40; x++) for (y = 0; y < 20; y++) { u8 *p = px(d, 0, x, y); p[0] = 0; p[1] = 0; p[2] = 255; }
    line(d, &b, 0x0000FF, 10, 10, 11, 10);
    ok("background eraser: protect foreground", px(d, 0, 30, 10)[3] == 255 && px(d, 0, 10, 10)[3] == 0);
    doc_free(d);
  }
  /* Color Replacement */
  {
    Doc *d = doc_new(40, 20, 0xC03030, 0);
    Brush b = brush(PT_REPLACE, 30, 100, 100);
    double h0, s0, v0, h1, s1, v1;
    u8 *p;
    for (y = 0; y < 20; y++) for (x = 18; x < 22; x++) { p = px(d, 0, x, y); p[0] = p[1] = p[2] = 128; }
    b.tolerance = 40; b.sampling = SMP_ONCE; b.limits = LIM_DISCONTIGUOUS; b.replace_mode = RPL_COLOR;
    line(d, &b, 0x0000FF, 8, 10, 32, 10);
    p = px(d, 0, 10, 10);
    rgb_to_hsv(p[0], p[1], p[2], &h1, &s1, &v1);
    rgb_to_hsv(0, 0, 255, &h0, &s0, &v0);
    ok("color replacement: the red becomes blue", fabs(h1 - h0) < 10 && s1 > 0.3);
    ok("color replacement: the grey outside the tolerance kept", px(d, 0, 20, 10)[0] == 128 && px(d, 0, 20, 10)[2] == 128);
    doc_free(d);
    d = doc_new(40, 20, 0xC03030, 0);
    b.replace_mode = RPL_LUMINOSITY;
    line(d, &b, 0xFFFFFF, 8, 10, 32, 10);
    p = px(d, 0, 10, 10);
    rgb_to_hsv(p[0], p[1], p[2], &h1, &s1, &v1);
    ok("color replacement, luminosity: brighter, still red", p[0] > 0xC0 && h1 < 15);
    doc_free(d);
  }
  /* History Brush */
  {
    Doc *d = doc_new(50, 30, 0x204080, 0);
    Snapshot *sn;
    Brush b = brush(PT_BRUSH, 20, 100, 100);
    int good = 1;
    for (y = 0; y < 30; y++) for (x = 0; x < 50; x++) { u8 *p = px(d, 0, x, y); p[0] = (u8)(x * 5); p[1] = (u8)(y * 8); }
    sn = snapshot_take(d, 0);
    line(d, &b, 0xFFFF00, 5, 15, 45, 15);
    ok("history: painted over", px(d, 0, 25, 15)[0] == 255 && px(d, 0, 25, 15)[2] == 0);
    b = brush(PT_HISTORY, 24, 100, 100);
    b.snapshot = sn;
    line(d, &b, 0, 5, 15, 45, 15);
    for (y = 8; y < 22; y++) for (x = 8; x < 42; x++) if (memcmp(px(d, 0, x, y), sn->px + ((long)y * 50 + x) * 4, 4)) good = 0;
    ok("history brush: restores the snapshot exactly", good);
    snapshot_free(sn);
    doc_free(d);
  }
  /* the Eraser's modes */
  {
    Doc *d = doc_new(60, 30, 0x000000, 0);
    Brush b = brush(PT_ERASER, 10, 0, 100);
    long k, partial = 0;
    b.eraser_mode = ER_BLOCK; b.opacity = 30;
    line(d, &b, 0, 10, 10, 10.1, 10);
    ok("eraser block: a square, fully", px(d, 0, 6, 6)[3] == 0 && px(d, 0, 13, 13)[3] == 0 && px(d, 0, 16, 10)[3] == 255);
    b.eraser_mode = ER_PENCIL; b.opacity = 100;
    line(d, &b, 0, 30, 20, 50, 20);
    for (k = 0; k < 60 * 30; k++) if (d->lay[0]->px[k * 4 + 3] != 0 && d->lay[0]->px[k * 4 + 3] != 255) partial++;
    ok("eraser pencil: hard edged, nothing partly erased", partial == 0 && px(d, 0, 40, 20)[3] == 0);
    doc_free(d);
  }
  /* the Pencil's Auto Erase */
  {
    Doc *d = doc_new(30, 20, 0xFFFFFF, 0);
    Brush b = brush(PT_PENCIL, 3, 100, 100);
    b.auto_erase = 1; b.bg = 0x00FF00;
    line(d, &b, 0xFF0000, 5, 5, 25, 5);
    ok("auto erase: on white it paints the foreground", px(d, 0, 15, 5)[0] == 255 && px(d, 0, 15, 5)[1] == 0);
    line(d, &b, 0xFF0000, 15, 5, 15, 15);
    ok("auto erase: starting on the foreground it paints the background", px(d, 0, 15, 12)[1] == 255 && px(d, 0, 15, 12)[0] == 0);
    doc_free(d);
  }
  /* the Clone Stamp sampling all layers */
  {
    Doc *d = doc2(40, 20);
    Brush b = brush(PT_CLONE, 6, 100, 100);
    for (y = 0; y < 20; y++) for (x = 0; x < 20; x++) { u8 *p = px(d, 0, x, y); p[0] = 0; p[1] = (u8)(x * 10); p[2] = 0; }
    b.clone_dx = -20; b.clone_dy = 0;
    line(d, &b, 0, 25, 10, 35, 10);
    ok("clone, current layer: nothing to copy on an empty layer", px(d, 1, 30, 10)[3] == 0);
    b.sample_all = 1;
    line(d, &b, 0, 25, 10, 35, 10);
    ok("clone, all layers: copies what is seen", px(d, 1, 30, 10)[3] == 255 && px(d, 1, 30, 10)[1] == 100);
    doc_free(d);
  }
  /* painting into a mask */
  {
    Doc *d = doc_new(40, 30, 0x808080, 0);
    u8 *m = (u8 *)calloc(40 * 30, 1);
    PaintTarget t;
    Brush b = brush(PT_BRUSH, 10, 100, 100);
    Box ch;
    Stroke *s;
    memset(&t, 0, sizeof t);
    t.px = m; t.w = 40; t.h = 30; t.bpp = 1;
    s = stroke_begin_target(d, &t, &b, 0xFFFFFF, 5, 15, 1, &ch); stroke_to(s, 35, 15, &ch); stroke_end(s);
    ok("mask: white paints 255", m[15 * 40 + 20] == 255 && m[2 * 40 + 20] == 0 && ch.x1 >= ch.x0);
    b = brush(PT_ERASER, 6, 100, 100); b.bg = 0x000000;
    s = stroke_begin_target(d, &t, &b, 0xFFFFFF, 20, 5, 1, &ch); stroke_to(s, 20, 25, &ch); stroke_end(s);
    ok("mask: the eraser paints the background's grey", m[15 * 40 + 20] == 0 && m[15 * 40 + 10] == 255);
    b = brush(PT_BRUSH, 8, 100, 50);
    s = stroke_begin_target(d, &t, &b, 0xFFFFFF, 20, 15, 1, &ch); stroke_end(s);
    ok("mask: half opacity", abs(m[15 * 40 + 20] - 128) <= 1);
    ok("mask: the layer untouched", px(d, 0, 20, 15)[0] == 128);
    free(m);
    doc_free(d);
  }
  /* airbrush build-up and smoothing */
  {
    Doc *d = doc2(60, 30);
    Brush b = brush(PT_BRUSH, 10, 100, 100);
    Box ch;
    Stroke *s;
    long n0;
    int a0, i;
    b.flow = 10; b.dyn.buildup = 1; b.dyn.buildup_rate = 50;
    ok("airbrush: a timer every 20 ms", stroke_tick_ms(&b) == 20 && stroke_wants_tick(&b));
    s = stroke_begin(d, &b, 0, 30, 15, &ch);
    a0 = px(d, 1, 30, 15)[3]; n0 = stroke_dabs(s);
    for (i = 0; i < 10; i++) stroke_tick(s, &ch);
    ok("airbrush: holding still builds up", stroke_dabs(s) == n0 + 10 && px(d, 1, 30, 15)[3] > a0 + 50 && !box_empty(ch));
    stroke_end(s);
    b = brush(PT_BRUSH, 6, 100, 100);
    b.dyn.smoothing = 60;
    s = stroke_begin(d, &b, 0, 5, 5, &ch);
    stroke_to(s, 55, 5, &ch);
    {
      double bx, by;
      ok("smoothing: the brush lags behind the pointer", stroke_string(s, &bx, &by) && bx < 40);
      for (i = 0; i < 60; i++) stroke_tick(s, &ch);
      ok("smoothing: catches up while the pointer holds still", px(d, 1, 50, 5)[3] == 255);
    }
    stroke_end(s);
    b.dyn.smoothing = 20; b.dyn.pulled_string = 1;
    s = stroke_begin(d, &b, 0, 5, 25, &ch);
    stroke_to(s, 15, 25, &ch);
    ok("pulled string: nothing until the string is taut", px(d, 1, 12, 25)[3] == 0);
    stroke_to(s, 45, 25, &ch);
    ok("pulled string: then the brush follows, a string behind", px(d, 1, 20, 25)[3] == 255 && px(d, 1, 30, 25)[3] == 0);
    stroke_end(s);
    ok("pulled string: the stroke finishes at the pointer", px(d, 1, 44, 25)[3] == 255);
    doc_free(d);
  }
}

/* ------------------------------------------------------------------ presets, .abr, swatches */
static void put16(u8 *p, u32 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static void presets(void)
{
  PresetList l, l2;
  size_t len;
  char *t, err[100];
  int i, good;
  presets_init(&l); presets_init(&l2);
  ok("brush presets: built in", presets_builtin(&l) >= 15 && !strcmp(l.p[0].name, "Hard Round"));
  t = presets_write(&l, &len);
  ok("brush presets: text round trip", t && presets_read(&l2, t, len) == l.n);
  good = l2.n == l.n;
  for (i = 0; good && i < l.n; i++) {
    Brush a = l.p[i].b, b = l2.p[i].b;
    a.tip = b.tip = NULL;
    good = !memcmp(&a, &b, sizeof a) && !strcmp(l.p[i].name, l2.p[i].name) && !strcmp(l.p[i].group, l2.p[i].group) && (!l.p[i].tip) == (!l2.p[i].tip);
    if (good && l.p[i].tip) good = l.p[i].tip->w == l2.p[i].tip->w && !memcmp(l.p[i].tip->a, l2.p[i].tip->a, (size_t)l.p[i].tip->w * l.p[i].tip->h);
    if (good) good = l2.p[i].b.tip == l2.p[i].tip;
  }
  ok("brush presets: every setting and tip the same", good);
  free(t);
  {
    Brush cur = l.p[0].b;
    Brush p;
    cur.tool = PT_ERASER; cur.dyn.smoothing = 33; cur.dyn.sym_mode = SYM_RADIAL;
    p = preset_pick(&l.p[13], &cur);
    ok("preset picked: its tip and size, the tool's smoothing and symmetry", p.tool == PT_ERASER && p.dyn.smoothing == 33 && p.dyn.sym_mode == SYM_RADIAL && p.tip == l.p[13].tip && p.size == l.p[13].b.size);
  }
  /* Define Brush Preset */
  {
    Doc *d = doc_new(40, 30, 0xFFFFFF, 0);
    i32 x, y;
    int k;
    Brush base = l.p[0].b;
    for (y = 10; y < 16; y++) for (x = 5; x < 25; x++) { u8 *p = px(d, 0, x, y); p[0] = p[1] = p[2] = 0; }
    sel_rect(d, 0, 0, 30, 30, SEL_REPLACE, 0);
    k = presets_define(&l, d, doc_box(d), 0, "Bar", &base);
    ok("define brush preset: dark paints, trimmed", k >= 0 && l.p[k].tip && l.p[k].tip->w == 20 && l.p[k].tip->h == 6 && l.p[k].tip->a[0] == 255 && l.p[k].b.size == 20);
    doc_free(d);
  }
  presets_free(&l2); presets_init(&l2);
  /* .abr v2: a computed brush and a sampled one (PackBits), then v6 with a raw 16-bit sample */
  {
    u8 f[400];
    size_t o = 0, s0;
    put16(f + o, 2); put16(f + o + 2, 2); o += 4;
    put16(f + o, 1); put32(f + o + 2, 14); o += 6;
    put32(f + o, 0); put16(f + o + 4, 30); put16(f + o + 6, 25); put16(f + o + 8, 50); put16(f + o + 10, (u32)(u16)-30); put16(f + o + 12, 80); o += 14;
    put16(f + o, 2); s0 = o + 2; o += 6;
    {
      size_t b0 = o;
      put32(f + o, 0); put16(f + o + 4, 40); o += 6;
      put32(f + o, 3); put16(f + o + 4, 'D'); put16(f + o + 6, 'o'); put16(f + o + 8, 0); o += 10;
      f[o++] = 1; memset(f + o, 0, 8); o += 8;
      put32(f + o, 0); put32(f + o + 4, 0); put32(f + o + 8, 2); put32(f + o + 12, 4); o += 16;
      put16(f + o, 8); f[o + 2] = 1; o += 3;
      put16(f + o, 2); put16(f + o + 2, 5); o += 4;
      f[o++] = 0xFD; f[o++] = 200;                     /* row 0: 200 x4 */
      f[o++] = 3; f[o++] = 1; f[o++] = 2; f[o++] = 3; f[o++] = 4;   /* row 1: 1 2 3 4 */
      put32(f + s0, (u32)(o - b0));
    }
    ok("abr v2: two brushes", presets_read_abr(&l2, f, o, "Test", err, sizeof err) == 2);
    ok("abr v2: the computed brush", l2.n == 2 && !l2.p[0].tip && l2.p[0].b.size == 25 && l2.p[0].b.roundness == 50 && l2.p[0].b.angle == -30 && l2.p[0].b.hardness == 80 && l2.p[0].b.spacing == 30);
    ok("abr v2: the sampled brush, PackBits, its name", l2.n == 2 && l2.p[1].tip && l2.p[1].tip->w == 4 && l2.p[1].tip->h == 2 && l2.p[1].tip->a[2] == 200 && l2.p[1].tip->a[7] == 4 && !strcmp(l2.p[1].name, "Do") && l2.p[1].b.spacing == 40);
    o = 0;
    put16(f, 6); put16(f + 2, 1); o = 4;
    memcpy(f + o, "8BIMsamp", 8); o += 8;
    {
      size_t lenpos = o, e0;
      o += 4;
      e0 = o;
      put32(f + o, 0); o += 4;
      f[o++] = 2; f[o++] = 'a'; f[o++] = 'b';
      memset(f + o, 0, 10); o += 10;
      put32(f + o, 0); put32(f + o + 4, 0); put32(f + o + 8, 1); put32(f + o + 12, 3); o += 16;
      put16(f + o, 16); f[o + 2] = 0; o += 3;
      f[o++] = 0x10; f[o++] = 0xFF; f[o++] = 0x80; f[o++] = 0; f[o++] = 0xFF; f[o++] = 0xFF;
      put32(f + e0, (u32)(o - e0 - 4));
      while ((o - e0) % 4) f[o++] = 0;
      put32(f + lenpos, (u32)(o - lenpos - 4));
    }
    presets_free(&l2); presets_init(&l2);
    ok("abr v6: a 16-bit sample", presets_read_abr(&l2, f, o, "Six", err, sizeof err) == 1 && l2.p[0].tip->w == 3 && l2.p[0].tip->a[0] == 0x10 && l2.p[0].tip->a[1] == 0x80 && l2.p[0].tip->a[2] == 0xFF);
    ok("abr: rubbish refused", presets_read_abr(&l2, (const u8 *)"\0\5abc", 5, "", err, sizeof err) == 0 && presets_read_abr(&l2, f, 9, "", err, sizeof err) == 0);
  }
  presets_free(&l); presets_free(&l2);
  /* swatches */
  {
    SwatchSet s, s2;
    u8 *d;
    swatches_init(&s); swatches_init(&s2);
    swatches_defaults(&s);
    swatches_add(&s, 0x123456, "Mine");
    d = swatches_write_gpl(&s, &len);
    ok("swatches: gpl round trip", d && swatches_read_gpl(d, len, &s2) == s.n && s2.s[s.n - 1].rgb == 0x123456 && !strcmp(s2.s[s.n - 1].name, "Mine") && !strcmp(s2.name, "Default"));
    free(d);
    swatches_clear(&s2);
    d = swatches_write_easel(&s, &len);
    ok("swatches: Easel's format round trip", d && swatches_read_easel(d, len, &s2) == s.n && s2.s[0].rgb == s.s[0].rgb);
    free(d);
    ok("swatches: save and load by extension", swatches_save(&s, "/tmp/br05_sw.gpl") && swatches_load(&s2, "/tmp/br05_sw.gpl") == s.n);
    ok("swatches: .aco waits for palette.c's hook", !swatch_supported(SWF_ACO, 0) && !swatches_save(&s, "/tmp/br05_sw.aco"));
    swatches_remove(&s, 0);
    ok("swatches: remove, find", swatches_find(&s, 0x123456) == s.n - 1 && swatches_find(&s, 0xABCDEF) < 0);
    swatches_free(&s); swatches_free(&s2);
  }
}

/* ------------------------------------------------------------------ randomised strokes */
static u32 rs = 12345;
static u32 rnd(void) { rs = rs * 1103515245UL + 12345; return (rs >> 8) & 0xFFFFFF; }
static int rn(int n) { return (int)(rnd() % (u32)n); }
static void random_strokes(int rounds)
{
  Tip *tip = tip_procedural(PTIP_SPATTER, 24, 3);
  CustomBrush *cb = cbrush_new(9, 7);
  Pattern *pat = pattern_builtin(PAT_BRICKS, 16);
  Doc *d = doc2(97, 71);
  Snapshot *sn = snapshot_take(d, 1);
  int r, i;
  long k;
  for (k = 0; k < 63; k++) { cb->px[k * 4] = (u8)(k * 4); cb->px[k * 4 + 3] = k % 5 ? 255 : 0; }
  for (r = 0; r < rounds; r++) {
    Brush b = brush(rn(PT_N), 1 + rn(r % 10 == 0 ? 200 : 40), rn(101), rn(101));
    Box ch;
    Stroke *s;
    int n = 1 + rn(8);
    b.flow = 1 + rn(100); b.spacing = 1 + rn(r % 7 == 0 ? 400 : 60); b.blend = rn(BM_N); b.strength = rn(101);
    b.angle = rn(361) - 180; b.roundness = rn(101); b.flip_x = rn(2); b.flip_y = rn(2); b.square = rn(8) == 0;
    if (rn(3) == 0) b.tip = tip;
    if (rn(4) == 0) { b.custom = cb; b.custom_mode = rn(CB_N); }
    b.eraser_mode = rn(3); b.auto_erase = rn(2); b.sample_all = rn(2);
    b.pattern = pat; b.pattern_aligned = rn(2); b.snapshot = sn;
    b.sampling = rn(3); b.limits = rn(3); b.tolerance = rn(256); b.protect_fg = rn(2); b.replace_mode = rn(4);
    b.clone_dx = rn(81) - 40; b.clone_dy = rn(81) - 40;
    b.dyn.shape_on = rn(2); b.dyn.size.jitter = rn(101); b.dyn.size.control = rn(CTL_N); b.dyn.size.minimum = rn(101);
    b.dyn.angle.jitter = rn(101); b.dyn.angle.control = rn(CTL_N); b.dyn.roundness.jitter = rn(101); b.dyn.flip_x_jitter = rn(2);
    b.dyn.scatter_on = rn(2); b.dyn.scatter.jitter = rn(1001); b.dyn.both_axes = rn(2); b.dyn.count = rn(20); b.dyn.count_jitter.jitter = rn(101);
    b.dyn.color_on = rn(2); b.dyn.per_stroke = rn(2); b.dyn.fgbg.jitter = rn(101); b.dyn.fgbg.control = rn(CTL_N); b.dyn.hue_jitter = rn(101); b.dyn.purity = rn(201) - 100;
    b.dyn.transfer_on = rn(2); b.dyn.opacity.jitter = rn(101); b.dyn.flow.control = rn(CTL_N); b.dyn.pressure_size = rn(2); b.dyn.pressure_opacity = rn(2);
    b.dyn.buildup = rn(2); b.dyn.smoothing = rn(3) ? 0 : rn(101); b.dyn.pulled_string = rn(2);
    b.dyn.sym_mode = rn(SYM_N); b.dyn.sym_segments = rn(40); b.dyn.sym_angle = rn(4) ? 0 : rn(720) - 360; b.dyn.sym_center_set = rn(2);
    b.dyn.sym_cx = rn(140) - 20; b.dyn.sym_cy = rn(100) - 15; b.dyn.seed = rnd();
    if (rn(5) == 0) sel_ellipse(d, rn(97), rn(71), rn(97), rn(71), SEL_REPLACE, rn(5));
    else if (rn(5) == 0) sel_none(d);
    d->lay[1]->alpha_lock = rn(6) == 0;
    s = stroke_begin_p(d, &b, rnd(), rn(160) - 30, rn(130) - 30, rn(101) / 100.0, &ch);
    if (!s) continue;
    for (i = 0; i < n; i++) {
      if (rn(4) == 0) stroke_tick(s, &ch);
      else stroke_to_p(s, rn(200) - 50 + rn(100) / 100.0, rn(170) - 50, rn(120) / 100.0 - 0.1, &ch);
      if (!box_empty(ch) && (ch.x0 < 0 || ch.y0 < 0 || ch.x1 > d->w || ch.y1 > d->h)) { ok("random strokes: the changed box inside the canvas", 0); break; }
    }
    stroke_end(s);
    if (rn(3) == 0) doc_undo(d);
    if (rn(10) == 0) magic_erase(d, rn(97), rn(71), rn(256), rn(2), rn(2), rn(101), rn(2));
  }
  ok("random strokes: survived", 1);
  snapshot_free(sn);
  doc_free(d);
  tip_free(tip); cbrush_free(cb); pattern_free(pat);
}

int main(int argc, char **argv)
{
  dab_shapes();
  dynamics();
  symmetry();
  custom_brushes();
  gradients();
  patterns();
  tools();
  presets();
  random_strokes(argc > 1 ? atoi(argv[1]) : 600);
  printf("%d tests, %d failed\n", tests, failed);
  return failed != 0;
}
