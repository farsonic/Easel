/*
 * codectest.c - native tests for Easel's picture files (core/codec.h): every decoder against the
 * reference pictures in tests/data (made by tests/make_testdata.py), round trips through every
 * encoder, OpenRaster documents, and damaged or truncated input of every kind (run it under
 * AddressSanitizer and UBSan: no crash, no read out of bounds).
 *
 * It also writes its encoders' output to a folder (default /tmp/easel_codec_out, or argv[1]) for
 * tests/check_codecs.py to check with Pillow, ImageMagick, ffmpeg and file.
 *
 * Build and run from the easel folder:
 *   cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. core/util.c core/zip.c core/xml.c core/image.c \
 *      core/doc.c core/codec.c core/png.c core/jpegenc.c core/bmp.c core/ilbm.c core/gif.c core/ora.c \
 *      tests/codectest.c -o /tmp/codectest -lm && /tmp/codectest
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core/codec.h"
#include "core/zip.h"

static int ntests, nfailed;
static const char *outdir = "/tmp/easel_codec_out";

/* one check: quiet when it passes */
static void ok(const char *name, int cond)
{
  ntests++;
  if (!cond) { nfailed++; printf("FAIL: %s\n", name); }
}

/* a small repeatable random number generator (the C library's differs between systems) */
static u32 rng = 12345;
static u32 rnd(void) { rng = rng * 1103515245UL + 12345; return (rng >> 8) & 0xFFFFFF; }

static u8 *slurp(const char *path, size_t *n)
{
  u8 *d = file_read(path, n);
  if (!d) printf("cannot read %s (run tests/make_testdata.py?)\n", path);
  return d;
}
static void save(const char *name, const void *d, size_t n)
{
  char path[512];
  snprintf(path, sizeof path, "%s/%s", outdir, name);
  if (!file_write(path, d, n)) printf("cannot write %s\n", path);
}
/* a picture as a .rgba file (the reference format) for check_codecs.py */
static void save_rgba(const char *name, const Rgba *im)
{
  u8 *b = (u8 *)malloc((size_t)im->w * im->h * 4 + 12);
  if (!b) return;
  memcpy(b, "RGBA", 4);
  b[4] = (u8)(im->w >> 24); b[5] = (u8)(im->w >> 16); b[6] = (u8)(im->w >> 8); b[7] = (u8)im->w;
  b[8] = (u8)(im->h >> 24); b[9] = (u8)(im->h >> 16); b[10] = (u8)(im->h >> 8); b[11] = (u8)im->h;
  memcpy(b + 12, im->px, (size_t)im->w * im->h * 4);
  save(name, b, (size_t)im->w * im->h * 4 + 12);
  free(b);
}

/* equal pixel for pixel; two fully transparent pixels count as equal whatever their colour */
static int same_pixels(const u8 *a, const u8 *b, long n)
{
  long i;
  for (i = 0; i < n; i++, a += 4, b += 4)
    if (memcmp(a, b, 4) && !(a[3] == 0 && b[3] == 0)) return 0;
  return 1;
}
static int same_rgba(const Rgba *a, const Rgba *b) { return a->w == b->w && a->h == b->h && same_pixels(a->px, b->px, (long)a->w * a->h); }
/* peak signal-to-noise ratio of the RGB channels, dB (999 if identical) */
static double psnr(const u8 *a, const u8 *b, long n)
{
  double se = 0;
  long i;
  for (i = 0; i < n * 4; i++) { double d; if ((i & 3) == 3) continue; d = (double)a[i] - b[i]; se += d * d; }
  if (se == 0) return 999;
  return 10 * log10(255.0 * 255.0 / (se / (n * 3)));
}

/* test pictures */
static void make_gradient(Rgba *im, i32 w, i32 h)
{
  i32 x, y;
  rgba_alloc(im, w, h);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      u8 *p = im->px + ((long)y * w + x) * 4;
      p[0] = (u8)(x * 255 / (w - 1)); p[1] = (u8)(y * 255 / (h - 1)); p[2] = (u8)((x + y) * 255 / (w + h - 2)); p[3] = 255;
    }
}
static void make_noise(Rgba *im, i32 w, i32 h, int alpha)
{
  long i;
  rgba_alloc(im, w, h);
  for (i = 0; i < (long)w * h * 4; i++) im->px[i] = (u8)rnd();
  if (!alpha) for (i = 0; i < (long)w * h; i++) im->px[i * 4 + 3] = 255;
}
/* at most ncol distinct colours, in blocks and noise */
static void make_palette_pic(Rgba *im, i32 w, i32 h, int ncol)
{
  u8 pal[256][3];
  i32 x, y;
  int i;
  for (i = 0; i < ncol; i++) { pal[i][0] = (u8)rnd(); pal[i][1] = (u8)rnd(); pal[i][2] = (u8)rnd(); }
  rgba_alloc(im, w, h);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      int c = y < h / 2 ? ((x / 7 + y / 5) % ncol) : (int)(rnd() % ncol);
      u8 *p = im->px + ((long)y * w + x) * 4;
      p[0] = pal[c][0]; p[1] = pal[c][1]; p[2] = pal[c][2]; p[3] = 255;
    }
}

/* ------------------------------------------------------------------ damaged input */
/* every truncation (or a spread of them for big files) and random corruptions: never a crash;
   truncations that cut off picture data must fail (strict: decoders that can tell) */
static void damage(const char *name, const u8 *d, size_t n, int strict_from)
{
  size_t len, step = n > 4000 ? n / 1500 : 1;
  int fails_ok = 1, k;
  u8 *copy;
  char msg[160], err[128];
  Rgba im;
  for (len = 0; len < n; len += step) {
    u8 *t = (u8 *)malloc(len ? len : 1);        /* an exact-size copy, so ASan sees any overrun */
    int r;
    memcpy(t, d, len);
    r = codec_decode(t, len, &im, err, sizeof err);
    if (r) rgba_free(&im);
    if (r && strict_from >= 0 && len < (size_t)strict_from) fails_ok = 0;
    free(t);
  }
  snprintf(msg, sizeof msg, "%s: truncated copies are refused", name);
  ok(msg, fails_ok);
  copy = (u8 *)malloc(n);
  for (k = 0; k < 300; k++) {
    int j, nm = 1 + (int)(rnd() % 4);
    memcpy(copy, d, n);
    for (j = 0; j < nm; j++) {
      size_t at = k < 150 ? rnd() % (n < 64 ? n : 64) : rnd() % n;    /* the headers get half the attention */
      copy[at] = (u8)(rnd() & 1 ? rnd() : (u32)(copy[at] ^ (1 << (rnd() % 8))));
    }
    if (codec_decode(copy, n, &im, err, sizeof err)) rgba_free(&im);
  }
  free(copy);
}

/* ------------------------------------------------------------------ the reference pictures */
static void reference_tests(void)
{
  char line[512], pic[200], exp[200], mode[40], path[400], msg[400], err[128];
  FILE *f = fopen("tests/data/manifest.txt", "r");
  int count = 0;
  if (!f) { ok("tests/data/manifest.txt exists (run tests/make_testdata.py)", 0); return; }
  while (fgets(line, sizeof line, f)) {
    u8 *d, *e;
    size_t n, en;
    Rgba im;
    int r, fmt;
    if (sscanf(line, "%199s %199s %39s", pic, exp, mode) != 3) continue;
    snprintf(path, sizeof path, "tests/data/%s", pic);
    if (!(d = slurp(path, &n))) { ok(pic, 0); continue; }
    snprintf(path, sizeof path, "tests/data/%s", exp);
    if (!(e = slurp(path, &en))) { free(d); ok(exp, 0); continue; }
    fmt = codec_detect(d, n);
    snprintf(msg, sizeof msg, "%s: detected format", pic);
    ok(msg, fmt == (strstr(pic, ".png") ? FMT_PNG : strstr(pic, ".jpg") ? FMT_JPEG : strstr(pic, ".bmp") ? FMT_BMP :
                    strstr(pic, ".gif") ? FMT_GIF : FMT_ILBM));
    r = codec_decode(d, n, &im, err, sizeof err);
    snprintf(msg, sizeof msg, "%s: decodes (%s)", pic, r ? "ok" : err);
    ok(msg, r);
    if (r) {
      u32 ew = ((u32)e[4] << 24) | ((u32)e[5] << 16) | ((u32)e[6] << 8) | e[7], eh = ((u32)e[8] << 24) | ((u32)e[9] << 16) | ((u32)e[10] << 8) | e[11];
      int sz = (u32)im.w == ew && (u32)im.h == eh && en == 12 + (size_t)ew * eh * 4;
      snprintf(msg, sizeof msg, "%s: size %ldx%ld", pic, (long)im.w, (long)im.h);
      ok(msg, sz);
      if (sz) {
        if (!strcmp(mode, "exact")) {
          snprintf(msg, sizeof msg, "%s: pixels exact", pic);
          ok(msg, same_pixels(im.px, e + 12, (long)ew * eh));
        } else {
          double want = atof(mode + 5), got = psnr(im.px, e + 12, (long)ew * eh);
          snprintf(msg, sizeof msg, "%s: PSNR %.1f dB against libjpeg (want %.0f)", pic, got, want);
          ok(msg, got >= want);
        }
      }
      rgba_free(&im);
    }
    /* damaged copies: PNG must refuse anything short of IEND, the others anything missing a
       quarter of the file; JPEG (image.c is lenient about truncated scans) just mustn't crash */
    damage(pic, d, n, fmt == FMT_PNG ? (int)n - 12 : fmt == FMT_JPEG ? -1 : (int)(n * 3 / 4));
    free(d); free(e);
    count++;
  }
  fclose(f);
  snprintf(msg, sizeof msg, "at least 80 reference pictures (found %d)", count);
  ok(msg, count >= 80);
}

/* ------------------------------------------------------------------ round trips */
static void png_tests(void)
{
  Rgba a, b, g;
  u8 *f;
  size_t n;
  char err[128];
  /* RGBA noise: exact, colour type 6 */
  make_noise(&a, 61, 47, 1);
  f = png_encode(&a, 0, &n);
  ok("PNG RGBA: encodes", f != NULL);
  ok("PNG RGBA: colour type 6", f && f[25] == 6);
  ok("PNG RGBA: decodes", f && png_read(f, n, &b, err, sizeof err));
  ok("PNG RGBA: exact", f && a.w == b.w && a.h == b.h && !memcmp(a.px, b.px, (size_t)a.w * a.h * 4));
  if (f) save("rgba.png", f, n);
  save_rgba("rgba.rgba", &a);
  free(f); rgba_free(&b); rgba_free(&a);
  /* opaque: RGB, smaller, with pHYs */
  make_gradient(&g, 300, 200);
  f = png_encode(&g, 150, &n);
  ok("PNG RGB: colour type 2", f && f[25] == 2);
  ok("PNG RGB: compresses a gradient", f && n < (size_t)300 * 200 * 3 / 4);
  ok("PNG RGB: decodes exact", f && png_read(f, n, &b, err, sizeof err) && !memcmp(g.px, b.px, (size_t)300 * 200 * 4));
  ok("PNG RGB: pHYs gives back 150 dpi", f && codec_dpi(f, n) == 150);
  if (f) save("gradient.png", f, n);
  save_rgba("gradient.rgba", &g);
  free(f); rgba_free(&b);
  /* tiny and 1-pixel-wide pictures */
  make_noise(&a, 1, 1, 1);
  f = png_encode(&a, 0, &n);
  ok("PNG 1x1 round trip", f && png_read(f, n, &b, err, sizeof err) && same_rgba(&a, &b));
  free(f); rgba_free(&b); rgba_free(&a);
  make_noise(&a, 1, 33, 0);
  f = png_encode(&a, 0, &n);
  ok("PNG 1x33 round trip", f && png_read(f, n, &b, err, sizeof err) && same_rgba(&a, &b));
  free(f); rgba_free(&b); rgba_free(&a);
  /* zlib helpers */
  {
    static const u8 txt[] = "Hello hello hello hello, Amiga!";
    u8 back[64];
    size_t zl;
    u8 *z = zlib_compress(txt, sizeof txt, &zl);
    ok("zlib round trip", z && zlib_decompress(z, zl, back, sizeof txt) == (long)sizeof txt && !memcmp(back, txt, sizeof txt));
    ok("adler32 of 'Wikipedia'", adler32(1, (const u8 *)"Wikipedia", 9) == 0x11E60398UL);
    free(z);
  }
  rgba_free(&g);
}

static void jpeg_tests(void)
{
  Rgba g, b, a;
  u8 *f;
  size_t n;
  char err[128];
  int q;
  make_gradient(&g, 256, 192);
  for (q = 0; q < 4; q++) {
    static const int qs[4] = { 90, 75, 50, 100 };
    static const double need[4] = { 35, 32, 30, 40 };
    char msg[100], name[40];
    f = jpeg_encode(&g, qs[q], 300, &n);
    snprintf(msg, sizeof msg, "JPEG q%d: encodes", qs[q]);
    ok(msg, f != NULL);
    if (!f) continue;
    snprintf(msg, sizeof msg, "JPEG q%d: decodes", qs[q]);
    ok(msg, jpeg_read(f, n, &b, err, sizeof err));
    if (b.px) {
      double p = psnr(g.px, b.px, (long)g.w * g.h);
      snprintf(msg, sizeof msg, "JPEG q%d: PSNR %.1f dB >= %.0f", qs[q], p, need[q]);
      ok(msg, p >= need[q]);
    }
    snprintf(msg, sizeof msg, "JPEG q%d: JFIF says 300 dpi", qs[q]);
    ok(msg, codec_dpi(f, n) == 300);
    snprintf(name, sizeof name, "q%d.jpg", qs[q]);
    save(name, f, n);
    free(f); rgba_free(&b);
  }
  save_rgba("q.rgba", &g);
  /* sizes that aren't whole macroblocks */
  make_noise(&a, 17, 9, 0);
  f = jpeg_encode(&a, 80, 0, &n);
  ok("JPEG 17x9: round trip size", f && jpeg_read(f, n, &b, err, sizeof err) && b.w == 17 && b.h == 9);
  free(f); rgba_free(&b); rgba_free(&a);
  /* transparency goes over white */
  rgba_alloc(&a, 16, 16);
  f = jpeg_encode(&a, 90, 72, &n);
  ok("JPEG: transparent comes out white", f && jpeg_read(f, n, &b, err, sizeof err) && b.px[0] >= 250 && b.px[1] >= 250 && b.px[2] >= 250);
  free(f); rgba_free(&b); rgba_free(&a);
  rgba_free(&g);
}

static void bmp_tests(void)
{
  Rgba a, b;
  u8 *f;
  size_t n;
  char err[128];
  make_noise(&a, 33, 21, 0);
  f = bmp_encode(&a, &n);
  ok("BMP 24: is 24-bit", f && f[28] == 24);
  ok("BMP 24: round trip", f && bmp_read(f, n, &b, err, sizeof err) && same_rgba(&a, &b));
  ok("BMP 24: 72 dpi", f && codec_dpi(f, n) == 72);
  if (f) save("out24.bmp", f, n);
  save_rgba("out24.rgba", &a);
  free(f); rgba_free(&b); rgba_free(&a);
  make_noise(&a, 31, 19, 1);
  f = bmp_encode(&a, &n);
  ok("BMP 32: is 32-bit V4", f && f[28] == 32 && f[14] == 108);
  ok("BMP 32: round trip with alpha", f && bmp_read(f, n, &b, err, sizeof err) && !memcmp(a.px, b.px, (size_t)a.w * a.h * 4));
  if (f) save("out32.bmp", f, n);
  save_rgba("out32.rgba", &a);
  free(f); rgba_free(&b); rgba_free(&a);
}

/* how many distinct colours a picture has (up to 100000) */
static int count_colours(const Rgba *im)
{
  static u8 seen[1 << 21];                         /* a bit per 24-bit colour */
  long i, n = (long)im->w * im->h;
  int c = 0;
  memset(seen, 0, sizeof seen);
  for (i = 0; i < n; i++) {
    u32 v = ((u32)im->px[i * 4] << 16) | ((u32)im->px[i * 4 + 1] << 8) | im->px[i * 4 + 2];
    if (!(seen[v >> 3] & (1 << (v & 7)))) { seen[v >> 3] |= (u8)(1 << (v & 7)); c++; }
  }
  return c;
}

static void ilbm_tests(void)
{
  Rgba a, b, g;
  u8 *f, pal[768];
  size_t n;
  char err[128], msg[100];
  int d;
  /* 24-bit deep: exact */
  make_noise(&a, 45, 17, 0);
  f = ilbm_encode(&a, 0, 0, &n);
  ok("ILBM 24: encodes", f != NULL);
  ok("ILBM 24: BMHD says 24 planes, ByteRun1", f && f[28] == 24 && f[30] == 1);
  ok("ILBM 24: round trip exact", f && ilbm_read(f, n, &b, err, sizeof err) && same_rgba(&a, &b));
  if (f) save("out24.iff", f, n);
  save_rgba("out24iff.rgba", &a);
  free(f); rgba_free(&b); rgba_free(&a);
  /* compressible content really shrinks */
  make_gradient(&g, 320, 64);
  f = ilbm_encode(&g, 0, 0, &n);
  ok("ILBM 24: gradient round trip", f && ilbm_read(f, n, &b, err, sizeof err) && same_rgba(&g, &b));
  free(f); rgba_free(&b);
  /* 8-bit with 200 colours: exact */
  make_palette_pic(&a, 77, 39, 200);
  f = ilbm_encode(&a, 1, 0, &n);
  ok("ILBM 8: BMHD says 8 planes", f && f[28] == 8);
  ok("ILBM 8: round trip exact for <= 256 colours", f && ilbm_read(f, n, &b, err, sizeof err) && same_rgba(&a, &b));
  if (f) save("out8.iff", f, n);
  save_rgba("out8iff.rgba", &a);
  free(f); rgba_free(&b);
  ok("quantize: exact count", quantize(&a, 256, 0, pal, NULL) == 200);
  rgba_free(&a);
  /* exactly 256 colours: still exact */
  make_palette_pic(&a, 64, 64, 256);
  f = ilbm_encode(&a, 1, 0, &n);
  ok("ILBM 8: 256 colours exact", f && ilbm_read(f, n, &b, err, sizeof err) && same_rgba(&a, &b));
  free(f); rgba_free(&b); rgba_free(&a);
  /* a gradient (thousands of colours) quantized, plain and dithered */
  for (d = 0; d < 2; d++) {
    f = ilbm_encode(&g, 1, d, &n);
    ok(d ? "ILBM 8 dithered: decodes" : "ILBM 8 median cut: decodes", f && ilbm_read(f, n, &b, err, sizeof err));
    if (b.px) {
      double p = psnr(g.px, b.px, (long)g.w * g.h);
      int c = count_colours(&b);
      snprintf(msg, sizeof msg, "ILBM 8 %s: %d colours, PSNR %.1f dB", d ? "dithered" : "median cut", c, p);
      ok(msg, c <= 256 && c > 100 && p > (d ? 30 : 33));
    }
    if (f) save(d ? "out8d.iff" : "out8q.iff", f, n);
    free(f); rgba_free(&b);
  }
  save_rgba("out8q.rgba", &g);
  /* transparency goes over white */
  rgba_alloc(&a, 5, 3);
  f = ilbm_encode(&a, 0, 0, &n);
  ok("ILBM: transparent becomes white", f && ilbm_read(f, n, &b, err, sizeof err) && b.px[0] == 255 && b.px[3] == 255);
  free(f); rgba_free(&b); rgba_free(&a);
  rgba_free(&g);
}

/* ------------------------------------------------------------------ OpenRaster */
static void fill_layer(Doc *d, Layer *l, Box r, u8 cr, u8 cg, u8 cb, u8 ca, int noise)
{
  i32 x, y;
  for (y = r.y0; y < r.y1; y++)
    for (x = r.x0; x < r.x1; x++) {
      u8 *p = l->px + ((long)y * d->w + x) * 4;
      p[0] = noise ? (u8)rnd() : cr; p[1] = noise ? (u8)rnd() : cg; p[2] = cb; p[3] = noise ? (u8)(rnd() | 1) : ca;
    }
}
static int same_layer(const Doc *a, const Doc *b, int i)
{
  const Layer *x = a->lay[i], *y = b->lay[i];
  return !strcmp(x->name, y->name) && x->opacity == y->opacity && x->blend == y->blend && x->visible == y->visible &&
         x->locked == y->locked && x->alpha_lock == y->alpha_lock && same_pixels(x->px, y->px, (long)a->w * a->h);
}
/* a zip holding the given entries, for hand-made OpenRaster files */
static u8 *make_zip(const char **names, const u8 **data, const size_t *lens, int n, size_t *len)
{
  ZipWriter w;
  int i;
  u8 *r;
  zip_begin(&w);
  for (i = 0; i < n; i++) zip_add(&w, names[i], data[i], lens[i]);
  zip_finish(&w);
  *len = w.out.len;
  r = (u8 *)buf_take(&w.out);
  zip_free(&w);
  return r;
}

static void ora_tests(void)
{
  Doc *d = doc_new(120, 90, 0x336699, 0), *e;
  Layer *l;
  u8 *f, *g;
  size_t n, m;
  char err[128];
  int i, all = 1;
  ZipReader z;
  /* the background, then layers with every blend mode, part-transparent and smaller than the canvas */
  fill_layer(d, d->lay[0], box_make(0, 0, 120, 90), 0, 0, 0, 0, 1);
  for (i = 1; i <= BM_N; i++) {
    char nm[64];
    snprintf(nm, sizeof nm, i == 1 ? "Ünïcødé <&\"> name" : "Layer %d", i);
    l = layer_new(d, nm);
    fill_layer(d, l, box_make(i * 5 % 100, i * 3 % 70, i * 5 % 100 + 20, i * 3 % 70 + 15), (u8)(i * 13), (u8)(i * 7), (u8)(255 - i), (u8)(i * 14), i % 3 == 0);
    l->blend = (u8)(i - 1);
    l->opacity = (u8)(255 - i * 9);
    l->visible = (u8)(i % 4 != 0);
    l->locked = (u8)(i % 5 == 0);
    l->alpha_lock = (u8)(i % 6 == 0);
    d->lay[d->nlay++] = l;
  }
  l = layer_new(d, "Empty");                     /* a clear layer */
  d->lay[d->nlay++] = l;
  d->active = 4;
  d->dpi = 240;
  f = ora_write(d, &n);
  ok("ORA: writes", f != NULL);
  if (!f) { doc_free(d); return; }
  save("out.ora", f, n);
  ok("ORA: detected", codec_detect(f, n) == FMT_ORA);
  ok("ORA: mimetype first and stored", n > 54 && !memcmp(f + 30, "mimetype", 8) && f[8] == 0 && f[9] == 0 && !memcmp(f + 38, "image/openraster", 16));
  ok("ORA: dpi", codec_dpi(f, n) == 240);
  e = ora_read(f, n, err, sizeof err);
  ok("ORA: reads back", e != NULL);
  if (e) {
    ok("ORA: size", e->w == 120 && e->h == 90);
    ok("ORA: layer count", e->nlay == d->nlay);
    for (i = 0; i < d->nlay && i < e->nlay; i++) if (!same_layer(d, e, i)) { all = 0; printf("  layer %d (%s) differs\n", i, d->lay[i]->name); }
    ok("ORA: names, opacity, blend, visibility, locks and pixels of every layer", all && e->nlay == d->nlay);
    ok("ORA: active layer", e->active == 4);
    ok("ORA: dpi read back", e->dpi == 240);
    ok("ORA: clean and no undo", !e->dirty && !doc_can_undo(e));
    doc_free(e);
  }
  /* the merged image and thumbnail inside */
  if (zip_open(&z, f, n)) {
    int k = zip_find(&z, "mergedimage.png");
    Rgba mi, ref, th;
    g = zip_read(&z, k, &m);
    ok("ORA: merged image decodes", g && png_read(g, m, &mi, err, sizeof err));
    rgba_alloc(&ref, d->w, d->h);
    doc_composite(d, doc_box(d), ref.px, (long)d->w * 4);
    ok("ORA: merged image is the composite", mi.px && same_rgba(&mi, &ref));
    free(g); rgba_free(&mi); rgba_free(&ref);
    k = zip_find(&z, "Thumbnails/thumbnail.png");
    g = zip_read(&z, k, &m);
    ok("ORA: thumbnail", g && png_read(g, m, &th, err, sizeof err) && th.w <= 256 && th.h <= 256);
    free(g); rgba_free(&th);
    ok("ORA: layers are cropped", zip_find(&z, "data/layer1.png") >= 0);
    k = zip_find(&z, "stack.xml");
    g = zip_read(&z, k, &m);
    ok("ORA: stack.xml has offsets and krita:subtract", g && strstr((char *)g, "x=\"40\" y=\"24\"") && strstr((char *)g, "krita:subtract"));
    if (g) save("stack.xml", g, m);
    free(g);
    zip_close(&z);
  }
  /* the generic loader and the flat decoder on an ORA */
  e = codec_load_doc(f, n, err, sizeof err);
  ok("codec_load_doc: ORA keeps its layers", e && e->nlay == d->nlay);
  doc_free(e);
  {
    Rgba mi;
    ok("codec_decode: ORA gives its merged image", codec_decode(f, n, &mi, err, sizeof err) && mi.w == 120);
    rgba_free(&mi);
  }
  damage("out.ora", f, n, -1);
  free(f);
  doc_free(d);

  /* hand-made: nested groups (flattened, opacity multiplied, hidden group hides), offsets
     partly off the canvas, an unknown op, a missing layer picture, no mergedimage */
  {
    static const char *stack =
      "<?xml version='1.0'?><image w='10' h='8' xres='300'><stack>"
      "<layer name='top' src='a.png' x='-2' y='6' opacity='1.0' composite-op='svg:multiply'/>"
      "<stack name='grp' opacity='0.5' visibility='visible' composite-op='svg:screen'>"
      "  <layer name='inner' src='a.png' x='8' y='0' opacity='0.5' composite-op='svg:subtract'/>"
      "  <stack opacity='1' visibility='hidden'><layer name='deep' src='a.png' opacity='1'/></stack>"
      "</stack>"
      "<layer name='missing' src='nope.png' composite-op='made:up'/>"
      "<layer name='bottom' src='a.png' x='0' y='0' selected='true'/>"
      "</stack></image>";
    Rgba a;
    const char *names[2] = { "stack.xml", "a.png" };
    const u8 *data[2];
    size_t lens[2], pl;
    u8 *png, *zipd;
    make_noise(&a, 4, 4, 0);
    png = png_encode(&a, 0, &pl);
    data[0] = (const u8 *)stack; lens[0] = strlen(stack); data[1] = png; lens[1] = pl;
    zipd = make_zip(names, data, lens, 2, &n);
    e = ora_read(zipd, n, err, sizeof err);
    ok("ORA groups: reads", e != NULL);
    if (e) {
      /* bottom first: bottom, missing, deep (in the hidden group), the hidden group, inner, grp, top */
      ok("ORA groups: groups kept, bottom first", e->nlay == 7 && !strcmp(e->lay[0]->name, "bottom") && !strcmp(e->lay[6]->name, "top") &&
         e->lay[5]->kind == LK_GROUP && e->lay[3]->kind == LK_GROUP && e->lay[2]->depth == 2 && e->lay[4]->depth == 1 && e->lay[5]->depth == 0);
      ok("ORA groups: the group keeps its opacity and mode", e->nlay == 7 && e->lay[5]->opacity == 128 && e->lay[5]->blend == BM_SCREEN &&
         e->lay[4]->opacity == 128 && e->lay[4]->blend == BM_SUBTRACT);
      ok("ORA groups: hidden group hidden, its layer as it was", e->nlay == 7 && !e->lay[3]->visible && e->lay[2]->visible);
      ok("ORA groups: unknown op is normal, missing picture is empty", e->nlay == 7 && e->lay[1]->blend == BM_NORMAL && e->lay[1]->px[3] == 0);
      ok("ORA groups: offset layer clipped (8,0 holds the picture's 0,0)", e->nlay == 7 && !memcmp(e->lay[4]->px + 8 * 4, a.px, 4));
      ok("ORA groups: negative offset clipped", e->nlay == 7 && !memcmp(e->lay[6]->px + (6 * 10 + 0) * 4, a.px + 2 * 4, 4));
      ok("ORA groups: selected layer active", e->active == 0);
      ok("ORA groups: xres", e->dpi == 300);
      doc_free(e);
    }
    free(zipd);
    /* only a merged image: one layer */
    names[0] = "mergedimage.png"; data[0] = png; lens[0] = pl;
    zipd = make_zip(names, data, lens, 1, &n);
    e = ora_read(zipd, n, err, sizeof err);
    ok("ORA without stack.xml opens from the merged image", e && e->nlay == 1 && e->w == 4 && same_pixels(e->lay[0]->px, a.px, 16));
    doc_free(e);
    free(zipd);
    /* nothing usable */
    names[0] = "other.txt";
    zipd = make_zip(names, data, lens, 1, &n);
    e = ora_read(zipd, n, err, sizeof err);
    ok("ORA with neither fails with a message", !e && err[0]);
    free(zipd);
    free(png); rgba_free(&a);
  }
}

/* ------------------------------------------------------------------ the generic loader */
static void loader_tests(void)
{
  Rgba a;
  u8 *f;
  size_t n;
  char err[128];
  Doc *d;
  make_noise(&a, 20, 10, 1);
  f = png_encode(&a, 96, &n);
  d = codec_load_doc(f, n, err, sizeof err);
  ok("codec_load_doc: PNG as one Background layer", d && d->nlay == 1 && !strcmp(d->lay[0]->name, "Background") && d->w == 20 && d->h == 10);
  ok("codec_load_doc: pixels and dpi", d && same_pixels(d->lay[0]->px, a.px, 200) && d->dpi == 96);
  ok("codec_load_doc: clean", d && !d->dirty && !doc_can_undo(d));
  doc_free(d);
  free(f);
  d = codec_load_doc((const u8 *)"hello world, not a picture", 26, err, sizeof err);
  ok("codec_load_doc: unknown data fails with a message", !d && strstr(err, "nknown"));
  rgba_free(&a);
  ok("codec_decode: empty input fails", !codec_decode((const u8 *)"", 0, &a, NULL, 0));
  ok("codec_name", !strcmp(codec_name(FMT_ILBM), "IFF ILBM"));
  /* headers claiming huge pictures must be refused before any big allocation */
  {
    static const u8 bigpng[] = { 137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0x40, 0, 0, 0, 0x40, 0, 8, 6, 0, 0, 0, 0, 0, 0, 0,
                                 0, 0, 0, 2, 'I', 'D', 'A', 'T', 0x78, 0x01, 0, 0, 0, 0 };
    static const u8 bigbmp[54] = { 'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0, 0, 0x40, 0, 0, 0, 0x40, 0, 0, 1, 0, 24, 0 };
    static const u8 bigilbm[] = { 'F', 'O', 'R', 'M', 0, 0, 0, 44, 'I', 'L', 'B', 'M', 'B', 'M', 'H', 'D', 0, 0, 0, 20, 0x3F, 0xFF, 0x3F, 0xFF,
                                  0, 0, 0, 0, 24, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 'B', 'O', 'D', 'Y', 0, 0, 0, 4, 0x81, 0, 0x81, 0 };
    static const u8 biggif[] = { 'G', 'I', 'F', '8', '9', 'a', 0xFF, 0x3F, 0xFF, 0x3F, 0, 0, 0, 0x2C, 0, 0, 0, 0, 0xFF, 0x3F, 0xFF, 0x3F, 0, 8, 2, 0x84, 0, 0, 0x3B };
    ok("huge PNG refused", !codec_decode(bigpng, sizeof bigpng, &a, err, sizeof err));
    ok("huge BMP refused", !codec_decode(bigbmp, sizeof bigbmp, &a, err, sizeof err));
    ok("huge ILBM refused", !codec_decode(bigilbm, sizeof bigilbm, &a, err, sizeof err));
    ok("huge GIF refused", !codec_decode(biggif, sizeof biggif, &a, err, sizeof err));
  }
  rgba_free(&a);
}

int main(int argc, char **argv)
{
  char cmd[600];
  setvbuf(stdout, NULL, _IOLBF, 0);
  if (argc > 1) outdir = argv[1];
  snprintf(cmd, sizeof cmd, "mkdir -p '%s'", outdir);
  if (system(cmd)) printf("cannot make %s\n", outdir);
  reference_tests();
  png_tests();
  jpeg_tests();
  bmp_tests();
  ilbm_tests();
  ora_tests();
  loader_tests();
  printf("%d tests, %d failed\n", ntests, nfailed);
  return nfailed != 0;
}
