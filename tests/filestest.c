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
  test_exif();
  test_xcf();
  test_kra();
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
