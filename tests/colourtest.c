/*
 * colourtest.c - Easel 1.2's colour, file format and type work (round 2): pixel aspect presets and
 * the canvas's vertical scale; the CMS (built-in profiles written and parsed back, matrix and LUT
 * transforms, intents and black point compensation, the press model and its gamut, proof views);
 * ICC profiles kept in PNG, JPEG, TIFF, PSD and WebP; CMYK and Lab documents saved in their model;
 * palette PNG/BMP opening Indexed and 1-bit TIFF/PBM opening as Bitmap. Files for the Python
 * cross-checks (tests/check_colour.py) are written to the directory given. Run under ASan/UBSan.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core/doc.h"
#include "core/codec.h"
#include "core/aspect.h"
#include "core/cms.h"
#include "core/palette.h"
#include "core/xwrite.h"
#include "core/colorx.h"
#include "core/xread2.h"
#include "core/type.h"
#include "core/frames.h"
#include "core/fx.h"
#include "core/exportx.h"
#include "core/paint.h"
#include "core/resample.h"
#include "core/fonts.h"

static int fails, tests;
#define CHECK(c, msg) do { tests++; if (!(c)) { fails++; printf("FAIL %s (line %d)\n", msg, __LINE__); } } while (0)
static const char *outdir = NULL;

static void save(const char *name, const u8 *d, size_t n)
{
  char p[512];
  if (!outdir || !d) return;
  snprintf(p, sizeof p, "%s/%s", outdir, name);
  file_write(p, d, n);
}
/* a test picture: a colour ramp across, a grey ramp down, a transparent corner */
static Doc *ramp_doc(i32 w, i32 h, int alpha)
{
  Doc *d = doc_new(w, h, 0xFFFFFF, 0);
  i32 x, y;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      u8 *p = d->lay[0]->px + ((long)y * w + x) * 4;
      p[0] = (u8)(x * 255 / (w - 1)); p[1] = (u8)(y * 255 / (h - 1)); p[2] = (u8)((x + y) * 255 / (w + h - 2)); p[3] = 255;
      if (alpha && x < 4 && y < 4) p[3] = 0;
    }
  return d;
}

/* ------------------------------------------------------------------ aspect */
static void test_aspect(void)
{
  u8 ax, ay;
  int i;
  CHECK(aspect_count() >= 12, "aspect presets");
  CHECK(aspect_find(1, 1) == 0 && aspect_find(0, 0) == 0, "square found");
  CHECK(aspect_find(10, 22) == 3, "a multiple of 5:11 is PAL hires");
  CHECK(aspect_find(7, 13) == -1, "a custom ratio isn't a preset");
  CHECK(fabs(aspect_yscale(5, 11, 1) - 2.2) < 1e-9, "hires drawn 2.2 times as tall");
  CHECK(aspect_yscale(5, 11, 0) == 1.0, "correction off: square");
  CHECK(aspect_yscale(1, 255, 1) == 4.0 && aspect_yscale(255, 1, 1) == 0.25, "the scale is kept within 1/4..4");
  aspect_from_ratio(0.91, &ax, &ay);
  CHECK(fabs((double)ax / ay - 0.91) < 0.002, "0.91 as a ratio");
  aspect_from_ratio(1.0, &ax, &ay);
  CHECK(ax == ay, "1.0 as a ratio");
  for (i = 0; i < aspect_count(); i++) CHECK(aspect_get(i)->ax && aspect_get(i)->ay && aspect_get(i)->name[0], "each preset has a ratio and a name");
  CHECK(aspect_get(aspect_count()) == NULL, "out of range");
}

/* ------------------------------------------------------------------ the CMS */
static double maxdiff_rgb(const u8 *a, const u8 *b, long n)
{
  long k;
  double m = 0;
  for (k = 0; k < n; k++) { int c; for (c = 0; c < 3; c++) { double v = fabs((double)a[k * 4 + c] - b[k * 4 + c]); if (v > m) m = v; } }
  return m;
}
static double meandiff_rgb(const u8 *a, const u8 *b, long n)
{
  long k;
  double t = 0;
  for (k = 0; k < n; k++) { int c; for (c = 0; c < 3; c++) t += fabs((double)a[k * 4 + c] - b[k * 4 + c]); }
  return t / (n * 3);
}
static void test_builtin_profiles(void)
{
  u32 len;
  u8 *b = icc_builtin_bytes(CMS_SRGB, &len), *a;
  IccProfile *p, *q;
  char err[80], desc[64];
  double in[3] = { 1, 1, 1 }, xyz[3], out[3];
  CHECK(b && len > 132 && !memcmp(b + 36, "acsp", 4), "sRGB bytes made");
  p = b ? icc_parse(b, len, err, sizeof err) : NULL;
  CHECK(p != NULL, "sRGB bytes parsed back");
  CHECK(p && icc_is_matrix(p) && icc_space(p) == ICC_RGB, "a matrix RGB profile");
  CHECK(p && icc_same(p, icc_srgb()), "the same as the built-in sRGB");
  CHECK(p && !icc_same(p, icc_adobergb()), "not Adobe RGB");
  icc_desc_of(b, len, desc, sizeof desc);
  CHECK(!strcmp(desc, "sRGB IEC61966-2.1"), "its description");
  icc_to_xyz(p, CI_RELATIVE, in, xyz);
  CHECK(fabs(xyz[0] - 0.9642) < 2e-3 && fabs(xyz[1] - 1) < 2e-3 && fabs(xyz[2] - 0.8249) < 2e-3, "white is D50");
  in[0] = 0.5; in[1] = 0.2; in[2] = 0.9;
  icc_to_xyz(p, CI_RELATIVE, in, xyz); icc_from_xyz(p, CI_RELATIVE, xyz, out);
  CHECK(fabs(out[0] - 0.5) < 1e-3 && fabs(out[1] - 0.2) < 1e-3 && fabs(out[2] - 0.9) < 1e-3, "sRGB there and back");
  save("srgb.icc", b, len);
  a = icc_builtin_bytes(CMS_ADOBERGB, &len);
  q = a ? icc_parse(a, len, err, sizeof err) : NULL;
  CHECK(q && icc_same(q, icc_adobergb()), "Adobe RGB bytes parsed back");
  save("adobergb.icc", a, len);
  CHECK(icc_parse((const u8 *)"not a profile at all", 20, err, sizeof err) == NULL, "junk refused");
  icc_free(p); icc_free(q); free(a); free(b);
}
/* a LUT profile (lut16, RGB in, XYZ PCS) made to match sRGB: the same transform as the matrix one */
static u8 *make_lut_profile(u32 *len, int lab)
{
  int g = 17, ie = 256, oe = 2, r, gg, bb, k;
  u32 lutsz = 52 + ie * 3 * 2 + g * g * g * 3 * 2 + oe * 3 * 2, total = 128 + 4 + 2 * 12 + 32 + ((lutsz + 3) & ~3u);
  u8 *b = (u8 *)calloc(1, total), *t;
  u32 at = 128 + 4 + 24;
  #define P32(p, v) do { (p)[0] = (u8)((v) >> 24); (p)[1] = (u8)((v) >> 16); (p)[2] = (u8)((v) >> 8); (p)[3] = (u8)(v); } while (0)
  #define P16(p, v) do { (p)[0] = (u8)((v) >> 8); (p)[1] = (u8)(v); } while (0)
  P32(b, total); P32(b + 8, 0x02100000); memcpy(b + 12, "mntr", 4); memcpy(b + 16, "RGB ", 4); memcpy(b + 20, lab ? "Lab " : "XYZ ", 4);
  memcpy(b + 36, "acsp", 4);
  P32(b + 128, 2);
  memcpy(b + 132, "wtpt", 4); P32(b + 136, at); P32(b + 140, 20);
  t = b + at; memcpy(t, "XYZ ", 4); P32(t + 8, 63190); P32(t + 12, 65536); P32(t + 16, 54061);
  at += 32;
  memcpy(b + 144, "A2B0", 4); P32(b + 148, at); P32(b + 152, lutsz);
  t = b + at; memcpy(t, "mft2", 4); t[8] = 3; t[9] = 3; t[10] = (u8)g;
  P32(t + 12, 65536); P32(t + 28, 65536); P32(t + 44, 65536);
  P16(t + 48, ie); P16(t + 50, oe);
  t += 52;
  for (k = 0; k < 3; k++) { int i; for (i = 0; i < ie; i++) { double v = i / 255.0; v = v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); P16(t, (u32)floor(v * 65535 + 0.5)); t += 2; } }
  for (r = 0; r < g; r++) for (gg = 0; gg < g; gg++) for (bb = 0; bb < g; bb++) {
    double lin[3], xyz[3];
    static const double m[9] = { 0.4360747, 0.3850649, 0.1430804, 0.2225045, 0.7168786, 0.0606169, 0.0139322, 0.0971045, 0.7141733 };
    lin[0] = r / (g - 1.0); lin[1] = gg / (g - 1.0); lin[2] = bb / (g - 1.0);
    for (k = 0; k < 3; k++) xyz[k] = m[3 * k] * lin[0] + m[3 * k + 1] * lin[1] + m[3 * k + 2] * lin[2];
    if (lab) {
      double l[3];
      xyz_to_lab(xyz, l);
      P16(t, (u32)floor(l[0] / 100 * 65280 + 0.5)); P16(t + 2, (u32)floor((l[1] + 128) / 255 * 65280 + 0.5)); P16(t + 4, (u32)floor((l[2] + 128) / 255 * 65280 + 0.5));
    } else for (k = 0; k < 3; k++) P16(t + 2 * k, (u32)floor(xyz[k] * 32768 + 0.5));
    t += 6;
  }
  for (k = 0; k < 3; k++) { P16(t, 0); P16(t + 2, 65535); t += 4; }
  *len = total;
  return b;
}
static void test_transforms(void)
{
  Doc *d = ramp_doc(64, 48, 0);
  long n = 64L * 48;
  u8 *a = (u8 *)malloc((size_t)n * 4), *b = (u8 *)malloc((size_t)n * 4);
  CmsXform *x, *y;
  u32 len;
  u8 *lp;
  IccProfile *lut;
  char err[80];
  memcpy(a, d->lay[0]->px, (size_t)n * 4);
  /* sRGB to sRGB: nothing */
  x = cms_xform_new(NULL, icc_srgb(), CI_PERCEPTUAL, 1);
  CHECK(x && cms_xform_identity(x), "sRGB to sRGB is identity");
  cms_xform_free(x);
  /* sRGB to Adobe RGB and back: within a level or two; Adobe's numbers are less saturated */
  x = cms_xform_new(icc_srgb(), icc_adobergb(), CI_RELATIVE, 0);
  y = cms_xform_new(icc_adobergb(), icc_srgb(), CI_RELATIVE, 0);
  CHECK(x && y && !cms_xform_identity(x), "the matrix transforms made");
  memcpy(b, a, (size_t)n * 4);
  cms_xform_apply(x, b, n);
  { u8 red[4] = { 255, 0, 0, 255 }; cms_xform_apply(x, red, 1); CHECK(red[0] > 210 && red[0] < 225 && red[1] < 5 && red[2] < 5, "sRGB red in Adobe RGB (219, 0, 0)"); }
  { u8 green[4] = { 0, 255, 0, 255 }; cms_xform_apply(x, green, 1); CHECK(green[0] > 130 && green[0] < 150 && green[1] > 250, "sRGB green in Adobe RGB (144, 255, 60)"); }
  save("ramp.rgba", a, (size_t)n * 4);
  save("ramp_adobe.rgba", b, (size_t)n * 4);
  cms_xform_apply(y, b, n);
  /* (8-bit Adobe numbers rounded put a few levels into dark sRGB: the toe of sRGB's curve is steep) */
  CHECK(maxdiff_rgb(a, b, n) <= 10, "Adobe RGB and back");
  cms_xform_free(x); cms_xform_free(y);
  /* a LUT profile that is sRGB: a grid transform, the same colours */
  lp = make_lut_profile(&len, 0);
  lut = icc_parse(lp, len, err, sizeof err);
  CHECK(lut && !icc_is_matrix(lut), "a lut16 profile parsed");
  save("lutxyz.icc", lp, len);
  x = lut ? cms_xform_new(lut, icc_srgb(), CI_PERCEPTUAL, 0) : NULL;
  CHECK(x && !cms_xform_identity(x), "a LUT transform");
  memcpy(b, a, (size_t)n * 4);
  if (x) cms_xform_apply(x, b, n);
  CHECK(maxdiff_rgb(a, b, n) <= 3, "the sRGB LUT profile gives sRGB's colours");
  cms_xform_free(x); icc_free(lut); free(lp);
  lp = make_lut_profile(&len, 1);
  lut = icc_parse(lp, len, err, sizeof err);
  x = lut ? cms_xform_new(lut, icc_adobergb(), CI_RELATIVE, 1) : NULL;
  y = cms_xform_new(icc_srgb(), icc_adobergb(), CI_RELATIVE, 1);
  CHECK(x && y, "a Lab-PCS LUT transform");
  memcpy(b, a, (size_t)n * 4);
  if (x) cms_xform_apply(x, b, n);
  save("ramp_lutlab_adobe.rgba", b, (size_t)n * 4);
  { u8 *c = (u8 *)malloc((size_t)n * 4); memcpy(c, a, (size_t)n * 4); cms_xform_apply(y, c, n);
    CHECK(meandiff_rgb(c, b, n) <= 1.0 && maxdiff_rgb(c, b, n) <= 24, "Lab PCS (legacy lut16 encoding) agrees with the matrices"); free(c); }
  save("lutlab.icc", lp, len);
  cms_xform_free(x); cms_xform_free(y); icc_free(lut); free(lp);
  free(a); free(b);
  doc_free(d);
}
static void test_press(void)
{
  double xyz[3], ck[4], de, lab[3], x2[3];
  double white[3] = { 1, 1, 1 }, in[3];
  int k;
  icc_to_xyz(NULL, CI_RELATIVE, white, xyz);
  de = press_from_xyz(xyz, ck);
  CHECK(de < 1 && ck[0] < 0.01 && ck[1] < 0.01 && ck[2] < 0.01 && ck[3] < 0.01, "white is no ink");
  in[0] = in[1] = in[2] = 0;
  icc_to_xyz(NULL, CI_RELATIVE, in, xyz);
  de = press_from_xyz(xyz, ck);
  CHECK(ck[3] > 0.95 && ck[0] + ck[1] + ck[2] + ck[3] <= 3.0001, "black is mostly K, within 300%");
  in[0] = in[1] = in[2] = 0.5;
  icc_to_xyz(NULL, CI_RELATIVE, in, xyz);
  de = press_from_xyz(xyz, ck);
  CHECK(de < 2, "mid grey prints");
  press_to_xyz(ck, x2); xyz_to_lab(x2, lab);
  CHECK(fabs(lab[1]) < 2 && fabs(lab[2]) < 2, "and prints neutral");
  /* a skin tone and a muted blue print; pure sRGB green and blue don't */
  in[0] = 0.85; in[1] = 0.65; in[2] = 0.55; icc_to_xyz(NULL, CI_RELATIVE, in, xyz);
  CHECK(press_from_xyz(xyz, ck) < 3, "a skin tone is in gamut");
  in[0] = 0.3; in[1] = 0.4; in[2] = 0.6; icc_to_xyz(NULL, CI_RELATIVE, in, xyz);
  CHECK(press_from_xyz(xyz, ck) < 3, "a muted blue is in gamut");
  in[0] = 0; in[1] = 1; in[2] = 0; icc_to_xyz(NULL, CI_RELATIVE, in, xyz);
  CHECK(press_from_xyz(xyz, ck) > 10, "sRGB green is out of gamut");
  in[0] = 0; in[1] = 0; in[2] = 1; icc_to_xyz(NULL, CI_RELATIVE, in, xyz);
  CHECK(press_from_xyz(xyz, ck) > 10, "sRGB blue is out of gamut");
  for (k = 0; k < 4; k++) CHECK(ck[k] >= 0 && ck[k] <= 1, "inks within 0..1");
}
static void test_views(void)
{
  ViewSpec v;
  CmsXform *x;
  u8 px[4 * 4] = { 0, 255, 0, 255,  200, 160, 140, 255,  255, 0, 0, 255,  128, 128, 128, 255 };
  u8 t[16];
  memset(&v, 0, sizeof v);
  v.intent = CI_RELATIVE; v.bpc = 1;
  CHECK(cms_view_new(NULL, &v) == NULL, "no proof, sRGB, sRGB display: nothing to do");
  v.on = 1; v.proof = PRF_MONITOR;
  x = cms_view_new(icc_adobergb(), &v);
  memcpy(t, px, 16); if (x) cms_xform_apply(x, t, 4);
  CHECK(x && !memcmp(t, px, 16), "Monitor RGB shows the numbers as they are");
  cms_xform_free(x);
  v.proof = PRF_NONE; v.on = 0;
  x = cms_view_new(icc_adobergb(), &v);
  memcpy(t, px, 16); if (x) cms_xform_apply(x, t, 4);
  CHECK(x && t[0] < 60 && t[1] == 255, "an Adobe RGB picture is shown converted (its green beyond sRGB's)");
  cms_xform_free(x);
  v.on = 1; v.proof = PRF_CMYK;
  x = cms_view_new(NULL, &v);
  memcpy(t, px, 16); if (x) cms_xform_apply(x, t, 4);
  CHECK(x && t[1] < 200 && abs(t[4] - 200) < 12 && abs(t[12] - 128) < 8, "CMYK proof: green dulled, skin and grey kept");
  cms_xform_free(x);
  v.proof = PRF_BLACK;
  x = cms_view_new(NULL, &v);
  memcpy(t, px, 16); if (x) cms_xform_apply(x, t, 4);
  CHECK(x && t[0] == t[1] && t[1] == t[2] && t[4] > 230, "the black plate: grey, little black in a skin tone");
  cms_xform_free(x);
  v.proof = PRF_PROTAN;
  x = cms_view_new(NULL, &v);
  memcpy(t, px, 16); if (x) cms_xform_apply(x, t, 4);
  CHECK(x && abs(t[8] - t[9]) < 120 && abs(t[12] - 128) < 4, "protanopia: red and green close, grey kept");
  cms_xform_free(x);
  v.proof = PRF_MAC;
  x = cms_view_new(NULL, &v);
  memcpy(t, px, 16); if (x) cms_xform_apply(x, t, 4);
  CHECK(x && t[12] > 140, "legacy Macintosh RGB: lighter");
  cms_xform_free(x);
  v.on = 0; v.gamut = 1;
  x = cms_view_new(NULL, &v);
  memcpy(t, px, 16); if (x) cms_xform_apply(x, t, 4);
  CHECK(x && t[0] == 0x80 && t[1] == 0x80 && t[2] == 0x80, "gamut warning: sRGB green painted grey");
  CHECK(x && t[4] == 200 && t[5] == 160 && t[12] == 128, "gamut warning: printable colours shown as they are");
  cms_xform_free(x);
  v.gamut = 0; v.gamma10 = 18;
  x = cms_view_new(NULL, &v);
  memcpy(t, px, 16); if (x) cms_xform_apply(x, t, 4);
  CHECK(x && t[12] < 128, "a 1.8 display gets darker numbers");
  cms_xform_free(x);
}

/* ------------------------------------------------------------------ profiles in files */
static void test_files(void)
{
  Doc *d = ramp_doc(40, 30, 1), *e;
  u32 len, l2;
  u8 *icc = icc_builtin_bytes(CMS_ADOBERGB, &len), *got, *f;
  size_t fl;
  char err[120];
  int fmts[5] = { FMT_PNG, FMT_JPEG, FMT_TIFF, FMT_PSD, FMT_WEBP }, k;
  static const char *names[5] = { "icc.png", "icc.jpg", "icc.tif", "icc.psd", "icc.webp" };
  d->icc = icc; d->icclen = len;
  for (k = 0; k < 5; k++) {
    f = colorx_write_doc(d, fmts[k], &fl);
    CHECK(f != NULL, names[k]);
    if (!f) continue;
    got = icc_from_file(f, fl, &l2);
    CHECK(got && l2 == len && !memcmp(got, icc, len), "the profile read back");
    free(got);
    e = codec_load_doc(f, fl, err, sizeof err);
    CHECK(e && e->icc && e->icclen == len, "the opened document has it");
    CHECK(e && e->w == 40 && e->h == 30, "and its size");
    save(names[k], f, fl);
    doc_free(e);
    free(f);
  }
  doc_free(d);
}
static void test_models(void)
{
  Doc *d = ramp_doc(32, 24, 0), *e;
  u8 *f;
  size_t fl;
  char err[120];
  long n = 32L * 24;
  u8 *before = (u8 *)malloc((size_t)n * 4);
  /* CMYK: colours brought into gamut, saved as a CMYK TIFF, opened again as a CMYK document */
  CHECK(colorx_convert_model(d, CM_CMYK, NULL), "to CMYK");
  CHECK(d->cmode == CM_CMYK && d->mode == MODE_RGB, "a CMYK document");
  memcpy(before, d->lay[0]->px, (size_t)n * 4);
  f = colorx_write_doc(d, FMT_TIFF, &fl);
  CHECK(f && cms_file_model(f, fl) == CM_CMYK, "saved as a CMYK TIFF");
  save("cmyk.tif", f, fl);
  e = f ? codec_load_doc(f, fl, err, sizeof err) : NULL;
  CHECK(e && e->cmode == CM_CMYK, "opened as CMYK");
  CHECK(e && meandiff_rgb(before, e->lay[0]->px, n) <= 1.5 && maxdiff_rgb(before, e->lay[0]->px, n) <= 20, "the colours survive the separation");
  doc_free(e); free(f);
  /* undo of the conversion */
  doc_undo(d);
  CHECK(d->cmode == CM_RGB, "undone: RGB again");
  /* Lab */
  CHECK(colorx_convert_model(d, CM_LAB, NULL), "to Lab");
  f = colorx_write_doc(d, FMT_TIFF, &fl);
  CHECK(f && cms_file_model(f, fl) == CM_LAB, "saved as a CIELab TIFF");
  save("lab.tif", f, fl);
  e = f ? codec_load_doc(f, fl, err, sizeof err) : NULL;
  CHECK(e && e->cmode == CM_LAB, "opened as Lab");
  /* (8-bit Lab's whole a* and b* steps show as a few levels in sRGB's darks) */
  CHECK(e && meandiff_rgb(d->lay[0]->px, e->lay[0]->px, n) <= 1.0 && maxdiff_rgb(d->lay[0]->px, e->lay[0]->px, n) <= 20, "Lab keeps the colours");
  doc_free(e); free(f);
  /* CMYK JPEG */
  colorx_convert_model(d, CM_CMYK, NULL);
  memcpy(before, d->lay[0]->px, (size_t)n * 4);
  f = colorx_write_doc(d, FMT_JPEG, &fl);
  CHECK(f && cms_file_model(f, fl) == CM_CMYK, "saved as a CMYK JPEG");
  save("cmyk.jpg", f, fl);
  e = f ? codec_load_doc(f, fl, err, sizeof err) : NULL;
  CHECK(e && e->cmode == CM_CMYK, "the JPEG opened as CMYK");
  CHECK(e && meandiff_rgb(before, e->lay[0]->px, n) <= 3, "its colours near");
  doc_free(e); free(f);
  free(before);
  doc_free(d);
}
static void test_assign_convert(void)
{
  Doc *d = ramp_doc(20, 10, 0);
  u8 g[4];
  memcpy(g, d->lay[0]->px + (5L * 20 + 10) * 4, 4);
  CHECK(colorx_assign(d, CMS_ADOBERGB, NULL, 0), "assign Adobe RGB");
  CHECK(d->icc && !memcmp(d->lay[0]->px + (5L * 20 + 10) * 4, g, 4), "assigning keeps the numbers");
  doc_undo(d);
  CHECK(d->icc == NULL, "undone: untagged");
  doc_redo(d);
  CHECK(d->icc != NULL, "redone");
  CHECK(colorx_convert(d, CMS_SRGB, NULL, 0, CI_RELATIVE, 1), "convert to sRGB");
  CHECK(d->icc && icc_space(NULL) == ICC_RGB, "tagged sRGB");
  { const u8 *p = d->lay[0]->px + (5L * 20 + 10) * 4;
    int s0 = g[0] > g[1] ? g[0] - g[1] : g[1] - g[0], s1 = p[0] > p[1] ? p[0] - p[1] : p[1] - p[0];
    CHECK(s1 > s0, "Adobe RGB numbers become more saturated sRGB ones"); }
  doc_undo(d);
  CHECK(!memcmp(d->lay[0]->px + (5L * 20 + 10) * 4, g, 4), "undone: the numbers back");
  doc_free(d);
}
/* ------------------------------------------------------------------ indexed and bitmap opening */
static void test_open_modes(void)
{
  Doc *d = ramp_doc(16, 16, 0), *e;
  u8 *f;
  size_t fl;
  char err[120];
  i32 x, y;
  doc_to_indexed(d, 7, QM_MEDIAN, 0, NULL, 0);
  f = png_write_doc(d, &fl);
  e = f ? codec_load_doc(f, fl, err, sizeof err) : NULL;
  CHECK(e && e->mode == MODE_INDEXED && e->npal == 7 && !memcmp(e->pal, d->pal, 21), "a palette PNG opens Indexed with its palette");
  CHECK(e && e->lay[0]->idx && !memcmp(e->lay[0]->px, d->lay[0]->px, 16 * 16 * 4), "and the same pixels");
  doc_free(e); free(f);
  {
    Rgba im;
    im.w = 16; im.h = 16; im.px = d->lay[0]->px;
    f = bmp_encode(&im, &fl);
  }
  e = f ? codec_load_doc(f, fl, err, sizeof err) : NULL;
  CHECK(e && e->mode == MODE_RGB, "a 24-bit BMP opens RGB");
  doc_free(e); free(f);
  f = colorx_bmp_indexed(d, &fl);
  e = f ? codec_load_doc(f, fl, err, sizeof err) : NULL;
  CHECK(e && e->mode == MODE_INDEXED && e->npal == 7 && !memcmp(e->lay[0]->px, d->lay[0]->px, 16 * 16 * 4), "a palette BMP opens Indexed");
  save("pal.bmp", f, fl);
  doc_free(e); free(f);
  doc_free(d);
  /* 1-bit TIFF and PBM: Bitmap */
  d = doc_new(24, 8, 0xFFFFFF, 0);
  for (y = 0; y < 8; y++) for (x = 0; x < 24; x++) if ((x + y) % 3 == 0) { u8 *p = d->lay[0]->px + (y * 24 + x) * 4; p[0] = p[1] = p[2] = 0; }
  doc_to_bitmap(d, BMM_THRESHOLD);
  f = tiff_encode_doc(d, TIFFC_NONE, &fl);
  e = f ? codec_load_doc(f, fl, err, sizeof err) : NULL;
  CHECK(e && e->mode == MODE_BITMAP && !memcmp(e->lay[0]->px, d->lay[0]->px, 24 * 8 * 4), "a 1-bit TIFF opens in Bitmap mode");
  doc_free(e); free(f);
  f = xwrite_doc(d, FMT_PNM, PNM_PBM, 0, &fl);
  e = f ? codec_load_doc(f, fl, err, sizeof err) : NULL;
  CHECK(e && e->mode == MODE_BITMAP, "a PBM opens in Bitmap mode");
  doc_free(e); free(f);
  doc_free(d);
}

/* ------------------------------------------------------------------ Cineon, DICOM, OpenEXR, HDR */
static double exr_value(int c, int x, int y)
{
  switch (c) {
  case 0: return x / 16.0;
  case 1: return y / 8.0;
  case 2: return ((x + y) % 7) / 4.0;
  case 3: return 1.0 - (x % 4) / 8.0;
  }
  return (x + 2 * y) / 32.0;
}
static void test_exr(void)
{
  static const char *files[6] = { "none_half_rgba", "rle_half_y", "zips_half_rgb", "zip_float_rgb", "piz_half_rgba", "piz_float_y" };
  static const int grey[6] = { 0, 1, 0, 0, 0, 1 }, alpha[6] = { 1, 0, 0, 0, 1, 0 };
  int k;
  for (k = 0; k < 6; k++) {
    char path[128], err[120];
    size_t n;
    u8 *d;
    float *f;
    i32 w = 0, h = 0, x, y;
    int bad = 0;
    snprintf(path, sizeof path, "tests/data/exr/%s.exr", files[k]);
    d = file_read(path, &n);
    CHECK(d && xread2_detect(d, n) == XR_EXR, files[k]);
    f = d ? exr_read_float(d, n, &w, &h, err, sizeof err) : NULL;
    if (!f) printf("%s: %s\n", files[k], err);
    CHECK(f && w == 37 && h == 41, "OpenEXR read: its size");
    if (f)
      for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        const float *p = f + ((long)y * w + x) * 4;
        int c;
        for (c = 0; c < 3; c++) if (fabs(p[c] - exr_value(grey[k] ? 4 : c, x, y)) > 1e-6) bad++;
        if (fabs(p[3] - (alpha[k] ? exr_value(3, x, y) : 1.0)) > 1e-6) bad++;
      }
    CHECK(f && !bad, "OpenEXR: every sample as written");
    if (bad) printf("%s: %d samples differ\n", files[k], bad);
    free(f);
    if (d) {
      Doc *doc = codec_load_doc(d, n, err, sizeof err);
      CHECK(doc && doc->w == 37 && doc->lay[0]->px[3] == 255, "an OpenEXR file opens (tone-mapped)");
      doc_free(doc);
      /* damaged copies fail cleanly: every byte of the header in turn, and the file cut short */
      {
        size_t i;
        u8 *c = (u8 *)malloc(n);
        for (i = 0; i < n; i += (i < 400 ? 1 : 97)) {
          Rgba im;
          memcpy(c, d, n); c[i] ^= 0x5A;
          if (xread2_decode(c, n, &im, err, sizeof err)) rgba_free(&im);
          if (xread2_decode(d, i, &im, err, sizeof err)) rgba_free(&im);
        }
        free(c);
      }
    }
    free(d);
  }
}
static void test_cineon_hdr_dicom(void)
{
  Doc *d = ramp_doc(50, 20, 0), *e;
  Rgba im, back;
  u8 *f;
  size_t n;
  char err[120];
  im.w = 50; im.h = 20; im.px = d->lay[0]->px;
  f = cineon_encode(&im, &n);
  CHECK(f && xread2_detect(f, n) == XR_CINEON, "a Cineon file written");
  CHECK(f && xread2_decode(f, n, &back, err, sizeof err) && back.w == 50 && back.h == 20, "and read back");
  if (f && back.px) {
    CHECK(meandiff_rgb(im.px, back.px, 1000) <= 1.0 && maxdiff_rgb(im.px, back.px, 1000) <= 6, "Cineon's 10-bit log keeps the colours");
    save("cineon.cin", f, n);
    { size_t pl; u8 *png = png_encode(&back, 0, &pl); save("cineon_easel.png", png, pl); free(png); }
    rgba_free(&back);
  }
  free(f);
  save("ramp50.png", NULL, 0);
  { size_t pl; u8 *png = png_encode(&im, 0, &pl); save("ramp50.png", png, pl); free(png); }
  f = hdr_encode(&im, &n);
  CHECK(f != NULL, "a Radiance HDR file written");
  e = f ? codec_load_doc(f, n, err, sizeof err) : NULL;
  CHECK(e && e->w == 50 && e->h == 20, "Easel's HDR reader opens it");
  save("ramp50.hdr", f, n);
  doc_free(e); free(f);
  doc_free(d);
  /* DICOM: 16-bit monochrome with a rescale and a window, written here element by element */
  {
    Buf b;
    i32 x, y;
    u8 *px;
    char ts[] = "1.2.840.10008.1.2.1";
    #define EL(g, e, vr, data, len) do { u8 h[8]; h[0] = (u8)(g); h[1] = (u8)((g) >> 8); h[2] = (u8)(e); h[3] = (u8)((e) >> 8); h[4] = (vr)[0]; h[5] = (vr)[1]; \
      buf_add(&b, h, 6); if (!strcmp(vr, "OW") || !strcmp(vr, "OB")) { u8 z[6] = { 0, 0, (u8)(len), (u8)((len) >> 8), (u8)((len) >> 16), (u8)((len) >> 24) }; buf_add(&b, z, 6); } \
      else { u8 z[2] = { (u8)(len), (u8)((len) >> 8) }; buf_add(&b, z, 2); } buf_add(&b, data, len); } while (0)
    u8 v16[2];
    buf_init(&b);
    for (x = 0; x < 128; x++) buf_addc(&b, 0);
    buf_add(&b, "DICM", 4);
    EL(0x0002, 0x0010, "UI", ts, 20);                  /* (the UID with its NUL pad) */
    EL(0x0008, 0x0060, "CS", "OT", 2);
    { const u8 sq[12] = { 0x08, 0x00, 0x15, 0x11, 'S', 'Q', 0, 0, 0xFF, 0xFF, 0xFF, 0xFF }; const u8 end[8] = { 0xFE, 0xFF, 0xDD, 0xE0, 0, 0, 0, 0 };
      const u8 item[8] = { 0xFE, 0xFF, 0x00, 0xE0, 0xFF, 0xFF, 0xFF, 0xFF }; const u8 iend[8] = { 0xFE, 0xFF, 0x0D, 0xE0, 0, 0, 0, 0 };
      buf_add(&b, sq, 12); buf_add(&b, item, 8); EL(0x0008, 0x1150, "UI", "1.2.3.4", 8); buf_add(&b, iend, 8); buf_add(&b, end, 8); }   /* (a sequence to skip) */
    v16[0] = 1; v16[1] = 0; EL(0x0028, 0x0002, "US", v16, 2);
    EL(0x0028, 0x0004, "CS", "MONOCHROME2 ", 12);
    v16[0] = 24; v16[1] = 0; EL(0x0028, 0x0010, "US", v16, 2);
    v16[0] = 40; v16[1] = 0; EL(0x0028, 0x0011, "US", v16, 2);
    v16[0] = 16; v16[1] = 0; EL(0x0028, 0x0100, "US", v16, 2); EL(0x0028, 0x0101, "US", v16, 2);
    v16[0] = 0; EL(0x0028, 0x0103, "US", v16, 2);
    EL(0x0028, 0x1050, "DS", "1000", 4);
    EL(0x0028, 0x1051, "DS", "2000", 4);
    EL(0x0028, 0x1052, "DS", "-1024 ", 6);
    EL(0x0028, 0x1053, "DS", "1 ", 2);
    px = (u8 *)malloc(40 * 24 * 2);
    for (y = 0; y < 24; y++) for (x = 0; x < 40; x++) { u32 v = (u32)(x * 100 + y * 10); px[(y * 40 + x) * 2] = (u8)v; px[(y * 40 + x) * 2 + 1] = (u8)(v >> 8); }
    EL(0x7FE0, 0x0010, "OW", px, 40 * 24 * 2);
    free(px);
    CHECK(!b.oom && xread2_detect((u8 *)b.data, b.len) == XR_DICOM, "a DICOM file made");
    CHECK(xread2_decode((u8 *)b.data, b.len, &back, err, sizeof err) && back.w == 40 && back.h == 24, "DICOM read");
    if (back.px) {
      /* the value at x, y is x*100 + y*10 - 1024; the window 0..2000 (centre 1000, width 2000) */
      int bad = 0;
      for (y = 0; y < 24; y++) for (x = 0; x < 40; x++) {
        double t = (x * 100 + y * 10 - 1024 - (1000 - 0.5 - 1999 / 2.0)) / 1999.0;
        int want = (int)((t < 0 ? 0 : t > 1 ? 1 : t) * 255 + 0.5), got = back.px[(y * 40 + x) * 4];
        if (abs(want - got) > 1) bad++;
      }
      CHECK(!bad, "DICOM: the rescale and the window");
      { size_t pl; u8 *png = png_encode(&back, 0, &pl); save("dicom_easel.png", png, pl); free(png); }
      rgba_free(&back);
    }
    save("test.dcm", (u8 *)b.data, b.len);
    {
      size_t i;
      u8 *c = (u8 *)malloc(b.len);
      for (i = 0; i < b.len; i++) {
        Rgba t;
        memcpy(c, b.data, b.len); c[i] ^= 0xA5;
        if (xread2_decode(c, b.len, &t, err, sizeof err)) rgba_free(&t);
        if (xread2_decode((u8 *)b.data, i, &t, err, sizeof err)) rgba_free(&t);
      }
      free(c);
    }
    buf_free(&b);
  }
}

/* ------------------------------------------------------------------ type: case, position, OpenType */
static FILE *gsub_out;
static TextLayout *lay(const char *family, const char *text, u16 ot, double size)
{
  static TextParams tp;
  text_params_init(&tp, family, size, 0, 10, 100);
  text_set_string(&tp, text);
  tp.run[0].ot = ot;
  return text_layout(&tp);
}
/* the glyphs drawn for text with ot, written for check_colour.py: font path, tag, text, glyph ids */
static void note(const char *path, const char *tag, const char *text, const TextLayout *tl)
{
  i32 i;
  if (!gsub_out) return;
  fprintf(gsub_out, "%s %s %s", path, tag, text);
  for (i = 0; i < tl->ng; i++) fprintf(gsub_out, " %d", tl->g[i].dead ? -1 : (int)tl->g[i].glyph);
  fprintf(gsub_out, "\n");
}
static void test_type(void)
{
  static const char *const carlito = "/usr/share/fonts/truetype/crosextra/Carlito-Regular.ttf";
  static const char *const freeserif = "/usr/share/fonts/truetype/freefont/FreeSerif.ttf";
  static const char *const liberation = "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf";
  FontSet *fs = (FontSet *)calloc(1, sizeof *fs);
  Face *c, *f, *l;
  TextLayout *tl, *t2;
  FILE *fp;
  char path[512];
  CHECK(text_upper('a') == 'A' && text_upper(0xE9) == 0xC9 && text_upper(0xF7) == 0xF7 && text_upper(0x101) == 0x100 && text_upper(0x17E) == 0x17D && text_upper('1') == '1', "capitals");
  if (!(fp = fopen(carlito, "rb"))) { printf("Carlito isn't installed: the OpenType tests are skipped\n"); free(fs); return; }
  fclose(fp);
  fonts_add(fs, carlito, "Carlito", 0, 0);
  if ((fp = fopen(freeserif, "rb")) != NULL) { fclose(fp); fonts_add(fs, freeserif, "FreeSerif", 0, 0); }
  if ((fp = fopen(liberation, "rb")) != NULL) { fclose(fp); fonts_add(fs, liberation, "Liberation", 0, 0); }
  text_fonts = fs;
  c = fonts_face(fs, "Carlito", 0, 0); f = fonts_face(fs, "FreeSerif", 0, 0); l = fonts_face(fs, "Liberation", 0, 0);
  CHECK(c && ttf_load(c), "Carlito loads");
  if (outdir) { snprintf(path, sizeof path, "%s/gsub.txt", outdir); gsub_out = fopen(path, "w"); }
  /* standard ligatures: fi becomes one glyph, its second part dead */
  tl = lay("Carlito", "fi", TO_LIGA, 40);
  CHECK(tl && tl->ng == 2 && !tl->g[0].dead && tl->g[1].dead && tl->g[0].glyph != ttf_glyph(c, 'f') && tl->g[1].adv == 0, "fi ligature");
  if (tl) note(carlito, "liga", "fi", tl);
  t2 = lay("Carlito", "fi", 0, 40);
  CHECK(t2 && !t2->g[1].dead && t2->g[0].glyph == ttf_glyph(c, 'f'), "no ligature when it is off");
  CHECK(tl && t2 && tl->bx1 < t2->bx1, "the ligature is narrower");
  text_layout_free(tl); text_layout_free(t2);
  tl = lay("Carlito", "ffl", TO_LIGA, 40);
  if (tl) note(carlito, "liga", "ffl", tl);
  text_layout_free(tl);
  tl = lay("Carlito", "0123", TO_ONUM, 40);
  CHECK(tl && tl->g[1].glyph != ttf_glyph(c, '1'), "oldstyle figures");
  if (tl) note(carlito, "onum", "0123", tl);
  text_layout_free(tl);
  tl = lay("Carlito", "Th", TO_DLIG, 40);
  if (tl) note(carlito, "dlig", "Th", tl);
  text_layout_free(tl);
  tl = lay("Carlito", "ag", TO_SALT, 40);
  if (tl) note(carlito, "salt", "ag", tl);
  text_layout_free(tl);
  tl = lay("Carlito", "1a", TO_ORDN, 40);
  if (tl) note(carlito, "ordn", "1a", tl);
  text_layout_free(tl);
  /* all caps */
  tl = lay("Carlito", "abc", TO_ALLCAPS, 40);
  CHECK(tl && tl->g[0].glyph == ttf_glyph(c, 'A') && tl->g[2].glyph == ttf_glyph(c, 'C') && tl->g[0].scale == 1, "All Caps");
  text_layout_free(tl);
  /* small caps: synthesized in a font without smcp, the font's own where it has them */
  if (l) {
    tl = lay("Liberation", "Ab", TO_SMALLCAPS, 40);
    CHECK(tl && tl->g[0].scale == 1 && tl->g[1].scale == 0.75 && tl->g[1].glyph == ttf_glyph(l, 'B'), "Small Caps synthesized (75% capitals)");
    text_layout_free(tl);
  }
  if (f && ttf_load(f)) {
    tl = lay("FreeSerif", "Ab", TO_SMALLCAPS, 40);
    CHECK(tl && tl->g[1].scale == 1 && tl->g[1].glyph != ttf_glyph(f, 'b') && tl->g[1].glyph != ttf_glyph(f, 'B'), "Small Caps from the font's smcp");
    if (tl) note(freeserif, "smcp", "Ab", tl);
    text_layout_free(tl);
  }
  /* superscript and subscript */
  tl = lay("Carlito", "x2", 0, 60);
  t2 = lay("Carlito", "x2", TO_SUPER, 60);
  CHECK(tl && t2 && fabs(t2->g[0].scale - 0.583) < 1e-9 && fabs((tl->g[0].y - t2->g[0].y) - 60 * 0.333) < 1e-6, "superscript: smaller and raised");
  CHECK(tl && t2 && t2->g[1].x - t2->g[0].x < tl->g[1].x - tl->g[0].x, "and narrower");
  text_layout_free(t2);
  t2 = lay("Carlito", "x2", TO_SUB, 60);
  CHECK(tl && t2 && fabs((t2->g[0].y - tl->g[0].y) - 60 * 0.333) < 1e-6, "subscript: lowered");
  text_layout_free(tl); text_layout_free(t2);
  /* drawing at the smaller size: less ink */
  {
    TextParams tp;
    Box b1, b2;
    u8 *p1, *p2;
    text_params_init(&tp, "Carlito", 60, 0, 10, 100); text_set_string(&tp, "H");
    p1 = p2 = NULL;
    text_render(&tp, 400, 300, &p1, &b1);
    tp.run[0].ot = TO_SUPER;
    text_render(&tp, 400, 300, &p2, &b2);
    CHECK(p1 && p2 && (b2.y1 - b2.y0) < (b1.y1 - b1.y0) && b2.y1 < b1.y1, "superscript drawn smaller and higher");
    free(p1); free(p2);
  }
  /* paragraphs: last line centred or right, left and right indents */
  {
    TextParams tp;
    TextLayout *a;
    text_params_init(&tp, "Carlito", 20, 0, 0, 0);
    text_set_string(&tp, "one two three four five six seven eight nine ten eleven");
    tp.box = 1; tp.bw = 200; tp.bh = 300;
    tp.align = TA_JUSTIFY_RIGHT;
    a = text_layout(&tp);
    CHECK(a && a->nl >= 2 && fabs(a->ln[a->nl - 1].x1 - 200) < 1.5 && a->ln[a->nl - 1].x0 > 1, "Justify Last Right");
    CHECK(a && fabs(a->ln[0].x0) < 1e-6 && fabs(a->ln[0].x1 - 200) < 1.5, "the other lines justified");
    text_layout_free(a);
    tp.align = TA_JUSTIFY_CENTER;
    a = text_layout(&tp);
    CHECK(a && fabs((a->ln[a->nl - 1].x0 + a->ln[a->nl - 1].x1) / 2 - 100) < 1.5, "Justify Last Centered");
    text_layout_free(a);
    tp.align = TA_LEFT; tp.indent_left = 30; tp.indent_right = 50;
    a = text_layout(&tp);
    CHECK(a && fabs(a->ln[0].x0 - 30) < 1e-6 && a->ln[0].x1 <= 150 + 1e-6, "left and right indents");
    text_layout_free(a);
  }
  /* a type layer's case, position, features and indents kept through OpenRaster */
  {
    Doc *d = doc_new(100, 60, 0xFFFFFF, 0), *e;
    TextParams tp;
    u8 *f2;
    size_t n;
    char err[100];
    text_params_init(&tp, "Carlito", 20, 0, 5, 30); text_set_string(&tp, "fi 1st");
    tp.run[0].ot = TO_LIGA | TO_ONUM | TO_SMALLCAPS | TO_SUPER;
    tp.indent_left = 3; tp.indent_right = 4; tp.align = TA_JUSTIFY_CENTER;
    doc_new_text(d, &tp, "T");
    f2 = ora_write(d, &n);
    e = f2 ? ora_read(f2, n, err, sizeof err) : NULL;
    {
      const TextParams *q = e && e->nlay > 1 && e->lay[1]->kind == LK_TEXT ? (const TextParams *)e->lay[1]->live : NULL;
      CHECK(q && q->run[0].ot == tp.run[0].ot && q->indent_left == 3 && q->indent_right == 4 && q->align == TA_JUSTIFY_CENTER, "kept in OpenRaster");
    }
    doc_free(d); doc_free(e); free(f2);
  }
  if (gsub_out) fclose(gsub_out);
  gsub_out = NULL;
  text_cache_flush();
  text_fonts = NULL;
  fonts_free(fs);
  free(fs);
}

/* ------------------------------------------------------------------ Export As */
static void test_export(void)
{
  Doc *d = ramp_doc(40, 30, 1), *t, *u;
  ExportOpts o;
  const u8 *p;
  strcpy(d->info.title, "Title"); strcpy(d->info.copyright, "(c) Easel test");
  exportx_defaults(&o);
  t = exportx_prepare(d, &o);
  CHECK(t && t->w == 40 && t->h == 30 && !memcmp(t->lay[0]->px + 40 * 4 * 10, d->lay[0]->px + 40 * 4 * 10, 40 * 20 * 4) && !strcmp(t->info.title, "Title"), "Export As: as it is");
  doc_free(t);
  o.pct = 50; o.method = RS_NEAREST;
  t = exportx_prepare(d, &o);
  o.method = RS_DETAILS;
  u = exportx_prepare(d, &o);
  CHECK(t && u && t->w == 20 && t->h == 15 && u->w == 20 && memcmp(t->lay[0]->px, u->lay[0]->px, 20 * 15 * 4), "scaled by the method chosen");
  doc_free(t); doc_free(u);
  exportx_defaults(&o);
  o.canvas_w = 60; o.canvas_h = 20; o.matte = EXM_WHITE; o.metadata = EXD_COPYRIGHT;
  t = exportx_prepare(d, &o);
  CHECK(t && t->w == 60 && t->h == 20, "the canvas size");
  if (t) {
    p = t->lay[0]->px;
    CHECK(p[3] == 255 && p[0] == 255 && p[1] == 255, "outside the picture: the white matte");
    CHECK(!memcmp(p + (5L * 60 + 10) * 4, d->lay[0]->px + (10L * 40 + 0) * 4, 3), "the picture centred (rows 5.. from 10..)");
    CHECK(!t->info.title[0] && !strcmp(t->info.copyright, "(c) Easel test"), "only the copyright kept");
  }
  doc_free(t);
  exportx_defaults(&o);
  o.metadata = EXD_NONE; o.matte = EXM_BLACK;
  t = exportx_prepare(d, &o);
  CHECK(t && docinfo_empty(&t->info) && t->lay[0]->px[0] == 0 && t->lay[0]->px[3] == 255, "no metadata; the transparent corner over black");
  doc_free(t);
  colorx_assign(d, CX_ADOBERGB, NULL, 0);
  exportx_defaults(&o);
  o.to_srgb = 1;
  t = exportx_prepare(d, &o);
  CHECK(t && t->icc && memcmp(t->lay[0]->px + (20L * 40 + 30) * 4, d->lay[0]->px + (20L * 40 + 30) * 4, 3), "converted to sRGB");
  if (t) { IccProfile *q = colorx_doc_profile(t); CHECK(q && icc_same(q, icc_srgb()), "and tagged sRGB"); icc_free(q); }
  doc_free(t);
  doc_free(d);
}

/* ------------------------------------------------------------------ IFF ANIM ops 7 and 8 */
#define AW 40
#define AH 12
#define ARB 6                          /* (row bytes: 40 pixels, a multiple of 16 bits; 6 isn't one of 4: the last long column is short) */
#define ANP 2
static void put32b(Buf *b, u32 v) { buf_addc(b, (int)(v >> 24)); buf_addc(b, (int)(v >> 16 & 255)); buf_addc(b, (int)(v >> 8 & 255)); buf_addc(b, (int)(v & 255)); }
static void put16b(Buf *b, u32 v) { buf_addc(b, (int)(v >> 8 & 255)); buf_addc(b, (int)(v & 255)); }
static u32 word_at(const u8 *pl, int p, int y, int col, int ws)
{
  u32 v = 0;
  int k;
  for (k = 0; k < ws; k++) { int x = col * ws + k; v = v << 8 | (x < ARB ? pl[((size_t)p * AH + y) * ARB + x] : 0); }
  return v;
}
/* a DLTA turning planes a into planes b, op 7 or 8, words of ws bytes */
static void make_delta(Buf *o, const u8 *a, const u8 *b, int op8, int ws)
{
  Buf ops[ANP], dat[ANP];
  int p, ncol = (ARB + ws - 1) / ws, k;
  u32 at;
  for (p = 0; p < ANP; p++) {
    int col;
    buf_init(&ops[p]); buf_init(&dat[p]);
    for (col = 0; col < ncol; col++) {
      Buf one;
      int y = 0, n = 0;
      buf_init(&one);
      while (y < AH) {
        int r = 0;
        while (y + r < AH && word_at(a, p, y + r, col, ws) == word_at(b, p, y + r, col, ws)) r++;
        if (r) { if (op8) { if (ws == 4) put32b(&one, 0x80000000UL | (u32)r); else put16b(&one, 0x8000 | (u32)r); } else buf_addc(&one, 0x80 | r); y += r; n++; continue; }
        r = 1;
        while (y + r < AH && word_at(b, p, y + r, col, ws) == word_at(b, p, y, col, ws)) r++;
        if (r >= 3) {
          if (op8) { if (ws == 4) { put32b(&one, 0); put32b(&one, (u32)r); put32b(&one, word_at(b, p, y, col, ws)); } else { put16b(&one, 0); put16b(&one, (u32)r); put16b(&one, word_at(b, p, y, col, ws)); } }
          else { buf_addc(&one, 0); buf_addc(&one, r); if (ws == 4) put32b(&dat[p], word_at(b, p, y, col, ws)); else put16b(&dat[p], word_at(b, p, y, col, ws)); }
          y += r; n++; continue;
        }
        r = 0;
        while (y + r < AH && word_at(a, p, y + r, col, ws) != word_at(b, p, y + r, col, ws)) r++;
        if (op8) { if (ws == 4) put32b(&one, (u32)r); else put16b(&one, (u32)r); } else buf_addc(&one, r);
        for (k = 0; k < r; k++) { Buf *dst = op8 ? &one : &dat[p]; if (ws == 4) put32b(dst, word_at(b, p, y + k, col, ws)); else put16b(dst, word_at(b, p, y + k, col, ws)); }
        y += r; n++;
      }
      if (op8) { if (ws == 4) put32b(&ops[p], (u32)n); else put16b(&ops[p], (u32)n); } else buf_addc(&ops[p], n);
      buf_add(&ops[p], one.data, one.len);
      buf_free(&one);
    }
  }
  at = op8 ? 32 : 64;
  for (p = 0; p < 8; p++) { if (p < ANP) { put32b(o, at); at += (u32)ops[p].len; } else put32b(o, 0); }
  if (!op8) for (p = 0; p < 8; p++) { if (p < ANP) { put32b(o, at); at += (u32)dat[p].len; } else put32b(o, 0); }
  for (p = 0; p < ANP; p++) buf_add(o, ops[p].data, ops[p].len);
  if (!op8) for (p = 0; p < ANP; p++) buf_add(o, dat[p].data, dat[p].len);
  for (p = 0; p < ANP; p++) { buf_free(&ops[p]); buf_free(&dat[p]); }
}
static void ilbm_head(Buf *b, int body, const u8 *planes)
{
  static const u8 cmap[12] = { 0, 0, 0, 255, 0, 0, 0, 255, 0, 255, 255, 255 };
  int y, p;
  buf_add(b, "BMHD", 4); put32b(b, 20); put16b(b, AW); put16b(b, AH); put16b(b, 0); put16b(b, 0);
  buf_addc(b, ANP); buf_addc(b, 0); buf_addc(b, 0); buf_addc(b, 0); put16b(b, 0); buf_addc(b, 10); buf_addc(b, 11); put16b(b, AW); put16b(b, AH);
  buf_add(b, "CMAP", 4); put32b(b, 12); buf_add(b, cmap, 12);
  if (body) { buf_add(b, "BODY", 4); put32b(b, ARB * ANP * AH); for (y = 0; y < AH; y++) for (p = 0; p < ANP; p++) buf_add(b, planes + ((size_t)p * AH + y) * ARB, ARB); }
}
static void test_anim78(void)
{
  u8 pl[3][ANP * AH * ARB];
  Buf b, f, dl;
  Frames fr;
  char err[100];
  int i, k, fpos;
  u32 seed = 7;
  /* three frames of noise, each changing some columns, with runs */
  for (i = 0; i < ANP * AH * ARB; i++) { seed = seed * 1103515245u + 12345u; pl[0][i] = (u8)(seed >> 16); }
  memcpy(pl[1], pl[0], sizeof pl[0]); memcpy(pl[2], pl[0], sizeof pl[0]);
  for (i = 0; i < ANP * AH * ARB; i++) { seed = seed * 1103515245u + 12345u; if ((seed >> 20) & 1) pl[1][i] = (u8)(seed >> 8); }
  for (i = 0; i < AH; i++) pl[1][i * ARB + 2] = 0x5A;           /* (a run down a column) */
  memcpy(pl[2], pl[1], sizeof pl[1]);
  for (i = 0; i < ANP * AH * ARB; i += 3) { seed = seed * 1103515245u + 12345u; pl[2][i] = (u8)(seed >> 12); }
  buf_init(&b);
  buf_add(&b, "FORM\0\0\0\0ANIM", 12);
  for (k = 0; k < 3; k++) {
    buf_init(&f);
    buf_add(&f, "ILBM", 4);
    if (k == 0) ilbm_head(&f, 1, pl[0]);
    else {
      buf_add(&f, "ANHD", 4); put32b(&f, 40);
      buf_addc(&f, k == 1 ? 7 : 8); buf_addc(&f, 0); put16b(&f, AW); put16b(&f, AH); put16b(&f, 0); put16b(&f, 0);
      put32b(&f, 0); put32b(&f, 6); buf_addc(&f, 1); buf_addc(&f, 0); put32b(&f, k == 2 ? 1 : 0);    /* frame 2: op 8 with long words */
      for (i = 0; i < 16; i++) buf_addc(&f, 0);
      buf_init(&dl);
      make_delta(&dl, pl[k - 1], pl[k], k == 2, k == 2 ? 4 : 2);
      buf_add(&f, "DLTA", 4); put32b(&f, (u32)dl.len); buf_add(&f, dl.data, dl.len); if (dl.len & 1) buf_addc(&f, 0);
      buf_free(&dl);
    }
    buf_add(&b, "FORM", 4); put32b(&b, (u32)f.len); buf_add(&b, f.data, f.len); if (f.len & 1) buf_addc(&b, 0);
    buf_free(&f);
  }
  b.data[4] = (char)((b.len - 8) >> 24); b.data[5] = (char)((b.len - 8) >> 16); b.data[6] = (char)((b.len - 8) >> 8); b.data[7] = (char)(b.len - 8);
  CHECK(anim_read_frames((u8 *)b.data, b.len, &fr, 0, 0, err, sizeof err) && fr.n == 3 && !fr.truncated, "an ANIM with op 7 and op 8 deltas read");
  for (k = 0; k < 3 && k < fr.n; k++) {
    Buf il;
    Rgba want;
    buf_init(&il);
    buf_add(&il, "FORM\0\0\0\0ILBM", 12);
    ilbm_head(&il, 1, pl[k]);
    il.data[4] = (char)((il.len - 8) >> 24); il.data[5] = (char)((il.len - 8) >> 16); il.data[6] = (char)((il.len - 8) >> 8); il.data[7] = (char)(il.len - 8);
    CHECK(codec_decode((u8 *)il.data, il.len, &want, err, sizeof err), "the frame as an ILBM");
    CHECK(want.px && !memcmp(want.px, fr.fr[k].px, AW * AH * 4), k == 1 ? "op 7 (short) frame exact" : k == 2 ? "op 8 (long) frame exact" : "first frame exact");
    rgba_free(&want);
    buf_free(&il);
  }
  for (k = 0; k < fr.n; k++) rgba_free(&fr.fr[k]);
  /* damaged: each byte in turn */
  for (fpos = 12; fpos < (int)b.len; fpos += 3) {
    u8 *c = (u8 *)malloc(b.len);
    memcpy(c, b.data, b.len); c[fpos] ^= 0x77;
    if (anim_read_frames(c, b.len, &fr, 0, 0, err, sizeof err)) for (k = 0; k < fr.n; k++) rgba_free(&fr.fr[k]);
    free(c);
  }
  buf_free(&b);
}

/* ------------------------------------------------------------------ PSD layer styles (lfx2) */
static void test_psdfx(void)
{
  Doc *d = doc_new(64, 48, 0xFFFFFF, 0), *e;
  Layer *l = layer_new(d, "Styled");
  LayerFx fx;
  u8 *f;
  size_t n;
  char err[100];
  i32 x, y;
  int k;
  for (y = 10; y < 38; y++) for (x = 12; x < 52; x++) { u8 *p = l->px + ((long)y * 64 + x) * 4; p[0] = 200; p[1] = 40; p[2] = 40; p[3] = 255; }
  d->lay[d->nlay++] = l; d->active = 1;
  fx_defaults_all(&fx);
  fx.e[FX_DROPSHADOW].on = 1; fx.e[FX_DROPSHADOW].color = 0x102030; fx.e[FX_DROPSHADOW].distance = 7; fx.e[FX_DROPSHADOW].size = 9;
  fx.e[FX_DROPSHADOW].angle = 135; fx.e[FX_DROPSHADOW].opacity = 191; fx.e[FX_DROPSHADOW].blend = BM_MULTIPLY; fx.e[FX_DROPSHADOW].flags &= ~FXF_GLOBAL;
  fx.e[FX_DROPSHADOW].spread = 20;
  fx.e[FX_STROKE].on = 1; fx.e[FX_STROKE].size = 3; fx.e[FX_STROKE].color = 0x00FF00; fx.e[FX_STROKE].style = FXS_INSIDE;
  fx.e[FX_GRADIENT].on = 1; fx.e[FX_GRADIENT].color = 0xFF0000; fx.e[FX_GRADIENT].color2 = 0x0000FF; fx.e[FX_GRADIENT].style = GR_RADIAL;
  fx.e[FX_GRADIENT].angle = 45; fx.e[FX_GRADIENT].depth = 80; fx.e[FX_GRADIENT].dx = 10; fx.e[FX_GRADIENT].dy = -5;
  fx.e[FX_BEVEL].on = 1; fx.e[FX_BEVEL].style = FXB_EMBOSS; fx.e[FX_BEVEL].size = 6; fx.e[FX_BEVEL].depth = 250; fx.e[FX_BEVEL].altitude = 40;
  fx.e[FX_BEVEL].flags |= FXF_INVERT; fx.e[FX_BEVEL].color2 = 0x202020;
  fx.e[FX_INNERGLOW].on = 1; fx.e[FX_INNERGLOW].style = FXG_CENTER; fx.e[FX_INNERGLOW].size = 12; fx.e[FX_INNERGLOW].spread = 50;
  fx.e[FX_SATIN].on = 1; fx.e[FX_SATIN].flags |= FXF_INVERT; fx.e[FX_SATIN].distance = 4;
  fx.e[FX_COLOR].on = 1; fx.e[FX_COLOR].color = 0x123456; fx.e[FX_COLOR].opacity = 128;
  l->fx = fx;
  f = psd_write_doc(d, &n);
  CHECK(f != NULL, "a PSD with a layer style written");
  e = f ? psd_read_doc(f, n, err, sizeof err) : NULL;
  CHECK(e && e->nlay == 2, "and read back");
  if (e && e->nlay == 2) {
    const LayerFx *g = &e->lay[1]->fx;
    for (k = 0; k < FX_N; k++) CHECK(g->e[k].on == fx.e[k].on, "each effect on or off as it was");
    CHECK(g->e[FX_DROPSHADOW].color == 0x102030 && g->e[FX_DROPSHADOW].distance == 7 && g->e[FX_DROPSHADOW].size == 9 && g->e[FX_DROPSHADOW].angle == 135 &&
          abs(g->e[FX_DROPSHADOW].opacity - 191) <= 1 && g->e[FX_DROPSHADOW].blend == BM_MULTIPLY && !(g->e[FX_DROPSHADOW].flags & FXF_GLOBAL) &&
          abs(g->e[FX_DROPSHADOW].spread - 20) <= 6, "the drop shadow's settings");
    CHECK(g->e[FX_STROKE].size == 3 && g->e[FX_STROKE].color == 0x00FF00 && g->e[FX_STROKE].style == FXS_INSIDE, "the stroke's");
    CHECK(g->e[FX_GRADIENT].color == 0xFF0000 && g->e[FX_GRADIENT].color2 == 0x0000FF && g->e[FX_GRADIENT].style == GR_RADIAL && g->e[FX_GRADIENT].angle == 45 &&
          g->e[FX_GRADIENT].depth == 80 && g->e[FX_GRADIENT].dx == 10 && g->e[FX_GRADIENT].dy == -5, "the gradient overlay's");
    CHECK(g->e[FX_BEVEL].style == FXB_EMBOSS && g->e[FX_BEVEL].size == 6 && g->e[FX_BEVEL].depth == 250 && g->e[FX_BEVEL].altitude == 40 &&
          (g->e[FX_BEVEL].flags & FXF_INVERT) && g->e[FX_BEVEL].color2 == 0x202020, "the bevel's");
    CHECK(g->e[FX_INNERGLOW].style == FXG_CENTER && g->e[FX_INNERGLOW].size == 12 && abs(g->e[FX_INNERGLOW].spread - 50) <= 5, "the inner glow's");
    CHECK((g->e[FX_SATIN].flags & FXF_INVERT) && g->e[FX_SATIN].distance == 4, "the satin's");
    CHECK(g->e[FX_COLOR].color == 0x123456 && abs(g->e[FX_COLOR].opacity - 128) <= 1, "the colour overlay's");
  }
  save("fx.psd", f, n);
  /* damaged blocks fail cleanly */
  if (f) {
    size_t i;
    u8 *c = (u8 *)malloc(n);
    for (i = 0; i < n; i += 7) { Doc *t; memcpy(c, f, n); c[i] ^= 0x41; t = psd_read_doc(c, n, err, sizeof err); doc_free(t); }
    free(c);
  }
  doc_free(e); free(f);
  doc_free(d);
}

/* ------------------------------------------------------------------ progressive JPEG */
static void test_progressive(void)
{
  static const int qs[2] = { 85, 95 }, sizes[3][2] = { { 37, 23 }, { 16, 16 }, { 1, 1 } };
  int qi, si;
  for (si = 0; si < 3; si++)
    for (qi = 0; qi < 2; qi++) {
      Doc *d = ramp_doc(sizes[si][0] < 2 ? 2 : sizes[si][0], sizes[si][1] < 2 ? 2 : sizes[si][1], 0);
      Rgba im, a, b;
      u8 *p1, *p2;
      size_t l1, l2;
      char err[80], name[40];
      im.w = sizes[si][0]; im.h = sizes[si][1]; im.px = d->lay[0]->px;
      p1 = jpeg_encode(&im, qs[qi], 72, &l1);
      p2 = jpeg_encode_progressive(&im, qs[qi], 72, &l2);
      CHECK(p2 && l2 > 4 && p2[0] == 0xFF && p2[1] == 0xD8, "a progressive JPEG written");
      CHECK(p1 && p2 && jpeg_read(p1, l1, &a, err, sizeof err) && jpeg_read(p2, l2, &b, err, sizeof err), "both read");
      if (a.px && b.px) CHECK(a.w == b.w && a.h == b.h && !memcmp(a.px, b.px, (size_t)a.w * a.h * 4), "progressive decodes as baseline does (the same coefficients)");
      { int k, sof2 = 0; for (k = 0; p2 && k + 1 < (int)l2; k++) if (p2[k] == 0xFF && p2[k + 1] == 0xC2) sof2 = 1; CHECK(sof2, "with a SOF2 frame"); }
      snprintf(name, sizeof name, "prog_%d_%d.jpg", si, qs[qi]); save(name, p2, l2);
      snprintf(name, sizeof name, "base_%d_%d.jpg", si, qs[qi]); save(name, p1, l1);
      rgba_free(&a); rgba_free(&b); free(p1); free(p2);
      doc_free(d);
    }
}

int main(int argc, char **argv)
{
  if (argc > 1) outdir = argv[1];
  if (outdir) { char cmd[600]; snprintf(cmd, sizeof cmd, "mkdir -p '%s'", outdir); if (system(cmd)) printf("cannot make %s\n", outdir); }
  test_aspect();
  test_builtin_profiles();
  test_transforms();
  test_press();
  test_views();
  test_files();
  test_models();
  test_assign_convert();
  test_open_modes();
  test_exr();
  test_cineon_hdr_dicom();
  test_type();
  test_export();
  test_anim78();
  test_psdfx();
  test_progressive();
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
