/*
 * gallerytest.c - native tests of the Filter Gallery effects and stacks (core/gallery.c).
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

int main(void)
{
  every_effect();
  selection_and_abort();
  stacks();
  known_answers();
  printf("%d tests, %d failed\n", tests, failed);
  return failed != 0;
}
