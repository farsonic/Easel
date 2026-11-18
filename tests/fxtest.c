/*
 * fxtest.c - Easel 1.1's layer styles (core/fx.c): each effect drawn where it should be and nowhere
 * else, fill opacity, clipping, the global light, undo and redo, the effects following the layer's
 * pixels as they change, Copy/Paste/Clear, Hide All, Scale Effects, Create Layers, the text form and
 * OpenRaster round trips; plus colour labels and the Paint.NET blend modes. Run under ASan/UBSan.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/doc.h"
#include "core/layers.h"
#include "core/fx.h"
#include "core/codec.h"

static int fails, tests;
#define CHECK(c, msg) do { tests++; if (!(c)) { fails++; printf("FAIL %s (line %d)\n", msg, __LINE__); } } while (0)

#define W 60
#define H 50
/* a dark grey background with an opaque blue square (x 20-39, y 15-34) on a layer above it */
static Doc *square_doc(void)
{
  Doc *d = doc_new(W, H, 0x404040, 0);
  Layer *l = layer_new(d, "Square");
  i32 x, y;
  for (y = 15; y < 35; y++)
    for (x = 20; x < 40; x++) { u8 *p = l->px + ((long)y * W + x) * 4; p[0] = 0; p[1] = 0; p[2] = 255; p[3] = 255; }
  d->lay[d->nlay++] = l;
  d->active = 1;
  return d;
}
static u8 *comp(Doc *d)
{
  u8 *o = (u8 *)malloc((size_t)d->w * d->h * 4);
  doc_composite(d, doc_box(d), o, (long)d->w * 4);
  return o;
}
static const u8 *px(const u8 *c, i32 x, i32 y) { return c + ((long)y * W + x) * 4; }
static int same_px(const u8 *a, const u8 *b, i32 x, i32 y) { return !memcmp(px(a, x, y), px(b, x, y), 4); }
static int maxdiff(const u8 *a, const u8 *b, long n)
{
  long k;
  int m = 0;
  for (k = 0; k < n; k++) { int v = abs(a[k] - b[k]); if (v > m) m = v; }
  return m;
}
/* layer 1 given effect k (its defaults, then the changes the caller makes) */
static LayerFx one(int k)
{
  LayerFx f;
  fx_defaults_all(&f);
  f.e[k].on = 1;
  return f;
}

static void test_drop_shadow(void)
{
  Doc *d = square_doc();
  u8 *c0 = comp(d), *c1;
  LayerFx f = one(FX_DROPSHADOW);
  f.e[FX_DROPSHADOW].opacity = 255; f.e[FX_DROPSHADOW].size = 0; f.e[FX_DROPSHADOW].distance = 6;
  CHECK(!fx_any(d->lay[1]), "no effects at first");
  CHECK(doc_set_fx(d, 1, &f), "set a drop shadow");
  CHECK(fx_any(d->lay[1]), "drop shadow on");
  c1 = comp(d);
  /* light from 120 degrees: the shadow falls down and to the right (dx +3, dy +5) */
  CHECK(px(c1, 41, 38)[0] == 0 && px(c1, 41, 38)[3] == 255, "shadow below right of the square is black");
  CHECK(same_px(c0, c1, 18, 13), "nothing above left of the square");
  CHECK(same_px(c0, c1, 30, 25), "the square itself unchanged");
  CHECK(same_px(c0, c1, 5, 5) && same_px(c0, c1, 55, 45), "far away unchanged");
  /* the global light moved round to 300 degrees: the shadow falls up and to the left */
  doc_global_light(d, -60, 30);
  free(c1); c1 = comp(d);
  CHECK(px(c1, 18, 11)[0] == 0, "global light: shadow now above left");
  CHECK(same_px(c0, c1, 41, 38), "global light: nothing below right");
  /* a soft shadow is softer at its edge than inside it */
  f.e[FX_DROPSHADOW].size = 8; f.e[FX_DROPSHADOW].flags = FXF_KNOCKOUT;   /* its own angle, 120 */
  doc_set_fx(d, 1, &f);
  free(c1); c1 = comp(d);
  CHECK(px(c1, 43, 33)[0] < px(c1, 46, 33)[0] && px(c1, 46, 33)[0] < 0x40, "soft shadow fades outwards");
  /* undo and redo */
  doc_undo(d);
  { u8 *c2 = comp(d); CHECK(px(c2, 18, 11)[0] == 0 && same_px(c0, c2, 43, 33), "undo: the hard shadow back (up left)"); free(c2); }
  doc_redo(d);
  { u8 *c2 = comp(d); CHECK(!maxdiff(c1, c2, (long)W * H * 4), "redo: the soft shadow again"); free(c2); }
  free(c0); free(c1);
  doc_free(d);
}
static void test_stroke(void)
{
  Doc *d = square_doc();
  u8 *c0 = comp(d), *c1;
  LayerFx f = one(FX_STROKE);
  f.e[FX_STROKE].size = 3;
  doc_set_fx(d, 1, &f);
  c1 = comp(d);
  CHECK(px(c1, 18, 25)[0] == 255 && px(c1, 18, 25)[1] == 0 && px(c1, 18, 25)[2] == 0, "outside stroke: red two pixels out");
  CHECK(px(c1, 41, 25)[0] == 255, "outside stroke: red on the right");
  CHECK(same_px(c0, c1, 15, 25), "outside stroke: five pixels out unchanged");
  CHECK(same_px(c0, c1, 21, 25) && same_px(c0, c1, 30, 25), "outside stroke: inside unchanged");
  /* inside */
  f.e[FX_STROKE].style = FXS_INSIDE;
  doc_set_fx(d, 1, &f);
  free(c1); c1 = comp(d);
  CHECK(px(c1, 21, 25)[0] == 255 && px(c1, 21, 25)[2] == 0, "inside stroke: red just inside");
  CHECK(same_px(c0, c1, 18, 25) && same_px(c0, c1, 30, 25), "inside stroke: outside and the middle unchanged");
  /* centre: on both sides of the edge */
  f.e[FX_STROKE].style = FXS_CENTER; f.e[FX_STROKE].size = 4;
  doc_set_fx(d, 1, &f);
  free(c1); c1 = comp(d);
  CHECK(px(c1, 19, 25)[0] == 255 && px(c1, 20, 25)[0] == 255 && same_px(c0, c1, 16, 25) && same_px(c0, c1, 24, 25), "centre stroke");
  /* the effects follow the pixels: the square painted out, a dot painted elsewhere */
  {
    Layer *l = d->lay[1];
    i32 x, y;
    doc_begin(d, "Paint");
    doc_note_layer(d, 1, doc_box(d));
    for (y = 0; y < H; y++) for (x = 0; x < W; x++) l->px[((long)y * W + x) * 4 + 3] = 0;
    for (y = 40; y < 46; y++) for (x = 5; x < 11; x++) { u8 *p = l->px + ((long)y * W + x) * 4; p[2] = 255; p[3] = 255; }
    doc_end(d);
    free(c1); c1 = comp(d);
    CHECK(same_px(c0, c1, 19, 25), "the stroke went with the square");
    CHECK(px(c1, 4, 42)[0] == 255 && px(c1, 4, 42)[2] == 0, "and came with the new dot");
    doc_undo(d);
    free(c1); c1 = comp(d);
    CHECK(px(c1, 19, 25)[0] == 255 && same_px(c0, c1, 4, 42), "undo: the stroke round the square again");
  }
  free(c0); free(c1);
  doc_free(d);
}
static void test_overlays(void)
{
  Doc *d = square_doc();
  u8 *c0 = comp(d), *c1;
  LayerFx f = one(FX_COLOR);
  f.e[FX_COLOR].color = 0x00FF00;
  doc_set_fx(d, 1, &f);
  c1 = comp(d);
  CHECK(px(c1, 30, 25)[1] == 255 && px(c1, 30, 25)[2] == 0, "color overlay paints the square green");
  CHECK(same_px(c0, c1, 10, 10), "color overlay: nothing outside");
  /* fill opacity 0: the pixels go but the overlay stays */
  d->lay[1]->fill_cut = 255;
  free(c1); c1 = comp(d);
  CHECK(px(c1, 30, 25)[1] == 255 && px(c1, 30, 25)[2] == 0, "fill 0: overlay still shown");
  d->lay[1]->fill_cut = 0;
  /* layer opacity scales the effects too */
  d->lay[1]->opacity = 0;
  free(c1); c1 = comp(d);
  CHECK(px(c1, 30, 25)[1] == 0x40 && px(c1, 30, 25)[2] == 0x40 && same_px(c0, c1, 10, 10), "opacity 0: nothing of the layer or its effects");
  d->lay[1]->opacity = 255;
  /* gradient overlay, left to right black to white */
  f = one(FX_GRADIENT);
  f.e[FX_GRADIENT].angle = 0;
  doc_set_fx(d, 1, &f);
  free(c1); c1 = comp(d);
  CHECK(px(c1, 20, 25)[0] < 20 && px(c1, 39, 25)[0] > 235 && px(c1, 30, 25)[0] > 100 && px(c1, 30, 25)[0] < 160, "gradient overlay runs left to right");
  f.e[FX_GRADIENT].flags = FXF_INVERT;
  doc_set_fx(d, 1, &f);
  free(c1); c1 = comp(d);
  CHECK(px(c1, 20, 25)[0] > 235 && px(c1, 39, 25)[0] < 20, "reversed");
  f.e[FX_GRADIENT].flags = 0; f.e[FX_GRADIENT].dx = 10;   /* its centre moved right (as a drag on the canvas does) */
  doc_set_fx(d, 1, &f);
  free(c1); c1 = comp(d);
  CHECK(px(c1, 30, 25)[0] < 80, "moved gradient: darker in the middle");
  /* a hidden style draws nothing */
  doc_hide_all_fx(d, 1);
  free(c1); c1 = comp(d);
  CHECK(!maxdiff(c0, c1, (long)W * H * 4), "Hide All Effects");
  doc_hide_all_fx(d, 0);
  free(c1); c1 = comp(d);
  CHECK(maxdiff(c0, c1, (long)W * H * 4) > 100, "Show All Effects");
  free(c0); free(c1);
  doc_free(d);
}
static void test_glows_shadows_bevel(void)
{
  Doc *d = square_doc();
  u8 *c0 = comp(d), *c1;
  LayerFx f = one(FX_OUTERGLOW);
  f.e[FX_OUTERGLOW].size = 6; f.e[FX_OUTERGLOW].opacity = 255;
  doc_set_fx(d, 1, &f);
  c1 = comp(d);
  CHECK(px(c1, 18, 25)[0] > 0x60 && px(c1, 18, 25)[0] > px(c1, 15, 25)[0], "outer glow: brighter near the edge");
  CHECK(same_px(c0, c1, 5, 25) && same_px(c0, c1, 30, 25), "outer glow: not far out, not on the square");
  /* spread makes it solid out to the spread */
  f.e[FX_OUTERGLOW].spread = 100;
  doc_set_fx(d, 1, &f);
  free(c1); c1 = comp(d);
  CHECK(px(c1, 16, 25)[0] == 255 && px(c1, 16, 25)[1] == 255 && same_px(c0, c1, 12, 25), "outer glow with spread 100 %");
  /* inner glow, from the edge: near the edge only */
  f = one(FX_INNERGLOW); f.e[FX_INNERGLOW].opacity = 255; f.e[FX_INNERGLOW].size = 4;
  doc_set_fx(d, 1, &f);
  free(c1); c1 = comp(d);
  CHECK(px(c1, 20, 25)[0] > 100 && same_px(c0, c1, 30, 25) && same_px(c0, c1, 18, 25), "inner glow at the edge");
  f.e[FX_INNERGLOW].style = FXG_CENTER;
  doc_set_fx(d, 1, &f);
  free(c1); c1 = comp(d);
  CHECK(px(c1, 30, 25)[0] > 200 && px(c1, 20, 25)[0] < px(c1, 30, 25)[0], "inner glow from the centre");
  /* inner shadow: the side the light comes from (upper left, so the top and left edges) */
  f = one(FX_INNERSHADOW); f.e[FX_INNERSHADOW].opacity = 255; f.e[FX_INNERSHADOW].size = 0; f.e[FX_INNERSHADOW].distance = 4;
  doc_set_fx(d, 1, &f);
  free(c1); c1 = comp(d);
  CHECK(px(c1, 21, 16)[2] == 0 && same_px(c0, c1, 38, 33) && same_px(c0, c1, 30, 25), "inner shadow on the top left inside edges");
  /* bevel: lit at the top left, shaded at the bottom right, flat in the middle */
  f = one(FX_BEVEL); f.e[FX_BEVEL].size = 4;
  doc_set_fx(d, 1, &f);
  free(c1); c1 = comp(d);
  CHECK(px(c1, 21, 25)[0] > 60, "bevel: left edge lit");
  CHECK(px(c1, 38, 25)[2] < 200 && px(c1, 38, 25)[0] == 0, "bevel: right edge shaded");
  CHECK(same_px(c0, c1, 30, 25) && same_px(c0, c1, 15, 25), "bevel: middle and outside unchanged");
  f.e[FX_BEVEL].flags |= FXF_INVERT;             /* direction Down */
  doc_set_fx(d, 1, &f);
  { u8 *c2 = comp(d); CHECK(px(c2, 38, 25)[0] > 60 && px(c2, 21, 25)[2] < 200, "bevel down: the other way round"); free(c2); }
  f.e[FX_BEVEL].flags &= ~FXF_INVERT; f.e[FX_BEVEL].style = FXB_OUTER;
  doc_set_fx(d, 1, &f);
  { u8 *c2 = comp(d); CHECK(same_px(c0, c2, 21, 25) && maxdiff(px(c0, 18, 25), px(c2, 18, 25), 3) > 10, "outer bevel: outside the edge only"); free(c2); }
  /* satin: something inside, nothing outside */
  f = one(FX_SATIN);
  doc_set_fx(d, 1, &f);
  free(c1); c1 = comp(d);
  {
    int changed = 0; i32 x, y;
    for (y = 15; y < 35; y++) for (x = 20; x < 40; x++) if (!same_px(c0, c1, x, y)) changed++;
    CHECK(changed > 20 && same_px(c0, c1, 10, 10), "satin inside the square");
  }
  CHECK(fx_coverage(d, d->lay[1], FX_SATIN, 5, 5) == 0, "fx_coverage outside");
  free(c0); free(c1);
  doc_free(d);
}
static void test_ops(void)
{
  Doc *d = square_doc();
  Layer *b;
  u8 *c0, *c1, *c2;
  LayerFx f = one(FX_DROPSHADOW), g;
  char *t;
  f.e[FX_STROKE].on = 1; f.e[FX_STROKE].size = 2; f.e[FX_COLOR].on = 1; f.e[FX_COLOR].opacity = 128; f.e[FX_COLOR].color = 0x123456;
  f.e[FX_BEVEL].on = 1; f.e[FX_GRADIENT].on = 1; f.e[FX_GRADIENT].opacity = 100; f.e[FX_GRADIENT].dx = -7;
  doc_set_fx(d, 1, &f);
  /* as text and back */
  t = fx_save(&d->lay[1]->fx);
  CHECK(t && strstr(t, "dropshadow") && strstr(t, "stroke") && !strstr(t, "satin"), "fx_save names what is on");
  CHECK(fx_load(&g, t), "fx_load");
  {
    int k, same = 1;
    for (k = 0; k < FX_N; k++) if (d->lay[1]->fx.e[k].on && memcmp(&g.e[k], &d->lay[1]->fx.e[k], sizeof g.e[k])) same = 0;
    CHECK(same && !g.e[FX_SATIN].on, "the text keeps every setting");
  }
  free(t);
  CHECK(!fx_load(&g, "nonsense 1 2;stroke 1;;") && !g.e[FX_STROKE].on, "fx_load refuses rubbish");
  /* OpenRaster keeps the style and the label */
  d->lay[1]->label = 3;
  c0 = comp(d);
  {
    size_t n;
    u8 *ora = ora_write(d, &n);
    char err[100];
    Doc *e = ora ? ora_read(ora, n, err, sizeof err) : NULL;
    CHECK(e && e->nlay == 2, "ORA read back");
    if (e) {
      c1 = comp(e);
      CHECK(!maxdiff(c0, c1, (long)W * H * 4), "ORA: the effects look the same");
      CHECK(e->lay[1]->label == 3 && !memcmp(&e->lay[1]->fx.e[FX_STROKE], &d->lay[1]->fx.e[FX_STROKE], sizeof(FxOne)), "ORA: label and settings");
      free(c1);
      doc_free(e);
    }
    free(ora);
  }
  /* copy and paste to another layer, then clear */
  b = layer_new(d, "Dot");
  b->px[(10 * W + 10) * 4 + 3] = 255;
  d->lay[d->nlay++] = b;
  d->active = 2;
  CHECK(doc_paste_fx(d, &d->lay[1]->fx) == 1 && d->lay[2]->fx.e[FX_STROKE].on, "paste layer style");
  doc_undo(d);
  CHECK(!fx_has(d->lay[2]), "undo paste");
  d->active = 1;
  CHECK(doc_clear_fx(d, 1) && !fx_has(d->lay[1]), "clear layer style");
  doc_undo(d);
  CHECK(fx_has(d->lay[1]), "undo clear");
  /* scale effects */
  CHECK(doc_scale_fx(d, 1, 200) && d->lay[1]->fx.e[FX_STROKE].size == 4 && d->lay[1]->fx.e[FX_DROPSHADOW].distance == 10 && d->lay[1]->fx.e[FX_GRADIENT].dx == -14, "scale effects 200 %");
  doc_undo(d);
  /* Create Layers: the same picture from plain layers */
  c1 = comp(d);
  {
    int before = d->nlay, m = doc_fx_to_layers(d, 1);
    CHECK(m == 6 && d->nlay == before + 6, "create layers: one for each effect, two for the bevel");
    CHECK(!fx_has(d->lay[2]) && !strcmp(d->lay[2]->name, "Square") && strstr(d->lay[1]->name, "Drop Shadow"), "create layers: the shadow below, the style gone");
    c2 = comp(d);
    CHECK(maxdiff(c1, c2, (long)W * H * 4) <= 3, "create layers: looks the same");
    free(c2);
    doc_undo(d);
    CHECK(d->nlay == before && fx_has(d->lay[1]), "undo create layers");
  }
  /* a layer clipped to the one below keeps to its alpha, effects and all */
  {
    Layer *c = layer_new(d, "Clipped");
    i32 x, y;
    LayerFx s = one(FX_STROKE);
    for (y = 0; y < H; y++) for (x = 0; x < 30; x++) { u8 *p = c->px + ((long)y * W + x) * 4; p[1] = 200; p[3] = 255; }
    c->clip = 1;
    d->lay[3] = d->lay[2]; d->lay[2] = c; d->nlay = 4;   /* above the square */
    doc_clear_fx(d, 1);
    doc_set_fx(d, 2, &s);
    free(c1); c1 = comp(d);
    CHECK(px(c1, 31, 25)[0] == 255 && px(c1, 31, 25)[2] == 0, "clipped: its stroke shows on the square");
    CHECK(px(c1, 10, 30)[0] == 0x40 && px(c1, 10, 30)[1] == 0x40, "clipped: nothing outside the square");
  }
  free(c0); free(c1);
  doc_free(d);
}
/* text, shape and fill layers have effects too; groups and adjustment layers don't */
static void test_kinds(void)
{
  Doc *d = square_doc();
  FillParams fp;
  LayerFx f = one(FX_STROKE);
  int i;
  u8 *c;
  memset(&fp, 0, sizeof fp); fp.c0 = 0xFFFF00;
  i = doc_new_fill(d, &fp, "Fill");
  doc_mask_add(d, i, MK_HIDE_ALL);
  {
    i32 x, y;
    for (y = 5; y < 10; y++) for (x = 45; x < 55; x++) d->lay[i]->mask[(long)y * W + x] = 255;
    layer_mask_changed(d->lay[i]);
  }
  CHECK(doc_set_fx(d, i, &f), "fill layer takes a style");
  c = comp(d);
  CHECK(px(c, 50, 11)[0] == 255 && px(c, 50, 11)[1] == 0 && px(c, 50, 7)[1] == 255, "stroke round a masked fill layer");
  free(c);
  i = doc_new_group(d, "Group");
  CHECK(!doc_set_fx(d, i, &f), "a group takes none");
  doc_free(d);
}

int main(void)
{
  test_drop_shadow();
  test_stroke();
  test_overlays();
  test_glows_shadows_bevel();
  test_ops();
  test_kinds();
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
