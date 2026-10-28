/*
 * rasterstest.c - native tests for core/rasters.h (tiff.c, psd.c, rasters.c): every reference
 * picture in tests/data/rasters (made by tests/make_rasters.py) decoded and compared with the
 * expected pixels, its format detected, and then damaged input of every kind: every prefix of
 * each file (all of them for small files, a spread for big ones) and random byte changes, which
 * must fail cleanly or decode, never crash or read out of bounds. Run it under AddressSanitizer
 * and UBSan.
 *
 * Build and run from the easel folder:
 *   cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. core/util.c core/zip.c core/xml.c \
 *      core/image.c core/doc.c core/codec.c core/png.c core/jpegenc.c core/bmp.c core/ilbm.c core/gif.c \
 *      core/ora.c core/tiff.c core/psd.c core/rasters.c tests/rasterstest.c -o /tmp/rasterstest -lm && /tmp/rasterstest
 * Optional arguments: a number of random damages per file (default 300), and "quick" to skip the
 * prefixes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core/rasters.h"

static int ntests, nfailed;
static void ok(const char *name, int cond)
{
  ntests++;
  if (!cond) { nfailed++; printf("FAIL: %s\n", name); }
}

/* a small repeatable random number generator */
static u32 rng = 12345;
static u32 rnd(void) { rng = rng * 1103515245UL + 12345; return (rng >> 8) & 0xFFFFFF; }

static int fmt_of(const char *s)
{
  static const char *const names[] = { "", "tiff", "psd", "tga", "pcx", "pnm", "xpm", "dds", "qoi", "ico", "hdr", "fits", "dpx" };
  int i;
  for (i = 1; i <= RAS_DPX; i++) if (!strcmp(s, names[i])) return i;
  return -1;
}

/* the largest channel difference (fully transparent pixels equal whatever their colour), -1 if
   the sizes differ */
static int maxdiff(const Rgba *im, const u8 *e, u32 ew, u32 eh, long *where)
{
  long i, n;
  int m = 0;
  if ((u32)im->w != ew || (u32)im->h != eh) return -1;
  n = (long)ew * eh;
  for (i = 0; i < n; i++) {
    const u8 *a = im->px + i * 4, *b = e + i * 4;
    int k;
    if (a[3] == 0 && b[3] == 0) continue;
    for (k = 0; k < 4; k++) {
      int d = a[k] > b[k] ? a[k] - b[k] : b[k] - a[k];
      if (d > m) { m = d; *where = i; }
    }
  }
  return m;
}
static double psnr(const Rgba *im, const u8 *e)
{
  double se = 0;
  long i, n = (long)im->w * im->h * 4;
  for (i = 0; i < n; i++) { double d; if ((i & 3) == 3) continue; d = (double)im->px[i] - e[i]; se += d * d; }
  if (se == 0) return 999;
  return 10 * log10(255.0 * 255.0 / (se / ((double)n * 3 / 4)));
}

/* damage: must never crash; returns whether it decoded */
static int try_decode(const u8 *d, size_t n)
{
  Rgba im;
  char err[128];
  int r = rasters_read(d, n, &im, err, sizeof err);
  if (r) {
    ok("a decoded picture has pixels", im.px && im.w > 0 && im.h > 0);
    rgba_free(&im);
  } else ok("a failed decode leaves the picture empty", !im.px && !im.w && !im.h && err[0]);
  return r;
}

static void fuzz(const char *name, const u8 *d, size_t n, int flips, int prefixes)
{
  u8 *b = (u8 *)malloc(n ? n : 1);
  size_t i, step;
  int k;
  if (!b) return;
  if (prefixes) {
    /* every prefix up to 4K, then about 1500 more spread over the rest; each in its own malloc'd
       block so ASan sees a read past the end */
    step = n <= 4096 ? 1 : (n - 4096) / 1500 + 1;
    for (i = 0; i < n; i += i < 4096 ? 1 : step) {
      u8 *p = (u8 *)malloc(i ? i : 1);
      if (!p) break;
      memcpy(p, d, i);
      try_decode(p, i);
      free(p);
    }
  }
  for (k = 0; k < flips; k++) {
    int j, nf = 1 + (int)(rnd() % 4);
    memcpy(b, d, n);
    for (j = 0; j < nf; j++) {
      size_t at = rnd() % n;
      u32 kind = rnd() % 4;
      if (kind == 0) b[at] = (u8)rnd();
      else if (kind == 1) b[at] ^= (u8)(1 << (rnd() % 8));
      else if (kind == 2) b[at] = 0xFF;
      else b[at] = 0;
    }
    /* mostly in the header, where the lengths and offsets are */
    if (k & 1) { size_t at = rnd() % (n < 512 ? n : 512); b[at] = (u8)rnd(); }
    try_decode(b, n);
  }
  (void)name;
  free(b);
}

int main(int argc, char **argv)
{
  FILE *f = fopen("tests/data/rasters/manifest.txt", "r");
  char line[512], file[256], expf[256], fmt[32], tol[32], path[600], msg[700];
  int flips = argc > 1 ? atoi(argv[1]) : 300, prefixes = !(argc > 2 && !strcmp(argv[2], "quick"));
  int nfiles = 0;
  if (!f) { printf("no tests/data/rasters/manifest.txt (run python3 tests/make_rasters.py)\n"); return 1; }
  while (fgets(line, sizeof line, f)) {
    size_t n, en;
    u8 *d, *e;
    Rgba im;
    char err[128];
    int want, got, r;
    if (sscanf(line, "%255s %255s %31s %31s", file, expf, fmt, tol) != 4) continue;
    snprintf(path, sizeof path, "tests/data/rasters/%s", file);
    d = file_read(path, &n);
    snprintf(path, sizeof path, "tests/data/rasters/%s", expf);
    e = file_read(path, &en);
    if (!d || !e || en < 12) { snprintf(msg, sizeof msg, "%s readable", file); ok(msg, 0); free(d); free(e); continue; }
    nfiles++;
    want = fmt_of(fmt);
    got = rasters_detect(d, n, 0);
    snprintf(msg, sizeof msg, "%s detected as %s (got %s)", file, rasters_name(want), rasters_name(got));
    ok(msg, got == want);
    r = rasters_read(d, n, &im, err, sizeof err);
    snprintf(msg, sizeof msg, "%s decodes (%s)", file, r ? "" : err);
    ok(msg, r);
    if (r) {
      u32 ew = ((u32)e[4] << 24) | ((u32)e[5] << 16) | ((u32)e[6] << 8) | e[7], eh = ((u32)e[8] << 24) | ((u32)e[9] << 16) | ((u32)e[10] << 8) | e[11];
      if (!strncmp(tol, "psnr:", 5)) {
        double p = (u32)im.w == ew && (u32)im.h == eh ? psnr(&im, e + 12) : 0;
        snprintf(msg, sizeof msg, "%s PSNR %.1f >= %s", file, p, tol + 5);
        ok(msg, p >= atof(tol + 5));
      } else {
        long where = 0;
        int m = maxdiff(&im, e + 12, ew, eh, &where);
        snprintf(msg, sizeof msg, "%s pixels within %s (%dx%d vs %ux%u, diff %d at pixel %ld)", file, tol, (int)im.w, (int)im.h, (unsigned)ew, (unsigned)eh, m, where);
        ok(msg, m >= 0 && m <= atoi(tol));
      }
      rgba_free(&im);
    }
    fuzz(file, d, n, flips, prefixes);
    free(d); free(e);
  }
  fclose(f);
  /* odd input */
  {
    Rgba im;
    ok("empty input fails", !rasters_read((const u8 *)"", 0, &im, NULL, 0));
    ok("nothing detected in a few bytes", rasters_detect((const u8 *)"P", 1, 0) == RAS_UNKNOWN);
    ok("every reader refuses empty input", !tiff_read((const u8 *)"", 0, &im, NULL, 0) && !psd_read((const u8 *)"", 0, &im, NULL, 0) &&
       !tga_read((const u8 *)"", 0, &im, NULL, 0) && !pcx_read((const u8 *)"", 0, &im, NULL, 0) && !pnm_read((const u8 *)"", 0, &im, NULL, 0) &&
       !xpm_read((const u8 *)"", 0, &im, NULL, 0) && !dds_read((const u8 *)"", 0, &im, NULL, 0) && !qoi_read((const u8 *)"", 0, &im, NULL, 0) &&
       !ico_read((const u8 *)"", 0, &im, NULL, 0) && !hdr_read((const u8 *)"", 0, &im, NULL, 0) && !fits_read((const u8 *)"", 0, &im, NULL, 0) &&
       !dpx_read((const u8 *)"", 0, &im, NULL, 0));
  }
  printf("%d files, %d tests, %d failed\n", nfiles, ntests, nfailed);
  return nfailed != 0;
}
