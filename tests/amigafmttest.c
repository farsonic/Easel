/*
 * amigafmttest.c - native tests for core/webp.c and core/amigafmt.c (see core/amigafmt.h): every
 * file in tests/data/amigafmt (made by tests/make_amigafmt.py) decoded and compared with the
 * pixels it must give, then damaged - every truncation (a spread of them for big files) and
 * random byte changes - to show no input crashes or reads out of bounds (run it under
 * AddressSanitizer and UBSan).
 *
 * WebP files are compared with libwebp's own decoding (through Pillow); the lossy ones are
 * expected to match exactly, and the largest difference seen is reported.
 *
 * Build and run from the easel folder:
 *   cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. core/util.c core/zip.c core/xml.c core/image.c \
 *      core/doc.c core/codec.c core/png.c core/jpegenc.c core/bmp.c core/ilbm.c core/gif.c core/ora.c \
 *      core/webp.c core/amigafmt.c tests/amigafmttest.c -o /tmp/amigafmttest -lm && /tmp/amigafmttest
 * (an optional argument names another data folder)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/codec.h"
#include "core/amigafmt.h"

static int ntests, nfailed;

static void ok(const char *name, int cond)
{
  ntests++;
  if (!cond) { nfailed++; printf("FAIL: %s\n", name); }
}

static u32 rng = 4321;
static u32 rnd(void) { rng = rng * 1103515245UL + 12345; return (rng >> 8) & 0xFFFFFF; }

static int ends_with(const char *s, const char *suf) { size_t a = strlen(s), b = strlen(suf); return a >= b && !strcmp(s + a - b, suf); }

/* the decoder a test file goes through: .info files straight to info_read_ex (a PNG icon is
   only PNG to amigafmt_detect), the rest through detection and amigafmt_decode */
static int decode(const char *name, const u8 *d, size_t n, int sel, Rgba *im, char *err, size_t errcap)
{
  if (ends_with(name, ".info")) return info_read_ex(d, n, sel, INFO_PAL_AUTO, 0, im, err, errcap);
  return amigafmt_decode(d, n, im, err, errcap);
}

/* truncations and byte changes: never a crash; a success gives a sane picture */
static void damage(const char *name, const u8 *d, size_t n)
{
  size_t len, step = n > 3000 ? n / 700 : 1;
  int k, sane = 1;
  char err[128], msg[400];
  u8 *copy;
  Rgba im;
  for (len = 0; len < n; len += step) {
    u8 *t = (u8 *)malloc(len ? len : 1);         /* exact size, so ASan sees any overrun */
    memcpy(t, d, len);
    if (decode(name, t, len, 0, &im, err, sizeof err)) {
      if (!im.px || im.w < 1 || im.h < 1) sane = 0;
      rgba_free(&im);
    } else if (im.px) sane = 0;
    free(t);
  }
  copy = (u8 *)malloc(n);
  for (k = 0; k < 250; k++) {
    int j, nm = 1 + (int)(rnd() % 4);
    memcpy(copy, d, n);
    for (j = 0; j < nm; j++) {
      size_t at = k < 120 ? rnd() % (n < 96 ? n : 96) : rnd() % n;   /* headers get half the attention */
      copy[at] = (u8)(k & 1 ? rnd() : (u32)copy[at] ^ (1u << (rnd() % 8)));
    }
    if (decode(name, copy, n, k & 1, &im, err, sizeof err)) {
      if (!im.px || im.w < 1 || im.h < 1) sane = 0;
      rgba_free(&im);
    } else if (im.px) sane = 0;
  }
  free(copy);
  snprintf(msg, sizeof msg, "%s: damaged copies fail cleanly or decode sanely", name);
  ok(msg, sane);
}

/* odd inputs that are not test files: empty, tiny, signatures with nothing after them */
static void hostile(void)
{
#define SIG(x) { x, sizeof(x) - 1 }
  static const struct { const char *s; size_t len; } sigs[] = {
    SIG("RIFF\x10\0\0\0WEBPVP8 "), SIG("RIFF\xff\xff\xff\xffWEBPVP8L\x05\0\0\0\x2f\xff\xff\xff\xff"),
    SIG("RIFF\x20\0\0\0WEBPVP8X\x0a\0\0\0\x02\0\0\0\xff\xff\xff\xff\xff\xff"), SIG("FORM\0\0\0\x04""ACBM"),
    SIG("FORM\0\0\0\x04RGBN"), SIG("FORM\xff\xff\xff\xffRGB8BMHD\0\0\0\x14"), SIG("FORM\0\0\0\x04""DEEP"),
    SIG("FORM\0\0\0\x04""ANIM"), SIG("FORM\0\0\0\x0cILBMSHAM\0\0\0\0"), SIG("\xe3\x10\0\x01"), SIG("\x59\xa6\x6a\x95"),
    SIG("\x01\xda\x01\x02\0\x03")
  };
#undef SIG
  size_t i, n;
  int k, crashfree = 1;
  char err[128];
  Rgba im;
  for (i = 0; i < sizeof sigs / sizeof *sigs; i++)
    for (n = 0; n < 600; n += 1 + n / 8) {
      size_t sl = sigs[i].len;
      u8 *t = (u8 *)malloc(n + sl + 1);
      memcpy(t, sigs[i].s, sl);
      for (k = 0; k < (int)n; k++) t[sl + k] = (u8)(k % 3 == 0 ? 0xFF : rnd());
      if (amigafmt_decode(t, n + sl, &im, err, sizeof err)) rgba_free(&im);
      if (info_read(t, n + sl, &im, err, sizeof err)) rgba_free(&im);
      free(t);
    }
  /* each reader on zeros and on noise of awkward sizes (Degas and NEOchrome sizes too) */
  {
    static const size_t sizes[] = { 0, 1, 7, 12, 20, 33, 78, 511, 512, 513, 32034, 32066, 32128 };
    int (*const rd[])(const u8 *, size_t, Rgba *, char *, size_t) = { webp_read, acbm_read, rgbn_read, deep_read, sham_read, anim_read,
                                                                      info_read, sunras_read, sgi_read, degas_read, neo_read };
    size_t s;
    for (s = 0; s < sizeof sizes / sizeof *sizes; s++) {
      u8 *t = (u8 *)malloc(sizes[s] ? sizes[s] : 1);
      for (k = 0; k < (int)(sizeof rd / sizeof *rd); k++) {
        memset(t, 0, sizes[s]);
        if (rd[k](t, sizes[s], &im, err, sizeof err)) rgba_free(&im);
        for (i = 0; i < sizes[s]; i++) t[i] = (u8)rnd();
        if (rd[k](t, sizes[s], &im, err, sizeof err)) rgba_free(&im);
      }
      free(t);
    }
  }
  ok("hostile inputs", crashfree);
  ok("a NULL err is allowed", !webp_read((const u8 *)"RIFF", 4, &im, NULL, 0) && !im.px);
  ok("names", !strcmp(amigafmt_name(AMF_WEBP), "WebP") && !strcmp(amigafmt_name(999), "unknown"));
}

int main(int argc, char **argv)
{
  const char *dir = argc > 1 ? argv[1] : "tests/data/amigafmt";
  char path[600], line[600], err[160], msg[700];
  FILE *mf;
  int nfiles = 0, lossyn = 0, lossymax = 0, maxall = 0;
  long lossydiffpx = 0;
  snprintf(path, sizeof path, "%s/manifest.txt", dir);
  if (!(mf = fopen(path, "r"))) { printf("cannot read %s (run tests/make_amigafmt.py)\n", path); return 1; }
  while (fgets(line, sizeof line, mf)) {
    char name[200], expname[200], mode[100];
    u8 *d, *e;
    size_t n, en;
    Rgba im, ex;
    int sel, tol = 0, r, maxd = 0;
    long diffpx = 0;
    if (sscanf(line, "%199s %199s %99s", name, expname, mode) != 3) continue;
    sel = strstr(mode, "sel") != NULL;
    if (!strncmp(mode, "maxdiff:", 8)) tol = atoi(mode + 8);
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(d = file_read(path, &n))) { printf("cannot read %s\n", path); nfailed++; continue; }
    snprintf(path, sizeof path, "%s/%s", dir, expname);
    if (!(e = file_read(path, &en)) || !png_read(e, en, &ex, err, sizeof err)) { printf("cannot read %s\n", path); free(d); free(e); nfailed++; continue; }
    nfiles++;
    if (!ends_with(name, ".info")) {
      snprintf(msg, sizeof msg, "%s: detected", name);
      ok(msg, amigafmt_detect(d, n) != AMF_UNKNOWN);
    }
    r = decode(name, d, n, sel, &im, err, sizeof err);
    snprintf(msg, sizeof msg, "%s: decodes (%s)", name, r ? "ok" : err);
    ok(msg, r);
    if (r) {
      snprintf(msg, sizeof msg, "%s: size %dx%d (expected %dx%d)", name, (int)im.w, (int)im.h, (int)ex.w, (int)ex.h);
      ok(msg, im.w == ex.w && im.h == ex.h);
      if (im.w == ex.w && im.h == ex.h) {
        long i;
        for (i = 0; i < (long)im.w * im.h * 4; i++) {
          int dd = abs(im.px[i] - ex.px[i]);
          if (dd > maxd) maxd = dd;
          if (dd) diffpx++;
        }
        snprintf(msg, sizeof msg, "%s: pixels (largest difference %d, %ld values differ)", name, maxd, diffpx);
        ok(msg, maxd <= tol);
        if (maxd > maxall) maxall = maxd;
        if (ends_with(name, ".webp")) {
          if (maxd > lossymax) lossymax = maxd;
          lossydiffpx += diffpx;
          lossyn++;
        }
      }
      rgba_free(&im);
    }
    damage(name, d, n);
    rgba_free(&ex);
    free(d); free(e);
  }
  fclose(mf);
  hostile();
  printf("%d files; WebP: %d files, largest difference from libwebp %d (%ld values differ); all formats: largest difference %d\n",
         nfiles, lossyn, lossymax, lossydiffpx, maxall);
  printf("%d tests, %d failed\n", ntests, nfailed);
  return nfailed != 0;
}
