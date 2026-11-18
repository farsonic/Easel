/*
 * gallerytest.c - native tests of the Filter Gallery effects and stacks (core/gallery.c), Color
 * Lookup, Equalize's areas and Black & White Auto (core/adjustx.c), Focus Area and Select and Mask
 * (core/selmask.c). It also writes a LUT, a picture and Easel's lookups of it for
 * tests/check_fx.py to check with numpy.
 *
 * Every effect changes a textured picture, gives the same pixels twice, and gives (to within
 * rounding) the same pixels worked out in 128-pixel tiles as worked out over the whole picture at
 * once; the selection is kept to; undo puts the layer back; an aborted run stops; a stack of two
 * is the two one after the other; and a few known answers (identities, Sketch effects drawing only
 * in the ink and paper colours, Chrome giving greys).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../core/doc.h"
#include "../core/paint.h"
#include "../core/util.h"
#include "../core/gallery.h"
#include "../core/adjust10.h"
#include "../core/adjustx.h"
#include "../core/selmask.h"
#include "../core/layers.h"
#include "../core/resample.h"

static int tests, failed;
static void ok(const char *name, int cond) { tests++; if (!cond) { failed++; printf("FAIL %s\n", name); } }
static void okf(const char *name, int fx, int cond) { char b[120]; snprintf(b, sizeof b, "%s (%s)", name, gallery_defs[fx].name); ok(b, cond); }
static u8 *px(Doc *d, i32 x, i32 y) { return d->lay[d->active]->px + ((long)y * d->w + x) * 4; }
static Doc *textured(i32 w, i32 h)
{
  Doc *d = doc_new(w, h, 0, 0);
  i32 x, y;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      u8 *p = px(d, x, y);
      double t = 40 * sin(x * 0.21) * cos(y * 0.17) + 25 * sin((x + 2 * y) * 0.05);
      p[0] = (u8)(70 + x * 0.4 + t); p[1] = (u8)(80 + y * 0.5 + t * 0.7); p[2] = (u8)(110 + t); p[3] = 255;
      if ((x / 37 + y / 29) % 3 == 0) { p[0] = (u8)(p[0] / 3); p[1] = (u8)(p[1] / 3); }
    }
  return d;
}
static u8 *copy_px(Doc *d) { u8 *c = (u8 *)malloc((size_t)d->w * d->h * 4); memcpy(c, d->lay[0]->px, (size_t)d->w * d->h * 4); return c; }
static long ndiff(const u8 *a, const u8 *b, long n, int tol, int *maxd)
{
  long k, c = 0;
  int m = 0;
  for (k = 0; k < n; k++) { int dd = abs(a[k] - b[k]); if (dd > m) m = dd; if (dd > tol) c++; }
  if (maxd) *maxd = m;
  return c;
}
static int abort_after;
static int abort_hook(long done, long total) { (void)done; (void)total; return --abort_after < 0; }

static void every_effect(void)
{
  int fx;
  for (fx = 0; fx < GX_N; fx++) {
    GalleryEffect e;
    Doc *d = textured(300, 190), *d2 = textured(300, 190);
    u8 *orig = copy_px(d), *whole = copy_px(d);
    long n = (long)d->w * d->h * 4;
    int md, r;
    gallery_default(fx, &e);
    ok("defaults are in range", e.v[0] >= gallery_defs[fx].sl[0].min && e.v[0] <= gallery_defs[fx].sl[0].max);
    r = flt_gallery(d, &e, 1, NULL);
    okf("changes the picture", fx, r == 1 && ndiff(d->lay[0]->px, orig, n, 0, NULL) > 0);
    okf("alpha kept", fx, px(d, 10, 10)[3] == 255 && px(d, 299, 189)[3] == 255);
    flt_gallery(d2, &e, 1, NULL);
    okf("the same twice", fx, !memcmp(d->lay[0]->px, d2->lay[0]->px, (size_t)n));
    /* tiles against the whole picture at once */
    ok("whole picture runs", gallery_run_rgba(whole, d->w, d->h, 0, 0, &e, 1));
    {
      long bad = ndiff(d->lay[0]->px, whole, n, 2, &md);
      okf("tiles match the whole picture", fx, bad <= n / 2000);
      if (bad > n / 2000) printf("  %ld differ by more than 2 (max %d)\n", bad, md);
    }
    doc_undo(d);
    okf("undo puts it back", fx, !memcmp(d->lay[0]->px, orig, (size_t)n));
    free(orig); free(whole);
    doc_free(d); doc_free(d2);
  }
}

static void selection_and_abort(void)
{
  Doc *d = textured(260, 200);
  u8 *orig = copy_px(d);
  GalleryEffect e;
  long n = (long)d->w * d->h * 4;
  int inside_changed = 0, outside_same = 1;
  i32 x, y;
  gallery_default(GX_WATERCOLOR, &e);
  sel_rect(d, 40, 30, 190, 160, SEL_REPLACE, 0);
  flt_gallery(d, &e, 1, NULL);
  for (y = 0; y < d->h; y++)
    for (x = 0; x < d->w; x++) {
      int in = x >= 40 && x < 190 && y >= 30 && y < 160;
      int same = !memcmp(px(d, x, y), orig + ((long)y * d->w + x) * 4, 4);
      if (in && !same) inside_changed = 1;
      if (!in && !same) outside_same = 0;
    }
  ok("selection: inside changed", inside_changed);
  ok("selection: outside left alone", outside_same);
  doc_undo(d);
  sel_none(d);
  /* Abort after a few tiles: the run stops and says nothing changed */
  abort_after = 2;
  progress_reset();
  progress_hook = abort_hook;
  ok("aborted run returns 0", flt_gallery(d, &e, 1, NULL) == 0);
  progress_hook = NULL;
  ok("abort is seen", progress_aborted);
  progress_reset();
  doc_undo(d);
  ok("aborted run undone", !memcmp(d->lay[0]->px, orig, (size_t)n));
  /* a locked layer isn't touched */
  d->lay[0]->locked = 1;
  ok("locked layer refused", flt_gallery(d, &e, 1, NULL) == 0);
  d->lay[0]->locked = 0;
  ok("memory estimate covers the undo copy", gallery_memory(d, &e, 1) >= n);
  free(orig);
  doc_free(d);
}

static void stacks(void)
{
  Doc *a = textured(280, 170), *b = textured(280, 170);
  GalleryEffect s[2];
  long n = (long)a->w * a->h * 4;
  int md;
  gallery_default(GX_ANGLED_STROKES, &s[0]);
  gallery_default(GX_GRAIN, &s[1]);
  flt_gallery(a, s, 2, "Filter Gallery");
  flt_gallery(b, &s[0], 1, NULL);
  flt_gallery(b, &s[1], 1, NULL);
  ok("a stack of two is the two in turn", ndiff(a->lay[0]->px, b->lay[0]->px, n, 2, &md) <= n / 2000);
  ok("a stack is one undo step", doc_undo(a) && !doc_can_undo(a));
  ok("too many effects refused", flt_gallery(a, s, GX_STACK + 1, NULL) == 0);
  doc_free(a); doc_free(b);
}

static void known_answers(void)
{
  Doc *d = textured(200, 150);
  u8 *orig = copy_px(d);
  GalleryEffect e;
  long n = (long)d->w * d->h * 4, k;
  int md, online = 1, grey = 1;
  /* Grain at no intensity and middle contrast leaves the picture as it is */
  gallery_default(GX_GRAIN, &e);
  e.v[0] = 0; e.v[1] = 50;
  flt_gallery(d, &e, 1, NULL);
  ok("Grain 0 is the identity", ndiff(d->lay[0]->px, orig, n, 1, &md) == 0);
  doc_undo(d);
  /* Stamp draws only in the ink and paper colours (red on yellow here) */
  gallery_default(GX_STAMP, &e);
  e.fg = 0xC00000; e.bg = 0xFFFF00;
  flt_gallery(d, &e, 1, NULL);
  for (k = 0; k < (long)d->w * d->h; k++) {
    const u8 *p = d->lay[0]->px + k * 4;
    /* on the line from (192, 0, 0) to (255, 255, 0): blue 0 and green following red */
    double t = p[1] / 255.0;
    if (p[2] > 1 || fabs(p[0] - (192 + 63 * t)) > 2) online = 0;
  }
  ok("Stamp uses only the ink and paper", online);
  doc_undo(d);
  /* the far ends: white paper where the picture is light, ink where dark */
  {
    Doc *bw = doc_new(64, 32, 0xFFFFFF, 0);
    i32 x, y;
    for (y = 0; y < 32; y++) for (x = 0; x < 32; x++) { u8 *p = bw->lay[0]->px + ((long)y * 64 + x) * 4; p[0] = p[1] = p[2] = 0; }
    gallery_default(GX_STAMP, &e);
    flt_gallery(bw, &e, 1, NULL);
    ok("Stamp: dark becomes ink", bw->lay[0]->px[(16 * 64 + 8) * 4] == 0);
    ok("Stamp: light becomes paper", bw->lay[0]->px[(16 * 64 + 56) * 4] == 255);
    doc_free(bw);
  }
  gallery_default(GX_CHROME, &e);
  flt_gallery(d, &e, 1, NULL);
  for (k = 0; k < (long)d->w * d->h; k++) { const u8 *p = d->lay[0]->px + k * 4; if (p[0] != p[1] || p[1] != p[2]) grey = 0; }
  ok("Chrome gives greys", grey);
  doc_undo(d);
  /* settings past their ranges are held to them, not read past */
  gallery_default(GX_CRAQUELURE, &e);
  e.v[0] = 100000; e.v[1] = -5; e.list1 = 99; e.fx = GX_N + 4;
  ok("wild settings are clamped", flt_gallery(d, &e, 1, NULL) >= 0 && gallery_reach(&e) >= 0);
  free(orig);
  doc_free(d);
}

/* ------------------------------------------------------------------ Color Lookup */
static void lookup(const char *outdir)
{
  static const char inv[] = "TITLE \"invert\"\n# a comment\nLUT_3D_SIZE 2\n1 1 1\n0 1 1\n1 0 1\n0 0 1\n1 1 0\n0 1 0\n1 0 0\n0 0 0\n";
  static const char inv1[] = "LUT_1D_SIZE 3\n1 1 1\n0.5 0.5 0.5\n0 0 0\n";
  char err[120], path[300];
  Lut3D *l = lut_parse_cube(inv, (long)strlen(inv), err, sizeof err), *id = lut_identity(17), *rt, *one, *rnd;
  Doc *d = textured(120, 90);
  u8 *orig = copy_px(d);
  long n = (long)d->w * d->h * 4, k, len;
  int md, tet, okinv = 1;
  char *txt;
  ok("cube parsed", l && l->n == 2 && !strcmp(l->title, "invert"));
  for (tet = 0; tet < 2; tet++) {
    adj_color_lookup(d, id, tet, 0, 100);
    ok("identity LUT leaves the picture", ndiff(d->lay[0]->px, orig, n, 1, &md) == 0);
    doc_undo(d);
    adj_color_lookup(d, l, tet, 0, 100);
    for (k = 0; k < (long)d->w * d->h * 4; k++) if (k % 4 != 3 && abs(d->lay[0]->px[k] - (255 - orig[k])) > 1) okinv = 0;
    ok("invert LUT inverts", okinv);
    doc_undo(d);
  }
  adj_color_lookup(d, l, 1, 0, 50);
  ok("opacity 50 is half way", abs(d->lay[0]->px[0] - (orig[0] + (255 - orig[0])) / 2) <= 1 + abs(255 - 2 * orig[0]) / 255);
  doc_undo(d);
  adj_color_lookup(d, id, 0, 1, 100);
  ok("dither moves by at most one", ndiff(d->lay[0]->px, orig, n, 1, &md) == 0 && md <= 1);
  doc_undo(d);
  txt = lut_format_cube(l, &len);
  rt = txt ? lut_parse_cube(txt, len, err, sizeof err) : NULL;
  ok("cube written and read back", rt && rt->n == 2 && fabs(rt->t[0] - 1) < 1e-5 && fabs(rt->t[21] - 0) < 1e-5);
  one = lut_parse_cube(inv1, (long)strlen(inv1), err, sizeof err);
  if (one) { adj_color_lookup(d, one, 0, 0, 100); ok("1D LUT inverts", abs(d->lay[0]->px[4] - (255 - orig[4])) <= 1); doc_undo(d); }
  else ok("1D LUT parsed", 0);
  ok("not a LUT refused", !lut_parse_cube("hello\n1 2 3\n", 12, err, sizeof err) && err[0]);
  ok("short LUT refused", !lut_parse_cube("LUT_3D_SIZE 3\n0 0 0\n", 21, err, sizeof err));
  ok("huge LUT refused", !lut_parse_cube("LUT_3D_SIZE 900\n0 0 0\n", 23, err, sizeof err));
  /* a bent LUT for numpy to check: a 9-point table with a twist and a curve */
  rnd = lut_identity(9);
  if (rnd) {
    FILE *f;
    for (k = 0; k < 9L * 9 * 9; k++) {
      float *e = rnd->t + k * 3, r0 = e[0], g0 = e[1], b0 = e[2];
      e[0] = (float)pow(r0, 1.7) * 0.8f + b0 * 0.2f; e[1] = (float)(0.5 + 0.5 * sin(g0 * 3.0 + r0)); e[2] = (float)(b0 * b0 * 0.6 + g0 * 0.3);
    }
    free(txt);
    txt = lut_format_cube(rnd, &len);
    snprintf(path, sizeof path, "%s/bent.cube", outdir); file_write(path, txt, (size_t)len);
    snprintf(path, sizeof path, "%s/in.rgba", outdir); file_write(path, orig, (size_t)n);
    for (tet = 0; tet < 2; tet++) {
      Lut3D *back = lut_load_cube((snprintf(path, sizeof path, "%s/bent.cube", outdir), path), err, sizeof err);
      if (back) adj_color_lookup(d, back, tet, 0, 100);
      snprintf(path, sizeof path, "%s/out_%s.rgba", outdir, tet ? "tetra" : "tri"); file_write(path, d->lay[0]->px, (size_t)n);
      if (back) doc_undo(d);
      lut_free(back);
    }
    f = fopen((snprintf(path, sizeof path, "%s/size.txt", outdir), path), "w");
    if (f) { fprintf(f, "%d %d\n", (int)d->w, (int)d->h); fclose(f); }
  }
  free(txt);
  lut_free(l); lut_free(id); lut_free(rt); lut_free(one); lut_free(rnd);
  free(orig);
  doc_free(d);
}

static void equalize_bw(void)
{
  Doc *d = textured(120, 90), *e;
  u8 *orig = copy_px(d);
  long n = (long)d->w * d->h * 4;
  int w6[6] = { 40, 60, 40, 60, 20, 80 }, w0[6] = { 40, 60, 40, 60, 20, 80 }, k, inrange = 1;
  sel_rect(d, 10, 10, 50, 40, SEL_REPLACE, 0);
  adj_equalize_ex(d, EQ_SELECTED);
  ok("Equalize selected: outside kept", !memcmp(px(d, 100, 80), orig + ((long)80 * d->w + 100) * 4, 4));
  doc_undo(d);
  adj_equalize_ex(d, EQ_ENTIRE);
  ok("Equalize entire: outside changed too", ndiff(px(d, 0, 60), orig + (long)60 * d->w * 4, (long)d->w * 4 * 30, 0, NULL) > 0);
  doc_undo(d);
  sel_none(d);
  ok("Equalize undone", !memcmp(d->lay[0]->px, orig, (size_t)n));
  /* Black & White Auto: a red and a blue of the same brightness become different greys */
  e = doc_new(64, 32, 0, 0);
  {
    i32 x, y;
    for (y = 0; y < 32; y++) for (x = 0; x < 64; x++) { u8 *p = e->lay[0]->px + ((long)y * 64 + x) * 4; p[3] = 255; if (x < 32) { p[0] = 200; p[1] = 40; p[2] = 40; } else { p[0] = 60; p[1] = 60; p[2] = 230; } }
  }
  ok("Auto finds weights", bw_auto_weights(e, w6));
  for (k = 0; k < 6; k++) if (w6[k] < -200 || w6[k] > 300) inrange = 0;
  ok("Auto weights in range", inrange);
  {
    int g1, g2, h1, h2;
    adj_black_white6(e, w0, 0, 0, 0); h1 = e->lay[0]->px[0]; h2 = e->lay[0]->px[40 * 4]; doc_undo(e);
    adj_black_white6(e, w6, 0, 0, 0); g1 = e->lay[0]->px[0]; g2 = e->lay[0]->px[40 * 4]; doc_undo(e);
    ok("Auto spreads the greys at least as far", abs(g1 - g2) >= abs(h1 - h2) && abs(g1 - g2) > 40);
  }
  doc_free(e);
  free(orig);
  doc_free(d);
}

static void focus_refine(void)
{
  Doc *d = doc_new(160, 100, 0, 0);
  i32 x, y;
  u8 *m;
  RefineParams r;
  long left = 0, right = 0;
  /* left: sharp fine texture; right: the same, smooth */
  for (y = 0; y < 100; y++)
    for (x = 0; x < 160; x++) {
      u8 *p = px(d, x, y);
      int v = x < 80 ? (((x * 7 + y * 13) % 5) * 40 + 40) : (int)(120 + 20 * sin(x * 0.05) * cos(y * 0.07));
      p[0] = p[1] = p[2] = (u8)v; p[3] = 255;
    }
  m = focus_area_mask(d, 50, 0, 0, 0);
  ok("Focus Area mask made", m != NULL);
  if (m) {
    for (y = 20; y < 80; y++) for (x = 0; x < 160; x++) { if (x < 60) left += m[(long)y * 160 + x]; else if (x > 100) right += m[(long)y * 160 + x]; }
    ok("Focus Area: sharp side selected", left > 60L * 60 * 200);
    ok("Focus Area: smooth side not", right < 60L * 59 * 40);
    free(m);
  }
  ok("Focus Area as a selection", sel_focus_area(d, 50, 0, 1, 0, SEL_REPLACE) && d->sel && doc_can_undo(d));
  doc_free(d);
  /* Select and Mask: a red square on blue, a selection a little off it, pulled onto the edge */
  d = doc_new(120, 120, 0, 0);
  for (y = 0; y < 120; y++) for (x = 0; x < 120; x++) { u8 *p = px(d, x, y); int in = x >= 40 && x < 80 && y >= 40 && y < 80; p[0] = in ? 220 : 20; p[1] = 30; p[2] = in ? 30 : 200; p[3] = 255; }
  memset(&r, 0, sizeof r);
  ok("Select and Mask needs a selection", !refine_mask(d, &r));
  sel_rect(d, 44, 36, 84, 76, SEL_REPLACE, 0);
  r.radius = 8;
  ok("refined", sel_refine(d, &r));
  ok("refine: off the square dropped", d->sel && d->sel[(long)60 * 120 + 82] < 30);
  ok("refine: in the square taken", d->sel && d->sel[(long)78 * 120 + 42] > 225 && d->sel[(long)38 * 120 + 60] < 30);
  ok("refine: middle kept", d->sel && d->sel[(long)60 * 120 + 60] == 255);
  doc_undo(d);
  memset(&r, 0, sizeof r);
  r.shift = 50;
  sel_refine(d, &r);
  ok("shift edge grows", d->sel && d->sel[(long)56 * 120 + 87] == 255 && d->sel[(long)56 * 120 + 92] == 0);
  doc_undo(d);
  memset(&r, 0, sizeof r);
  r.feather10 = 40;
  sel_refine(d, &r);
  ok("feather softens", d->sel && d->sel[(long)56 * 120 + 84] > 40 && d->sel[(long)56 * 120 + 84] < 215);
  doc_undo(d);
  memset(&r, 0, sizeof r);
  r.feather10 = 40; r.contrast = 100;
  sel_refine(d, &r);
  ok("contrast hardens", d->sel && (d->sel[(long)56 * 120 + 83] == 255 || d->sel[(long)56 * 120 + 83] == 0));
  doc_free(d);
  /* All Layers and Deselect Layers */
  d = doc_new(20, 20, 0, 0);
  doc_add_layer(d, layer_new(d, "a"), 1); doc_add_layer(d, layer_new(d, "b"), 2);
  ok("All Layers picks every layer", doc_pick_all_layers(d) == 3 && doc_npicked(d) == 3);
  doc_pick_no_layers(d);
  ok("Deselect Layers leaves the active one", doc_npicked(d) == 1 && d->lay[d->active]->picked);
  doc_free(d);
}

/* ------------------------------------------------------------------ Image Size's methods */
static double roughness(Doc *d)
{
  double t = 0;
  i32 x, y;
  for (y = 0; y < d->h; y++) for (x = 1; x < d->w; x++) t += abs(px(d, x, y)[0] - px(d, x - 1, y)[0]);
  return t / ((double)d->h * (d->w - 1));
}
static void resampling(const char *outdir)
{
  Doc *a = textured(40, 30), *b = textured(40, 30), *c;
  char path[300];
  FILE *f;
  double rs, rb;
  snprintf(path, sizeof path, "%s/rs_in.rgba", outdir); file_write(path, a->lay[0]->px, 40 * 30 * 4);
  ok("Preserve Details enlarges", image_resize(a, 120, 90, RS_DETAILS) && a->w == 120 && a->h == 90);
  snprintf(path, sizeof path, "%s/rs_lanczos.rgba", outdir); file_write(path, a->lay[0]->px, 120 * 90 * 4);
  f = fopen((snprintf(path, sizeof path, "%s/rs_size.txt", outdir), path), "w");
  if (f) { fprintf(f, "40 30 120 90\n"); fclose(f); }
  doc_undo(a);
  ok("undone back to 40 x 30", a->w == 40 && a->h == 30);
  image_resize(a, 120, 90, RS_SMOOTHER); rs = roughness(a);
  image_resize(b, 120, 90, RS_BICUBIC); rb = roughness(b);
  ok("Bicubic Smoother is smoother than Bicubic", rs < rb && rs > rb * 0.6);
  doc_free(a); doc_free(b);
  a = textured(200, 150); b = textured(200, 150);
  image_resize(a, 60, 45, RS_SHARPER); image_resize(b, 60, 45, RS_BICUBIC);
  ok("Bicubic Sharper is crisper than Bicubic when reducing", roughness(a) > roughness(b) * 1.05);
  c = doc_new(16, 16, 0x808080, 0);
  image_resize(c, 48, 48, RS_DETAILS);
  ok("a flat colour stays flat", px(c, 20, 20)[0] == 0x80 && px(c, 0, 47)[1] == 0x80);
  doc_free(a); doc_free(b); doc_free(c);
}

int main(int argc, char **argv)
{
  char outdir[260];
  const char *tmp = getenv("TMP");
  snprintf(outdir, sizeof outdir, "%s", argc > 1 ? argv[1] : tmp && tmp[0] ? tmp : "/tmp");
  every_effect();
  selection_and_abort();
  stacks();
  known_answers();
  lookup(outdir);
  equalize_bw();
  focus_refine();
  resampling(outdir);
  printf("%d tests, %d failed\n", tests, failed);
  return failed != 0;
}
