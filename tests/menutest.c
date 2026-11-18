/*
 * menutest.c - Easel 1.2's menu and panel additions (core/viewx.c, core/actions.c, the 1.2
 * Distribute items in core/layers.c): pixel aspect presets, guide layouts, zoom helpers, Trim's
 * options, the checkerboard and grid styles, distributing by edges and spacing, the action
 * recorder and its text form, saved layer styles, layer comps and the histogram's statistics.
 * Run under ASan/UBSan.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core/doc.h"
#include "core/layers.h"
#include "core/guides.h"
#include "core/fx.h"
#include "core/viewx.h"
#include "core/actions.h"
#include "core/picinfo.h"
#include "core/codec.h"

static int fails, tests;
#define CHECK(c, msg) do { tests++; if (!(c)) { fails++; printf("FAIL %s (line %d)\n", msg, __LINE__); } } while (0)

static void rect(Layer *l, i32 w, i32 x0, i32 y0, i32 x1, i32 y1, u32 c)
{
  i32 x, y;
  for (y = y0; y < y1; y++) for (x = x0; x < x1; x++) {
    u8 *p = l->px + ((long)y * w + x) * 4;
    p[0] = (u8)(c >> 16); p[1] = (u8)(c >> 8); p[2] = (u8)c; p[3] = 255;
  }
}
static int has_guide(const Doc *d, int vertical, i32 pos)
{
  int i, n = vertical ? d->guides.nv : d->guides.nh;
  for (i = 0; i < n; i++) if ((vertical ? d->guides.v : d->guides.h)[i] == pos) return 1;
  return 0;
}

static void test_par(void)
{
  Doc *d = doc_new(40, 30, 0xFFFFFF, 0);
  u8 ax, ay;
  int i;
  par_approx(0.9090909, &ax, &ay); CHECK(ax == 10 && ay == 11, "0.909 is 10:11");
  par_approx(2.0, &ax, &ay); CHECK(ax == 2 && ay == 1, "2 is 2:1");
  par_approx(1.0940, &ax, &ay); CHECK(fabs((double)ax / ay - 1.094) < 0.001, "1.094 close");
  par_approx(0, &ax, &ay); CHECK(ax == 1 && ay == 1, "nonsense is square");
  CHECK(par_find(d) == 0, "a new picture is square");
  CHECK(par_view_ratio(d, 1) == 1.0, "square shows square");
  CHECK(par_set(d, 10, 11), "set D1 NTSC");
  CHECK(d->aspect_x == 10 && d->aspect_y == 11, "aspect kept");
  CHECK(par_find(d) == 1, "found as D1/DV NTSC (the first match)");
  CHECK(fabs(par_view_ratio(d, 1) - 1.1) < 1e-9, "drawn 1.1 times as tall");
  CHECK(par_view_ratio(d, 0) == 1.0, "correction off: square");
  CHECK(!par_set(d, 20, 22), "the same ratio again is no change");
  CHECK(par_set(d, 2, 1) && par_find(d) == 7, "anamorphic");
  doc_undo(d);
  CHECK(d->aspect_x == 10 && d->aspect_y == 11, "undo brings the aspect back");
  CHECK(!par_set(d, 0, 3), "zero refused");
  for (i = 0; i < PAR_N; i++) CHECK(par_presets[i].ax && par_presets[i].ay && par_presets[i].name[0], "preset sane");
  doc_free(d);
}

static void test_guides(void)
{
  Doc *d = doc_new(200, 100, 0xFFFFFF, 0);
  GuideLayout g;
  Layer *l;
  Box b;
  memset(&g, 0, sizeof g);
  g.cols = 4; g.colgut = 0;
  CHECK(guide_layout(d, &g) == 5, "4 columns, no gutter: 5 guides");
  CHECK(has_guide(d, 1, 0) && has_guide(d, 1, 50) && has_guide(d, 1, 100) && has_guide(d, 1, 150) && has_guide(d, 1, 200), "at 0, 50 ... 200");
  CHECK(d->guides.show_guides, "guides shown");
  memset(&g, 0, sizeof g);
  g.clear = 1; g.cols = 2; g.colgut = 20; g.mleft = 10; g.mright = 10;
  guide_layout(d, &g);
  /* between the margins 10..190 (180): two columns of 80 with a gutter of 20 */
  CHECK(d->guides.nv == 4 && has_guide(d, 1, 10) && has_guide(d, 1, 90) && has_guide(d, 1, 110) && has_guide(d, 1, 190), "gutters and margins");
  memset(&g, 0, sizeof g);
  g.clear = 1; g.cols = 2; g.colw = 40; g.colgut = 10; g.centre = 1;
  guide_layout(d, &g);
  /* 90 wide in 200: from 55 */
  CHECK(has_guide(d, 1, 55) && has_guide(d, 1, 95) && has_guide(d, 1, 105) && has_guide(d, 1, 145), "centred columns of a set width");
  memset(&g, 0, sizeof g);
  g.clear = 1; g.rows = 2; g.mtop = 5;
  guide_layout(d, &g);
  CHECK(d->guides.nv == 0 && has_guide(d, 0, 5) && has_guide(d, 0, 53) && has_guide(d, 0, 100), "rows inside a top margin");
  /* from a shape: the active layer's content */
  guide_clear(d);
  l = layer_new(d, "Shape");
  rect(l, 200, 20, 10, 60, 30, 0xFF0000);
  doc_add_layer(d, l, 1);
  d->active = 1;
  CHECK(guide_shape_box(d, &b) && b.x0 == 20 && b.y1 == 30, "the shape's box");
  CHECK(guides_from_box(d, b) == 6, "six guides from a shape");
  CHECK(has_guide(d, 1, 40) && has_guide(d, 0, 20), "and its centre lines");
  /* zoom helpers */
  CHECK(fabs(zoom_print_size(144, 72) - 0.5) < 1e-9, "Print Size of a 144 dpi picture at 72 dpi");
  CHECK(fit_layers_box(d, &b) && b.x1 == 60, "Fit Layer box");
  CHECK(fabs(zoom_fit_box(b, 408, 1000, 1) - 10) < 1e-9, "fit 40 wide in 400");
  CHECK(fabs(zoom_fit_box(b, 1000, 108, 2) - 2.5) < 1e-9, "fit 20 tall (at 2:1) in 100");
  doc_free(d);
}

static void test_trim(void)
{
  Doc *d = doc_new(50, 40, 0x00FF00, 0);
  Box b;
  rect(d->lay[0], 50, 10, 5, 30, 25, 0x0000FF);
  CHECK(!trim_box(d, TRIM_TRANSPARENT, TRIM_ALL, &b), "nothing transparent to trim");
  CHECK(trim_box(d, TRIM_TOPLEFT, TRIM_ALL, &b) && b.x0 == 10 && b.y0 == 5 && b.x1 == 30 && b.y1 == 25, "the top-left colour trimmed");
  CHECK(trim_box(d, TRIM_BOTTOMRIGHT, TRIM_TOP | TRIM_LEFT, &b) && b.x0 == 10 && b.y0 == 5 && b.x1 == 50 && b.y1 == 40, "only top and left");
  CHECK(image_trim_ex(d, TRIM_TOPLEFT, TRIM_LEFT | TRIM_RIGHT) && d->w == 20 && d->h == 40, "trimmed left and right");
  doc_undo(d);
  CHECK(d->w == 50, "undone");
  doc_free(d);
  d = doc_new(30, 30, 0, 1);
  rect(d->lay[0], 30, 3, 4, 5, 6, 0xFFFFFF);
  CHECK(trim_box(d, TRIM_TRANSPARENT, TRIM_ALL, &b) && b.x0 == 3 && b.y0 == 4 && b.x1 == 5 && b.y1 == 6, "transparent trimmed");
  doc_free(d);
}

static void test_looks(void)
{
  CHECK(checker_at(0, 0, 2, CHK_LIGHT, 0, 0) == 0xFFFFFF && checker_at(8, 0, 2, CHK_LIGHT, 0, 0) == 0xCCCCCC, "medium light squares are 8");
  CHECK(checker_at(4, 0, 1, CHK_LIGHT, 0, 0) == 0xCCCCCC, "small squares are 4");
  CHECK(checker_at(15, 0, 3, CHK_DARK, 0, 0) == 0x666666 && checker_at(16, 0, 3, CHK_DARK, 0, 0) == 0x333333, "large dark");
  CHECK(checker_at(9, 9, 0, CHK_CUSTOM, 0x123456, 0x654321) == 0x123456, "none: the first colour");
  CHECK(checker_at(8, 0, 2, CHK_CUSTOM, 0x123456, 0x654321) == 0x654321, "custom colours");
  CHECK(line_style_on(LS_LINES, 5) && line_style_on(LS_DASHED, 3) && !line_style_on(LS_DASHED, 5) && line_style_on(LS_DOTS, 8) && !line_style_on(LS_DOTS, 9), "line styles");
  CHECK(!pixgrid_line(16, 0, 4), "no pixel grid at 400%");
  CHECK(pixgrid_line(16, 0, 8) && !pixgrid_line(17, 0, 8) && pixgrid_line(19, 3, 8), "a line where each pixel starts");
  CHECK(wrap_coord(-1, 10) == 9 && wrap_coord(23, 10) == 3 && wrap_coord(5, 0) == 0, "wrapping");
}

static void test_distribute(void)
{
  Doc *d = doc_new(200, 50, 0xFFFFFF, 0);
  int i;
  Box b;
  static const i32 at[3][2] = { { 0, 10 }, { 30, 70 }, { 150, 200 } };    /* widths 10, 40, 50 */
  for (i = 0; i < 3; i++) {
    Layer *l = layer_new(d, "L");
    rect(l, 200, at[i][0], 10, at[i][1], 20, 0xFF0000);
    doc_add_layer(d, l, d->nlay);
  }
  for (i = 1; i < 4; i++) d->lay[i]->picked = 1;
  d->active = 1;
  CHECK(doc_align(d, AL_DIST_LEFT), "distribute left edges");
  CHECK(layer_content_box(d, 2, &b) && b.x0 == 75, "middle's left edge half way (0 .. 150)");
  doc_undo(d);
  CHECK(doc_align(d, AL_DIST_RIGHT) && layer_content_box(d, 2, &b) && b.x1 == 105, "right edges (10 .. 200)");
  doc_undo(d);
  /* room 200 - 100 used = 100, two gaps of 50: the middle from 60 */
  CHECK(doc_align(d, AL_SPACE_H) && layer_content_box(d, 2, &b) && b.x0 == 60 && b.x1 == 100, "equal spacing");
  doc_undo(d);
  CHECK(doc_align(d, AL_DIST_H) && layer_content_box(d, 2, &b) && b.x0 == 70, "centres as before (5 .. 175: 90)");
  doc_undo(d);
  CHECK(layer_content_box(d, 2, &b) && b.x0 == 30, "undone");
  doc_free(d);
}

typedef struct { u32 ids[16]; int n; int stop_at; } PlayLog;
static int play_run(void *ctx, u32 id, int dialog, const char *name)
{
  PlayLog *p = (PlayLog *)ctx;
  (void)dialog; (void)name;
  if (id == ACT_STOP && p->stop_at) return 0;
  if (p->n < 16) p->ids[p->n++] = id;
  return 1;
}
static void test_actions(void)
{
  ActionLib l, m;
  PlayLog pl;
  const char *sets[8];
  char *t;
  int a, b;
  actlib_init(&l);
  a = act_new(&l, NULL, "Shrink and Sharpen");
  CHECK(a == 0 && !strcmp(l.a[0].set, "Default Actions"), "a new action in the default set");
  CHECK(act_add_step(&l, a, 100, "Image Size...", 1) && act_add_step(&l, a, 200, "Unsharp\tMask...", 0) && act_add_step(&l, a, 300, "Flatten Image", 0), "three steps");
  CHECK(!strcmp(l.a[0].s[1].name, "Unsharp Mask..."), "a tab in a name made a space");
  CHECK(act_insert_step(&l, a, 1, ACT_STOP, "Check it", 0) && l.a[0].n == 4 && l.a[0].s[1].id == ACT_STOP, "a stop inserted");
  memset(&pl, 0, sizeof pl);
  CHECK(act_play(&l, a, 0, play_run, &pl) == 4 && pl.n == 4 && pl.ids[0] == 100 && pl.ids[3] == 300, "played in order");
  memset(&pl, 0, sizeof pl); pl.stop_at = 1;
  CHECK(act_play(&l, a, 0, play_run, &pl) == 1 && pl.n == 1, "the stop ended it");
  memset(&pl, 0, sizeof pl);
  CHECK(act_play(&l, a, 2, play_run, &pl) == 2 && pl.ids[0] == 200, "played from a step");
  l.a[0].s[2].on = 0;
  memset(&pl, 0, sizeof pl);
  CHECK(act_play(&l, a, 0, play_run, &pl) == 3 && pl.ids[2] == 300, "a step turned off is skipped");
  CHECK(act_move_step(&l, a, 3, 0) && l.a[0].s[0].id == 300, "a step moved up");
  b = act_new(&l, "Amiga", "To 32 Colours");
  act_add_step(&l, b, 400, "Indexed Color...", 1);
  l.a[b].color = 3;
  CHECK(act_sets(&l, sets, 8) == 2 && !strcmp(sets[1], "Amiga"), "two sets");
  CHECK(act_duplicate(&l, 0) == 1 && l.n == 3 && !strcmp(l.a[1].name, "Shrink and Sharpen copy") && l.a[1].n == 4 && !l.a[1].s[3].on, "duplicated after it");
  CHECK(act_delete(&l, 1) && l.n == 2, "deleted");
  t = actlib_save(&l);
  CHECK(t && !strncmp(t, "EaselActions 1\n", 15), "saved");
  actlib_init(&m);
  CHECK(actlib_load(&m, t, 0) && m.n == 2 && m.a[0].n == 4 && m.a[1].color == 3 && !strcmp(m.a[1].set, "Amiga"), "read back");
  CHECK(m.a[0].s[2].id == ACT_STOP && !m.a[0].s[3].on && m.a[1].s[0].dialog == 1 && !strcmp(m.a[0].s[3].name, "Unsharp Mask..."), "steps read back");
  CHECK(actlib_load(&m, t, 1) && m.n == 4, "merged");
  CHECK(!actlib_load(&m, "nonsense", 0) && m.n == 4, "not an actions file: nothing changed");
  CHECK(actlib_load(&m, "EaselActions 1\nstep 5 1 0\tOrphan\naction S\tA\t0\nstep zz\nstep 7 1 0\n", 0) && m.n == 1 && m.a[0].n == 1 && m.a[0].s[0].id == 7, "bad lines skipped");
  free(t);
  actlib_free(&l); actlib_free(&m);
}

static void test_styles_comps(void)
{
  StyleLib s, s2;
  Doc *d = doc_new(40, 30, 0xFFFFFF, 0);
  Layer *l = layer_new(d, "Top");
  LayerComp c;
  char *t;
  rect(l, 40, 5, 5, 20, 20, 0x0000FF);
  doc_add_layer(d, l, 1);
  stylelib_init(&s);
  stylelib_defaults(&s);
  CHECK(s.n == 6 && s.s[0].fx.e[FX_DROPSHADOW].on, "built-in styles");
  CHECK(stylelib_add(&s, "Mine", &s.s[5].fx) == 6 && stylelib_rename(&s, 6, "Mine Too") && !strcmp(s.s[6].name, "Mine Too"), "added and renamed");
  t = stylelib_save(&s);
  stylelib_init(&s2);
  CHECK(t && stylelib_load(&s2, t, 0) && s2.n == 7 && s2.s[6].fx.e[FX_STROKE].on && s2.s[6].fx.e[FX_STROKE].color == 0x40FF40, "styles read back");
  CHECK(stylelib_delete(&s2, 0) && s2.n == 6, "deleted");
  free(t);
  /* comps */
  CHECK(comp_capture(&c, d, "Plain", 0) && c.nlay == 2, "captured");
  d->lay[1]->visible = 0; d->lay[1]->opacity = 100; d->lay[1]->fx = s.s[0].fx;
  CHECK(comp_apply(d, &c) == 1 && d->lay[1]->visible && d->lay[1]->opacity == 255 && !fx_has(d->lay[1]), "applied back");
  doc_undo(d);
  CHECK(!d->lay[1]->visible && d->lay[1]->opacity == 100 && fx_has(d->lay[1]), "undone");
  c.what = COMP_VIS;
  CHECK(comp_apply(d, &c) == 1 && d->lay[1]->visible && d->lay[1]->opacity == 100, "visibility only");
  comp_free(&c);
  stylelib_free(&s); stylelib_free(&s2);
  doc_free(d);
}

static void test_histogram(void)
{
  Doc *d = doc_new(10, 10, 0x000000, 0);
  u32 h[256];
  HistoStats st;
  rect(d->lay[0], 10, 0, 0, 10, 5, 0xFF0000);       /* the top half red */
  CHECK(doc_histogram(d, box_make(0, 0, 0, 0), HC_RED, h) && h[255] == 50 && h[0] == 50, "red channel");
  histo_stats(h, &st);
  CHECK(st.pixels == 100 && fabs(st.mean - 127.5) < 1e-9 && fabs(st.stddev - 127.5) < 1e-9 && st.median == 0 && st.min == 0 && st.max == 255, "statistics");
  doc_histogram(d, box_make(0, 0, 10, 2), HC_LUM, h);
  histo_stats(h, &st);
  CHECK(st.pixels == 20 && st.median == 76, "luminosity of red in a box");
  memset(h, 0, sizeof h);
  histo_stats(h, &st);
  CHECK(st.pixels == 0 && st.mean == 0, "empty");
  doc_free(d);
}

static void test_picinfo(void)
{
  Doc *d = doc_new(32, 20, 0x112233, 0);
  Rgba im;
  u8 *png;
  size_t n;
  char err[100], *t;
  im.w = 32; im.h = 20; im.px = (u8 *)malloc(32 * 20 * 4);
  doc_composite(d, doc_box(d), im.px, 32 * 4);
  png = png_encode(&im, 144, &n);
  t = png ? picture_info(png, n, err, sizeof err) : NULL;
  CHECK(t && strstr(t, "Format: PNG\n") && strstr(t, "Size: 32 x 20 pixels\n") && strstr(t, "Resolution: 144 dpi\n") && strstr(t, "Layers: 1 (1 pixel"), "a PNG described");
  free(t);
  CHECK(!picture_info((const u8 *)"nonsense!", 9, err, sizeof err) && err[0], "not a picture: a message");
  free(png); free(im.px);
  doc_free(d);
}

int main(void)
{
  test_picinfo();
  test_par();
  test_guides();
  test_trim();
  test_looks();
  test_distribute();
  test_actions();
  test_styles_comps();
  test_histogram();
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
