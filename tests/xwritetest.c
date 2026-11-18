/*
 * xwritetest.c - Easel 1.1's new writers (core/xwrite.c): WebP lossless, PDF, PBM/PGM/PPM/PAM, QOI,
 * ICO, DDS (uncompressed, BC1, BC3, mipmaps), DPX (8 and 10 bits) and FITS, each read back by
 * Easel's own reader (exactly, for the lossless ones), then fuzzed: every file written is mutated
 * a few hundred times and fed to codec_decode with a work bound (the progress hook stops a decoder
 * after a fixed number of steps), under AddressSanitizer and UBSan.
 *
 * The files go to a folder (argv[1], default /tmp/easel_xwrite_out) with each reference picture
 * beside them as .rgba, for tests/check_xwrite.py to check with Pillow, ImageMagick and poppler.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core/xwrite.h"
#include "core/rasters.h"
#include "core/amigafmt.h"
#include "core/batch.h"
#include "core/palette.h"
#include "core/svg.h"
#include "core/shape.h"
#include "core/path.h"

static int ntests, nfailed;
static const char *outdir = "/tmp/easel_xwrite_out";
static void ok(const char *name, int cond)
{
  ntests++;
  if (!cond) { nfailed++; printf("FAIL: %s\n", name); }
}
static u32 rng = 777;
static u32 rnd(void) { rng = rng * 1103515245UL + 12345; return (rng >> 8) & 0xFFFFFF; }
static void save(const char *name, const void *d, size_t n)
{
  char path[512];
  snprintf(path, sizeof path, "%s/%s", outdir, name);
  if (!file_write(path, d, n)) printf("cannot write %s\n", path);
}
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

/* test pictures: kind 0 a photo-like gradient with noise, 1 the same with alpha, 2 grey, 3 one
   colour, 4 noise (nothing repeats), 5 flat areas and stripes (long copies) */
static void make(Rgba *im, i32 w, i32 h, int kind)
{
  i32 x, y;
  rgba_alloc(im, w, h);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      u8 *p = im->px + ((size_t)y * w + x) * 4;
      u32 r = rnd();
      switch (kind) {
      case 0: case 1:
        p[0] = (u8)(x * 255 / (w > 1 ? w - 1 : 1)); p[1] = (u8)(y * 255 / (h > 1 ? h - 1 : 1)); p[2] = (u8)((x + y) * 3 + (r & 7));
        p[3] = kind ? (u8)((x * 7 + y * 3) & 255) : 255;
        if (kind && x < 4) p[3] = 0;
        break;
      case 2: p[0] = p[1] = p[2] = (u8)((x * 255 / (w > 1 ? w - 1 : 1) + (r & 3)) & 255); p[3] = 255; break;
      case 3: p[0] = 40; p[1] = 90; p[2] = 200; p[3] = 255; break;
      case 4: p[0] = (u8)r; p[1] = (u8)(r >> 8); p[2] = (u8)(r >> 16); p[3] = (u8)(rnd() | 1); break;
      default:
        p[0] = (u8)((x / 10) * 40); p[1] = (u8)((y / 7) & 1 ? 255 : 0); p[2] = 128; p[3] = 255;
        if (y % 13 == 5) { p[0] = (u8)x; p[1] = 7; }
        break;
      }
    }
  if (kind == 0 || kind == 2) { im->px[0] = im->px[1] = im->px[2] = 0; im->px[(size_t)(w * h - 1) * 4] = im->px[(size_t)(w * h - 1) * 4 + 1] = im->px[(size_t)(w * h - 1) * 4 + 2] = 255; }
}
static int same(const Rgba *a, const Rgba *b, int tol)
{
  long n, k;
  if (a->w != b->w || a->h != b->h || !a->px || !b->px) return 0;
  n = (long)a->w * a->h * 4;
  for (k = 0; k < n; k++) if (abs(a->px[k] - b->px[k]) > tol) return 0;
  return 1;
}
static double mean_err(const Rgba *a, const Rgba *b)
{
  long n = (long)a->w * a->h * 4, k;
  double s = 0;
  if (a->w != b->w || a->h != b->h) return 999;
  for (k = 0; k < n; k++) s += abs(a->px[k] - b->px[k]);
  return s / n;
}

/* ------------------------------------------------------------------ fuzzing with a work bound */
static long fuzz_steps;
static int fuzz_hook(long done, long total) { (void)done; (void)total; return ++fuzz_steps > 200; }
static long nfuzzed;
static void fuzz(const u8 *d, size_t n, int rounds)
{
  u8 *m = (u8 *)malloc(n + 1);
  int r;
  if (!m) return;
  for (r = 0; r < rounds; r++) {
    Rgba out;
    size_t len = n, k;
    int nm = 1 + (int)(rnd() % 8);
    memcpy(m, d, n);
    for (k = 0; k < (size_t)nm; k++) {
      size_t at = rnd() % n;
      switch (rnd() % 4) {
      case 0: m[at] = (u8)rnd(); break;
      case 1: m[at] ^= (u8)(1 << (rnd() % 8)); break;
      case 2: m[at] = (rnd() & 1) ? 0xFF : 0; break;
      default: len = at + 1; break;                    /* truncated */
      }
    }
    fuzz_steps = 0;
    progress_reset();
    progress_hook = fuzz_hook;
    if (codec_decode(m, len, &out, NULL, 0)) rgba_free(&out);
    progress_hook = NULL;
    progress_reset();
    nfuzzed++;
  }
  free(m);
}

/* strstr through binary data */
static char *mfind(char *s, size_t n, const char *pat)
{
  size_t m = strlen(pat), i;
  for (i = 0; i + m <= n; i++) if (!memcmp(s + i, pat, m)) return s + i;
  return NULL;
}
static void roundtrip(const char *name, u8 *data, size_t len, const Rgba *ref, int (*rd)(const u8 *, size_t, Rgba *, char *, size_t), int tol, const char *file)
{
  Rgba back;
  char err[120] = "", nm[160];
  int r;
  snprintf(nm, sizeof nm, "%s written", name); ok(nm, data != NULL);
  if (!data) return;
  r = rd(data, len, &back, err, sizeof err);
  snprintf(nm, sizeof nm, "%s read back (%s)", name, err); ok(nm, r);
  if (r) {
    snprintf(nm, sizeof nm, "%s same picture", name); ok(nm, same(ref, &back, tol));
    rgba_free(&back);
  }
  snprintf(nm, sizeof nm, "%s detected", name); ok(nm, codec_decode(data, len, &back, NULL, 0) && (rgba_free(&back), 1));
  if (file) {
    char rf[160];
    save(file, data, len);
    snprintf(rf, sizeof rf, "%s.rgba", file);
    save_rgba(rf, ref);
  }
  fuzz(data, len, 120);
  free(data);
}

#define RT(name, expr, ...) do { size_t l_; u8 *d_; len = 0; d_ = (expr); l_ = len; roundtrip(name, d_, l_, __VA_ARGS__); } while (0)
int main(int argc, char **argv)
{
  Rgba pic, alpha, grey, flat, noise, stripes, one, wide;
  size_t len;
  u8 *d;
  int i;
  if (argc > 1) outdir = argv[1];
  { char cmd[600]; snprintf(cmd, sizeof cmd, "mkdir -p '%s'", outdir); if (system(cmd)) printf("cannot make %s\n", outdir); }
  make(&pic, 67, 45, 0); make(&alpha, 67, 45, 1); make(&grey, 50, 31, 2); make(&flat, 300, 200, 3);
  make(&noise, 64, 64, 4); make(&stripes, 233, 101, 5); make(&one, 1, 1, 4); make(&wide, 1000, 3, 0);

  /* WebP lossless: every picture exactly */
  RT("webp rgb", webp_encode_lossless(&pic, &len), &pic, webp_read, 0, "pic.webp");
  RT("webp rgba", webp_encode_lossless(&alpha, &len), &alpha, webp_read, 0, "alpha.webp");
  RT("webp grey", webp_encode_lossless(&grey, &len), &grey, webp_read, 0, "grey.webp");
  RT("webp one colour", webp_encode_lossless(&flat, &len), &flat, webp_read, 0, "flat.webp");
  RT("webp noise", webp_encode_lossless(&noise, &len), &noise, webp_read, 0, "noise.webp");
  RT("webp stripes", webp_encode_lossless(&stripes, &len), &stripes, webp_read, 0, "stripes.webp");
  RT("webp 1x1", webp_encode_lossless(&one, &len), &one, webp_read, 0, "one.webp");
  RT("webp wide", webp_encode_lossless(&wide, &len), &wide, webp_read, 0, "wide.webp");
  d = webp_encode_lossless(&flat, &len);
  ok("webp one colour is small", d && len < 200);
  free(d);
  d = webp_encode_lossless(&stripes, &len);
  ok("webp stripes compress", d && len < (size_t)stripes.w * stripes.h / 4);
  free(d);

  /* Netpbm */
  RT("ppm", pnm_encode(&pic, PNM_AUTO, &len), &pic, pnm_read, 0, "pic.ppm");
  RT("pgm", pnm_encode(&grey, PNM_AUTO, &len), &grey, pnm_read, 0, "grey.pgm");
  RT("pam", pnm_encode(&alpha, PNM_AUTO, &len), &alpha, pnm_read, 0, "alpha.pam");
  d = pnm_encode(&pic, PNM_AUTO, &len); ok("ppm chosen for colour", d && !memcmp(d, "P6", 2)); free(d);
  d = pnm_encode(&grey, PNM_AUTO, &len); ok("pgm chosen for grey", d && !memcmp(d, "P5", 2)); free(d);
  {
    Rgba bw, back;
    i32 k;
    rgba_alloc(&bw, 21, 9);
    for (k = 0; k < 21 * 9; k++) { u8 v = (k % 3) ? 255 : 0; bw.px[k * 4] = bw.px[k * 4 + 1] = bw.px[k * 4 + 2] = v; bw.px[k * 4 + 3] = 255; }
    d = pnm_encode(&bw, PNM_PBM, &len);
    ok("pbm written", d && !memcmp(d, "P4", 2) && len == 8 + 3 * 9);
    ok("pbm read back", d && pnm_read(d, len, &back, NULL, 0) && same(&bw, &back, 0));
    rgba_free(&back);
    if (d) { save("bw.pbm", d, len); save_rgba("bw.pbm.rgba", &bw); fuzz(d, len, 100); }
    free(d); rgba_free(&bw);
  }

  /* QOI */
  RT("qoi rgb", qoi_encode(&pic, &len), &pic, qoi_read, 0, "pic.qoi");
  RT("qoi rgba", qoi_encode(&alpha, &len), &alpha, qoi_read, 0, "alpha.qoi");
  RT("qoi noise", qoi_encode(&noise, &len), &noise, qoi_read, 0, "noise.qoi");
  RT("qoi flat", qoi_encode(&flat, &len), &flat, qoi_read, 0, NULL);

  /* ICO: the biggest entry is what ico_read opens */
  {
    Rgba sq, back;
    make(&sq, 48, 48, 1);
    d = ico_encode(&sq, 0, &len);
    ok("ico written with four sizes", d && d[4] == 4 && d[6] == 16 && d[6 + 48] == 48);
    roundtrip("ico 48", d, len, &sq, ico_read, 0, "sq48.ico");
    d = ico_encode(&flat, 0, &len);
    ok("ico of a big picture has 256 (as PNG)", d && d[4] == 7 && d[6 + 6 * 16] == 0 && ico_read(d, len, &back, NULL, 0) && back.w == 256 && back.h == 256);
    if (d) { save("big.ico", d, len); rgba_free(&back); }
    free(d);
    d = ico_encode(&one, 0, &len);
    ok("ico of 1x1", d && ico_read(d, len, &back, NULL, 0) && back.w == 1 && same(&one, &back, 0));
    if (d) rgba_free(&back);
    free(d);
    rgba_free(&sq);
  }

  /* DDS */
  RT("dds rgba", dds_encode(&alpha, DDS_RGBA, 0, &len), &alpha, dds_read, 0, "alpha_rgba.dds");
  RT("dds rgba mipmaps", dds_encode(&alpha, DDS_RGBA, 1, &len), &alpha, dds_read, 0, "alpha_mip.dds");
  for (i = 0; i < 2; i++) {
    Rgba back, *src = i ? &alpha : &pic;
    int kind = i ? DDS_BC3 : DDS_BC1;
    d = dds_encode(src, kind, 1, &len);
    ok(i ? "bc3 written" : "bc1 written", d != NULL);
    if (d && dds_read(d, len, &back, NULL, 0)) {
      double e = mean_err(src, &back);
      ok(i ? "bc3 close" : "bc1 close", e < 6.0);
      if (e >= 6.0) printf("  mean error %.2f\n", e);
      rgba_free(&back);
    } else ok(i ? "bc3 read" : "bc1 read", 0);
    if (d) { save(i ? "alpha_bc3.dds" : "pic_bc1.dds", d, len); save_rgba(i ? "alpha_bc3.dds.rgba" : "pic_bc1.dds.rgba", src); fuzz(d, len, 100); }
    free(d);
  }
  {
    /* BC1 with transparency: the transparent pixels stay transparent */
    Rgba back;
    d = dds_encode(&alpha, DDS_BC1, 0, &len);
    ok("bc1 alpha", d && dds_read(d, len, &back, NULL, 0) && back.px[3] == 0 && back.px[(5 * 67 + 30) * 4 + 3] == 255);
    if (d) rgba_free(&back);
    free(d);
  }

  /* DPX */
  RT("dpx 8", dpx_encode(&pic, 0, &len), &pic, dpx_read, 0, "pic8.dpx");
  RT("dpx 10", dpx_encode(&pic, 1, &len), &pic, dpx_read, 1, "pic10.dpx");
  RT("dpx 8 rgba", dpx_encode(&alpha, 0, &len), &alpha, dpx_read, 0, "alpha8.dpx");
  RT("dpx 10 rgba", dpx_encode(&alpha, 1, &len), &alpha, dpx_read, 1, "alpha10.dpx");

  /* FITS (the reader scales from the darkest to the lightest value: both pictures span 0-255) */
  RT("fits grey", fits_encode(&grey, &len), &grey, fits_read, 1, "grey.fits");
  RT("fits rgb", fits_encode(&pic, &len), &pic, fits_read, 1, "pic.fits");

  /* PDF: the structure (every cross-reference offset at its object), the image and the page size */
  {
    DocInfo inf;
    int nobj = 0, bad = 0;
    docinfo_clear(&inf);
    strcpy(inf.title, "Easel \303\251t\303\251");
    strcpy(inf.author, "Test");
    for (i = 0; i < 3; i++) {
      Rgba *src = i == 1 ? &alpha : &pic;
      d = pdf_encode(src, 144, 0, i == 2 ? 90 : 0, &inf, &len);
      ok("pdf written", d && len > 100 && !memcmp(d, "%PDF-1.4", 8));
      if (!d) continue;
      {
        char *s = (char *)malloc(len + 1), *x;
        memcpy(s, d, len); s[len] = 0;
        for (x = s; (x = mfind(x, len - (size_t)(x - s), " 00000 n ")); x++) {
          long off = atol(x - 10);
          char want[16];
          nobj++;
          snprintf(want, sizeof want, "%d 0 obj", nobj);
          if (off < 0 || (size_t)off >= len || strncmp(s + off, want, strlen(want))) bad++;
        }
        ok("pdf offsets", nobj > 0 && !bad);
        ok("pdf page size at 144 dpi", mfind(s, len, "/MediaBox [0 0 33.50 22.50]") != NULL);
        ok("pdf soft mask", (i == 1) == (mfind(s, len, "/SMask") != NULL));
        ok("pdf jpeg", (i == 2) == (mfind(s, len, "/DCTDecode") != NULL));
        ok("pdf title in UTF-16", mfind(s, len, "/Title <FEFF0045006100730065006C002000E9007400E9>") != NULL);
        free(s);
      }
      save(i == 0 ? "pic.pdf" : i == 1 ? "alpha.pdf" : "jpeg.pdf", d, len);
      if (i == 0) save_rgba("pic.pdf.rgba", src);
      if (i == 1) save_rgba("alpha.pdf.rgba", src);
      free(d);
      nobj = 0; bad = 0;
    }
  }

  /* the quality an opened JPEG was saved at */
  {
    static const int qs[6] = { 10, 35, 50, 75, 90, 100 };
    int good = 1;
    for (i = 0; i < 6; i++) {
      u8 *j = jpeg_encode(&pic, qs[i], 72, &len);
      int e = j ? jpeg_estimate_quality(j, len) : -1;
      if (e < qs[i] - 1 || e > qs[i] + 1) { good = 0; printf("  quality %d estimated %d\n", qs[i], e); }
      free(j);
    }
    ok("jpeg quality estimated", good);
    ok("jpeg quality of a PNG", jpeg_estimate_quality((const u8 *)"\x89PNG", 4) == 0);
  }

  /* the table and the document writer */
  ok("ext webp", xwrite_format_of_ext(".WebP") == FMT_WEBP && xwrite_format_of_ext("pdf") == FMT_PDF_OUT && xwrite_format_of_ext(".tiff") == FMT_TIFF);
  ok("ext pbm", xwrite_format_of_ext(".pbm") == FMT_PNM && xwrite_sub_of_ext(".pbm") == PNM_PBM && xwrite_sub_of_ext(".pam") == PNM_PAM);
  ok("ext none", xwrite_format_of_ext(".xyz") == FMT_UNKNOWN && xwrite_format_of_ext(NULL) == FMT_UNKNOWN);
  ok("handles", xwrite_handles(FMT_QOI) && xwrite_handles(FMT_PDF_OUT) && !xwrite_handles(FMT_PNG));
  ok("ext back", !strcmp(xwrite_ext(FMT_QOI), ".qoi") && !strcmp(xwrite_ext(FMT_PDF_OUT), ".pdf"));
  {
    u8 *px = (u8 *)malloc((size_t)pic.w * pic.h * 4);
    Doc *doc;
    Rgba back;
    memcpy(px, pic.px, (size_t)pic.w * pic.h * 4);
    doc = doc_adopt(pic.w, pic.h, px);
    doc->dpi = 300;
    for (i = 0; i < 8; i++) {
      static const int fm[8] = { FMT_WEBP, FMT_PNM, FMT_QOI, FMT_ICO, FMT_DDS, FMT_DPX, FMT_FITS, FMT_PDF_OUT };
      char nm[64];
      d = xwrite_doc(doc, fm[i], fm[i] == FMT_DDS ? DDS_RGBA : 0, 0, &len);
      snprintf(nm, sizeof nm, "xwrite_doc %s", xwrite_name(fm[i]));
      ok(nm, d != NULL && (fm[i] == FMT_PDF_OUT || fm[i] == FMT_ICO || (codec_decode(d, len, &back, NULL, 0) && same(&back, &pic, fm[i] == FMT_FITS) && (rgba_free(&back), 1))));
      free(d);
    }
    /* a Bitmap document goes out as PBM */
    doc->mode = MODE_BITMAP;
    d = xwrite_doc(doc, FMT_PNM, 0, 0, &len);
    ok("bitmap as pbm", d && !memcmp(d, "P4", 2));
    free(d);
    doc_free(doc);
  }

  /* batch conversion (EaselConvert) to the new formats, each from the output name's extension */
  {
    char in[600], outp[600];
    static const char *const exts[] = { ".webp", ".pbm", ".pgm", ".pam", ".qoi", ".ico", ".dds", ".dpx", ".fits", ".pdf", ".tif", ".tga", ".psd" };
    BatchOpts o;
    size_t n;
    u8 *png = png_encode(&pic, 0, &n);
    snprintf(in, sizeof in, "%s/batch_in.png", outdir);
    if (png) { file_write(in, png, n); free(png); }
    ok("batch names", batch_format_named("WEBP") == FMT_WEBP && batch_format_named("pdf") == FMT_PDF_OUT && batch_format_named("Targa") == FMT_TGA && batch_format_named("PPM") == FMT_PNM);
    for (i = 0; i < (int)(sizeof exts / sizeof exts[0]); i++) {
      char err[160] = "", nm[240];
      u8 *f;
      size_t fl = 0;
      batch_opts_default(&o);
      snprintf(outp, sizeof outp, "%s/batch_out%s", outdir, exts[i]);
      snprintf(nm, sizeof nm, "batch to %s (%s)", exts[i], err);
      ok(nm, batch_convert(in, outp, &o, err, sizeof err));
      f = file_read(outp, &fl);
      snprintf(nm, sizeof nm, "batch %s file kind", exts[i]);
      ok(nm, f && fl > 8 && (i == 1 ? !memcmp(f, "P4", 2) : i == 2 ? !memcmp(f, "P5", 2) : i == 3 ? !memcmp(f, "P7", 2) : i == 9 ? !memcmp(f, "%PDF", 4) : 1));
      if (f && i != 9) {
        Rgba back;
        snprintf(nm, sizeof nm, "batch %s reads back", exts[i]);
        if (codec_decode(f, fl, &back, NULL, 0)) { ok(nm, back.w == (i == 5 ? 64 : pic.w)); rgba_free(&back); }
        else ok(nm, 0);
      }
      free(f);
    }
    ok("batch ext", !strcmp(batch_ext(FMT_WEBP), ".webp") && !strcmp(batch_ext(FMT_TIFF), ".tif") && !strcmp(batch_ext(FMT_PDF_OUT), ".pdf"));
  }

  /* SVG: shapes in, paths and shapes out */
  {
    static const char svg[] =
      "<?xml version=\"1.0\"?>\n<!-- a test drawing -->\n"
      "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"100\" viewBox=\"0 0 400 200\">\n"
      "<defs><linearGradient id=\"g\"><stop offset=\"0\"/></linearGradient><rect id=\"hidden\" width=\"9\" height=\"9\"/></defs>\n"
      "<rect id=\"box\" x=\"20\" y=\"20\" width=\"100\" height=\"60\" fill=\"#f00\"/>\n"
      "<g fill=\"blue\" transform=\"translate(200,0)\"><circle id=\"dot\" cx=\"50\" cy=\"50\" r=\"40\"/>"
      "<g transform=\"scale(2)\"><polygon id=\"tri\" points=\"10,60 30,60 20,90\" style=\"fill: rgb(0, 128, 0); stroke:#000000; stroke-width:2\"/></g></g>\n"
      "<path id=\"p\" d=\"M 300 150 L 380 150 L 380 190 Z\" fill=\"none\" stroke=\"black\" stroke-width=\"4\" opacity=\"0.5\"/>\n"
      "<line x1=\"0\" y1=\"0\" x2=\"400\" y2=\"200\" stroke=\"#123456\"/>\n"
      "<text x=\"5\" y=\"5\">skipped</text><ellipse cx=\"1\" cy=\"1\" rx=\"0\" ry=\"3\"/>\n"
      "</svg>\n";
    Doc *sd;
    char err[160] = "";
    const ShapeParams *sp;
    double x0, y0, x1, y1;
    ok("svg detected", svg_detect((const u8 *)svg, sizeof svg - 1) && !svg_detect((const u8 *)"<html>", 6));
    sd = svg_load_doc((const u8 *)svg, sizeof svg - 1, err, sizeof err);
    ok("svg opened", sd && sd->w == 200 && sd->h == 100);
    if (sd) {
      ok("svg: five shapes", sd->nlay == 5);
      if (sd->nlay == 5) {
        Path p;
        sp = layer_shape(sd->lay[0]); p = shape_path(sp);
        ok("svg rect", sp && !strcmp(sd->lay[0]->name, "box") && sp->fill_color == 0xFF0000 && path_bounds(&p, &x0, &y0, &x1, &y1) &&
           fabs(x0 - 10) < 0.01 && fabs(y1 - 40) < 0.01 && fabs(x1 - 60) < 0.01);
        sp = layer_shape(sd->lay[1]); p = shape_path(sp);
        ok("svg circle: group fill and transform", sp && sp->fill_color == 0x0000FF && path_bounds(&p, &x0, &y0, &x1, &y1) && fabs(x0 - 105) < 0.5 && fabs(x1 - 145) < 0.5);
        sp = layer_shape(sd->lay[2]); p = shape_path(sp);
        ok("svg polygon: style, nested scale", sp && sp->fill_color == 0x008000 && sp->stroke_on && fabs(sp->stroke_width - 2) < 0.01 &&
           path_bounds(&p, &x0, &y0, &x1, &y1) && fabs(x0 - 110) < 0.01 && fabs(y1 - 90) < 0.01);
        sp = layer_shape(sd->lay[3]);
        ok("svg path: stroke only, opacity", sp && !sp->fill_on && sp->stroke_on && fabs(sp->stroke_width - 2) < 0.01 && sp->stroke_alpha >= 126 && sp->stroke_alpha <= 128);
        sp = layer_shape(sd->lay[4]);
        ok("svg line: stroked", sp && !sp->fill_on && sp->stroke_color == 0x123456);
      }
      {
        /* out again as SVG, with a work path, and back */
        u8 *out;
        size_t ol;
        Doc *back;
        Path *wp = doc_path(sd, -1, 1);
        path_move_to(wp, 1, 2); path_line_to(wp, 50, 60);
        out = svg_write_paths(sd, 1, &ol);
        ok("svg written", out && svg_detect(out, ol));
        if (out) {
          save("shapes.svg", out, ol);
          back = svg_load_doc(out, ol, NULL, 0);
          ok("svg read back", back && back->w == 200 && back->nlay == 6);
          if (back && back->nlay == 6) {
            Path a = shape_path(layer_shape(sd->lay[2])), b = shape_path(layer_shape(back->lay[3]));
            double a0, a1, a2, a3;
            ok("svg read back: same polygon", path_bounds(&a, &x0, &y0, &x1, &y1) && path_bounds(&b, &a0, &a1, &a2, &a3) &&
               fabs(x0 - a0) < 0.01 && fabs(y1 - a3) < 0.01 && layer_shape(back->lay[3])->fill_color == 0x008000 && !strcmp(back->lay[0]->name, "Work Path"));
          }
          doc_free(back);
          {
            /* fuzzed */
            int r;
            u8 *m = (u8 *)malloc(ol);
            for (r = 0; r < 300 && m; r++) {
              size_t k, ml = ol;
              Doc *fd;
              memcpy(m, out, ol);
              for (k = 0; k < 1 + rnd() % 6; k++) { size_t at = rnd() % ol; if (rnd() & 1) m[at] = (u8)rnd(); else ml = at + 1; }
              if ((fd = svg_load_doc(m, ml, NULL, 0))) doc_free(fd);
              nfuzzed++;
            }
            free(m);
          }
          free(out);
        }
      }
      doc_free(sd);
    }
    { static const char e[] = "<svg width=\"10\" height=\"10\"></svg>"; ok("svg without shapes", !svg_load_doc((const u8 *)e, sizeof e - 1, err, sizeof err) && err[0]); }
  }

  /* a picture too big for the format */
  { Rgba big; big.w = 20000; big.h = 2; big.px = NULL; ok("webp refuses 20000 wide", !webp_encode_lossless(&big, &len)); }

  rgba_free(&pic); rgba_free(&alpha); rgba_free(&grey); rgba_free(&flat); rgba_free(&noise); rgba_free(&stripes); rgba_free(&one); rgba_free(&wide);
  printf("xwritetest: %ld mutated files decoded\n", nfuzzed);
  printf("%d tests, %d failed\n", ntests, nfailed);
  return nfailed != 0;
}
