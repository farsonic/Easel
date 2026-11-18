/*
 * toolsxtest.c - Easel 1.2's tool extras (core/toolsx.c: Perspective Crop, the turned crop box, Note,
 * Count, Slice and their OpenRaster round trip, Rotate View, the History Options and History Log)
 * and Amiga-era extras (core/legacy.c: Shear and Bend, draw modes for any brush, animated brushes,
 * bitplanes, sprites and copper gradients, overscan sizes and safe areas, frames and onion skins,
 * diagonal and tiled symmetry, icon tool types). Run under ASan/UBSan.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core/doc.h"
#include "core/paint.h"
#include "core/toolsx.h"
#include "core/legacy.h"
#include "core/codec.h"
#include "core/iconwrite.h"

static int fails, tests;
#define CHECK(c, msg) do { tests++; if (!(c)) { fails++; printf("FAIL %s (line %d)\n", msg, __LINE__); } } while (0)
static const char *tmpdir = "/tmp";

static void test_perspective(void)
{
  /* a 100 x 80 picture with a quad whose corners are marked in four colours; cropped upright */
  Doc *d = doc_new(100, 80, 0x808080, 0);
  double q[8] = { 20, 10, 80, 20, 75, 70, 15, 60 }, h[9], w, hh;
  static const u32 col[4] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00 };
  int k;
  i32 x, y;
  for (k = 0; k < 4; k++)
    for (y = -3; y <= 3; y++) for (x = -3; x <= 3; x++) {
      u8 *p = d->lay[0]->px + (((long)q[k * 2 + 1] + y) * 100 + (long)q[k * 2] + x) * 4;
      p[0] = (u8)(col[k] >> 16); p[1] = (u8)(col[k] >> 8); p[2] = (u8)col[k];
    }
  CHECK(homography_unit(q, h), "homography");
  {
    double u = 1, v = 1, X = (h[0] * u + h[1] * v + h[2]) / (h[6] * u + h[7] * v + h[8]), Y = (h[3] * u + h[4] * v + h[5]) / (h[6] * u + h[7] * v + h[8]);
    CHECK(fabs(X - 75) < 1e-6 && fabs(Y - 70) < 1e-6, "unit corner (1,1) to the third corner");
  }
  perspective_size(q, &w, &hh);
  CHECK(fabs(w - (hypot(60, 10) + hypot(60, 10)) / 2) < 1e-6, "perspective size");
  CHECK(image_perspective_crop(d, q, 50, 40, RS_BILINEAR) && d->w == 50 && d->h == 40, "cropped to 50 x 40");
  {
    const u8 *a = d->lay[0]->px + (1 * 50 + 1) * 4, *b = d->lay[0]->px + (1 * 50 + 48) * 4, *c = d->lay[0]->px + (38 * 50 + 48) * 4, *e = d->lay[0]->px + (38 * 50 + 1) * 4;
    CHECK(a[0] > 200 && a[1] < 80, "top left red");
    CHECK(b[1] > 200 && b[0] < 80, "top right green");
    CHECK(c[2] > 200 && c[0] < 80, "bottom right blue");
    CHECK(e[0] > 200 && e[1] > 200 && e[2] < 80, "bottom left yellow");
  }
  CHECK(!strcmp(doc_undo_name(d), "Perspective Crop"), "one step");
  doc_undo(d);
  CHECK(d->w == 100, "undo");
  /* the free size: the quad's own */
  CHECK(image_perspective_crop(d, q, 0, 0, RS_BILINEAR) && d->w == (i32)floor(w + 0.5) && d->h == (i32)floor(hh + 0.5), "perspective's own size");
  {
    double bad[8] = { 0, 0, 10, 10, 20, 20, 30, 30 };
    CHECK(!image_perspective_crop(d, bad, 10, 10, RS_BILINEAR), "a flat quad refused");
  }
  doc_free(d);
  /* the turned crop box */
  {
    double c[8];
    crop_corners(50, 50, 40, 20, 90, c);
    CHECK(fabs(c[0] - 60) < 1e-9 && fabs(c[1] - 30) < 1e-9, "turned corner");
    CHECK(crop_inside(50, 50, 40, 20, 90, 50, 68) && !crop_inside(50, 50, 40, 20, 90, 68, 50), "inside the turned box");
    CHECK(fabs(crop_drag_angle(50, 50, 100, 50, 50, 100) - 90) < 1e-9, "drag angle");
  }
}

static void test_annots(void)
{
  Doc *d = doc_new(60, 40, 0xFFFFFF, 0), *e;
  u8 *ora;
  size_t n;
  char err[80], *rec;
  int i;
  CHECK(note_add(d, 5, 6, "Me", "Fix <this> & \"that\"\nnow", 0xFFFF00) == 0, "note");
  CHECK(note_at(d, 8, 9, 0) == 0 && note_at(d, 30, 30, 2) == -1, "note at");
  note_move(d, 0, 10, 12);
  CHECK(d->annot->note[0].x == 10, "moved");
  CHECK(count_add(d, 1, 1) == 0 && count_add(d, 2, 2) == 1, "count markers");
  CHECK(count_group_add(d, "Birds", 0x0000FF) == 1, "second group");
  count_add(d, 3, 3);
  CHECK(count_number(d, 2) == 1 && count_number(d, 1) == 2 && count_in_group(d, 0) == 2, "numbered per group");
  CHECK(count_at(d, 1, 0, 1) == 0 && count_at(d, 20, 20, 2) == -1, "count at");
  rec = count_record(d);
  CHECK(rec && strstr(rec, "Count Group 1\t2") && strstr(rec, "Birds\t1"), "record measurements");
  free(rec);
  CHECK(slice_add(d, box_make(0, 0, 30, 20)) == 0, "slice");
  CHECK(slice_divide(d, 0, 2, 2) == 4 && d->annot->nslice == 4, "divide");
  CHECK(slice_at(d, 20, 15) == 3, "slice at");
  ora = ora_write(d, &n);
  e = ora ? ora_read(ora, n, err, sizeof err) : NULL;
  CHECK(e && e->annot && e->annot->nnote == 1 && !strcmp(e->annot->note[0].text, "Fix <this> & \"that\"\nnow") && !strcmp(e->annot->note[0].author, "Me"), "notes kept");
  CHECK(e && e->annot && e->annot->nmark == 3 && e->annot->ngroup == 2 && !strcmp(e->annot->group[1].name, "Birds") && e->annot->mark[2].group == 1, "counts kept");
  CHECK(e && e->annot && e->annot->nslice == 4 && e->annot->slice[3].b.x0 == 15 && e->annot->slice[3].b.y1 == 20, "slices kept");
  if (e) doc_free(e);
  free(ora);
  /* slices from guides, and written out */
  d->guides.v[d->guides.nv++] = 20; d->guides.h[d->guides.nh++] = 10;
  CHECK(slices_from_guides(d) == 4, "slices from guides");
  {
    char path[300];
    size_t len;
    u8 *f;
    Rgba im;
    CHECK(slices_export(d, tmpdir, "easel_slice") == 4, "slices exported");
    snprintf(path, sizeof path, "%s/easel_slice_04.png", tmpdir);
    f = file_read(path, &len);
    CHECK(f && codec_decode(f, len, &im, err, sizeof err) && im.w == 40 && im.h == 30, "the last slice's size");
    if (f) { rgba_free(&im); free(f); }
    for (i = 1; i <= 4; i++) { snprintf(path, sizeof path, "%s/easel_slice_%02d.png", tmpdir, i); remove(path); }
  }
  CHECK(note_clear(d) == 1 && count_clear(d, 1) == 1 && count_group_delete(d, 1) && d->annot->ngroup == 1, "clearing");
  doc_free(d);
}

static void test_view_hist(void)
{
  double ix, iy, sx, sy;
  view_rotate(90, 10, 0, &ix, &iy);
  view_unrotate(90, ix, iy, &sx, &sy);
  CHECK(fabs(sx - 10) < 1e-9 && fabs(sy) < 1e-9, "rotate view round trip");
  CHECK(fabs(iy + 10) < 1e-9 || fabs(iy - 10) < 1e-9, "a quarter turn");
  CHECK(view_angle_snap(37, 1) == 30 && view_angle_snap(190, 0) == -170, "snap and wrap");
  {
    Doc *d = doc_new(10, 10, 0, 0);
    char path[200], buf[2000];
    FILE *f;
    size_t n;
    snprintf(path, sizeof path, "%s/easel_history.log", tmpdir);
    remove(path);
    hist_opts.log_on = 1; hist_opts.log_where = HL_BOTH; hist_opts.log_detail = HL_DETAILED;
    snprintf(hist_opts.log_file, sizeof hist_opts.log_file, "%s", path);
    histlog_event(d, "2026-11-18 10:00:00", "Opened", "Test.png");
    histlog_step(d, "2026-11-18 10:00:05", "Test.png", "Gaussian Blur", "Radius 2");
    f = fopen(path, "r");
    n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
    buf[n] = 0;
    if (f) fclose(f);
    CHECK(strstr(buf, "Opened: Test.png") && strstr(buf, "Test.png: Gaussian Blur (Radius 2)"), "the log file");
    CHECK(strstr(d->info.description, "History:\n") && strstr(d->info.description, "Gaussian Blur"), "the log in File Info");
    hist_opts.log_detail = HL_SESSIONS;
    CHECK(histlog_step(d, "t", "x", "Levels", NULL) == NULL, "sessions only: no steps");
    {
      int k;
      hist_opts.log_detail = HL_CONCISE; hist_opts.log_where = HL_META;
      for (k = 0; k < 40; k++) histlog_step(d, "2026-11-18 10:00:06", "Test.png", "Brush Tool", NULL);
      CHECK(strlen(d->info.description) < sizeof d->info.description && strstr(d->info.description, "History:\n"), "the metadata log keeps within its room");
    }
    CHECK(histopts_line("history_log 1 2 0") && hist_opts.log_where == HL_BOTH && hist_opts.log_detail == HL_SESSIONS, "preference line");
    CHECK(histopts_line("history_auto_snapshot 0") && !hist_opts.auto_snapshot, "auto snapshot off");
    hist_opts.log_on = 0;
    remove(path);
    doc_free(d);
  }
}

static void test_legacy(void)
{
  CustomBrush *b = cbrush_new(4, 4), *s, *bb;
  int k;
  for (k = 0; k < 16; k++) { b->px[k * 4] = (u8)(k * 15); b->px[k * 4 + 3] = 255; }
  b->hx = 0; b->hy = 0;
  s = cbrush_shear(b, 1, 0);
  CHECK(s && s->w == 8 && s->h == 4, "shear widens");
  CHECK(s && s->px[(3 * 8 + 3) * 4 + 3] == 255 && s->px[(3 * 8 + 0) * 4 + 3] == 0 && s->px[(0 * 8 + 0) * 4 + 3] == 255, "rows moved along");
  bb = cbrush_bend(b, 3, 0);
  CHECK(bb && bb->w == 7 && bb->px[(0 * 7 + 3) * 4 + 3] == 255 && bb->px[(0 * 7 + 0) * 4 + 3] == 0, "bend: the end rows go furthest");
  cbrush_free(s); cbrush_free(bb);
  /* Tint with a plain brush keeps the brightness, takes the hue */
  {
    Doc *d = doc_new(30, 30, 0x404040, 0);
    Brush br;
    Stroke *st;
    const u8 *p;
    memset(&br, 0, sizeof br);
    br.tool = PT_BRUSH; br.size = 10; br.hardness = 100; br.opacity = 100; br.flow = 100; br.spacing = 25; br.custom_mode = CB_TINT;
    st = stroke_begin(d, &br, 0xFF0000, 15, 15, NULL); stroke_end(st);
    p = d->lay[0]->px + (15 * 30 + 15) * 4;
    CHECK(p[0] > p[1] + 20 && abs((p[0] * 30 + p[1] * 59 + p[2] * 11) / 100 - 0x40) < 6, "tinted red at the same brightness");
    br.custom_mode = CB_SHADE;
    st = stroke_begin(d, &br, 0, 5, 5, NULL); stroke_to(st, 6, 5, NULL); stroke_end(st);
    CHECK(d->lay[0]->px[(5 * 30 + 5) * 4] < 0x40, "shade darkens with a plain brush");
    br.custom_mode = CB_CYCLE;
    st = stroke_begin(d, &br, 0xFF0000, 5, 25, NULL); stroke_to(st, 25, 25, NULL); stroke_end(st);
    p = d->lay[0]->px + (25 * 30 + 24) * 4;
    CHECK(!(p[0] == 255 && p[1] == 0 && p[2] == 0), "cycle steps the colour along the stroke");
    doc_free(d);
  }
  /* an animated brush steps a frame a dab */
  {
    Doc *d = doc_new(40, 10, 0xFFFFFF, 0);
    CustomBrush *f[2];
    const CustomBrush *ff[2];
    AnimBrush a;
    Brush br;
    double pts[4] = { 5, 5, 25, 5 };
    f[0] = cbrush_new(3, 3); f[1] = cbrush_new(3, 3);
    for (k = 0; k < 9; k++) { f[0]->px[k * 4] = 255; f[0]->px[k * 4 + 3] = 255; f[1]->px[k * 4 + 2] = 255; f[1]->px[k * 4 + 3] = 255; }
    ff[0] = f[0]; ff[1] = f[1];
    a.frame = ff; a.n = 2; a.step = 1;
    memset(&br, 0, sizeof br);
    br.tool = PT_BRUSH; br.size = 3; br.opacity = 100; br.flow = 100; br.spacing = 25;
    animbrush_stroke(d, &br, &a, pts, 2);
    CHECK(d->lay[0]->px[(5 * 40 + 5) * 4] == 255 && d->lay[0]->px[(5 * 40 + 5) * 4 + 2] == 0, "first dab: frame 1");
    CHECK(d->lay[0]->px[(5 * 40 + 25) * 4 + 2] == 255 && d->lay[0]->px[(5 * 40 + 25) * 4] == 0, "second dab: frame 2");
    cbrush_free(f[0]); cbrush_free(f[1]);
    doc_free(d);
  }
  cbrush_free(b);
  /* bitplanes */
  {
    u8 idx[16 * 2], pal[4 * 3] = { 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 255 }, *raw;
    size_t len;
    char *src;
    for (k = 0; k < 32; k++) idx[k] = (u8)(k % 4);
    raw = bitplanes_raw(idx, 16, 2, 2, pal, 4, 0, &len);
    CHECK(raw && len == 8 && raw[0] == 0x55 && raw[4] == 0x33, "two planes, plane after plane");
    free(raw);
    raw = bitplanes_raw(idx, 16, 2, 2, pal, 4, BP_INTERLEAVED | BP_PALETTE4, &len);
    CHECK(raw && len == 16 && raw[0] == 0x55 && raw[2] == 0x33 && raw[10] == 0x0F && raw[11] == 0xFF && raw[12] == 0x0F && raw[13] == 0x00, "interleaved with LoadRGB4 words");
    free(raw);
    raw = bitplanes_raw(idx, 16, 2, 2, pal, 4, BP_PALETTE32, &len);
    CHECK(raw && len == 8 + 4 + 48 + 4 && raw[8] == 0 && raw[9] == 4 && raw[12 + 12] == 255 && raw[12 + 13] == 255, "LoadRGB32 table");
    free(raw);
    src = bitplanes_source(idx, 16, 2, 2, pal, 4, 0, 0, "pic");
    CHECK(src && strstr(src, "__chip UWORD pic[4]") && strstr(src, "0x5555") && strstr(src, "pic_palette[4]") && strstr(src, "0xf00"), "C source");
    free(src);
    src = bitplanes_source(idx, 16, 2, 2, pal, 4, 0, 1, "pic");
    CHECK(src && strstr(src, "SECTION pic,DATA_C") && strstr(src, "dc.w\t$5555") && strstr(src, "$00f"), "assembler source");
    free(src);
    src = sprites_source(idx, 16, 2, 0, "spr", 0, 0);
    CHECK(src && strstr(src, "__chip UWORD spr_0[]") && strstr(src, "0x5555, 0x3333") && strstr(src, "0x2c40, 0x2e00"), "a sprite with its control words");
    free(src);
  }
  /* copper gradient */
  {
    u32 c[3] = { 0xFF0000, 0x00FF00, 0x0000FF };
    size_t len;
    u8 *cl = copper_gradient(c, 3, 254, 0, 0, &len);
    char *src;
    CHECK(cl && len == 3 * 8 + 4 + 4, "copper list with the PAL wrap");
    CHECK(cl && cl[0] == 0xFE && cl[1] == 0x07 && cl[4] == 0x01 && cl[5] == 0x80 && cl[6] == 0x0F && cl[7] == 0x00, "WAIT then MOVE COLOR00");
    CHECK(cl && cl[16] == 0xFF && cl[17] == 0xDF, "the wait for line 255's end");
    CHECK(cl && cl[len - 4] == 0xFF && cl[len - 3] == 0xFF && cl[len - 2] == 0xFF && cl[len - 1] == 0xFE, "the end marker");
    free(cl);
    cl = copper_gradient(c, 1, 50, 1, 1, &len);
    CHECK(cl && len == 4 + 16 + 4 && cl[8] == 0x01 && cl[9] == 0x82, "AGA: COLOR01 in two halves");
    free(cl);
    src = copper_gradient_source(c, 3, 40, 0, 0, 1, "grad");
    CHECK(src && strstr(src, "dc.w\t$2807,$fffe,$0180,$0f00"), "copper source");
    free(src);
  }
  /* overscan and safe areas */
  {
    Doc *d = doc_new(352, 283, 0, 0);
    Box a, t;
    safe_areas(352, 283, &a, &t);
    CHECK(a.x0 == 18 && t.x1 == 317, "safe areas");
    CHECK(guides_safe_areas(d) == 8 && d->guides.show_guides, "safe area guides");
    CHECK(noverscan_sizes >= 8 && overscan_sizes[0].w == 352 && overscan_sizes[0].h == 283, "overscan sizes");
    doc_free(d);
  }
  /* frames */
  {
    Doc *d = doc_new(10, 10, 0x000000, 0);
    Layer *l = layer_new(d, "Frame 2 (0.25 s)");
    u8 out[100 * 4];
    char nm[64];
    int f[8];
    for (k = 0; k < 100; k++) { l->px[k * 4] = 255; l->px[k * 4 + 3] = 255; }
    d->lay[d->nlay++] = l;
    CHECK(frames_list(d, f, 8) == 2 && frame_time(l->name) == 25 && frame_time("x (120ms)") == 12 && frame_time("plain") == 0, "frame times");
    frame_set_time(l->name, 150, nm, sizeof nm);
    CHECK(!strcmp(nm, "Frame 2 (1.50 s)"), "set a frame's time");
    frames_onion(d, 0, 0, 1, 50, doc_box(d), out, 40);
    CHECK(out[0] > 100 && out[0] < 160 && out[3] == 255, "onion skin: the next frame at half");
    frames_show(d, 1);
    CHECK(!d->lay[0]->visible && d->lay[1]->visible, "a frame shown alone");
    doc_free(d);
  }
  /* symmetry: diagonal and tiles */
  {
    BrushDynamics dy;
    double ox[64], oy[64], rot[64];
    int mir[64], n;
    memset(&dy, 0, sizeof dy);
    dy.sym_mode = SYM_DIAGONAL;
    n = brush_symmetry(&dy, 100, 100, 60, 50, ox, oy, rot, mir);
    CHECK(n == 4 && fabs(ox[2] - 50) < 1e-9 && fabs(oy[2] - 60) < 1e-9, "diagonal");
    dy.sym_mode = SYM_TILE; dy.sym_segments = 4;
    n = brush_symmetry(&dy, 100, 100, 10, 10, ox, oy, rot, mir);
    CHECK(n == 16 && fabs(ox[1] - 35) < 1e-9 && fabs(oy[15] - 85) < 1e-9, "tiled");
  }
  /* an icon's own tool types */
  {
    Rgba pic;
    IconOpts o;
    const char *tt[2] = { "FILETYPE=ILBM", "DONOTWAIT" };
    size_t len, i;
    u8 *ic;
    int found = 0;
    rgba_alloc(&pic, 8, 8);
    icon_opts_default(&o, ICON_CLASSIC);
    o.tooltypes = tt; o.ntooltypes = 2;
    ic = icon_write_ex(&pic, &o, &len, NULL, 0);
    for (i = 0; ic && i + 13 < len; i++) if (!memcmp(ic + i, "FILETYPE=ILBM", 13)) found = 1;
    CHECK(found, "tool types in the icon");
    free(ic); rgba_free(&pic);
  }
}

int main(int argc, char **argv)
{
  if (argc > 1) tmpdir = argv[1];
  test_perspective();
  test_annots();
  test_view_hist();
  test_legacy();
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
