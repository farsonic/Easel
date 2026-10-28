/*
 * exchangetest.c - Easel 0.6's exchange formats: TIFF writing (every compression with every
 * colour mode), Targa writing (raw and RLE, true colour, grey and colour-mapped), Photoshop layers
 * read and written (names, opacity, blend modes, visibility, clipping, fill opacity, locks, masks,
 * groups nested two deep, pass-through, the merged composite; a hand-made 16-bit file and a flat
 * one), the JPEG EXIF orientation (all eight, either byte order) and File Info round trips through
 * PNG, JPEG, TIFF, ORA and PSD. Damaged and truncated files are fed back to the readers (run it
 * under AddressSanitizer and UBSan).
 *
 * It writes its files to a folder (default /tmp/easel_exchange_out, or argv[1]) for
 * tests/check_exchange.py to check with Pillow (and psd-tools when it is installed).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/codec.h"
#include "core/palette.h"
#include "core/layers.h"
#include "core/rasters.h"

static int ntests, nfailed;
static const char *outdir = "/tmp/easel_exchange_out";

static void ok(const char *name, int cond)
{
  ntests++;
  if (!cond) { nfailed++; printf("FAIL: %s\n", name); }
}
static u32 rng = 4321;
static u32 rnd(void) { rng = rng * 1103515245UL + 12345; return (rng >> 8) & 0xFFFFFF; }

static void save(const char *name, const void *d, size_t n)
{
  char path[512];
  snprintf(path, sizeof path, "%s/%s", outdir, name);
  if (!file_write(path, d, n)) printf("cannot write %s\n", path);
}
/* a picture as .rgba (magic, big-endian width and height, the pixels) for check_exchange.py */
static void save_rgba(const char *name, const u8 *px, i32 w, i32 h)
{
  u8 *b = (u8 *)malloc((size_t)w * h * 4 + 12);
  if (!b) return;
  memcpy(b, "RGBA", 4);
  b[4] = (u8)(w >> 24); b[5] = (u8)(w >> 16); b[6] = (u8)(w >> 8); b[7] = (u8)w;
  b[8] = (u8)(h >> 24); b[9] = (u8)(h >> 16); b[10] = (u8)(h >> 8); b[11] = (u8)h;
  memcpy(b + 12, px, (size_t)w * h * 4);
  save(name, b, (size_t)w * h * 4 + 12);
  free(b);
}
static int maxdiff(const u8 *a, const u8 *b, long n)
{
  long k;
  int m = 0;
  for (k = 0; k < n; k++) { int v = abs(a[k] - b[k]); if (v > m) m = v; }
  return m;
}
static u8 *composite(Doc *d)
{
  u8 *o = (u8 *)malloc((size_t)d->w * d->h * 4);
  doc_composite(d, doc_box(d), o, (long)d->w * 4);
  return o;
}

/* ------------------------------------------------------------------ test pictures */
/* bands of a few colours with some noise and, if alpha, a transparent stripe and a soft edge */
static Doc *test_doc(i32 w, i32 h, int alpha, int ncol)
{
  Doc *d = doc_new(w, h, 0xFFFFFF, 0);
  static const u32 cols[8] = { 0xFF0000, 0x00A000, 0x2040FF, 0xFFFF00, 0x000000, 0xFFFFFF, 0x808080, 0xC0A060 };
  i32 x, y;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      u8 *p = d->lay[0]->px + ((size_t)y * w + x) * 4;
      u32 c = cols[((x / 5) + (y / 7) + (ncol > 8 ? (int)(rnd() % 3) : 0)) % (ncol < 8 ? ncol : 8)];
      p[0] = (u8)(c >> 16); p[1] = (u8)(c >> 8); p[2] = (u8)c; p[3] = 255;
      if (ncol > 8) { p[0] = (u8)(p[0] ^ (rnd() & 15)); p[1] = (u8)(p[1] + x); p[2] = (u8)(p[2] + y * 3); }
      if (alpha && x % 11 == 3) p[3] = 0;
      if (alpha && x % 11 == 4) p[3] = (u8)(y * 255 / h);
    }
  d->dpi = 150;
  return d;
}

/* ------------------------------------------------------------------ TIFF */
static void tiff_tests(void)
{
  static const char *const modes[] = { "rgb", "rgba", "gray", "graya", "indexed", "bitmap" };
  int comp, m;
  char name[128];
  for (m = 0; m < 6; m++) {
    Doc *d = test_doc(37, 29, m == 1 || m == 3, m == 4 ? 6 : 9);
    u8 *ref;
    if (m == 2 || m == 3) doc_to_gray(d);
    if (m == 4) ok("tiff: indexed", doc_to_indexed(d, 16, QM_MEDIAN, DI_NONE, NULL, 0) && d->mode == MODE_INDEXED);
    if (m == 5) doc_to_bitmap(d, BMM_THRESHOLD);
    ref = doc_flat_rgba(d);
    if (m == 5) rgba_flatten_white(&(Rgba){ d->w, d->h, ref });
    snprintf(name, sizeof name, "ref_%s.rgba", modes[m]);
    save_rgba(name, ref, d->w, d->h);
    for (comp = 0; comp < TIFFC_N; comp++) {
      size_t len = 0;
      u8 *f = tiff_encode_doc(d, comp, &len);
      Rgba im;
      char err[128];
      snprintf(name, sizeof name, "tiff %s %s", modes[m], tiff_comp_names[comp]);
      ok(name, f != NULL && codec_detect(f, len) == FMT_TIFF);
      if (!f) continue;
      ok(name, codec_decode(f, len, &im, err, sizeof err) && im.w == d->w && im.h == d->h && maxdiff(im.px, ref, (long)im.w * im.h * 4) == 0);
      if (im.px && maxdiff(im.px, ref, (long)im.w * im.h * 4)) printf("  %s: off by %d (%s)\n", name, maxdiff(im.px, ref, (long)im.w * im.h * 4), err);
      rgba_free(&im);
      snprintf(name, sizeof name, "%s_%s.tif", modes[m], tiff_comp_names[comp]);
      save(name, f, len);
      /* every truncation fails cleanly or gives a picture */
      {
        size_t k;
        for (k = 0; k < len; k += 1 + len / 50) { Rgba t; if (codec_decode(f, k, &t, NULL, 0)) rgba_free(&t); }
      }
      free(f);
    }
    free(ref);
    doc_free(d);
  }
  /* a big noisy picture: LZW's table fills and is cleared many times; a smooth one compresses */
  {
    Rgba im, back;
    i32 i;
    size_t len, rawlen;
    u8 *f;
    rgba_alloc(&im, 300, 211);
    for (i = 0; i < 300 * 211 * 4; i++) im.px[i] = (u8)(i % 4 == 3 ? 255 : rnd());
    for (i = 0; i < TIFFC_N; i++) {
      f = tiff_encode(&im, i, 0, &len);
      ok("tiff noisy", f && codec_decode(f, len, &back, NULL, 0) && maxdiff(back.px, im.px, 300L * 211 * 4) == 0);
      rgba_free(&back); free(f);
    }
    for (i = 0; i < 300 * 211 * 4; i++) im.px[i] = (u8)(i % 4 == 3 ? (i / 1200) : (i / 4) % 300 / 3);
    f = tiff_encode(&im, TIFFC_NONE, 72, &rawlen); free(f);
    f = tiff_encode(&im, TIFFC_LZW, 72, &len);
    ok("tiff LZW compresses", f && len < rawlen / 4);
    ok("tiff LZW smooth RGBA", f && codec_decode(f, len, &back, NULL, 0) && maxdiff(back.px, im.px, 300L * 211 * 4) == 0);
    if (f) save("smooth_lzw.tif", f, len);
    save_rgba("ref_smooth.rgba", im.px, im.w, im.h);
    rgba_free(&back); free(f);
    f = tiff_encode(&im, TIFFC_PACKBITS, 72, &len);
    ok("tiff PackBits compresses", f && len < rawlen);
    free(f);
    rgba_free(&im);
  }
  /* one-pixel pictures and odd widths */
  {
    Rgba im, back;
    int c;
    rgba_alloc(&im, 1, 1);
    im.px[0] = 9; im.px[1] = 8; im.px[2] = 7; im.px[3] = 255;
    for (c = 0; c < TIFFC_N; c++) {
      size_t len;
      u8 *f = tiff_encode(&im, c, 0, &len);
      ok("tiff 1x1", f && codec_decode(f, len, &back, NULL, 0) && !memcmp(back.px, im.px, 4));
      rgba_free(&back); free(f);
    }
    rgba_free(&im);
  }
}

/* ------------------------------------------------------------------ Targa */
static void tga_tests(void)
{
  static const char *const modes[] = { "rgb", "rgba", "gray", "graya", "indexed", "indexed_trans" };
  int m, rle;
  char name[128];
  for (m = 0; m < 6; m++) {
    Doc *d = test_doc(41, 23, m == 1 || m == 3 || m == 5, m >= 4 ? 7 : 9);
    u8 *ref;
    if (m == 2 || m == 3) doc_to_gray(d);
    if (m >= 4) doc_to_indexed(d, 32, QM_MEDIAN, DI_NONE, NULL, 0);
    ref = doc_flat_rgba(d);
    if (m == 5) {                                        /* colour-mapped: alpha is all or nothing */
      long k;
      for (k = 0; k < (long)d->w * d->h; k++) if (ref[k * 4 + 3] < 128) memset(ref + k * 4, 0, 4); else ref[k * 4 + 3] = 255;
    }
    for (rle = 0; rle < 2; rle++) {
      size_t len = 0;
      u8 *f = tga_write_doc(d, rle, &len);
      Rgba im;
      int ok2;
      snprintf(name, sizeof name, "tga %s %s", modes[m], rle ? "rle" : "raw");
      ok(name, f && codec_detect(f, len) == FMT_TGA);
      if (!f) continue;
      ok2 = codec_decode(f, len, &im, NULL, 0) && im.w == d->w && im.h == d->h;
      if (ok2 && m == 5) { long k; for (k = 0; k < (long)im.w * im.h; k++) if (!im.px[k * 4 + 3]) memset(im.px + k * 4, 0, 4); }
      ok(name, ok2 && maxdiff(im.px, ref, (long)im.w * im.h * 4) == 0);
      rgba_free(&im);
      snprintf(name, sizeof name, "%s_%s.tga", modes[m], rle ? "rle" : "raw");
      save(name, f, len);
      { size_t k; for (k = 0; k < len; k += 1 + len / 40) { Rgba t; if (codec_decode(f, k, &t, NULL, 0)) rgba_free(&t); } }
      free(f);
    }
    snprintf(name, sizeof name, "tref_%s.rgba", modes[m]);
    save_rgba(name, ref, d->w, d->h);
    free(ref);
    doc_free(d);
  }
  /* runs longer than a packet, and RLE beats raw on flat colour */
  {
    Rgba im, back;
    size_t l0, l1;
    u8 *f0, *f1;
    long k;
    rgba_alloc(&im, 300, 5);
    for (k = 0; k < 300 * 5; k++) { im.px[k * 4] = (u8)(k / 200); im.px[k * 4 + 3] = 255; }
    f0 = tga_encode(&im, 0, &l0); f1 = tga_encode(&im, 1, &l1);
    ok("tga rle smaller", f0 && f1 && l1 < l0 / 10);
    ok("tga long runs", f1 && codec_decode(f1, l1, &back, NULL, 0) && maxdiff(back.px, im.px, 300L * 5 * 4) == 0);
    rgba_free(&back); free(f0); free(f1); rgba_free(&im);
  }
}

/* ------------------------------------------------------------------ PSD */
/* a layer of a coloured rectangle (transparent outside) */
static Layer *rect_layer(Doc *d, const char *name, i32 x0, i32 y0, i32 x1, i32 y1, u32 c, int a)
{
  Layer *l = layer_new(d, name);
  i32 x, y;
  for (y = y0; y < y1; y++)
    for (x = x0; x < x1; x++) {
      u8 *p = l->px + ((size_t)y * d->w + x) * 4;
      p[0] = (u8)(c >> 16); p[1] = (u8)(c >> 8); p[2] = (u8)(c ^ (u32)(x * 7 + y)); p[3] = (u8)(a ? a : 40 + (x * 13 + y * 5) % 216);
    }
  return l;
}
static Layer *group_layer(const char *name)
{
  Layer *l = (Layer *)calloc(1, sizeof *l);
  strcpy(l->name, name);
  l->kind = LK_GROUP; l->opacity = 255; l->visible = 1;
  return l;
}
/* the layered document the PSD tests write: from the bottom,
     0 Background
     1 "Ünïcödé layer" multiply 50%, fill 60%, locked transparency
     2 "Clipped" clipped to 1, hidden
     3   "Inner A" (depth 2)
     4   "Masked" (depth 2) with a mask, density 80%, feather 3, unlinked
     5  "Inner" group (depth 1): screen 78%, collapsed
     6  "Outer B" (depth 1), linear light
     7 "Outer" group (depth 0): pass through
     8 "Top" difference, mask turned off */
static Doc *psd_doc(void)
{
  Doc *d = doc_new(48, 40, 0x406080, 0);
  Layer *l;
  i32 x, y;
  strcpy(d->lay[0]->name, "Background");
  for (y = 0; y < 40; y++) for (x = 0; x < 48; x++) d->lay[0]->px[((size_t)y * 48 + x) * 4 + 1] = (u8)(x * 5);
  l = rect_layer(d, "\303\234n\303\257c\303\266d\303\251 layer", 4, 3, 30, 20, 0xFF2000, 0);
  l->blend = BM_MULTIPLY; l->opacity = 128; l->fill_cut = 255 - 153; l->alpha_lock = 1;
  d->lay[d->nlay++] = l;
  l = rect_layer(d, "Clipped", 10, 10, 40, 30, 0x20FF40, 255);
  l->clip = 1; l->visible = 0; l->lock_pos = 1;
  d->lay[d->nlay++] = l;
  l = rect_layer(d, "Inner A", 0, 0, 48, 12, 0x0000FF, 0); l->depth = 2; l->blendif[0] = 20; l->blendif[3] = 30;
  d->lay[d->nlay++] = l;
  l = rect_layer(d, "Masked", 6, 6, 44, 36, 0xFFFF00, 200); l->depth = 2;
  l->mask = (u8 *)malloc(48 * 40);
  for (y = 0; y < 40; y++) for (x = 0; x < 48; x++) l->mask[y * 48 + x] = (u8)(x > 20 && y > 15 ? (x * 9) & 255 : 255);
  l->mask_on = 1; l->mask_linked = 0; l->mask_fade = 255 - 204; l->mask_feather = 3; l->locked = 1;
  d->lay[d->nlay++] = l;
  l = group_layer("Inner"); l->depth = 1; l->blend = BM_SCREEN; l->opacity = 200; l->collapsed = 1;
  d->lay[d->nlay++] = l;
  l = rect_layer(d, "Outer B", 20, 0, 48, 40, 0x903050, 0); l->depth = 1; l->blend = BM_LINEARLIGHT;
  d->lay[d->nlay++] = l;
  l = group_layer("Outer"); l->blend = BM_PASS;
  d->lay[d->nlay++] = l;
  l = rect_layer(d, "Top", 0, 25, 48, 40, 0x10A0A0, 0); l->blend = BM_DIFFERENCE; l->lock_all = 1;
  l->mask = (u8 *)malloc(48 * 40);
  for (y = 0; y < 40; y++) for (x = 0; x < 48; x++) l->mask[y * 48 + x] = (u8)(y < 30 ? 0 : 255);
  l->mask_on = 0; l->mask_linked = 1;
  d->lay[d->nlay++] = l;
  d->active = d->nlay - 1;
  d->dpi = 240;
  return d;
}
static void compare_layers(Doc *a, Doc *b, const char *what)
{
  int i;
  char name[160];
  snprintf(name, sizeof name, "%s: layer count %d vs %d", what, a->nlay, b ? b->nlay : -1);
  ok(name, b && a->nlay == b->nlay);
  if (!b || a->nlay != b->nlay) return;
  for (i = 0; i < a->nlay; i++) {
    Layer *p = a->lay[i], *q = b->lay[i];
    snprintf(name, sizeof name, "%s: layer %d (%s) ", what, i, p->name);
    ok(strcat(name, "name"), !strcmp(p->name, q->name));
    name[strlen(name) - 4] = 0;
    ok(strcat(name, "kind/depth"), p->kind == q->kind && p->depth == q->depth);
    name[strlen(name) - 10] = 0;
    ok(strcat(name, "props"), p->opacity == q->opacity && p->blend == q->blend && p->visible == q->visible && p->clip == q->clip &&
       p->fill_cut == q->fill_cut && p->collapsed == q->collapsed);
    name[strlen(name) - 5] = 0;
    ok(strcat(name, "locks"), p->alpha_lock == q->alpha_lock && p->locked == q->locked && p->lock_pos == q->lock_pos && p->lock_all == q->lock_all);
    name[strlen(name) - 5] = 0;
    ok(strcat(name, "blend if"), !memcmp(p->blendif, q->blendif, 4));
    name[strlen(name) - 8] = 0;
    if (p->px) ok(strcat(name, "pixels"), q->px && !memcmp(p->px, q->px, (size_t)a->w * a->h * 4));
    else ok(strcat(name, "no pixels"), !q->px);
    name[strlen(name) - (p->px ? 6 : 9)] = 0;
    ok(strcat(name, "mask"), !p->mask == !q->mask && (!p->mask || (!memcmp(p->mask, q->mask, (size_t)a->w * a->h) &&
       p->mask_on == q->mask_on && p->mask_linked == q->mask_linked && p->mask_fade == q->mask_fade && p->mask_feather == q->mask_feather)));
  }
}

/* big-endian helpers for the hand-made files */
static void p16(Buf *b, u32 v) { buf_addc(b, (int)((v >> 8) & 255)); buf_addc(b, (int)(v & 255)); }
static void p32(Buf *b, u32 v) { p16(b, v >> 16); p16(b, v & 0xFFFF); }

/* a 16-bit grey PSD whose one layer (raw samples, a 2x2 rect at 1, 1) sits in an Lr16 block */
static u8 *psd16(size_t *len)
{
  Buf b, li;
  int c, k;
  buf_init(&b); buf_init(&li);
  /* the layer info: one layer, channels -1 and 0, raw */
  p16(&li, 1);
  p32(&li, 1); p32(&li, 1); p32(&li, 3); p32(&li, 3);
  p16(&li, 2);
  p16(&li, 0xFFFF); p32(&li, 2 + 8);
  p16(&li, 0); p32(&li, 2 + 8);
  buf_add(&li, "8BIMscrn", 8); buf_addc(&li, 100); buf_addc(&li, 0); buf_addc(&li, 0); buf_addc(&li, 0);
  p32(&li, 4 + 4 + 4);
  p32(&li, 0); p32(&li, 0);
  buf_addc(&li, 3); buf_add(&li, "Sixteen", 3);         /* a Pascal name, four bytes in all */
  p16(&li, 0); for (k = 0; k < 4; k++) p16(&li, 0xFFFF);                  /* alpha: opaque */
  p16(&li, 0); p16(&li, 0x0000); p16(&li, 0x8080); p16(&li, 0xFFFF); p16(&li, 0x4000);
  buf_add(&b, "8BPS\0\1\0\0\0\0\0\0", 12);
  p16(&b, 1); p32(&b, 4); p32(&b, 4); p16(&b, 16); p16(&b, 1);
  p32(&b, 0); p32(&b, 0);
  p32(&b, 4 + 4 + 12 + (u32)li.len);                     /* the section: empty layer info, no global mask, then Lr16 */
  p32(&b, 0); p32(&b, 0);
  buf_add(&b, "8BIMLr16", 8); p32(&b, (u32)li.len); buf_add(&b, li.data, li.len);
  p16(&b, 0);
  for (c = 0; c < 16; c++) p16(&b, 0x2000);              /* the composite: grey 32 */
  buf_free(&li);
  *len = b.len;
  return (u8 *)buf_take(&b);
}

static void psd_tests(void)
{
  Doc *d = psd_doc(), *e;
  size_t len;
  u8 *f = psd_write_doc(d, &len), *c0, *c1;
  char err[160];
  Rgba flat;
  ok("psd written", f != NULL && codec_detect(f, len) == FMT_PSD);
  if (!f) { doc_free(d); return; }
  save("layers.psd", f, len);
  e = codec_load_doc(f, len, err, sizeof err);
  compare_layers(d, e, "psd");
  ok("psd dpi", e && e->dpi == 240);
  c0 = composite(d);
  save_rgba("psd_composite.rgba", c0, d->w, d->h);
  if (e) { c1 = composite(e); ok("psd composite after reading", maxdiff(c0, c1, (long)d->w * d->h * 4) == 0); free(c1); }
  /* the merged image (as a reader without layers sees it): matted on white, unmatted again */
  ok("psd merged image", psd_read(f, len, &flat, err, sizeof err) && flat.w == d->w);
  if (flat.px) {
    long k, bad = 0;
    for (k = 0; k < (long)d->w * d->h; k++) {
      const u8 *p = flat.px + k * 4, *q = c0 + k * 4;
      if (p[3] != q[3] || (q[3] > 16 && (abs(p[0] - q[0]) > 255 / q[3] + 1 || abs(p[1] - q[1]) > 255 / q[3] + 1 || abs(p[2] - q[2]) > 255 / q[3] + 1))) bad++;
    }
    ok("psd merged matches composite", bad == 0);
  }
  rgba_free(&flat);
  free(c0);
  if (e) doc_free(e);
  /* truncated and damaged: never a crash */
  {
    size_t k;
    u8 *g = (u8 *)malloc(len);
    for (k = 0; k < len; k += 1 + len / 300) {
      Doc *t = codec_load_doc(f, k, NULL, 0);
      if (t) doc_free(t);
    }
    for (k = 0; k < 400; k++) {
      size_t at = rnd() % len;
      Doc *t;
      memcpy(g, f, len);
      g[at] ^= (u8)(1 + rnd() % 255);
      if (k & 1) g[(at + 1) % len] = 0xFF;
      if ((t = codec_load_doc(g, len, NULL, 0)) != NULL) doc_free(t);
    }
    free(g);
  }
  free(f);
  /* a fill layer goes as its pixels, an adjustment layer as an empty one */
  {
    FillParams fp;
    AdjParams ap;
    int i;
    memset(&fp, 0, sizeof fp); fp.c0 = 0x123456;
    memset(&ap, 0, sizeof ap);
    i = doc_new_fill(d, &fp, "Solid");
    doc_new_adjust(d, &ap, "Adjust");
    f = psd_write_doc(d, &len);
    e = f ? codec_load_doc(f, len, err, sizeof err) : NULL;
    ok("psd fill and adjustment layers", e && e->nlay == d->nlay && e->lay[i]->kind == LK_PIXEL && e->lay[i]->px[0] == 0x12 &&
       e->lay[i]->px[2] == 0x56 && e->lay[i]->px[3] == 255 && e->lay[i + 1]->kind == LK_PIXEL && !strcmp(e->lay[i + 1]->name, "Adjust"));
    if (e) doc_free(e);
    free(f);
  }
  doc_free(d);
  /* a 16-bit grey file with its layer in Lr16 */
  {
    u8 *g = psd16(&len);
    e = codec_load_doc(g, len, err, sizeof err);
    ok("psd 16-bit layers", e && e->nlay == 1 && !strcmp(e->lay[0]->name, "Six") && e->lay[0]->blend == BM_SCREEN && e->lay[0]->opacity == 100);
    if (e) {
      const u8 *p = e->lay[0]->px;
      ok("psd 16-bit samples", p[3] == 0 && p[(1 * 4 + 1) * 4] == 0 && p[(1 * 4 + 1) * 4 + 3] == 255 && p[(1 * 4 + 2) * 4 + 1] == 128 &&
         p[(2 * 4 + 1) * 4 + 2] == 255 && p[(2 * 4 + 2) * 4] == 64);
      doc_free(e);
    }
    free(g);
  }
  /* a flat file (no layer section) opens from its composite */
  {
    Buf b;
    int k;
    buf_init(&b);
    buf_add(&b, "8BPS\0\1\0\0\0\0\0\0", 12);
    p16(&b, 3); p32(&b, 2); p32(&b, 3); p16(&b, 8); p16(&b, 3);
    p32(&b, 0); p32(&b, 0); p32(&b, 0);
    p16(&b, 0);
    for (k = 0; k < 18; k++) buf_addc(&b, k * 10);
    e = codec_load_doc((u8 *)b.data, b.len, err, sizeof err);
    ok("psd flat", e && e->nlay == 1 && e->w == 3 && e->h == 2 && e->lay[0]->px[4] == 10 && e->lay[0]->px[1] == 60 && e->lay[0]->px[2] == 120);
    if (e) doc_free(e);
    buf_free(&b);
  }
}

/* ------------------------------------------------------------------ EXIF orientation */
/* where pixel (x, y) of the upright picture comes from in the stored one (w x h stored) */
static void orient_src(int o, i32 w, i32 h, i32 x, i32 y, i32 *sx, i32 *sy)
{
  switch (o) {
  case 2: *sx = w - 1 - x; *sy = y; break;
  case 3: *sx = w - 1 - x; *sy = h - 1 - y; break;
  case 4: *sx = x; *sy = h - 1 - y; break;
  case 5: *sx = y; *sy = x; break;
  case 6: *sx = y; *sy = h - 1 - x; break;
  case 7: *sx = w - 1 - y; *sy = h - 1 - x; break;
  case 8: *sx = w - 1 - y; *sy = x; break;
  default: *sx = x; *sy = y;
  }
}
/* a JPEG with an Exif APP1 holding only the Orientation, in either byte order */
static u8 *with_exif(const u8 *j, size_t n, int o, int le, size_t *len)
{
  u8 *r = (u8 *)malloc(n + 64), seg[40];
  size_t at = 20, sl = 0;
  memset(seg, 0, sizeof seg);
  memcpy(seg, "Exif\0\0", 6); sl = 6;
  if (le) { memcpy(seg + sl, "II\052\0\010\0\0\0", 8); } else memcpy(seg + sl, "MM\0\052\0\0\0\010", 8);
  sl += 8;
  if (le) { seg[sl] = 1; seg[sl + 2] = 0x12; seg[sl + 3] = 0x01; seg[sl + 4] = 3; seg[sl + 6] = 1; seg[sl + 10] = (u8)o; }
  else { seg[sl + 1] = 1; seg[sl + 2] = 0x01; seg[sl + 3] = 0x12; seg[sl + 5] = 3; seg[sl + 9] = 1; seg[sl + 11] = (u8)o; }
  sl += 2 + 12 + 4;
  memcpy(r, j, at);
  r[at] = 0xFF; r[at + 1] = 0xE1; r[at + 2] = (u8)((sl + 2) >> 8); r[at + 3] = (u8)(sl + 2);
  memcpy(r + at + 4, seg, sl);
  memcpy(r + at + 4 + sl, j + at, n - at);
  *len = n + 4 + sl;
  return r;
}
static void exif_tests(void)
{
  static const u32 cols[6] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF };
  Rgba im, back;
  size_t n, len;
  u8 *j;
  int o, le;
  i32 x, y;
  /* 3 x 2 blocks of 8 x 8, one colour each */
  rgba_alloc(&im, 24, 16);
  for (y = 0; y < 16; y++)
    for (x = 0; x < 24; x++) {
      u32 c = cols[(y / 8) * 3 + x / 8];
      u8 *p = im.px + ((size_t)y * 24 + x) * 4;
      p[0] = (u8)(c >> 16); p[1] = (u8)(c >> 8); p[2] = (u8)c; p[3] = 255;
    }
  j = jpeg_encode(&im, 95, 0, &n);
  for (le = 0; le < 2; le++)
    for (o = 1; o <= 8; o++) {
      u8 *f = with_exif(j, n, o, le, &len);
      char name[64];
      int good;
      snprintf(name, sizeof name, "exif orientation %d %s", o, le ? "II" : "MM");
      ok(name, jpeg_orientation(f, len) == o);
      good = codec_decode(f, len, &back, NULL, 0) && back.w == (o >= 5 ? 16 : 24) && back.h == (o >= 5 ? 24 : 16);
      if (good)
        for (y = 4; y < back.h; y += 8)
          for (x = 4; x < back.w; x += 8) {
            i32 sx, sy;
            const u8 *p = back.px + ((size_t)y * back.w + x) * 4, *q;
            orient_src(o, 24, 16, x, y, &sx, &sy);
            q = im.px + ((size_t)sy * 24 + sx) * 4;
            if (abs(p[0] - q[0]) > 24 || abs(p[1] - q[1]) > 24 || abs(p[2] - q[2]) > 24) good = 0;
          }
      ok(name, good);
      rgba_free(&back);
      if (o == 6 && !le) {
        Doc *d = codec_load_doc_scaled(f, len, 12, 8, NULL, 0);
        ok("exif scaled", d && d->w == 8 && d->h == 12);
        if (d) doc_free(d);
        save("exif6.jpg", f, len);
      }
      free(f);
    }
  ok("no exif is 1", jpeg_orientation(j, n) == 1);
  free(j);
  rgba_free(&im);
  /* rgba_orient on a random odd-sized picture against the definition */
  for (o = 1; o <= 8; o++) {
    Rgba a;
    int good = 1;
    rgba_alloc(&a, 13, 7);
    for (x = 0; x < 13 * 7 * 4; x++) a.px[x] = (u8)rnd();
    rgba_alloc(&back, 13, 7);
    memcpy(back.px, a.px, 13 * 7 * 4);
    rgba_orient(&back, o);
    if (back.w != (o >= 5 ? 7 : 13)) good = 0;
    for (y = 0; y < back.h && good; y++)
      for (x = 0; x < back.w; x++) {
        i32 sx, sy;
        orient_src(o, 13, 7, x, y, &sx, &sy);
        if (memcmp(back.px + ((size_t)y * back.w + x) * 4, a.px + ((size_t)sy * 13 + sx) * 4, 4)) good = 0;
      }
    ok("rgba_orient", good);
    rgba_free(&a); rgba_free(&back);
  }
}

/* ------------------------------------------------------------------ File Info */
static void set_info(DocInfo *f, int unicode)
{
  docinfo_clear(f);
  strcpy(f->title, unicode ? "Caf\303\251 <night> & \"day\"" : "Harbour at dusk");
  strcpy(f->author, unicode ? "J\303\274rgen \305\201ukasz" : "A. Painter");
  strcpy(f->description, "A picture\nof two lines");
  strcpy(f->keywords, "sea, boats, evening");
  strcpy(f->copyright, unicode ? "\302\251 2026 Somebody" : "(c) 2026 Somebody");
  strcpy(f->url, "https://example.org/pictures?a=1&b=2");
}
static int same_info(const DocInfo *a, const DocInfo *b)
{
  return !strcmp(a->title, b->title) && !strcmp(a->author, b->author) && !strcmp(a->description, b->description) &&
         !strcmp(a->keywords, b->keywords) && !strcmp(a->copyright, b->copyright) && !strcmp(a->url, b->url);
}
static void info_tests(void)
{
  static const char *const fmts[] = { "png", "jpeg", "tiff", "ora", "psd" };
  int u, k;
  for (u = 0; u < 2; u++)
    for (k = 0; k < 5; k++) {
      Doc *d = test_doc(20, 12, 0, 5), *e;
      size_t len = 0;
      u8 *f = NULL;
      char name[64], fn[64];
      DocInfo got;
      set_info(&d->info, u);
      switch (k) {
      case 0: f = png_write_doc(d, &len); break;
      case 1: f = jpeg_write_doc(d, 90, &len); break;
      case 2: f = tiff_encode_doc(d, TIFFC_LZW, &len); break;
      case 3: f = ora_write(d, &len); break;
      default: f = psd_write_doc(d, &len); break;
      }
      snprintf(name, sizeof name, "info %s %s", fmts[k], u ? "unicode" : "ascii");
      ok(name, f != NULL);
      if (!f) { doc_free(d); continue; }
      e = codec_load_doc(f, len, NULL, 0);
      ok(name, e && same_info(&d->info, &e->info));
      if (e && !same_info(&d->info, &e->info))
        printf("  got title '%s' author '%s' desc '%s' kw '%s' copy '%s' url '%s'\n", e->info.title, e->info.author, e->info.description,
               e->info.keywords, e->info.copyright, e->info.url);
      ok(name, codec_read_info(f, len, &got) && same_info(&d->info, &got));
      if (u) { snprintf(fn, sizeof fn, "info.%s", k == 1 ? "jpg" : k == 2 ? "tif" : fmts[k]); save(fn, f, len); }
      /* damaged copies: the metadata readers never read past the data */
      {
        u8 *g = (u8 *)malloc(len);
        int t;
        for (t = 0; t < 150; t++) {
          size_t at = rnd() % len;
          memcpy(g, f, len);
          g[at] = (u8)rnd(); g[(at + 7) % len] = (u8)rnd();
          codec_read_info(g, t & 1 ? len : at + 1, &got);
        }
        free(g);
      }
      /* the picture still reads as it did */
      if (k != 3) {
        Rgba im;
        ok(name, codec_decode(f, len, &im, NULL, 0) && im.w == 20 && im.h == 12);
        rgba_free(&im);
      }
      if (e) doc_free(e);
      free(f);
      doc_free(d);
    }
  /* a TIFF whose XMP is hidden: the Latin-1 ASCII tags come back as UTF-8 */
  {
    Doc *d = test_doc(9, 9, 0, 3);
    size_t len, k;
    u8 *f;
    DocInfo got;
    set_info(&d->info, 1);
    f = tiff_encode_doc(d, TIFFC_NONE, &len);
    for (k = 8; f && k + 4 <= len; k++) if (f[k] == 2 && f[k + 1] == 0xBC && f[k + 2] == 0 && f[k + 3] == 1) { f[k + 1] = 0xBB; break; }
    ok("info: TIFF ASCII tags", f && codec_read_info(f, len, &got) && !strcmp(got.title, d->info.title) && !strcmp(got.author, "J\303\274rgen ?ukasz") &&
       !strcmp(got.copyright, d->info.copyright) && !got.keywords[0]);
    free(f);
    doc_free(d);
  }
  /* nothing written when there is nothing to say, and nothing read back */
  {
    Doc *d = test_doc(8, 8, 0, 3);
    size_t l1, l2;
    u8 *a = png_write_doc(d, &l1), *b = doc_png_encode(d, d->dpi, &l2);
    DocInfo got;
    ok("info: empty PNG unchanged", a && b && l1 == l2 && !memcmp(a, b, l1));
    ok("info: none read", !codec_read_info(a, l1, &got) && docinfo_empty(&got));
    free(a); free(b);
    doc_free(d);
  }
  /* EXIF's Artist and Copyright fill in for a JPEG without XMP */
  {
    static const u8 exif[] = { 'E', 'x', 'i', 'f', 0, 0, 'M', 'M', 0, 42, 0, 0, 0, 8, 0, 2,
                               0x01, 0x3B, 0, 2, 0, 0, 0, 4, 'B', 'o', 'b', 0,
                               0x82, 0x98, 0, 2, 0, 0, 0, 7, 0, 0, 0, 38, 0, 0, 0, 0, 'C', 'o', 'p', 'y', 'r', '.', 0 };
    Rgba im;
    size_t n;
    u8 *j, *f;
    DocInfo got;
    rgba_alloc(&im, 8, 8);
    memset(im.px, 255, 8 * 8 * 4);
    j = jpeg_encode(&im, 80, 0, &n);
    f = (u8 *)malloc(n + sizeof exif + 4);
    memcpy(f, j, 20);
    f[20] = 0xFF; f[21] = 0xE1; f[22] = 0; f[23] = (u8)(sizeof exif + 2);
    memcpy(f + 24, exif, sizeof exif);
    memcpy(f + 24 + sizeof exif, j + 20, n - 20);
    ok("info: EXIF artist and copyright", codec_read_info(f, n + 4 + sizeof exif, &got) && !strcmp(got.author, "Bob") && !strcmp(got.copyright, "Copyr."));
    free(f); free(j); rgba_free(&im);
  }
  /* an XMP packet from elsewhere: attributes, several creators, Alt languages */
  {
    static const char x[] = "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
      "<rdf:Description rdf:about='' xmpRights:WebStatement='http://a.b/c'><dc:creator><rdf:Seq><rdf:li>One</rdf:li><rdf:li>Two</rdf:li></rdf:Seq></dc:creator>"
      "<dc:title><rdf:Alt><rdf:li xml:lang='x-default'>Main</rdf:li><rdf:li xml:lang='de'>Haupt</rdf:li></rdf:Alt></dc:title>"
      "</rdf:Description></rdf:RDF></x:xmpmeta>";
    DocInfo f;
    docinfo_clear(&f);
    ok("xmp parse", xmp_parse(x, sizeof x - 1, &f) && !strcmp(f.author, "One, Two") && !strcmp(f.title, "Main") && !strcmp(f.url, "http://a.b/c"));
  }
  /* a long field is cut between characters */
  {
    DocInfo f;
    char s[300];
    int i;
    for (i = 0; i < 149; i++) { s[i * 2] = (char)0xC3; s[i * 2 + 1] = (char)0xA9; }
    docinfo_set(f.title, sizeof f.title, s, 298);
    ok("docinfo_set cuts whole characters", strlen(f.title) == 126);
  }
}

int main(int argc, char **argv)
{
  char cmd[600];
  setvbuf(stdout, NULL, _IOLBF, 0);
  if (argc > 1) outdir = argv[1];
  snprintf(cmd, sizeof cmd, "mkdir -p '%s'", outdir);
  if (system(cmd)) printf("cannot make %s\n", outdir);
  tiff_tests();
  tga_tests();
  psd_tests();
  exif_tests();
  info_tests();
  printf("%d tests, %d failed\n", ntests, nfailed);
  return nfailed != 0;
}
