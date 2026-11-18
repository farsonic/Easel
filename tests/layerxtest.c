/*
 * layerxtest.c - Easel 1.2's layer extras (core/layerx.c): vector masks (Reveal All, Hide All, a path,
 * feather, link, rasterize, undo), linked layers moving together, pattern fill layers and Pattern
 * Overlay, smart objects kept on disk (transforms worked out from the source without loss, reset,
 * replace, export, rasterize), artboards clipping their members, frames. Run under ASan/UBSan.
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
#include "core/paint.h"
#include "core/live.h"
#include "core/codec.h"

static int fails, tests;
#define CHECK(c, msg) do { tests++; if (!(c)) { fails++; printf("FAIL %s (line %d)\n", msg, __LINE__); } } while (0)
#define W 60
#define H 50
static const char *tmpdir = "/tmp";

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
static void comp_px(Doc *d, i32 x, i32 y, u8 *out) { doc_composite(d, box_make(x, y, x + 1, y + 1), out, 4); }
static int is_blue(Doc *d, i32 x, i32 y) { u8 c[4]; comp_px(d, x, y, c); return c[2] > 200 && c[0] < 30; }
static int is_grey(Doc *d, i32 x, i32 y) { u8 c[4]; comp_px(d, x, y, c); return c[0] == 0x40 && c[2] == 0x40; }

static void test_vmask(void)
{
  Doc *d = square_doc();
  Path *p = path_new();
  Layer *l = d->lay[1];
  path_move_to(p, 20, 15); path_line_to(p, 30, 15); path_line_to(p, 30, 35); path_line_to(p, 20, 35); path_close(p);
  CHECK(doc_vmask_add(d, 1, VM_REVEAL_ALL, NULL), "reveal all");
  CHECK(is_blue(d, 25, 20) && is_blue(d, 35, 20), "reveal all shows everything");
  CHECK(doc_vmask_add(d, 1, VM_HIDE_ALL, NULL), "hide all");
  CHECK(is_grey(d, 25, 20) && is_grey(d, 35, 20), "hide all hides everything");
  doc_undo(d);
  CHECK(is_blue(d, 35, 20), "undo hide all");
  CHECK(doc_vmask_add(d, 1, VM_PATH, p), "current path");
  CHECK(is_blue(d, 25, 20) && is_grey(d, 35, 20), "the path's inside shows, outside hidden");
  CHECK(layer_has_vmask(l) && layer_vmask_at(d, l, 25, 20) == 255 && layer_vmask_at(d, l, 35, 20) == 0, "coverage");
  /* a layer mask as well: both apply */
  doc_mask_add(d, 1, MK_REVEAL_ALL);
  d->lay[1]->mask[(long)20 * W + 22] = 0; layer_mask_changed(d->lay[1]);
  CHECK(is_grey(d, 22, 20) && is_blue(d, 25, 20), "layer mask and vector mask together");
  /* off */
  CHECK(doc_vmask_set(d, 1, 0, -1, -1, -1), "disable");
  CHECK(is_blue(d, 35, 20), "disabled shows again");
  doc_vmask_set(d, 1, 1, -1, -1, -1);
  /* linked: moves with the layer */
  move_layer(d, 5, 0);
  CHECK(layer_vmask_at(d, d->lay[1], 33, 20) == 255 && layer_vmask_at(d, d->lay[1], 22, 20) == 0, "linked vector mask moves");
  doc_undo(d);
  CHECK(layer_vmask_at(d, d->lay[1], 22, 20) == 255, "undo the move");
  doc_vmask_set(d, 1, -1, 0, -1, -1);
  move_layer(d, 5, 0);
  CHECK(layer_vmask_at(d, d->lay[1], 22, 20) == 255, "unlinked stays");
  doc_undo(d);
  /* feather softens the edge */
  doc_vmask_set(d, 1, -1, -1, 6, -1);
  { int a = layer_vmask_at(d, d->lay[1], 30, 25); CHECK(a > 40 && a < 220, "feathered edge"); }
  doc_vmask_set(d, 1, -1, -1, 0, -1);
  /* rasterize into the layer mask */
  CHECK(doc_vmask_rasterize(d, 1), "rasterize");
  CHECK(!layer_has_vmask(d->lay[1]) && d->lay[1]->mask && d->lay[1]->mask[(long)20 * W + 35] == 0 && d->lay[1]->mask[(long)20 * W + 25] == 255, "into the mask");
  CHECK(is_grey(d, 35, 20) && is_blue(d, 25, 20), "same look after rasterizing");
  doc_undo(d);
  CHECK(layer_has_vmask(d->lay[1]), "undo rasterize");
  CHECK(doc_vmask_delete(d, 1), "delete");
  CHECK(!d->lay[1]->x, "block dropped when empty");
  doc_undo(d);
  CHECK(layer_has_vmask(d->lay[1]), "undo delete");
  /* a copy carries it */
  {
    Layer *c = layer_copy(d->lay[1], W, H);
    CHECK(c && layer_has_vmask(c), "copied");
    layer_free(c);
  }
  /* the canvas cropped: the path moves with it */
  image_crop(d, box_make(10, 0, W, H));
  CHECK(layer_vmask_at(d, d->lay[1], 15, 20) == 255 && layer_vmask_at(d, d->lay[1], 25, 20) == 0, "vector mask follows a crop");
  path_free(p);
  doc_free(d);
}

static void test_links(void)
{
  Doc *d = square_doc();
  Layer *l2 = layer_new(d, "Dot");
  Box r;
  u8 *p;
  l2->px[((long)5 * W + 5) * 4 + 3] = 255;
  d->lay[d->nlay++] = l2;
  d->active = 1; d->lay[2]->picked = 1;
  CHECK(doc_link_layers(d), "link");
  CHECK(d->lay[1]->link && d->lay[1]->link == d->lay[2]->link && !d->lay[0]->link, "same number");
  d->lay[2]->picked = 0;
  CHECK(doc_select_linked(d) == 2 && d->lay[2]->picked, "select linked");
  d->lay[2]->picked = 0;
  r = doc_move_linked(d, 3, 2);
  CHECK(!box_empty(r), "moved");
  p = d->lay[2]->px;
  CHECK(p[((long)7 * W + 8) * 4 + 3] == 255 && !p[((long)5 * W + 5) * 4 + 3], "the linked layer moved too");
  CHECK(d->lay[1]->px[((long)17 * W + 23) * 4 + 3] == 255 && !d->lay[1]->px[((long)15 * W + 20) * 4 + 3], "the active one moved");
  doc_undo(d);
  CHECK(d->lay[2]->px[((long)5 * W + 5) * 4 + 3] == 255, "one undo puts both back");
  d->active = 2;
  CHECK(doc_unlink_layers(d), "unlink");
  CHECK(!d->lay[1]->link && !d->lay[2]->link, "a link of one goes");
  doc_free(d);
}

static void test_patterns(void)
{
  Doc *d = square_doc();
  Pattern *pt = pattern_new("Checks", 4, 4);
  int i, k;
  u8 c[4];
  for (k = 0; k < 16; k++) { u8 *q = pt->px + k * 4; int on = ((k & 3) < 2) == ((k >> 2) < 2); q[0] = q[1] = q[2] = (u8)(on ? 255 : 0); q[3] = 255; }
  i = doc_new_pattern_fill(d, pt, 100, NULL);
  CHECK(i == 2 && d->lay[2]->kind == LK_FILL && d->lay[2]->fillp.kind == 2, "pattern fill layer");
  comp_px(d, 0, 0, c); CHECK(c[0] == 255, "tile 0,0 white");
  comp_px(d, 2, 0, c); CHECK(c[0] == 0, "tile 2,0 black");
  comp_px(d, 4, 4, c); CHECK(c[0] == 255, "tiles repeat");
  doc_set_pattern_fill(d, 2, NULL, 200);
  comp_px(d, 2, 0, c); CHECK(c[0] == 255, "scaled 200 %");
  comp_px(d, 4, 0, c); CHECK(c[0] == 0, "scaled 200 % next");
  doc_undo(d);
  comp_px(d, 2, 0, c); CHECK(c[0] == 0, "undo the scale");
  doc_undo(d);
  CHECK(d->nlay == 2, "undo the fill layer");
  /* Pattern Overlay on the square */
  CHECK(doc_set_pattern_overlay(d, 1, 1, pt, BM_NORMAL, 255, 100), "pattern overlay");
  comp_px(d, 20, 16, c); CHECK(c[0] == 255 && c[2] == 255, "overlay white on the square");
  comp_px(d, 22, 16, c); CHECK(c[0] == 0 && c[2] == 0, "overlay black on the square");
  comp_px(d, 5, 5, c); CHECK(c[0] == 0x40, "not outside the square");
  doc_set_pattern_overlay(d, 1, 1, NULL, BM_NORMAL, 128, -1);
  comp_px(d, 22, 16, c); CHECK(c[2] > 100 && c[2] < 160, "half opacity");
  doc_set_pattern_overlay(d, 1, 0, NULL, -1, -1, -1);
  comp_px(d, 22, 16, c); CHECK(c[2] == 255 && c[0] == 0, "off");
  CHECK(!d->lay[1]->x, "nothing kept when off");
  pattern_free(pt);
  doc_free(d);
}

static void test_smart(void)
{
  Doc *d = square_doc();
  Layer *l = d->lay[1];
  double half[6] = { 0.5, 0, 0, 0, 0.5, 0 }, twice[6] = { 2, 0, 0, 0, 2, 0 };
  u8 before[W * H * 4];
  char out[300];
  snprintf(smart_drawer, sizeof smart_drawer, "%s", tmpdir);
  memcpy(before, l->px, sizeof before);
  CHECK(doc_smart_convert(d, 1), "convert");
  CHECK(layer_is_smart(d->lay[1]) && layerx_get(d->lay[1])->so_w == 20 && layerx_get(d->lay[1])->so_h == 20, "source is the content box");
  CHECK(!memcmp(before, d->lay[1]->px, sizeof before), "same pixels");
  /* scale down to a quarter, then up again: lossless */
  CHECK(doc_smart_transform(d, 1, half), "half");
  CHECK(!d->lay[1]->px[((long)20 * W + 15) * 4 + 3] && d->lay[1]->px[((long)12 * W + 15) * 4 + 3] == 255, "scaled down");
  CHECK(doc_smart_transform(d, 1, twice), "twice");
  CHECK(!memcmp(before, d->lay[1]->px, sizeof before), "back exactly from the source");
  /* move: the place goes with the pixels */
  move_layer(d, 4, 0);
  CHECK(fabs(layerx_get(d->lay[1])->so_m[2] - 24) < 1e-9, "moved");
  CHECK(doc_smart_transform(d, 1, half) && doc_smart_reset(d, 1), "reset");
  CHECK(d->lay[1]->px[((long)15 * W + 20) * 4 + 3] == 255 && d->lay[1]->px[((long)15 * W + 8) * 4 + 3] == 255 && !d->lay[1]->px[((long)15 * W + 30) * 4 + 3], "reset to full size about its centre");
  doc_undo(d); doc_undo(d); doc_undo(d);
  CHECK(!memcmp(before, d->lay[1]->px, sizeof before), "undo");
  /* export, replace */
  snprintf(out, sizeof out, "%s/easel_smart_export.png", tmpdir);
  CHECK(doc_smart_export(d, 1, out), "export");
  {
    int c = doc_smart_copy(d, 1);
    CHECK(c == 2 && layer_is_smart(d->lay[2]) && strcmp(layer_smart_path(d->lay[2]), layer_smart_path(d->lay[1])), "copy has its own source");
  }
  CHECK(doc_smart_replace(d, 2, out), "replace");
  CHECK(doc_smart_to_layers(d, 2) && d->nlay == 4 && !d->lay[2]->visible, "convert to layers");
  CHECK(doc_smart_rasterize(d, 1) && !layer_is_smart(d->lay[1]), "rasterize");
  CHECK(doc_smart_link(d, 2, out) && layerx_get(d->lay[2])->so_linked, "linked");
  CHECK(doc_smart_update_all(d) == 1, "update linked");
  CHECK(doc_smart_embed(d, 2) && !layerx_get(d->lay[2])->so_linked, "embedded");
  {
    Doc *o = smart_open(out, NULL, 0);
    CHECK(o && o->w == 20 && layer_is_smart(o->lay[0]), "open as smart object");
    if (o) { remove(layer_smart_path(o->lay[0])); doc_free(o); }
  }
  remove(layer_smart_path(d->lay[2]));
  remove(out);
  doc_free(d);
}

static void test_artboards_frames(void)
{
  Doc *d = square_doc();
  int a, f, k;
  d->active = 1;
  a = doc_new_artboard(d, box_make(25, 20, 50, 40), 0xFFFFFF, NULL);
  CHECK(a == 2 && d->lay[2]->artboard == 2, "artboard");
  /* the square moved into it */
  d->active = 1;
  CHECK(doc_move_block(d, 1, 0, 1), "into the artboard");
  for (k = 0; k < d->nlay; k++) if (d->lay[k]->artboard) a = k;
  CHECK(is_blue(d, 30, 25), "inside shows");
  CHECK(is_grey(d, 21, 16), "the member clipped outside the box");
  { u8 c[4]; comp_px(d, 45, 38, c); CHECK(c[0] == 255 && c[1] == 255, "the board's white"); }
  CHECK(doc_artboard_at(d, 30, 25) == a && doc_artboard_at(d, 5, 5) == -1, "artboard at");
  doc_free(d);

  d = square_doc();
  d->active = 1;
  f = doc_new_frame(d, box_make(5, 5, 15, 15), FR_RECT, 1, 0xFF0000);
  CHECK(f == 3 && layer_is_frame(d->lay[3]) && d->lay[2]->depth == 1, "frame with a placeholder");
  { u8 c[4]; comp_px(d, 5, 9, c); CHECK(c[0] == 255 && c[1] == 0, "its stroke"); comp_px(d, 9, 7, c); CHECK(c[0] == 224, "the placeholder"); comp_px(d, 20, 5, c); CHECK(c[0] == 0x40, "nothing outside"); }
  f = doc_frame_place(d, 3, 1, 0);
  CHECK(f >= 0 && d->nlay == 3 && layer_is_frame(d->lay[f]), "placed");
  CHECK(is_blue(d, 9, 9), "the picture fills the frame");
  CHECK(is_grey(d, 30, 25), "and left where it was");
  doc_undo(d);
  CHECK(d->nlay == 4 && is_blue(d, 30, 25), "undo the placing");
  {
    Doc *e = square_doc();
    e->active = 1;
    doc_new_frame(e, box_make(10, 10, 30, 30), FR_ELLIPSE, 0, 0);
    CHECK(is_grey(e, 11, 11), "an ellipse frame's corner hidden");
    doc_free(e);
  }
  doc_free(d);
}

static void test_ora(void)
{
  Doc *d = square_doc(), *e;
  Pattern *pt = pattern_new("P", 2, 2);
  Path *p = path_new();
  u8 *ora;
  size_t n;
  char err[80];
  int k;
  for (k = 0; k < 4; k++) { pt->px[k * 4] = (u8)(k * 60); pt->px[k * 4 + 3] = 255; }
  path_move_to(p, 20, 15); path_line_to(p, 30, 15); path_line_to(p, 30, 35); path_close(p);
  doc_vmask_add(d, 1, VM_PATH, p);
  doc_set_pattern_overlay(d, 1, 1, pt, BM_MULTIPLY, 200, 150);
  d->lay[1]->link = 3;
  doc_new_pattern_fill(d, pt, 300, NULL);
  d->active = 2;
  doc_new_artboard(d, box_make(1, 2, 30, 40), 0x112233, NULL);
  ora = ora_write(d, &n);
  CHECK(ora != NULL, "ora written");
  e = ora ? ora_read(ora, n, err, sizeof err) : NULL;
  CHECK(e && e->nlay == 4, "ora read");
  if (e) {
    const LayerX *a = layerx_get(d->lay[1]), *b = layerx_get(e->lay[1]);
    CHECK(b && b->vm_has && b->po_on && b->po_blend == BM_MULTIPLY && b->po_opacity == 200 && b->pat_scale == 150 && b->pat_w == 2, "settings kept");
    CHECK(e->lay[1]->link == 3, "link kept");
    CHECK(e->lay[2]->kind == LK_FILL && e->lay[2]->fillp.kind == 2 && layerx_get(e->lay[2])->pat_scale == 300, "pattern fill kept");
    CHECK(e->lay[3]->artboard == 2 && e->lay[3]->abox.x0 == 1 && e->lay[3]->abox.y1 == 40 && e->lay[3]->fillp.c0 == 0x112233, "artboard kept");
    {
      u8 c1[4], c2[4];
      i32 x, y;
      int same = 1;
      for (y = 0; y < H; y += 3) for (x = 0; x < W; x += 3) { comp_px(d, x, y, c1); comp_px(e, x, y, c2); if (memcmp(c1, c2, 4)) same = 0; }
      CHECK(same, "same picture after the round trip");
    }
    (void)a;
    doc_free(e);
  }
  free(ora);
  path_free(p); pattern_free(pt);
  doc_free(d);
}

int main(int argc, char **argv)
{
  if (argc > 1) tmpdir = argv[1];
  layerx_init();
  test_vmask();
  test_links();
  test_patterns();
  test_smart();
  test_artboards_frames();
  test_ora();
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
