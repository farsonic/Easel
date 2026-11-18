/*
 * menu3test.c - Easel 1.3's menu, tool and automation additions (core/menu3.c): Save for Web and
 * its preview, Export As's estimate, the Image Processor, the New dialog's presets, units and modes,
 * Pixel Art Preview's steps, Reset Tool, Copy Color as HTML, Fill from History and Preserve
 * Transparency, Stroke's mode, Color Lookup Tables, Contact Sheet II, Statistics, Image Assets, Find
 * and Replace Text, Apply Image and Calculations, Analysis, layer-based slices, artboards and frames
 * from layers, Open As and Photoshop Raw, the blend and opacity keys, snapping to layers and slices,
 * the Paint Bucket's mode and the Wand's Sample Size, and a grabbed screen. Run under ASan/UBSan.
 * argv[1]: a drawer to write into (default /tmp).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "core/doc.h"
#include "core/paint.h"
#include "core/layers.h"
#include "core/layerops.h"
#include "core/layerx.h"
#include "core/adjust.h"
#include "core/guides.h"
#include "core/toolsx.h"
#include "core/type.h"
#include "core/palette.h"
#include "core/cms.h"
#include "core/menu3.h"

static int fails, tests;
#define CHECK(c, msg) do { tests++; if (!(c)) { fails++; printf("FAIL %s (line %d)\n", msg, __LINE__); } } while (0)
static const char *tmp = "/tmp";

static void rect(Layer *l, i32 w, i32 x0, i32 y0, i32 x1, i32 y1, u32 c, int a)
{
  i32 x, y;
  for (y = y0; y < y1; y++) for (x = x0; x < x1; x++) {
    u8 *p = l->px + ((long)y * w + x) * 4;
    p[0] = (u8)(c >> 16); p[1] = (u8)(c >> 8); p[2] = (u8)c; p[3] = (u8)a;
  }
}
static const u8 *px(const Doc *d, int li, i32 x, i32 y) { return d->lay[li]->px + ((long)y * d->w + x) * 4; }
static Doc *gradient_doc(i32 w, i32 h)
{
  Doc *d = doc_new(w, h, 0xFFFFFF, 0);
  i32 x, y;
  for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
    u8 *p = d->lay[0]->px + ((long)y * w + x) * 4;
    p[0] = (u8)(x * 255 / (w - 1)); p[1] = (u8)(y * 255 / (h - 1)); p[2] = (u8)((x + y) & 255); p[3] = 255;
  }
  return d;
}
static int mk(const char *path) { mkdir(path, 0755); return 1; }
static long fsize(const char *path) { FILE *f = fopen(path, "rb"); long n; if (!f) return -1; fseek(f, 0, SEEK_END); n = ftell(f); fclose(f); return n; }

static void test_sfw(void)
{
  Doc *d = gradient_doc(64, 48);
  SfwOpts o;
  size_t len, l2;
  u8 *f;
  Rgba im;
  int fmt;
  char s[16];
  sfw_defaults(&o);
  for (fmt = 0; fmt < SFW_N; fmt++) {
    o.fmt = fmt;
    f = sfw_encode(d, &o, &len);
    CHECK(f && len > 50, "Save for Web writes each format");
    CHECK(sfw_preview(f, len, &im) && im.w == 64 && im.h == 48, "and its preview reads back");
    if (fmt == SFW_PNG24) CHECK(!memcmp(im.px + 4 * 70, px(d, 0, 6, 1), 3), "PNG-24 is exact");
    if (fmt == SFW_JPEG) CHECK(abs(im.px[4 * 70] - px(d, 0, 6, 1)[0]) < 30, "JPEG is close");
    rgba_free(&im); free(f);
  }
  /* fewer colours: smaller */
  o.fmt = SFW_GIF; o.colors = 256; o.dither = DI_NONE; f = sfw_encode(d, &o, &len); free(f);
  o.colors = 4; f = sfw_encode(d, &o, &l2);
  CHECK(l2 < len, "4 colours are smaller than 256");
  CHECK(sfw_preview(f, l2, &im), "4-colour preview");
  { long k, n = 0; u32 seen[8]; int ns = 0, i;
    for (k = 0; k < 64 * 48; k++) { u32 c = (u32)im.px[k * 4] << 16 | im.px[k * 4 + 1] << 8 | im.px[k * 4 + 2]; for (i = 0; i < ns; i++) if (seen[i] == c) break; if (i == ns && ns < 8) seen[ns++] = c; n++; }
    CHECK(ns <= 4, "the preview has 4 colours at most"); }
  rgba_free(&im); free(f);
  /* JPEG quality */
  o.fmt = SFW_JPEG; o.quality = 10; f = sfw_encode(d, &o, &len); free(f);
  o.quality = 95; f = sfw_encode(d, &o, &l2); free(f);
  CHECK(len < l2, "low quality is smaller");
  /* scale */
  o.pct = 50; f = sfw_encode(d, &o, &len);
  CHECK(sfw_preview(f, len, &im) && im.w == 32 && im.h == 24, "50 % scale");
  rgba_free(&im); free(f);
  /* transparency in PNG-8: a transparent hole stays, over a matte when not kept */
  rect(d->lay[0], 64, 0, 0, 10, 10, 0, 0);
  sfw_defaults(&o); o.fmt = SFW_PNG8;
  f = sfw_encode(d, &o, &len);
  CHECK(sfw_preview(f, len, &im) && im.px[3] == 0 && im.px[4 * 64 * 20 + 4 * 20 + 3] == 255, "PNG-8 keeps the transparent part");
  rgba_free(&im); free(f);
  o.transparency = 0; o.matte = 0xFF0000; o.fmt = SFW_PNG24;
  f = sfw_encode(d, &o, &len);
  CHECK(sfw_preview(f, len, &im) && im.px[0] == 255 && im.px[1] == 0 && im.px[3] == 255, "the matte under it");
  rgba_free(&im); free(f);
  CHECK(sfw_seconds(7200, 57600) == 1 && sfw_seconds(72000, 57600) == 10, "download time");
  size_text(s, 500); CHECK(!strcmp(s, "500B"), "bytes");
  size_text(s, 12595); CHECK(!strcmp(s, "12.3K"), "kilobytes");
  size_text(s, 1310720); CHECK(!strcmp(s, "1.25M"), "megabytes");
  CHECK(!strcmp(sfw_ext(SFW_JPEG), ".jpg") && !strcmp(sfw_ext(SFW_ILBM), ".iff"), "extensions");
  /* Export As's estimate matches what is written */
  {
    size_t e = export_estimate(d, NULL, FMT_PNG, 0), e2 = export_estimate(d, NULL, FMT_JPEG, 90), e3 = export_estimate(d, NULL, FMT_JPEG, 20);
    size_t pl = 0; u8 *pngd = png_encode(&(Rgba){ d->w, d->h, d->lay[0]->px }, d->dpi, &pl);
    CHECK(e > 0 && labs((long)e - (long)pl) < 200, "the PNG estimate is the PNG's size");
    CHECK(e2 > e3 && e3 > 0, "the JPEG estimate follows the quality");
    CHECK(export_estimate(d, NULL, FMT_WEBP, 0) > 0, "WebP too");
    free(pngd);
  }
  doc_free(d);
}

static void test_improc(void)
{
  Doc *d = gradient_doc(80, 40);
  ImProcOpts o;
  char drawer[300], p[400], err[160];
  int n;
  Doc *back;
  u8 *data; size_t len;
  snprintf(drawer, sizeof drawer, "%s/easel_improc", tmp);
  mkdir(drawer, 0755);
  improc_defaults(&o);
  o.jpeg = 1; o.jpeg_resize = 1; o.jpeg_w = 40; o.jpeg_h = 40;
  o.png = 1; o.tiff = 1; o.psd = 1; o.ilbm = 1; o.ilbm_colors = 32;
  strcpy(o.copyright, "(c) Easel test");
  n = improc_doc(d, "pic.ora", drawer, &o, mk, err, sizeof err);
  CHECK(n == 5, "five files written");
  path_join(p, sizeof p, drawer, "JPEG/pic.jpg");
  data = file_read(p, &len);
  CHECK(data && (back = codec_load_doc(data, len, err, sizeof err)) != NULL, "the JPEG reads back");
  if (back) { CHECK(back->w == 40 && back->h == 20, "fitted inside 40 x 40"); doc_free(back); }
  free(data);
  path_join(p, sizeof p, drawer, "PNG/pic.png");
  data = file_read(p, &len);
  CHECK(data && (back = codec_load_doc(data, len, err, sizeof err)) != NULL, "the PNG reads back");
  if (back) { CHECK(back->w == 80 && !strcmp(back->info.copyright, "(c) Easel test"), "unresized, with the copyright"); doc_free(back); }
  free(data);
  path_join(p, sizeof p, drawer, "TIFF/pic.tif"); CHECK(fsize(p) > 0, "TIFF");
  path_join(p, sizeof p, drawer, "PSD/pic.psd"); CHECK(fsize(p) > 0, "PSD");
  path_join(p, sizeof p, drawer, "ILBM/pic.iff"); CHECK(fsize(p) > 0, "ILBM");
  CHECK(d->w == 80 && !d->info.copyright[0], "the picture itself unchanged");
  { i32 w, h; fit_size(100, 50, 30, 30, 0, &w, &h); CHECK(w == 30 && h == 15, "fit wide"); fit_size(10, 5, 30, 30, 1, &w, &h); CHECK(w == 10 && h == 5, "never enlarged"); }
  path_join(p, sizeof p, "Work:", "x.png"); CHECK(!strcmp(p, "Work:x.png"), "an Amiga volume takes no slash");
  path_join(p, sizeof p, "Work:Pics", "x.png"); CHECK(!strcmp(p, "Work:Pics/x.png"), "a drawer does");
  doc_free(d);
}

static void test_newdoc(void)
{
  NewPreset p, q;
  char line[200], err[120];
  Doc *d;
  int i;
  CHECK(fabs(units_to_px(2, NU_INCHES, 300) - 600) < 1e-9, "inches");
  CHECK(fabs(units_to_px(25.4, NU_MM, 100) - 100) < 1e-9, "millimetres");
  CHECK(fabs(units_to_px(2.54, NU_CM, 72) - 72) < 1e-9, "centimetres");
  CHECK(fabs(units_to_px(72, NU_POINTS, 150) - 150) < 1e-9, "points");
  CHECK(fabs(units_to_px(6, NU_PICAS, 200) - 200) < 1e-9, "picas");
  CHECK(fabs(px_to_units(600, NU_INCHES, 300) - 2) < 1e-9, "and back");
  for (i = 0; i < newdoc_nbuiltin; i++) { newpreset_format(&newdoc_builtin[i], line, sizeof line); CHECK(newpreset_parse(line, &q) && !strcmp(q.name, newdoc_builtin[i].name) && q.mode == newdoc_builtin[i].mode, "built-in round trip"); }
  memset(&p, 0, sizeof p);
  strcpy(p.name, "My|Card"); p.w = 85; p.h = 55; p.unit = NU_MM; p.dpi = 300; p.mode = NM_GRAY; p.bg = NB_BLACK; p.ax = 10; p.ay = 11;
  newpreset_format(&p, line, sizeof line);
  CHECK(newpreset_parse(line, &q) && !strcmp(q.name, "My/Card") && q.w == 85 && q.unit == NU_MM && q.ax == 10, "a saved preset reads back");
  CHECK(!newpreset_parse("nonsense", &q) && !newpreset_parse("x|1|1|9|72|0|0|1|1", &q), "bad lines refused");
  d = newdoc_make(&p, 0, err, sizeof err);
  CHECK(d && d->w == 1004 && d->h == 650 && d->dpi == 300, "85 x 55 mm at 300 ppi");
  CHECK(d && d->mode == MODE_GRAY && px(d, 0, 5, 5)[0] == 0 && d->aspect_x == 10, "grey, black, its aspect");
  CHECK(d && !doc_can_undo(d) && !strcmp(d->last_action, "New"), "one history state, New");
  doc_free(d);
  p.mode = NM_BITMAP; p.bg = NB_TRANSPARENT; p.unit = NU_PIXELS; p.w = 16; p.h = 8;
  d = newdoc_make(&p, 0, err, sizeof err);
  CHECK(d && d->mode == MODE_BITMAP && px(d, 0, 1, 1)[3] == 255, "Bitmap: never transparent");
  doc_free(d);
  p.mode = NM_CMYK; p.bg = NB_BGCOLOR;
  d = newdoc_make(&p, 0x336699, err, sizeof err);
  CHECK(d && d->mode == MODE_RGB && d->cmode == CM_CMYK, "a CMYK document");
  doc_free(d);
  p.mode = NM_LAB; d = newdoc_make(&p, 0, err, sizeof err); CHECK(d && d->cmode == CM_LAB, "a Lab document"); doc_free(d);
  p.mode = NM_RGB; p.bg = NB_TRANSPARENT; d = newdoc_make(&p, 0, err, sizeof err); CHECK(d && px(d, 0, 1, 1)[3] == 0, "transparent RGB"); doc_free(d);
  p.w = 20000; CHECK(!newdoc_make(&p, 0, err, sizeof err) && err[0], "too big");
}

static void test_small(void)
{
  ToolTip t;
  char s[32];
  OpacityKeys k;
  CHECK(tool_reset_tip("Brush", &t) && t.size == 13 && t.hardness == 0, "Brush: 13 px soft");
  CHECK(tool_reset_tip("pencil", &t) && t.size == 1 && t.hardness == 100, "Pencil: 1 px hard");
  CHECK(tool_reset_tip("Burn", &t) && t.size == 65 && t.spacing == 25, "Burn: 65 px at 25 %");
  CHECK(tool_reset_tip("Clone Stamp", &t) && t.size == 21, "Clone Stamp: 21 px");
  CHECK(tool_reset_tip("Eraser", &t) && t.smoothing == 0 && t.hardness == 100, "Eraser: hard, no smoothing");
  CHECK(!tool_reset_tip("Zoom", &t) && t.size == 13, "an unknown tool gets general defaults");
  color_html(0xC8641E, s); CHECK(!strcmp(s, "color: #c8641e;"), "Copy Color as HTML");
  color_hex(0x00FF08, s); CHECK(!strcmp(s, "00ff08"), "the hex code");
  CHECK(pixelart_zoom(1000, 1) == 2000 && pixelart_zoom(2000, -1) == 1000, "whole steps");
  CHECK(pixelart_zoom(1500, 0) == 2000 || pixelart_zoom(1500, 0) == 1000, "nearest");
  CHECK(pixelart_zoom(1000, -1) == 500 && pixelart_zoom(500, -1) == 333 && pixelart_zoom(333, 1) == 500, "1/n below 100 %");
  CHECK(pixelart_zoom(3300, 0) == 3000 && pixelart_zoom(64000, 1) == 64000, "nearest and the top");
  CHECK(blend_step(BM_NORMAL, 1) == BM_DISSOLVE && blend_step(BM_DISSOLVE, -1) == BM_NORMAL, "Shift + and -");
  CHECK(blend_step(BM_NORMAL, -1) == BM_NORMAL && blend_step(BM_LUMINOSITY, 1) == BM_LUMINOSITY, "stops at the ends");
  CHECK(blend_step(BM_DARKERCOLOR, 1) == BM_LIGHTEN, "group to group");
  {
    signed char a[26], b[26]; char line[200]; int i;
    for (i = 0; i < 26; i++) a[i] = (signed char)(i % 3 ? 0 : i + 1);
    toolkeys_format(a, line, sizeof line);
    CHECK(!strncmp(line, "1,0,0,4,", 8), "the tool letters' memory as text");
    CHECK(toolkeys_parse(line, b, 80) && !memcmp(a, b, 26), "and back");
    CHECK(toolkeys_parse(line, b, 3) && b[0] == 1 && b[3] == 0, "tools past the count dropped");
    CHECK(!toolkeys_parse("1,2,x", b, 80), "a bad line refused");
  }
  memset(&k, 0, sizeof k);
  CHECK(opacity_key(&k, 4, 1000) == 40, "4 is 40 %");
  CHECK(opacity_key(&k, 5, 1200) == 45, "4 5 quickly is 45 %");
  CHECK(opacity_key(&k, 0, 5000) == 100, "0 is 100 %");
  CHECK(opacity_key(&k, 0, 5100) == 0, "00 is 0 %");
  CHECK(opacity_key(&k, 7, 6000) == 70 && opacity_key(&k, 2, 7000) == 20, "slowly: each on its own");
}

static void test_fill_stroke(void)
{
  Doc *d = doc_new(20, 20, 0, 1);
  u8 *src = (u8 *)malloc(20 * 20 * 4);
  int i;
  rect(d->lay[0], 20, 5, 5, 15, 15, 0x808080, 255);
  for (i = 0; i < 400; i++) { src[i * 4] = 200; src[i * 4 + 1] = 10; src[i * 4 + 2] = 20; src[i * 4 + 3] = 255; }
  fill_from_pixels(d, src, 100, BM_NORMAL, 1);
  CHECK(px(d, 0, 7, 7)[0] == 200 && px(d, 0, 1, 1)[3] == 0, "Fill from History keeping transparency");
  doc_undo(d);
  fill_from_pixels(d, src, 100, BM_NORMAL, 0);
  CHECK(px(d, 0, 1, 1)[3] == 255 && px(d, 0, 1, 1)[0] == 200, "without it everything is filled");
  doc_undo(d);
  fill_keep(d, 0x0000FF, 100, BM_NORMAL, 1);
  CHECK(px(d, 0, 7, 7)[2] == 255 && px(d, 0, 1, 1)[3] == 0, "Fill with a colour keeping transparency");
  CHECK(!d->lay[0]->alpha_lock, "the lock is put back");
  doc_undo(d);
  fill_keep(d, 0x404040, 100, BM_MULTIPLY, 0);
  CHECK(px(d, 0, 7, 7)[0] == 32 || px(d, 0, 7, 7)[0] == 33, "Fill in Multiply");
  doc_undo(d);
  sel_rect(d, 4, 4, 16, 16, SEL_REPLACE, 0);
  stroke_keep(d, 2, 0, 0xFF0000, 100, BM_NORMAL, 1);
  CHECK(px(d, 0, 5, 5)[0] == 255 && px(d, 0, 4, 4)[3] == 0, "Stroke inside keeps the transparent edge");
  doc_undo(d);
  stroke_keep(d, 4, 1, 0x404040, 100, BM_SCREEN, 0);
  CHECK(px(d, 0, 5, 8)[0] > 0x80, "Stroke in Screen lightens");
  free(src);
  doc_free(d);
}

static void test_lut(void)
{
  Doc *d = doc_new(8, 8, 0x808080, 0);
  AdjParams p;
  char *cube, *tdl;
  CHECK(!lut_cube_from_doc(d, 17, "x"), "no adjustment layers: nothing");
  adj_defaults(AK_INVERT, &p);
  doc_new_adjust(d, &p, "Invert 1");
  cube = lut_cube_from_doc(d, 2, "Inverted");
  CHECK(cube && strstr(cube, "LUT_3D_SIZE 2") && strstr(cube, "TITLE \"Inverted\""), "cube header");
  CHECK(cube && strstr(cube, "\n1.000000 1.000000 1.000000\n0.000000 1.000000 1.000000\n"), "inverted, red fastest");
  free(cube);
  cube = lut_cube_from_doc(d, 33, NULL);
  { int lines = 0; char *q; for (q = cube; q && *q; q++) if (*q == '\n') lines++; CHECK(lines == 5 + 33 * 33 * 33, "33^3 entries"); }
  free(cube);
  tdl = lut_3dl_from_doc(d, 2);
  CHECK(tdl && !strncmp(tdl, "0 1023\n1023 1023 1023\n", 22), "3DL");
  free(tdl);
  d->lay[1]->opacity = 128;
  cube = lut_cube_from_doc(d, 2, NULL);
  CHECK(cube && strstr(cube, "0.50"), "half opacity mixes half");
  free(cube);
  doc_free(d);
}

static void test_contact_stats(void)
{
  Doc *p[7];
  const char *names[7] = { "a", "b", "c", "d", "e", "f", "g" };
  ContactOpts o;
  Doc *s;
  int i;
  for (i = 0; i < 7; i++) { p[i] = doc_new(i % 2 ? 40 : 20, i % 2 ? 20 : 40, (u32)(i * 0x202020), 0); }
  contact_defaults(&o);
  o.w = 200; o.h = 100; o.cols = 2; o.rows = 2; o.spacing = 10; o.dpi = 72; o.bg = 0xFFFFFF; o.captions = 0;
  CHECK(contact_pages(7, &o) == 2, "seven on two pages");
  s = contact_sheet(p, names, 7, 0, &o);
  CHECK(s && s->w == 200 && s->nlay == 5 && !strcmp(s->lay[1]->name, "a") && !strcmp(s->lay[4]->name, "d"), "a layer each, named");
  CHECK(s && px(s, 2, 120, 20)[3] == 255 && px(s, 2, 108, 20)[3] == 0, "the second picture in the second cell");
  CHECK(s && px(s, 0, 2, 2)[0] == 255, "on white");
  doc_free(s);
  o.captions = 1; o.font_px = 8;
  s = contact_sheet(p, names, 7, 0, &o);
  CHECK(s && s->nlay == 9 && layer_text(s->lay[2]) && !strcmp(layer_text(s->lay[2])->text, "a"), "a caption under each, as type");
  doc_free(s);
  o.flatten = 1;
  s = contact_sheet(p, names, 7, 1, &o);
  CHECK(s && s->nlay == 1 && !strcmp(s->lay[0]->name, "Contact Sheet II"), "flattened: one normal layer named Contact Sheet II");
  doc_free(s);
  CHECK(!contact_sheet(p, names, 7, 2, &o), "no third page");
  /* Statistics: the mean of three flat pictures */
  {
    Doc *q[3]; Doc *r;
    q[0] = doc_new(10, 10, 0x000000, 0); q[1] = doc_new(10, 10, 0x606060, 0); q[2] = doc_new(20, 20, 0xC0C0C0, 0);
    r = doc_statistics(q, 3, STK_MEAN);
    CHECK(r && r->nlay == 1 && r->w == 10 && abs(px(r, 0, 5, 5)[0] - 0x60) <= 1, "Statistics: the mean");
    doc_free(r);
    r = doc_statistics(q, 3, STK_MAX);
    CHECK(r && abs(px(r, 0, 5, 5)[0] - 0xC0) <= 1 && !strcmp(r->lay[0]->name, stack_names[STK_MAX]), "the maximum, named");
    doc_free(r);
    CHECK(!doc_statistics(q, 1, STK_MEAN), "one picture isn't a stack");
    for (i = 0; i < 3; i++) doc_free(q[i]);
  }
  for (i = 0; i < 7; i++) doc_free(p[i]);
}

static void test_assets(void)
{
  AssetSpec a[8];
  int n;
  Doc *d;
  Layer *l;
  char drawer[300], path[400], log[400];
  n = assets_parse("logo.png, 50% logo-small.jpg8, icon.gif", a, 8);
  CHECK(n == 3 && !strcmp(a[0].file, "logo.png") && a[1].pct == 50 && !strcmp(a[1].file, "logo-small.jpg") && a[1].quality == 80, "names parsed");
  CHECK(!strcmp(a[2].file, "icon.gif"), "the third");
  CHECK(assets_parse("Layer 1", a, 8) == 0 && assets_parse("photo.jpg70%", a, 8) == 1 && a[0].quality == 70, "plain names ask nothing; 70 %");
  CHECK(assets_parse("x.png8", a, 8) == 1 && a[0].quality == 8, "PNG-8");
  d = doc_new(40, 30, 0xFFFFFF, 0);
  l = layer_new(d, "box.png, 200% box2x.png, box.jpg");
  rect(l, 40, 10, 5, 20, 15, 0xFF0000, 255);
  doc_add_layer(d, l, 1);
  snprintf(drawer, sizeof drawer, "%s/easel_assets", tmp);
  mkdir(drawer, 0755);
  CHECK(assets_generate(d, drawer, log, sizeof log) == 3, "three files");
  CHECK(strstr(log, "box.png (10 x 10)") && strstr(log, "box2x.png (20 x 20)"), "trimmed and scaled");
  path_join(path, sizeof path, drawer, "box2x.png");
  { size_t len; u8 *data = file_read(path, &len); char err[80]; Doc *b = data ? codec_load_doc(data, len, err, sizeof err) : NULL;
    CHECK(b && b->w == 20 && px(b, 0, 10, 10)[0] == 255 && px(b, 0, 10, 10)[1] == 0, "the layer alone, red"); doc_free(b); free(data); }
  path_join(path, sizeof path, drawer, "Layer 1.png");
  CHECK(layer_to_file(d, 1, path), "a layer exported");
  { size_t len; u8 *data = file_read(path, &len); char err[80]; Doc *b = data ? codec_load_doc(data, len, err, sizeof err) : NULL;
    CHECK(b && b->w == 10 && b->h == 10 && px(b, 0, 0, 0)[0] == 255, "trimmed to its content"); doc_free(b); free(data); }
  CHECK(!layer_to_file(d, 5, path), "no such layer");
  doc_free(d);
}

static void test_find_replace(void)
{
  Doc *d = doc_new(100, 50, 0xFFFFFF, 0);
  TextParams *tp = (TextParams *)calloc(1, sizeof *tp);
  int layer, pos;
  text_params_init(tp, "Sans", 12, 0, 5, 20);
  text_set_string(tp, "The cat sat on the Cat mat; category");
  doc_new_text(d, tp, "T1");
  text_set_string(tp, "No cats here");
  doc_new_text(d, tp, "T2");
  CHECK(type_find_next(d, 0, 0, "cat", 0, 1, &layer, &pos) && layer == 1 && pos == 4, "found the first whole word");
  CHECK(type_find_next(d, 1, 5, "cat", 0, 1, &layer, &pos) && layer == 1 && pos == 19, "then the next");
  CHECK(!type_find_next(d, 1, 20, "cat", 0, 1, &layer, &pos), "no more whole words");
  CHECK(type_find_replace(d, -1, "cat", "dog", 0, 1, 1) == 2, "two replaced (whole words, any case)");
  CHECK(!strcmp(layer_text(d->lay[1])->text, "The dog sat on the dog mat; category"), "the text");
  CHECK(!strcmp(layer_text(d->lay[2])->text, "No cats here"), "cats is not a whole word");
  doc_undo(d);
  CHECK(!strcmp(layer_text(d->lay[1])->text, "The cat sat on the Cat mat; category"), "one undo step");
  CHECK(type_find_replace(d, -1, "cat", "X", 1, 0, 1) == 3 && !strcmp(layer_text(d->lay[2])->text, "No Xs here"), "case-sensitive parts of words");
  doc_undo(d);
  CHECK(type_find_replace(d, -1, "cat", "", 0, 0, 0) == 1 && !strncmp(layer_text(d->lay[1])->text, "The  sat", 8), "only the first, deleted");
  free(tp);
  doc_free(d);
}

static void test_apply_calc(void)
{
  Doc *d = doc_new(10, 10, 0x808080, 0), *s = doc_new(10, 10, 0xFF0000, 0);
  AiSource src;
  u8 *m;
  int c;
  src.doc = s; src.layer = -1; src.channel = AC_RGB; src.invert = 0;
  CHECK(apply_image(d, &src, BM_NORMAL, 100, 0, NULL) && px(d, 0, 3, 3)[0] == 255 && px(d, 0, 3, 3)[1] == 0, "Apply Image, Normal");
  doc_undo(d);
  src.channel = AC_RED; src.invert = 1;
  CHECK(apply_image(d, &src, BM_NORMAL, 100, 0, NULL) && px(d, 0, 3, 3)[0] == 0 && px(d, 0, 3, 3)[1] == 0, "the red channel inverted: black");
  doc_undo(d);
  src.channel = AC_GREEN; src.invert = 0;
  apply_image(d, &src, BM_NORMAL, 50, 0, NULL);
  CHECK(abs(px(d, 0, 3, 3)[0] - 64) <= 1, "50 %");
  doc_undo(d);
  src.channel = AC_RGB;
  sel_rect(d, 0, 0, 5, 10, SEL_REPLACE, 0);
  apply_image(d, &src, BM_MULTIPLY, 100, 0, NULL);
  CHECK(px(d, 0, 2, 2)[1] == 0 && px(d, 0, 7, 2)[1] == 0x80, "Multiply, through the selection");
  sel_none(d);
  { Doc *o = doc_new(5, 5, 0, 0); src.doc = o; CHECK(!apply_image(d, &src, BM_NORMAL, 100, 0, NULL), "sizes must match"); doc_free(o); src.doc = s; }
  /* Calculations: red of s (255) multiplied by grey of d */
  {
    AiSource a = { s, -1, AC_RED, 0 }, b = { d, 0, AC_GRAY, 0 };
    m = calculations(&a, &b, BM_MULTIPLY, 100, 10, 10);
    CHECK(m && m[77] >= 0x7E && m[77] <= 0x82, "255 x grey 128 is 128");
    c = calc_result(d, m, 0, "Calc");
    CHECK(c == 0 && d->nchan == 1 && !strcmp(d->chan_name[0], "Calc"), "a new channel");
    doc_undo(d);
    CHECK(d->nchan == 0, "undone");
    m = calculations(&a, &b, BM_NORMAL, 100, 10, 10);
    CHECK(calc_result(d, m, 1, NULL) == -2 && d->sel && d->sel[5] == 255, "or the selection");
  }
  doc_free(d); doc_free(s);
}

static void test_analysis(void)
{
  Doc *d = doc_new(100, 80, 0xFFFFFF, 0);
  MeasureScale s;
  Buf b;
  double ruler[4] = { 0, 0, 30, 40 };
  int i;
  measure_scale_get(d, &s); CHECK(s.px == 1 && !strcmp(s.unit, "pixels"), "pixels at first");
  s.px = 10; s.len = 1; strcpy(s.unit, "mm");
  measure_scale_set(d, &s);
  CHECK(fabs(measure_len(d, 50) - 5) < 1e-9, "50 px is 5 mm");
  sel_rect(d, 10, 10, 30, 20, SEL_REPLACE, 0);
  buf_init(&b);
  CHECK(measure_record(d, ruler, &b) >= 1, "a record");
  buf_addc(&b, 0);
  CHECK(strstr(b.data, "Area\tPerimeter") && strstr(b.data, "\t2.00\t6.00\t2.00\t1.00\t2.00\t1.50\t5.00\t-53.13\t0\tmm"), "area 2 mm2, perimeter 6, length 5 mm");
  buf_free(&b);
  i = scale_marker(d, 3, 0, 4);
  CHECK(i == 1 && d->nlay == 2 && !strcmp(d->lay[1]->name, "Measurement Scale Marker"), "a scale marker layer");
  { Box bb; CHECK(layer_content_box(d, 1, &bb) && bb.x1 - bb.x0 == 30, "3 mm long is 30 px"); }
  doc_undo(d);
  CHECK(d->nlay == 1, "undone");
  measure_forget(d);
  measure_scale_get(d, &s); CHECK(s.px == 1, "forgotten");
  doc_free(d);
}

static void test_layers_slices(void)
{
  Doc *d = doc_new(60, 40, 0xFFFFFF, 0);
  Layer *l = layer_new(d, "Logo");
  Annots *a;
  int k, g;
  rect(l, 60, 10, 5, 30, 25, 0xFF0000, 255);
  doc_add_layer(d, l, 1);
  CHECK(layer_bounds(d, 1).x0 == 10 && layer_bounds(d, 1).y1 == 25, "a layer's bounds");
  k = slice_from_layer(d, 1);
  a = doc_annots(d, 0);
  CHECK(k >= 0 && a && a->slice[k].b.x0 == 10 && a->slice[k].b.x1 == 30 && !strcmp(a->slice[k].name, "Logo"), "New Layer Based Slice");
  /* snapping to the other layers: the background is active, the logo's edge at 10 is near 12 */
  menu3_init();
  d->active = 0;
  { double v = 12; CHECK(snap_coord(d, &v, 1, SNAP_LAYERS) && v == 10, "snaps to a layer's edge"); }
  { double v = 21; CHECK(snap_coord(d, &v, 1, SNAP_LAYERS) && v == 20, "or its centre"); }
  { double v = 50; CHECK(!snap_coord(d, &v, 1, SNAP_LAYERS), "nothing near"); }
  { double v = 28; d->active = 1; CHECK(!snap_coord(d, &v, 1, SNAP_LAYERS), "not to itself"); }
  { double v = 31; CHECK(snap_coord(d, &v, 1, SNAP_SLICES) && v == 30, "to a slice's edge"); }
  d->active = 1;
  g = artboard_from_layers(d);
  CHECK(g >= 0 && d->lay[g]->artboard && d->lay[g]->abox.x0 == 10 && d->lay[g]->abox.y1 == 25, "Artboard from Layers fits them");
  doc_undo(d);
  CHECK(d->nlay == 2, "one step");
  d->active = 1;
  g = frame_from_layer(d, 1);
  CHECK(g >= 0 && layer_is_frame(d->lay[g]), "Frame from Layers");
  doc_free(d);
}

static void test_package(void)
{
  Doc *d = doc_new(20, 20, 0xFFFFFF, 0);
  Layer *l = layer_new(d, "Logo");
  char src[400], drawer[300];
  const char *p;
  rect(l, 20, 2, 2, 12, 12, 0x00FF00, 255);
  doc_add_layer(d, l, 1);
  snprintf(smart_drawer, sizeof smart_drawer, "%s", tmp);
  snprintf(src, sizeof src, "%s/easel_pkg_logo.png", tmp);
  snprintf(drawer, sizeof drawer, "%s/easel_package", tmp);
  mkdir(drawer, 0755);
  CHECK(package_links(d, drawer) == 0, "nothing linked: nothing copied");
  CHECK(doc_smart_convert(d, 1) && doc_smart_link(d, 1, src), "a linked smart object");
  CHECK(package_links(d, drawer) == 1, "Package copies it");
  p = layer_smart_path(d->lay[1]);
  CHECK(p && strstr(p, "easel_package/easel_pkg_logo.png"), "and relinks the layer to the copy");
  CHECK(fsize(p) == fsize(src) && fsize(p) > 0, "the same file");
  CHECK(package_links(d, drawer) == 1, "packaging again keeps it where it is");
  doc_free(d);
}

static void test_open_as(void)
{
  u8 raw[4 * 3 * 2];
  RawOpts r;
  Doc *d;
  char err[80];
  int i;
  for (i = 0; i < 24; i++) raw[i] = (u8)(i * 10);
  raw_guess(sizeof raw, &r);
  CHECK(r.channels == 3 && r.w * r.h == 8, "Raw's guess");
  r.w = 4; r.h = 2; r.channels = 3; r.interleaved = 1; r.header = 0;
  d = open_as(raw, sizeof raw, OA_RAW, &r, err, sizeof err);
  CHECK(d && d->w == 4 && px(d, 0, 1, 0)[0] == 30 && px(d, 0, 1, 0)[2] == 50, "interleaved RGB");
  doc_free(d);
  r.interleaved = 0;
  d = open_as(raw, sizeof raw, OA_RAW, &r, err, sizeof err);
  CHECK(d && px(d, 0, 1, 0)[0] == 10 && px(d, 0, 1, 0)[1] == 90, "planar");
  doc_free(d);
  r.channels = 1; r.header = 8; r.w = 4; r.h = 4;
  d = open_as(raw, sizeof raw, OA_RAW, &r, err, sizeof err);
  CHECK(d && px(d, 0, 0, 0)[0] == 80 && px(d, 0, 0, 0)[2] == 80, "grey after a header");
  doc_free(d);
  r.h = 9; CHECK(!open_as(raw, sizeof raw, OA_RAW, &r, err, sizeof err) && err[0], "too small");
  /* a PNG opened as PNG, and refused as JPEG */
  {
    Doc *s = gradient_doc(8, 8);
    size_t len; u8 *png = png_encode(&(Rgba){ 8, 8, s->lay[0]->px }, 0, &len);
    d = open_as(png, len, OA_PNG, NULL, err, sizeof err); CHECK(d && d->w == 8, "as PNG"); doc_free(d);
    CHECK(!open_as(png, len, OA_JPEG, NULL, err, sizeof err), "not a JPEG");
    free(png); doc_free(s);
  }
}

static void test_bucket_wand_screen(void)
{
  Doc *d = doc_new(20, 20, 0x808080, 0);
  u8 pal[6] = { 0, 0, 0, 255, 255, 255 }, pens[16] = { 0, 1, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1 };
  fill_bucket_mode(d, 5, 5, 0x404040, 0, 1, 0, 100, 0, BM_MULTIPLY);
  CHECK(px(d, 0, 5, 5)[0] == 32 || px(d, 0, 5, 5)[0] == 33, "the Paint Bucket in Multiply");
  doc_undo(d);
  /* Sample Size: a speckled spot; the point misses, the 5 x 5 average is grey enough */
  { u8 *p = d->lay[0]->px + (10 * 20 + 10) * 4; p[0] = p[1] = p[2] = 0; }
  similar_sample = 1;
  sel_wand_ex(d, 10, 10, 10, 0, 0, SEL_REPLACE, 0);
  CHECK(d->sel && d->sel[0] == 0 && d->sel[10 * 20 + 10] == 255, "point sample: only the black pixel");
  similar_sample = 5;
  sel_wand_ex(d, 10, 10, 10, 0, 0, SEL_REPLACE, 0);
  CHECK(d->sel && d->sel[0] == 255, "5 by 5 average: the grey");
  similar_sample = 1;
  doc_free(d);
  d = doc_from_screen(pens, pal, 2, NULL, 4, 4, 1);
  CHECK(d && d->mode == MODE_INDEXED && d->npal == 2 && px(d, 0, 1, 0)[0] == 255 && px(d, 0, 0, 0)[0] == 0, "a screen's pens with its palette");
  doc_free(d);
  { u8 rgb[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 }; d = doc_from_screen(NULL, NULL, 0, rgb, 2, 2, 0);
    CHECK(d && d->mode == MODE_RGB && px(d, 0, 1, 1)[2] == 12, "an RGB screen"); doc_free(d); }
}

int main(int argc, char **argv)
{
  if (argc > 1) tmp = argv[1];
  adjlayer_init();
  layerx_init();
  test_sfw();
  test_improc();
  test_newdoc();
  test_small();
  test_fill_stroke();
  test_lut();
  test_contact_stats();
  test_assets();
  test_find_replace();
  test_apply_calc();
  test_analysis();
  test_layers_slices();
  test_package();
  test_open_as();
  test_bucket_wand_screen();
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
