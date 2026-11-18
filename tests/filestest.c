/*
 * filestest.c - Easel round 3's file work: other programs' layered documents (core/foreign.h: GIMP
 * .xcf, Krita .kra, Paint.NET .pdn) checked against what tests/make_foreign.py says each file holds,
 * then every one of them cut short and damaged at random (each must fail cleanly or load, within a
 * work bound); .look LUTs; and the rest of round 3's formats (see each test). Run under ASan/UBSan:
 *   filestest DIR      (DIR made by python3 tests/make_foreign.py DIR)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core/doc.h"
#include "core/codec.h"
#include "core/foreign.h"
#include "core/adjustx.h"
#include "core/filesx.h"
#include "core/xread2.h"
#include "core/modex.h"
#include "core/cms.h"
#include "core/timeline.h"
#include "core/legacy.h"
#include "core/measure.h"
#include "core/toolsx.h"
#include "core/gradient.h"
#include "core/brushx.h"
#include "core/paint.h"
#include "core/palette.h"
#include <math.h>

static int fails, tests;
#define CHECK(c, msg) do { tests++; if (!(c)) { fails++; printf("FAIL %s (line %d)\n", msg, __LINE__); } } while (0)
static char dir[512];

static u8 *slurp(const char *name, size_t *len)
{
  char p[700];
  snprintf(p, sizeof p, "%s/%s", dir, name);
  return file_read(p, len);
}
static int find_layer(Doc *d, const char *name)
{
  int i;
  for (i = 0; i < d->nlay; i++) if (!strcmp(d->lay[i]->name, name)) return i;
  return -1;
}
static int mode_of(const char *s)
{
  if (!strcmp(s, "normal")) return BM_NORMAL;
  if (!strcmp(s, "multiply")) return BM_MULTIPLY;
  if (!strcmp(s, "screen")) return BM_SCREEN;
  if (!strcmp(s, "pass")) return BM_PASS;
  if (!strcmp(s, "reflect")) return BM_REFLECT;
  if (!strcmp(s, "difference")) return BM_DIFFERENCE;
  if (!strcmp(s, "overlay")) return BM_OVERLAY;
  if (!strcmp(s, "xor")) return BM_XOR;
  return -1;
}

/* a file against its .txt: tol is how far a sample may be off (more for the deep precisions) */
static void check_expect(const char *name, int tol)
{
  char p[700], line[300], msg[400];
  size_t len;
  u8 *data = slurp(name, &len);
  FILE *f;
  Doc *d;
  char err[200] = "";
  int li = 0;
  snprintf(msg, sizeof msg, "%s reads", name);
  CHECK(data != NULL, msg);
  if (!data) return;
  d = codec_load_doc(data, len, err, sizeof err);
  if (!d) printf("  %s: %s\n", name, err);
  CHECK(d != NULL, msg);
  free(data);
  if (!d) return;
  snprintf(p, sizeof p, "%s/%s.txt", dir, name);
  if (!(f = fopen(p, "r"))) { CHECK(0, "expectations"); doc_free(d); return; }
  while (fgets(line, sizeof line, f)) {
    char a[64], b[64], c[64];
    int x, y, v, r, g, bl, al, dep, op, vis;
    if (sscanf(line, "S %d %d %d", &x, &y, &v) == 3) {
      snprintf(msg, sizeof msg, "%s: size %dx%d dpi %d", name, x, y, v);
      CHECK(d->w == x && d->h == y && (!v || d->dpi == v), msg);
    } else if (sscanf(line, "L %63s %63s %d %d %d %63s", a, b, &dep, &op, &vis, c) == 6) {
      Layer *l = li < d->nlay ? d->lay[li] : NULL;
      snprintf(msg, sizeof msg, "%s: layer %d is %s (%s, depth %d, opacity %d, %s, %s)", name, li, a, b, dep, op, vis ? "shown" : "hidden", c);
      CHECK(l && !strcmp(l->name, a) && (l->kind == LK_GROUP) == !strcmp(b, "group") && l->depth == dep &&
            abs(l->opacity - op) <= 1 && l->visible == vis && l->blend == mode_of(c), msg);
      if (l && strcmp(l->name, a)) printf("  found %s (depth %d, opacity %d, mode %d)\n", l->name, l->depth, l->opacity, l->blend);
      li++;
    } else if (sscanf(line, "P %63s %d %d %d %d %d %d", a, &x, &y, &r, &g, &bl, &al) == 7) {
      int k = find_layer(d, a);
      const u8 *px = k >= 0 && d->lay[k]->px ? d->lay[k]->px + ((long)y * d->w + x) * 4 : NULL;
      snprintf(msg, sizeof msg, "%s: %s at %d,%d is %d,%d,%d,%d", name, a, x, y, r, g, bl, al);
      CHECK(px && abs(px[0] - r) <= tol && abs(px[1] - g) <= tol && abs(px[2] - bl) <= tol && abs(px[3] - al) <= tol, msg);
      if (px && !(abs(px[0] - r) <= tol && abs(px[1] - g) <= tol && abs(px[2] - bl) <= tol && abs(px[3] - al) <= tol))
        printf("  found %d,%d,%d,%d\n", px[0], px[1], px[2], px[3]);
    } else if (sscanf(line, "M %63s %d %d %d", a, &x, &y, &v) == 4) {
      int k = find_layer(d, a);
      snprintf(msg, sizeof msg, "%s: %s's mask at %d,%d is %d", name, a, x, y, v);
      CHECK(k >= 0 && d->lay[k]->mask && abs(d->lay[k]->mask[(long)y * d->w + x] - v) <= tol, msg);
    } else if (sscanf(line, "C %d %d %d", &x, &y, &v) == 3) {
      snprintf(msg, sizeof msg, "%s: channel at %d,%d is %d", name, x, y, v);
      CHECK(d->nchan >= 1 && abs(d->chan[0][(long)y * d->w + x] - v) <= tol, msg);
    } else if (sscanf(line, "Q %d %d %d", &x, &y, &v) == 3) {
      snprintf(msg, sizeof msg, "%s: selection at %d,%d is %d", name, x, y, v);
      CHECK(d->sel && abs(d->sel[(long)y * d->w + x] - v) <= tol, msg);
    }
  }
  fclose(f);
  snprintf(msg, sizeof msg, "%s: every layer listed", name);
  CHECK(li == d->nlay, msg);
  CHECK(!doc_can_undo(d) && !d->dirty, "opened clean");
  /* the composite, for tests/check_files.py to compare with ImageMagick's */
  {
    Rgba im;
    if (rgba_alloc(&im, d->w, d->h)) {
      size_t pl;
      u8 *png;
      doc_composite(d, doc_box(d), im.px, (long)d->w * 4);
      if ((png = png_encode(&im, 0, &pl)) != NULL) {
        snprintf(p, sizeof p, "%s/%s.easel.png", dir, name);
        file_write(p, png, pl);
        free(png);
      }
      rgba_free(&im);
    }
  }
  doc_free(d);
}

/* every file cut short at many places and damaged at random: never a crash or a hang */
static u32 rng = 12345;
static u32 rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }
static void fuzz(const char *name, int rounds)
{
  size_t len, k;
  u8 *data = slurp(name, &len), *m;
  int i, loaded = 0;
  clock_t t0 = clock();
  char msg[300];
  if (!data) return;
  m = (u8 *)malloc(len);
  for (i = 0; i < rounds; i++) {
    char err[200];
    size_t n = len;
    Doc *d;
    memcpy(m, data, len);
    if (i % 3 == 0) n = (size_t)(rnd() % len);                  /* cut short */
    else for (k = 0; k < 1 + (size_t)(rnd() % 8); k++) m[rnd() % len] = (u8)rnd();   /* bytes changed */
    d = codec_load_doc(m, n, err, sizeof err);
    if (d) { loaded++; doc_free(d); }
  }
  snprintf(msg, sizeof msg, "%s: %d damaged copies handled in %.1f s (%d opened)", name, rounds, (double)(clock() - t0) / CLOCKS_PER_SEC, loaded);
  CHECK((double)(clock() - t0) / CLOCKS_PER_SEC < 60, msg);
  printf("  %s\n", msg);
  free(m);
  free(data);
}

static void test_xcf(void)
{
  static const char *const files[] = { "v3_rle.xcf", "v0_raw.xcf", "v11_zlib.xcf", "v3_grey.xcf", "v3_indexed.xcf", "v11_gz.xcf.gz", "im_simple.xcf", NULL };
  static const char *const deep[] = { "v12_16bit.xcf", "v12_16lin.xcf", "v12_half.xcf", "v12_float.xcf", "v12_double.xcf", NULL };
  int i;
  size_t len;
  u8 *d;
  for (i = 0; files[i]; i++) check_expect(files[i], 0);
  for (i = 0; deep[i]; i++) check_expect(deep[i], 2);
  if ((d = slurp("v3_rle.xcf", &len)) != NULL) {
    CHECK(foreign_detect(d, len) == FF_XCF, "an XCF is detected");
    CHECK(!strcmp(foreign_name(FF_XCF), "GIMP XCF"), "named");
    free(d);
  }
  {
    Doc *doc;
    char err[200];
    if ((d = slurp("v3_rle.xcf", &len)) != NULL) {
      doc = codec_load_doc(d, len, err, sizeof err);
      CHECK(doc && doc->lay[0]->label == 1 && doc->lay[0]->lock_pos, "Bottom's red tag and position lock");
      CHECK(doc && !strcmp(doc->chan_name[0], "Spot"), "the channel's name");
      if (doc) doc_free(doc);
      free(d);
    }
  }
  fuzz("v3_rle.xcf", 300);
  fuzz("v11_zlib.xcf", 300);
  fuzz("v12_double.xcf", 150);
}

static void test_kra(void)
{
  static const char *const files[] = { "rgba8.kra", "rgba16.kra", "graya8.kra", NULL };
  int i;
  size_t len;
  u8 *d;
  for (i = 0; files[i]; i++) check_expect(files[i], 0);
  if ((d = slurp("rgba8.kra", &len)) != NULL) {
    char err[200];
    Doc *doc = codec_load_doc(d, len, err, sizeof err);
    CHECK(foreign_detect(d, len) == FF_KRA, "a Krita file is detected");
    CHECK(codec_detect(d, len) != FMT_ORA, "and isn't taken for OpenRaster");
    CHECK(doc && doc->lay[0]->locked && doc->lay[3]->collapsed, "Bottom locked, the group collapsed");
    if (doc) doc_free(doc);
    free(d);
  }
  fuzz("rgba8.kra", 300);
  fuzz("rgba16.kra", 150);
}

/* checks for tests/check_files.py, one per line */
static void want(const char *line)
{
  char p[700];
  FILE *f;
  snprintf(p, sizeof p, "%s/checks.txt", dir);
  if ((f = fopen(p, "a")) != NULL) { fprintf(f, "%s\n", line); fclose(f); }
}
static void save(const char *name, const u8 *data, size_t len)
{
  char p[700];
  snprintf(p, sizeof p, "%s/%s", dir, name);
  CHECK(file_write(p, data, len), "written");
}

/* a little-endian EXIF block: IFD0 with Orientation o, Make "TestCam" and the EXIF IFD (pixel sizes
   99 x 77), then a thumbnail IFD1 */
static u32 make_exif(u8 *e, int o)
{
  u8 *p = e;
  memset(e, 0, 200);
  memcpy(p, "II*\0", 4); p[4] = 8;
  p = e + 8;
  p[0] = 3;                                                   /* 3 entries */
  /* 0x010F Make, ASCII 8 at 100 */
  p[2] = 0x0F; p[3] = 0x01; p[4] = 2; p[6] = 8; p[10] = 100;
  /* 0x0112 Orientation SHORT 1 */
  p[14] = 0x12; p[15] = 0x01; p[16] = 3; p[18] = 1; p[22] = (u8)o;
  /* 0x8769 ExifIFD LONG at 120 */
  p[26] = 0x69; p[27] = 0x87; p[28] = 4; p[30] = 1; p[34] = 120;
  p[38] = 160;                                                /* next IFD (IFD1) at 160 */
  memcpy(e + 100, "TestCam", 8);
  p = e + 120;
  p[0] = 2;
  p[2] = 0x02; p[3] = 0xA0; p[4] = 3; p[6] = 1; p[10] = 99;  /* PixelXDimension SHORT */
  p[14] = 0x03; p[15] = 0xA0; p[16] = 4; p[18] = 1; p[22] = 77;  /* PixelYDimension LONG */
  e[160] = 0;                                                 /* an empty IFD1 */
  return 200;
}
static void test_exif(void)
{
  u8 e[200], *j, *f;
  u32 el = make_exif(e, 6), gl;
  size_t jl, fl;
  Rgba im;
  long v;
  Doc *d;
  char err[100];
  int x, y;
  CHECK(exif_get(e, el, 0x0112, &v) && v == 6, "orientation read");
  CHECK(exif_get(e, el, 0xA003, &v) && v == 77, "a tag in the EXIF IFD read");
  /* a 30 x 20 JPEG with the block (orientation 6: Easel turns it upright, 20 x 30) */
  rgba_alloc(&im, 30, 20);
  for (y = 0; y < 20; y++) for (x = 0; x < 30; x++) { u8 *p = im.px + (y * 30 + x) * 4; p[0] = (u8)(x * 8); p[1] = (u8)(y * 12); p[2] = 90; p[3] = 255; }
  j = jpeg_encode(&im, 90, 72, &jl);
  f = (u8 *)malloc(jl + el + 10);
  memcpy(f, j, 2);
  f[2] = 0xFF; f[3] = 0xE1; f[4] = (u8)((el + 8) >> 8); f[5] = (u8)(el + 8);
  memcpy(f + 6, "Exif\0\0", 6); memcpy(f + 12, e, el);
  memcpy(f + 12 + el, j + 2, jl - 2);
  fl = jl + el + 10;
  free(j);
  {
    u8 *x2 = exif_from_file(f, fl, &gl);
    CHECK(x2 && gl == el, "the block found in the JPEG");
    free(x2);
  }
  d = codec_load_doc(f, fl, err, sizeof err);
  CHECK(d && d->w == 20 && d->h == 30, "turned upright");
  CHECK(d && d->exif && exif_get(d->exif, d->exiflen, 0x0112, &v) && v == 1, "the kept block says orientation 1");
  CHECK(d && d->exif && d->exif[8 + 2 + 3 * 12] == 0 && d->exif[8 + 2 + 3 * 12 + 1] == 0, "no thumbnail IFD");
  free(f);
  if (d) {
    u8 *out;
    size_t ol;
    Doc *d2;
    out = jpeg_write_doc(d, 90, &ol);
    out = exif_embed(d, FMT_JPEG, out, &ol);
    save("exif.jpg", out, ol);
    want("exif exif.jpg 274 1");
    want("exif exif.jpg 271 TestCam");
    want("exif exif.jpg 40962 20");
    want("exif exif.jpg 40963 30");
    d2 = codec_load_doc(out, ol, err, sizeof err);
    CHECK(d2 && d2->w == 20 && d2->exif && exif_get(d2->exif, d2->exiflen, 0xA002, &v) && v == 20, "written back and read again");
    if (d2) doc_free(d2);
    free(out);
    out = png_write_doc(d, &ol);
    out = exif_embed(d, FMT_PNG, out, &ol);
    save("exif.png", out, ol);
    want("exif exif.png 271 TestCam");
    want("exif exif.png 40963 30");
    d2 = codec_load_doc(out, ol, err, sizeof err);
    CHECK(d2 && d2->exif && exif_get(d2->exif, d2->exiflen, 0x0112, &v) && v == 1, "PNG's eXIf read again");
    if (d2) doc_free(d2);
    free(out);
    doc_free(d);
  }
  rgba_free(&im);
  /* damaged blocks: never read past */
  {
    u8 b[200];
    int k;
    for (k = 0; k < 2000; k++) {
      int i;
      memcpy(b, e, 200);
      for (i = 0; i < 6; i++) b[rnd() % 200] = (u8)rnd();
      exif_normalize(b, (u32)(rnd() % 200));
      exif_get(b, (u32)(rnd() % 200), 0xA002, &v);
    }
    CHECK(1, "damaged EXIF handled");
  }
}

static void test_pdn(void)
{
  size_t len;
  u8 *d;
  check_expect("layers.pdn", 0);
  if ((d = slurp("layers.pdn", &len)) != NULL) {
    CHECK(foreign_detect(d, len) == FF_PDN, "a Paint.NET file is detected");
    free(d);
  }
  fuzz("layers.pdn", 400);
}

/* a document of three layers (one with a mask, one in a group) for the layered writers */
static Doc *layered_doc(void)
{
  Doc *d = doc_new(48, 40, 0xFFFFFF, 0);
  Layer *a = layer_new(d, "Red box"), *b = layer_new(d, "Blue"), *g;
  i32 x, y;
  for (y = 5; y < 25; y++) for (x = 4; x < 30; x++) { u8 *p = a->px + (y * 48 + x) * 4; p[0] = 220; p[1] = 30; p[2] = 40; p[3] = 255; }
  for (y = 15; y < 38; y++) for (x = 20; x < 46; x++) { u8 *p = b->px + (y * 48 + x) * 4; p[0] = 20; p[1] = 60; p[2] = 230; p[3] = 160; }
  b->mask = (u8 *)malloc(48 * 40);
  for (y = 0; y < 40; y++) for (x = 0; x < 48; x++) b->mask[y * 48 + x] = (u8)(x < 33 ? 255 : 0);
  b->mask_on = 1; b->mask_linked = 1;
  b->blend = BM_MULTIPLY; b->opacity = 200;
  doc_add_layer(d, a, 1);
  doc_add_layer(d, b, 2);
  g = (Layer *)calloc(1, sizeof *g);
  strcpy(g->name, "Group"); g->kind = LK_GROUP; g->opacity = 255; g->visible = 1; g->blend = BM_PASS;
  doc_add_layer(d, g, 3);
  d->lay[2]->depth = 1;
  doc_forget_undo(d);
  return d;
}
static int same_layers(Doc *a, Doc *b, const char *what)
{
  int i, ok = a && b && a->nlay == b->nlay && a->w == b->w && a->h == b->h;
  char msg[200];
  for (i = 0; ok && i < a->nlay; i++) {
    Layer *p = a->lay[i], *q = b->lay[i];
    ok = !strcmp(p->name, q->name) && p->kind == q->kind && p->depth == q->depth && p->blend == q->blend && p->opacity == q->opacity && (!p->mask) == (!q->mask);
    if (ok && p->px) {
      long k, bad = 0;
      for (k = 0; k < (long)a->w * a->h; k++) if (p->px[k * 4 + 3] != q->px[k * 4 + 3] || (p->px[k * 4 + 3] && memcmp(p->px + k * 4, q->px + k * 4, 3))) bad++;
      ok = !bad;
    }
    if (ok && p->mask) ok = !memcmp(p->mask, q->mask, (size_t)a->w * a->h);
  }
  snprintf(msg, sizeof msg, "%s: the layers come back", what);
  CHECK(ok, msg);
  return ok;
}
static void test_layered(void)
{
  Doc *d = layered_doc(), *r;
  u8 *f;
  size_t fl;
  char err[200];
  Rgba im;
  /* PSB */
  f = psb_write_doc(d, &fl);
  CHECK(f && f[5] == 2, "PSB written as version 2");
  r = f ? codec_load_doc(f, fl, err, sizeof err) : NULL;
  same_layers(d, r, "PSB");
  if (r) doc_free(r);
  if (f) { save("layers.psb", f, fl); free(f); }
  /* the composite, for psd-tools */
  if (rgba_alloc(&im, d->w, d->h)) {
    u8 *png;
    doc_composite(d, doc_box(d), im.px, (long)d->w * 4);
    rgba_flatten_white(&im);
    if ((png = png_encode(&im, 0, &fl)) != NULL) { save("layers.ref.png", png, fl); free(png); }
    rgba_free(&im);
  }
  want("psdtools layers.psb 4 2 layers.ref.png 2");
  /* layered TIFF: with each compression */
  {
    int c;
    for (c = 0; c < TIFFC_N; c++) {
      char nm[64], line[200];
      f = tiff_encode_layered(d, c, &fl);
      r = f ? codec_load_doc(f, fl, err, sizeof err) : NULL;
      snprintf(nm, sizeof nm, "layered TIFF (%s)", tiff_comp_names[c]);
      same_layers(d, r, nm);
      if (r) doc_free(r);
      snprintf(nm, sizeof nm, "layers_%d.tif", c);
      if (f) { save(nm, f, fl); free(f); }
      snprintf(line, sizeof line, "same %s layers.flat.png 0", nm);
      want(line);
      snprintf(line, sizeof line, "frames %s 4 3 26 23 26 23 20 15", nm);   /* (the masked layer: its pixels' box) */
      want(line);
    }
  }
  /* what other programs show: the flat picture */
  if (rgba_alloc(&im, d->w, d->h)) {
    u8 *png;
    doc_composite(d, doc_box(d), im.px, (long)d->w * 4);
    if ((png = png_encode(&im, 0, &fl)) != NULL) { save("layers.flat.png", png, fl); free(png); }
    rgba_free(&im);
  }
  /* one layer: a plain TIFF, no tag */
  {
    Doc *one = doc_new(10, 10, 0x808080, 0);
    u32 ifd[4];
    f = tiff_encode_layered(one, TIFFC_LZW, &fl);
    r = f ? codec_load_doc(f, fl, err, sizeof err) : NULL;
    CHECK(r && r->nlay == 1, "a flat TIFF opens as one layer");
    CHECK(f && tiff_pages(f, fl, ifd, 4) == 1, "one page");
    if (r) doc_free(r);
    free(f);
    doc_free(one);
  }
  /* several pages: a layer each (the pages made by hand: two TIFFs joined) */
  {
    Doc *p1 = doc_new(12, 8, 0xFF0000, 0), *p2 = doc_new(12, 8, 0x00FF00, 0);
    size_t l1, l2;
    u8 *t1 = tiff_encode_doc(p1, TIFFC_NONE, &l1), *t2 = tiff_encode_doc(p2, TIFFC_NONE, &l2), *m;
    if (t1 && t2 && (m = (u8 *)malloc(l1 + l2)) != NULL) {
      u32 ifd[4];
      int le = t1[0] == 'I', np;
      /* t2's offsets moved by l1; its IFD chained after t1's */
      memcpy(m, t1, l1); memcpy(m + l1, t2, l2);
      np = tiff_pages(t2, l2, ifd, 4);
      if (np == 1) {
        u32 off = ifd[0], ne, k, last;
        u8 *e = m + l1 + off;
        ne = le ? (u32)(e[0] | e[1] << 8) : (u32)(e[0] << 8 | e[1]);
        for (k = 0; k < ne; k++) {
          u8 *t = e + 2 + k * 12;
          u32 type = le ? (u32)(t[2] | t[3] << 8) : (u32)(t[2] << 8 | t[3]);
          u32 cnt = le ? (u32)(t[4] | t[5] << 8 | t[6] << 16 | (u32)t[7] << 24) : ((u32)t[4] << 24 | t[5] << 16 | t[6] << 8 | t[7]);
          u32 sz = (type == 3 ? 2 : type == 4 ? 4 : type == 5 ? 8 : 1) * cnt;
          int ptr = sz > 4;
          u32 tag = le ? (u32)(t[0] | t[1] << 8) : (u32)(t[0] << 8 | t[1]);
          if (tag == 273 && !ptr) ptr = 1, sz = 0;     /* the strip offsets themselves move */
          if (ptr) {
            u32 v = le ? (u32)(t[8] | t[9] << 8 | t[10] << 16 | (u32)t[11] << 24) : ((u32)t[8] << 24 | t[9] << 16 | t[10] << 8 | t[11]);
            if (tag == 273 && cnt == 1) v += (u32)l1;
            else {
              v += (u32)l1;
              if (tag == 273) { u32 j; for (j = 0; j < cnt; j++) { u8 *q = m + v + j * (type == 3 ? 2 : 4); if (type == 4) { u32 o = le ? (u32)(q[0] | q[1] << 8 | q[2] << 16 | (u32)q[3] << 24) : ((u32)q[0] << 24 | q[1] << 16 | q[2] << 8 | q[3]); o += (u32)l1; if (le) { q[0] = (u8)o; q[1] = (u8)(o >> 8); q[2] = (u8)(o >> 16); q[3] = (u8)(o >> 24); } else { q[0] = (u8)(o >> 24); q[1] = (u8)(o >> 16); q[2] = (u8)(o >> 8); q[3] = (u8)o; } } } }
            }
            if (le) { t[8] = (u8)v; t[9] = (u8)(v >> 8); t[10] = (u8)(v >> 16); t[11] = (u8)(v >> 24); } else { t[8] = (u8)(v >> 24); t[9] = (u8)(v >> 16); t[10] = (u8)(v >> 8); t[11] = (u8)v; }
          }
        }
        /* chain: t1's IFD's next pointer to t2's IFD */
        {
          u32 f1[4];
          tiff_pages(t1, l1, f1, 4);
          {
            u8 *e1 = m + f1[0];
            u32 n1 = le ? (u32)(e1[0] | e1[1] << 8) : (u32)(e1[0] << 8 | e1[1]);
            u8 *nx = e1 + 2 + n1 * 12;
            last = off + (u32)l1;
            if (le) { nx[0] = (u8)last; nx[1] = (u8)(last >> 8); nx[2] = (u8)(last >> 16); nx[3] = (u8)(last >> 24); } else { nx[0] = (u8)(last >> 24); nx[1] = (u8)(last >> 16); nx[2] = (u8)(last >> 8); nx[3] = (u8)last; }
          }
        }
        CHECK(tiff_pages(m, l1 + l2, ifd, 4) == 2, "two pages found");
        r = codec_load_doc(m, l1 + l2, err, sizeof err);
        CHECK(r && r->nlay == 2 && !strcmp(r->lay[1]->name, "Page 1") && r->lay[1]->px[0] == 255 && r->lay[0]->px[1] == 255, "each page a layer, the first on top");
        if (r) doc_free(r);
      }
      free(m);
    }
    free(t1); free(t2);
    doc_free(p1); doc_free(p2);
  }
  doc_free(d);
}

/* round 3's OpenEXR: tiles, PXR24, B44 and B44A against make_foreign.py's values */
static void test_exr2(void)
{
  static const char *const files[] = { "tiled_none.exr", "tiled_zip.exr", "pxr24.exr", "pxr24_tiled.exr", "b44.exr", "b44a.exr", NULL };
  int i;
  for (i = 0; files[i]; i++) {
    size_t len;
    u8 *d = slurp(files[i], &len);
    i32 w, h;
    char err[200] = "", msg[300], p[700], line[200];
    float *f = d ? exr_read_float(d, len, &w, &h, err, sizeof err) : NULL;
    FILE *fp;
    snprintf(msg, sizeof msg, "%s reads (%s)", files[i], err);
    CHECK(f && w == 45 && h == 38, msg);
    snprintf(p, sizeof p, "%s/%s.txt", dir, files[i]);
    if (f && (fp = fopen(p, "r")) != NULL) {
      while (fgets(line, sizeof line, fp)) {
        int x, y, c, bad = 0;
        double v[4], tol;
        if (sscanf(line, "E %d %d %lf %lf %lf %lf %lf", &x, &y, &v[0], &v[1], &v[2], &v[3], &tol) != 7) continue;
        for (c = 0; c < 4; c++) if (fabs(f[((long)y * w + x) * 4 + c] - v[c]) > tol + 1e-6 * (1 + fabs(v[c]))) bad = 1;
        snprintf(msg, sizeof msg, "%s at %d,%d is %g %g %g %g", files[i], x, y, v[0], v[1], v[2], v[3]);
        CHECK(!bad, msg);
        if (bad) printf("  found %g %g %g %g\n", f[((long)y * w + x) * 4], f[((long)y * w + x) * 4 + 1], f[((long)y * w + x) * 4 + 2], f[((long)y * w + x) * 4 + 3]);
      }
      fclose(fp);
    }
    free(f);
    free(d);
    fuzz(files[i], 200);
  }
}

static void test_camraw(void)
{
  static const char *const files[] = { "cam.nef", "cam.dng", "cam.cr2", "cam.raf", "cam.cr3", NULL };
  int i;
  for (i = 0; files[i]; i++) {
    size_t len;
    u8 *d = slurp(files[i], &len);
    char msg[100];
    snprintf(msg, sizeof msg, "%s is a camera raw", files[i]);
    CHECK(d && camraw_detect(d, len), msg);
    free(d);
    check_expect(files[i], 4);                          /* (Pillow's JPEG decoder and Easel's differ a little) */
    fuzz(files[i], 200);
  }
  /* a camera's ordinary TIFF (no sensor data) stays a TIFF */
  {
    Doc *t = doc_new(16, 12, 0x336699, 0);
    size_t tl;
    u8 *f = tiff_encode_doc(t, TIFFC_NONE, &tl);
    CHECK(f && !camraw_detect(f, tl), "a plain TIFF isn't a raw");
    free(f);
    doc_free(t);
  }
}

/* JPEG Options' size readout: within a fifth of the real size, exact for small pictures */
static void test_jpeg_size(void)
{
  static const int sizes[3][2] = { { 120, 90 }, { 800, 600 }, { 1500, 400 } };
  int s, q;
  for (s = 0; s < 3; s++) {
    Doc *d = doc_new(sizes[s][0], sizes[s][1], 0xFFFFFF, 0);
    Rgba th;
    i32 x, y;
    for (y = 0; y < d->h; y++) for (x = 0; x < d->w; x++) {
      u8 *p = d->lay[0]->px + ((long)y * d->w + x) * 4;
      u32 r = (u32)(x * 7 + y * 13) * 2654435761u;
      p[0] = (u8)(128 + 100 * sin(x / 37.0) + (r >> 29)); p[1] = (u8)(128 + 90 * cos(y / 23.0) + (r >> 28 & 7)); p[2] = (u8)((x + y) / 9 + (r >> 26 & 15));
    }
    CHECK(jpeg_size_thumb(d, &th), "the size copy made");
    CHECK(th.w * th.h <= JSZ_MAX, "within its limit");
    for (q = 30; q <= 95; q += 65) {
      size_t real;
      u8 *j = jpeg_write_doc(d, q, &real);
      long est = jpeg_size_estimate(&th, d->w, d->h, q, 0);
      char msg[200];
      snprintf(msg, sizeof msg, "%dx%d at %d: about %ld bytes, really %ld", d->w, d->h, q, est, (long)real);
      if (s == 0) CHECK(labs(est - (long)real) < 64, msg);
      else CHECK(est > (long)real * 0.8 && est < (long)real * 1.2, msg);
      printf("  %s\n", msg);
      free(j);
    }
    rgba_free(&th);
    doc_free(d);
  }
}

/* Indexed Color's Matte */
static void test_matte(void)
{
  Doc *d = doc_new(4, 1, 0, 1);
  u8 *p = d->lay[0]->px;
  static const u8 px[16] = { 200, 0, 0, 255,  200, 0, 0, 128,  200, 0, 0, 0,  0, 0, 200, 64 };
  memcpy(p, px, 16);
  d->fg = 0x00FF00;
  CHECK(matte_color(d, MT_FG) == 0x00FF00 && matte_color(d, MT_GRAY50) == 0x808080 && matte_color(d, MT_NETSCAPE) == 0xC0C0C0, "matte colours");
  doc_begin(d, "Indexed Color");
  doc_note_all(d);
  CHECK(layers_matte(d, 0xFFFFFF, 1) == 2, "two edge pixels matted");
  CHECK(p[3] == 255 && p[4] == 227 && p[5] == 127 && p[7] == 255, "the half see-through red over white");
  CHECK(p[11] == 0, "the see-through pixel stays so");
  CHECK(p[12] == 191 && p[14] == 241 && p[15] == 255, "the faint blue over white");
  CHECK(doc_to_indexed(d, 8, QM_MEDIAN, DI_NONE, NULL, 0), "made indexed in the same step");
  doc_end(d);
  CHECK(d->mode == MODE_INDEXED, "indexed");
  doc_undo(d);
  CHECK(!memcmp(d->lay[0]->px, px, 16) && d->mode == MODE_RGB, "one undo brings the edges and the mode back");
  p = d->lay[0]->px;
  doc_begin(d, "Indexed Color");
  doc_note_all(d);
  CHECK(layers_matte(d, 0, 0) == 3, "without transparency every pixel is matted");
  doc_end(d);
  p = d->lay[0]->px;
  CHECK(p[8] == 0 && p[11] == 255, "the see-through pixel black and opaque");
  doc_free(d);
}

/* the monitor's profile: the canvas's view goes through it */
static void test_monitor(void)
{
  ViewSpec v;
  CmsXform *x;
  u8 px[9] = { 255, 0, 0, 0, 128, 0, 200, 200, 200 };
  memset(&v, 0, sizeof v);
  v.proof = PRF_CMYK; v.intent = CI_RELATIVE; v.bpc = 1;
  CHECK(cms_view_new(NULL, &v) == NULL, "an sRGB picture on an sRGB display: nothing to do");
  v.monitor = icc_srgb();
  CHECK(cms_view_new(NULL, &v) == NULL, "an sRGB monitor profile changes nothing");
  v.monitor = icc_adobergb();
  x = cms_view_new(NULL, &v);
  CHECK(x != NULL, "an Adobe RGB monitor: a transform");
  if (x) {
    cms_xform_apply_rgb(x, px, 3);
    CHECK(px[0] >= 215 && px[0] <= 223 && px[1] <= 2 && px[2] <= 2, "sRGB red as the wide monitor shows it");
    CHECK(px[6] >= 198 && px[6] <= 202 && px[7] == px[6] && px[8] == px[6], "greys stay grey");
    printf("  red on the Adobe RGB monitor: %d %d %d; green %d %d %d\n", px[0], px[1], px[2], px[3], px[4], px[5]);
    cms_xform_free(x);
  }
  /* with proofing on too, the proof is shown through the monitor's profile */
  v.on = 1; v.proof = PRF_CMYK;
  x = cms_view_new(NULL, &v);
  CHECK(x != NULL, "proofing on the wide monitor");
  cms_xform_free(x);
}

/* File > Open As: the reader chosen, whatever the file looks like */
static void test_open_as(void)
{
  Doc *d = layered_doc(), *r;
  size_t tl, pl, gl;
  char err[200];
  u8 *t = tiff_encode_layered(d, TIFFC_LZW, &tl), *png = png_write_doc(d, &pl), *tga = tga_write_doc(d, 0, &gl);
  int k;
  for (k = 0; k < OA_N; k++) CHECK(open_as_names[k] && open_as_names[k][0], "every reader named");
  CHECK(open_as_names[OA_N] == NULL, "the list ends");
  r = codec_load_as(t, tl, OA_TIFF, err, sizeof err);
  CHECK(r && r->nlay == d->nlay, "TIFF with its layers");
  if (r) doc_free(r);
  r = codec_load_as(t, tl, OA_TIFFFLAT, err, sizeof err);
  CHECK(r && r->nlay == 1 && r->w == d->w, "TIFF flat: the first page only");
  if (r) doc_free(r);
  err[0] = 0;
  r = codec_load_as(png, pl, OA_JPEG, err, sizeof err);
  CHECK(!r && err[0], "a PNG isn't a JPEG: refused with a reason");
  if (r) doc_free(r);
  r = codec_load_as(tga, gl, OA_TGA, err, sizeof err);
  CHECK(r && r->w == d->w && r->h == d->h, "Targa as Targa");
  if (r) doc_free(r);
  r = codec_load_as(png, pl, OA_PNG, err, sizeof err);
  CHECK(r && r->w == d->w, "PNG as PNG");
  if (r) doc_free(r);
  r = codec_load_as(png, pl, OA_N, err, sizeof err);
  CHECK(!r, "no such reader");
  {
    size_t xl;
    u8 *x = slurp("v11_gz.xcf.gz", &xl);
    r = x ? codec_load_as(x, xl, OA_XCF, err, sizeof err) : NULL;
    CHECK(r && r->nlay == 4, "a gzipped GIMP file as GIMP");
    if (r) doc_free(r);
    r = x ? codec_load_as(x, xl, OA_KRA, err, sizeof err) : NULL;
    CHECK(!r, "and not as Krita");
    free(x);
  }
  /* every reader on every kind of file: refused or read, never past the data */
  {
    static const char *const files[] = { "v3_rle.xcf", "rgba8.kra", "layers.pdn", "cam.nef", "tiled_zip.exr", NULL };
    int f;
    for (f = 0; files[f]; f++) {
      size_t xl;
      u8 *x = slurp(files[f], &xl);
      for (k = 0; x && k < OA_N; k++) { r = codec_load_as(x, xl, k, err, sizeof err); if (r) doc_free(r); }
      free(x);
    }
    for (k = 0; k < OA_N; k++) { r = codec_load_as(png, pl, k, err, sizeof err); if (r) doc_free(r); r = codec_load_as(t, tl, k, err, sizeof err); if (r) doc_free(r); }
    CHECK(1, "every reader on every file");
  }
  free(t); free(png); free(tga);
  doc_free(d);
}

/* the Timeline window's frame commands */
static void test_timeline(void)
{
  Doc *d = doc_new(8, 6, 0, 1);
  Layer *b = layer_new(d, "Frame 2 (0.20 s)");
  int f[MAXLAYERS], n;
  long k;
  for (k = 0; k < 48; k++) { u8 *p = d->lay[0]->px + k * 4; p[0] = 255; p[3] = 255; }       /* red, opaque */
  for (k = 0; k < 24; k++) { u8 *p = b->px + k * 4; p[2] = 255; p[3] = 255; }                /* blue, top half */
  doc_set_layer_props(d, 0, "Frame 1 (0.10 s)", -1, -1, -1);
  doc_add_layer(d, b, 1);
  doc_forget_undo(d);
  CHECK(tl_length(d, 10) == 30, "the animation lasts 0.3 s");
  CHECK(tl_tween(d, 0, 3), "three frames tweened");
  n = frames_list(d, f, MAXLAYERS);
  CHECK(n == 5, "five frames");
  {
    const u8 *p = d->lay[2]->px, *q = d->lay[2]->px + 40 * 4;   /* the middle tween: top half, bottom half */
    CHECK(p[0] >= 126 && p[0] <= 129 && p[2] >= 126 && p[2] <= 129 && p[3] == 255, "half red, half blue where both are");
    CHECK(q[0] == 255 && q[2] == 0 && q[3] >= 126 && q[3] <= 129, "the red fading out where only it is");
    CHECK(frame_time(d->lay[2]->name) == 10, "a tween takes the first frame's time");
  }
  CHECK(tl_length(d, 10) == 60, "now 0.6 s");
  doc_undo(d);
  CHECK(frames_list(d, f, MAXLAYERS) == 2, "Tween undone");
  CHECK(tl_new_frame(d, 1), "a new frame");
  CHECK(d->nlay == 3 && !strcmp(d->lay[2]->name, "Frame 3 (0.20 s)") && d->active == 2, "a copy named as the third, with its time");
  CHECK(tl_reverse(d) && !strcmp(d->lay[0]->name, "Frame 3 (0.20 s)") && !strcmp(d->lay[2]->name, "Frame 1 (0.10 s)"), "reversed");
  CHECK(tl_set_all_times(d, 50) && tl_length(d, 10) == 150, "every frame 0.5 s");
  CHECK(tl_delete_frame(d, 0) && d->nlay == 2, "a frame deleted");
  CHECK(tl_delete_frame(d, 0) && !tl_delete_frame(d, 0) && d->nlay == 1, "but not the last");
  CHECK(!tl_tween(d, 0, 2), "nothing to tween towards");
  doc_undo(d); doc_undo(d); doc_undo(d); doc_undo(d);
  CHECK(d->nlay == 3 && !strcmp(d->lay[0]->name, "Frame 1 (0.10 s)"), "all undone");
  doc_free(d);
}

/* the Measurement Log */
static void test_measure(void)
{
  Doc *d = doc_new(20, 10, 0xFFFFFF, 0);
  MeasureRec m;
  static MeasureLog lg;
  char line[200], *t;
  i32 x, y;
  for (y = 0; y < 10; y++) for (x = 0; x < 10; x++) { u8 *p = d->lay[0]->px + (y * 20 + x) * 4; p[0] = p[1] = p[2] = 0; }   /* the left half black */
  CHECK(measure_selection(d, &m) && m.count == 200 && fabs(m.area - 200) < 1e-9 && m.perimeter == 60, "the whole canvas: 200 px, 60 round");
  CHECK(fabs(m.mean - 127.5) < 0.01 && m.minv == 0 && m.maxv == 255 && fabs(m.density - 25500) < 1, "half black, half white");
  d->sel = (u8 *)calloc(200, 1);
  for (y = 2; y < 6; y++) for (x = 8; x < 14; x++) d->sel[y * 20 + x] = 255;   /* 6 x 4 over the edge */
  d->selbox = box_make(8, 2, 14, 6);
  CHECK(measure_selection(d, &m) && m.count == 24 && m.perimeter == 20 && m.w == 6 && m.h == 4, "a 6 x 4 selection");
  CHECK(fabs(m.mean - 255.0 * 4 / 6) < 0.01, "two thirds of it white");
  CHECK(mlog_add(&lg, &m, "Test") == 0, "logged");
  measure_ruler(0, 0, 3, 4, &m);
  CHECK(fabs(m.length - 5) < 1e-9, "the ruler's 3-4-5");
  mlog_add(&lg, &m, "Test");
  note_add(d, 1, 1, "", "", 0);
  count_add(d, 3, 3); count_add(d, 5, 5);
  measure_count(d, &m);
  CHECK(m.count == 2, "two markers counted");
  mlog_add(&lg, &m, "Test");
  mlog_line(&lg.r[1], 1, "Ruler Tool", line, sizeof line);
  CHECK(strstr(line, "5 px") != NULL, "the ruler's row");
  t = mlog_text(&lg);
  CHECK(t && strstr(t, "Measurement 1\tTest\tSelection\t24\t24\t20\t6\t4") && strstr(t, "Measurement 3\tTest\tCount Tool\t2"), "the exported log");
  free(t);
  mlog_delete(&lg, 0);
  CHECK(lg.n == 2 && lg.r[0].source == MS_RULER, "a row deleted");
  doc_free(d);
}

/* .grd written, and read back */
static void test_grd(void)
{
  static Gradient g[3], back[4];
  size_t len;
  u8 *f;
  char err[100];
  int n, k;
  gradient_init(&g[0], "Red to blue", 0xFF0000, 0x0000FF);
  gradient_init(&g[1], "Grün", 0x00FF00, 0xFFFFFF);
  g[1].color[1].kind = GS_FG; g[1].color[1].mid = 0.3;
  g[1].opacity[1].opacity = 0.25;
  gradient_init(&g[2], "Three", 0x102030, 0x405060);
  g[2].color[2] = g[2].color[1]; g[2].color[1].pos = 0.4; g[2].color[1].rgb = 0x80FF00; g[2].ncolor = 3;
  f = grd_write(g, 3, &len);
  CHECK(f != NULL, ".grd written");
  n = f ? grd_read(f, len, back, 4, err, sizeof err) : 0;
  CHECK(n == 3, "three gradients read back");
  CHECK(n == 3 && !strcmp(back[1].name, "Grün") && back[1].color[1].kind == GS_FG && fabs(back[1].color[1].mid - 0.3) < 0.01, "the name, a foreground stop, its midpoint");
  CHECK(n == 3 && fabs(back[1].opacity[1].opacity - 0.25) < 0.001, "an opacity stop");
  for (k = 0; n == 3 && k < 3; k++) CHECK(back[2].color[k].rgb == g[2].color[k].rgb && fabs(back[2].color[k].pos - g[2].color[k].pos) < 0.001, "the colours and places");
  if (f) { save("written.grd", f, len); want("grd written.grd 3 Red to blue"); }
  free(f);
}

/* the Brush's Behind and Clear modes */
static void test_behind_clear(void)
{
  Doc *d = doc_new(40, 20, 0, 1);
  Brush b;
  Box ch;
  Stroke *s;
  u8 *p = d->lay[0]->px;
  i32 x, y;
  for (y = 0; y < 20; y++) for (x = 0; x < 20; x++) { u8 *q = p + (y * 40 + x) * 4; q[0] = 255; q[3] = 255; }   /* the left half opaque red */
  doc_forget_undo(d);
  memset(&b, 0, sizeof b);
  b.tool = PT_BRUSH; b.size = 6; b.hardness = 100; b.opacity = 100; b.flow = 100; b.spacing = 25; b.strength = 50; b.bg = 0xFFFFFF;
  b.blend = BM_BEHIND;
  s = stroke_begin(d, &b, 0x0000FF, 2, 10, &ch);
  CHECK(s != NULL, "a Behind stroke");
  if (s) { stroke_to(s, 38, 10, &ch); stroke_end(s); }
  CHECK(p[(10 * 40 + 10) * 4] == 255 && p[(10 * 40 + 10) * 4 + 2] == 0, "the opaque red stays red: Behind paints under it");
  CHECK(p[(10 * 40 + 30) * 4 + 2] == 255 && p[(10 * 40 + 30) * 4 + 3] == 255, "where the layer was see-through it is blue");
  doc_undo(d);
  b.blend = BM_CLEAR;
  s = stroke_begin(d, &b, 0x0000FF, 2, 10, &ch);
  if (s) { stroke_to(s, 38, 10, &ch); stroke_end(s); }
  p = d->lay[0]->px;
  CHECK(p[(10 * 40 + 10) * 4 + 3] == 0, "Clear takes the red away along the stroke");
  CHECK(p[(2 * 40 + 10) * 4 + 3] == 255, "and leaves the rest");
  CHECK(p[(10 * 40 + 30) * 4 + 3] == 0, "and puts no paint where there was none");
  {
    u8 px[4] = { 10, 20, 30, 128 }, c[3] = { 200, 200, 200 };
    paint_px_mode(px, c, 255, BM_BEHIND);
    CHECK(px[3] == 255 && px[0] > 10 && px[0] < 200, "Behind: the half see-through pixel over the paint");
  }
  doc_free(d);
}

static void test_look(void)
{
  /* an inverting 2x2x2 table, hex floats little-endian, red fastest, in UTF-8 and UTF-16 */
  char text[2000], *p = text;
  int r, g, b, c;
  Lut3D *l;
  char err[100];
  p += sprintf(p, "<?xml version=\"1.0\"?>\n<look><shaders/><LUT>\n<size>\"2\"</size>\n<data>\"");
  for (b = 0; b < 2; b++) for (g = 0; g < 2; g++) for (r = 0; r < 2; r++)
    for (c = 0; c < 3; c++) {
      int v = 1 - (c == 0 ? r : c == 1 ? g : b);
      p += sprintf(p, "%s", v ? "0000803F" : "00000000");   /* 1.0f and 0.0f */
    }
  p += sprintf(p, "\"</data></LUT></look>\n");
  l = lut_parse_look(text, (long)(p - text), err, sizeof err);
  CHECK(l && l->n == 2, ".look read");
  if (l) {
    double o[3];
    u8 in[3] = { 255, 0, 64 };
    lut_lookup(l, in, 0, o);
    CHECK(o[0] < 0.01 && o[1] > 0.99 && o[2] > 0.74 && o[2] < 0.76, ".look inverts");
    lut_free(l);
  }
  {
    long n = (long)(p - text), k;
    char *u = (char *)malloc((size_t)n * 2 + 2);
    u[0] = (char)0xFF; u[1] = (char)0xFE;
    for (k = 0; k < n; k++) { u[2 + k * 2] = text[k]; u[3 + k * 2] = 0; }
    l = lut_parse_look(u, n * 2 + 2, err, sizeof err);
    CHECK(l && l->n == 2 && l->t[0] > 0.99f, ".look in UTF-16LE");
    lut_free(l);
    for (k = 0; k < n; k++) { u[2 + k * 2] = 0; u[3 + k * 2] = text[k]; }
    u[0] = (char)0xFE; u[1] = (char)0xFF;
    l = lut_parse_look(u, n * 2 + 2, err, sizeof err);
    CHECK(l && l->n == 2, ".look in UTF-16BE");
    lut_free(l);
    l = lut_parse_look(text, (long)(p - text) - 40, err, sizeof err);
    CHECK(!l, "a short .look refused");
    lut_free(l);
    l = lut_parse_look("<look><LUT><size>99</size><data></data></LUT></look>", 52, err, sizeof err);
    CHECK(!l, "a .look too big refused");
    free(u);
  }
}

int main(int argc, char **argv)
{
  snprintf(dir, sizeof dir, "%s", argc > 1 ? argv[1] : "/tmp/easel_foreign");
  {
    char p[700];
    snprintf(p, sizeof p, "%s/checks.txt", dir);
    remove(p);
  }
  test_look();
  test_jpeg_size();
  test_matte();
  test_monitor();
  test_open_as();
  test_timeline();
  test_measure();
  test_grd();
  test_behind_clear();
  test_exif();
  test_layered();
  test_exr2();
  test_camraw();
  test_xcf();
  test_kra();
  test_pdn();
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
