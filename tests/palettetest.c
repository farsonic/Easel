/*
 * palettetest.c - native tests for Easel 0.4's palettes and Amiga output: every quantizer and
 * dither, indexed documents (sync, palette edits, undo), colour cycling and CRNG/CCRT/DRNG, palette
 * files, the ILBM writer (planes, EHB, HAM6, HAM8, SHAM, 24-bit, mask, transparent colour) read back
 * through Easel's own readers, the GIF and palette/grey PNG writers, the screen-mode table, and
 * guides and snapping.
 *
 * It also writes its files to a folder (default /tmp/easel_pal_out, or argv[1]) with a manifest,
 * for tests/check_palette.py to check independently: Pillow decodes the GIFs, PNGs, .gpl and .act,
 * and a HAM decoder written in Python decodes the HAM ILBMs.
 *
 * Build and run from the easel folder:
 *   cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. -Iav1/include core/util.c core/zip.c \
 *      core/xml.c core/image.c core/doc.c core/codec.c core/png.c core/jpegenc.c core/bmp.c core/ilbm.c \
 *      core/gif.c core/ora.c core/avif.c core/rasters.c core/amigafmt.c core/tiff.c core/psd.c core/webp.c \
 *      core/palette.c core/gifenc.c core/amigamodes.c core/guides.c tests/palettetest.c \
 *      /tmp/easel_dav1d/libdav1d.a -o /tmp/palettetest -lm -lpthread
 *   /tmp/palettetest && python3 tests/check_palette.py
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core/codec.h"
#include "core/palette.h"
#include "core/amigamodes.h"
#include "core/guides.h"

static int ntests, nfailed;
static const char *outdir = "/tmp/easel_pal_out";
static FILE *manifest;
static void ok(const char *name, int cond)
{
  static int verbose = -1;
  if (verbose < 0) verbose = getenv("PALTEST_VERBOSE") != NULL;
  ntests++;
  if (!cond) { nfailed++; printf("FAIL: %s\n", name); }
  else if (verbose) printf("ok: %s\n", name);
}

/* ------------------------------------------------------------------ pictures */
static u32 seed = 1;
static int rnd(int n) { seed = seed * 1103515245UL + 12345; return (int)((seed >> 16) % (u32)n); }
/* smooth gradients, a few discs and a little noise: a stand-in for a photograph */
static u8 *make_photo(i32 w, i32 h)
{
  u8 *px = (u8 *)malloc((size_t)w * h * 4);
  i32 x, y;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      u8 *p = px + ((long)y * w + x) * 4;
      int r = x * 255 / (w - 1), g = y * 255 / (h - 1), b = (x + y) * 255 / (w + h - 2);
      long dx = x - w / 3, dy = y - h / 2, dx2 = x - 2 * w / 3, dy2 = y - h / 3;
      if (dx * dx + dy * dy < (long)(w / 5) * (w / 5)) { r = 230; g = 60 + y % 40; b = 40; }
      if (dx2 * dx2 + dy2 * dy2 < (long)(w / 6) * (w / 6)) { r = 30; g = 90; b = 200 - x % 50; }
      r += rnd(9) - 4; g += rnd(9) - 4; b += rnd(9) - 4;
      p[0] = (u8)(r < 0 ? 0 : r > 255 ? 255 : r); p[1] = (u8)(g < 0 ? 0 : g > 255 ? 255 : g); p[2] = (u8)(b < 0 ? 0 : b > 255 ? 255 : b); p[3] = 255;
    }
  return px;
}
/* n flat colours in blocks */
static u8 *make_few(i32 w, i32 h, int n, u8 *cols)
{
  u8 *px = (u8 *)malloc((size_t)w * h * 4);
  i32 x, y;
  int i;
  for (i = 0; i < n; i++) { cols[i * 3] = (u8)(37 * i + 11); cols[i * 3 + 1] = (u8)(91 * i + 50); cols[i * 3 + 2] = (u8)(53 * i + 200); }
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      u8 *p = px + ((long)y * w + x) * 4;
      int k = ((x / 5) + (y / 7) * 3) % n;
      p[0] = cols[k * 3]; p[1] = cols[k * 3 + 1]; p[2] = cols[k * 3 + 2]; p[3] = 255;
    }
  return px;
}
static double psnr(const u8 *a, const u8 *b, long n)
{
  double se = 0;
  long i;
  int c;
  for (i = 0; i < n; i++) for (c = 0; c < 3; c++) { double d = (double)a[i * 4 + c] - b[i * 4 + c]; se += d * d; }
  if (se == 0) return 99;
  return 10 * log10(255.0 * 255.0 * n * 3 / se);
}
/* the PSNR after an 8 x 8 box blur of both (how well a dither keeps the local average) */
static double blur_psnr(const u8 *a, const u8 *b, i32 w, i32 h)
{
  double se = 0;
  long cnt = 0;
  i32 x, y;
  int c;
  /* (8 x 8, the period of the largest ordered matrix) */
  for (y = 0; y + 8 <= h; y += 8)
    for (x = 0; x + 8 <= w; x += 8)
      for (c = 0; c < 3; c++) {
        int sa = 0, sb = 0, i, j;
        for (j = 0; j < 8; j++) for (i = 0; i < 8; i++) { sa += a[((long)(y + j) * w + x + i) * 4 + c]; sb += b[((long)(y + j) * w + x + i) * 4 + c]; }
        se += ((sa - sb) / 64.0) * ((sa - sb) / 64.0); cnt++;
      }
  if (se == 0) return 99;
  return 10 * log10(255.0 * 255.0 * cnt / se);
}
static Doc *doc_of(const u8 *px, i32 w, i32 h)
{
  u8 *c = (u8 *)malloc((size_t)w * h * 4);
  memcpy(c, px, (size_t)w * h * 4);
  return doc_adopt(w, h, c);
}
static void save(const char *name, const u8 *d, size_t n)
{
  char path[512];
  sprintf(path, "%s/%s", outdir, name);
  if (!file_write(path, d, n)) printf("(could not write %s)\n", path);
}
/* every layer's idx consistent with its RGB and the palette */
static int doc_consistent(Doc *d)
{
  int i;
  long k, n = (long)d->w * d->h;
  for (i = 0; i < d->nlay; i++) {
    Layer *l = d->lay[i];
    if (!l->px) continue;
    if (!l->idx) return 0;
    for (k = 0; k < n; k++) if (memcmp(l->px + k * 4, d->pal + l->idx[k] * 3, 3)) return 0;
  }
  return 1;
}

/* ------------------------------------------------------------------ quantizers and dithers */
static void quantizers(void)
{
  i32 w = 96, h = 80;
  long n = (long)w * h;
  u8 *src = make_photo(w, h), cols[64 * 3];
  int m, di, nc;
  char name[128];
  static const int counts[3] = { 256, 64, 16 };
  for (m = 0; m < 3; m++) {
    int ci;
    double lastp = 99;
    for (ci = 0; ci < 3; ci++) {
      Doc *d = doc_of(src, w, h);
      double p;
      ok("to_indexed runs", doc_to_indexed(d, counts[ci], m, DI_NONE, NULL, 0));
      p = psnr(src, d->lay[0]->px, n);
      sprintf(name, "%s %d colours: PSNR %.1f dB sane", quant_names[m], counts[ci], p);
      ok(name, p > (counts[ci] == 256 ? 30 : counts[ci] == 64 ? 26 : 20) && p <= lastp + 0.5);
      lastp = p;
      sprintf(name, "%s %d colours: palette size and mode", quant_names[m], counts[ci]);
      ok(name, d->mode == MODE_INDEXED && d->npal >= 2 && d->npal <= counts[ci] && doc_consistent(d));
      doc_free(d);
    }
    for (di = 0; di < DI_N; di++) {
      Doc *d = doc_of(src, w, h), *d0 = doc_of(src, w, h);
      double bp, bp0, p;
      doc_to_indexed(d, 16, m, di, NULL, 0);
      doc_to_indexed(d0, 16, m, DI_NONE, NULL, 0);
      p = psnr(src, d->lay[0]->px, n); bp = blur_psnr(src, d->lay[0]->px, w, h); bp0 = blur_psnr(src, d0->lay[0]->px, w, h);
      sprintf(name, "%s + %s at 16: PSNR %.1f, blurred %.1f (undithered %.1f)", quant_names[m], dither_names[di], p, bp, bp0);
      ok(name, doc_consistent(d) && p > 17 && (di == DI_NONE || bp > bp0 + 0.5));
      doc_free(d); doc_free(d0);
    }
    /* exact when there are few enough colours, dithered or not */
    {
      u8 *few = make_few(w, h, 7, cols);
      Doc *d = doc_of(few, w, h);
      doc_to_indexed(d, 16, m, DI_FS, NULL, 0);
      sprintf(name, "%s: 7 colours give an exact 7-entry palette", quant_names[m]);
      ok(name, d->npal == 7 && !memcmp(d->lay[0]->px, few, (size_t)n * 4));
      doc_free(d);
      d = doc_of(few, w, h);
      doc_to_indexed(d, 7, m | QM_OCS12, DI_BAYER4, NULL, 0);
      sprintf(name, "%s: exactly ncolors colours are kept exact (OCS rounding skipped)", quant_names[m]);
      ok(name, d->npal == 7 && !memcmp(d->lay[0]->px, few, (size_t)n * 4));
      doc_free(d);
      free(few);
    }
  }
  /* OCS 12-bit: every made colour a multiple of 17 */
  {
    Doc *d = doc_of(src, w, h);
    int i, all = 1;
    doc_to_indexed(d, 32, QM_MEDIAN | QM_OCS12, DI_NONE, NULL, 0);
    for (i = 0; i < d->npal * 3; i++) if (d->pal[i] % 17) all = 0;
    ok("OCS 12-bit palette: each gun a multiple of 17", all && d->npal == 32);
    doc_free(d);
  }
  /* fixed palettes */
  {
    u8 pal[768];
    int k;
    for (k = 0; k < PP_N; k++) {
      Doc *d = doc_of(src, w, h);
      int np = pal_preset(k, pal);
      sprintf(name, "preset %s (%d) applied", preset_names[k], np);
      ok(name, np >= 2 && doc_to_indexed(d, 0, QM_FIXED, DI_FS, pal, np) && d->npal == np && !memcmp(d->pal, pal, (size_t)np * 3) && doc_consistent(d));
      doc_free(d);
    }
  }
  /* locked and forced entries */
  {
    Doc *d = doc_of(src, w, h);
    u8 forced[6] = { 1, 2, 3, 250, 251, 252 };
    d->npal = 16; d->pal[5 * 3] = 0x12; d->pal[5 * 3 + 1] = 0x34; d->pal[5 * 3 + 2] = 0x56; d->pal_lock[5] = 1;
    doc_to_indexed(d, 16, QM_WU, DI_NONE, forced, 2);
    ok("locked entry 5 kept in place", d->pal[15] == 0x12 && d->pal[16] == 0x34 && d->pal[17] == 0x56 && d->pal_lock[5]);
    ok("forced colours take the first free places", !memcmp(d->pal, forced, 3) && !memcmp(d->pal + 3, forced + 3, 3));
    doc_free(d);
  }
  /* quantize_rgba and nearest */
  {
    u8 pal[768], *idx = (u8 *)malloc((size_t)n);
    nc = quantize_rgba(src, w, h, 32, QM_OCTREE, DI_SIERRA, pal, 0, idx);
    ok("quantize_rgba: octree to 32", nc >= 16 && nc <= 32);
    free(idx);
  }
  free(src);
}

/* ------------------------------------------------------------------ modes and undo */
static void modes(void)
{
  i32 w = 64, h = 48;
  long n = (long)w * h, k;
  u8 *src = make_photo(w, h);
  Doc *d = doc_of(src, w, h);
  int i, bw;
  doc_to_indexed(d, 32, QM_MEDIAN, DI_FS, NULL, 0);
  ok("indexed: one undo step", doc_can_undo(d) && !strcmp(doc_undo_name(d), "Indexed Color"));
  doc_undo(d);
  ok("undo: RGB again, pixels as they were, no indices", d->mode == MODE_RGB && !memcmp(d->lay[0]->px, src, (size_t)n * 4) && !d->lay[0]->idx);
  doc_redo(d);
  ok("redo: indexed again, consistent", d->mode == MODE_INDEXED && d->npal == 32 && doc_consistent(d));
  doc_to_rgb(d);
  ok("to RGB: no indices, pixels kept", d->mode == MODE_RGB && !d->lay[0]->idx);
  doc_undo(d);
  ok("undo to RGB: indexed", d->mode == MODE_INDEXED && doc_consistent(d));
  doc_free(d);
  d = doc_of(src, w, h);
  doc_to_gray(d);
  for (k = 0, i = 1; k < n; k++) if (d->lay[0]->px[k * 4] != d->lay[0]->px[k * 4 + 1] || d->lay[0]->px[k * 4] != d->lay[0]->px[k * 4 + 2]) i = 0;
  ok("grayscale: R = G = B", d->mode == MODE_GRAY && i);
  /* painting in grey mode: synced to grey */
  doc_begin(d, "paint"); doc_note_layer(d, 0, box_make(0, 0, 4, 4));
  memset(d->lay[0]->px, 0, 16); d->lay[0]->px[0] = 255; d->lay[0]->px[3] = 255;
  doc_index_sync(d, 0, box_make(0, 0, 4, 4)); doc_end(d);
  ok("grey sync: red painted becomes its grey", d->lay[0]->px[0] == d->lay[0]->px[1] && d->lay[0]->px[1] == d->lay[0]->px[2] && d->lay[0]->px[0] == 77);
  doc_free(d);
  for (i = 0; i < BMM_N; i++) {
    char name[96];
    d = doc_of(src, w, h);
    doc_to_bitmap(d, i);
    for (k = 0, bw = 1; k < n; k++) { const u8 *p = d->lay[0]->px + k * 4; if (!((p[0] == 0 && p[1] == 0 && p[2] == 0) || (p[0] == 255 && p[1] == 255 && p[2] == 255))) bw = 0; }
    sprintf(name, "bitmap %s: only black and white, 2 entries", bitmap_names[i]);
    ok(name, d->mode == MODE_BITMAP && d->npal == 2 && bw && doc_consistent(d));
    doc_free(d);
  }
  free(src);
}

/* ------------------------------------------------------------------ indexed sync and palette edits */
static void editing(void)
{
  i32 w = 40, h = 30;
  long n = (long)w * h, k;
  u8 cols[16 * 3], *few = make_few(w, h, 8, cols), before[768], *look;
  Doc *d = doc_of(few, w, h);
  int q, i, used;
  unsigned long counts[256];
  doc_to_indexed(d, 16, QM_MEDIAN, DI_NONE, NULL, 0);
  ok("8 colours exact", d->npal == 8);
  /* painting a box with a colour near entry 3, then syncing */
  doc_begin(d, "Paint");
  doc_note_layer(d, 0, box_make(2, 2, 10, 10));
  for (k = 0; k < 8; k++) { i32 y = 2 + (i32)k, x; for (x = 2; x < 10; x++) { u8 *p = d->lay[0]->px + ((long)y * w + x) * 4; p[0] = (u8)(d->pal[9] + 3); p[1] = (u8)(d->pal[10] - 2); p[2] = d->pal[11]; } }
  doc_index_sync(d, 0, box_make(2, 2, 10, 10));
  doc_end(d);
  ok("sync: the painted box snapped to entry 3", d->lay[0]->idx[5 * w + 5] == 3 && !memcmp(d->lay[0]->px + (5 * w + 5) * 4, d->pal + 9, 3) && doc_consistent(d));
  doc_undo(d);
  ok("undo of the paint: pixels and indices back", !memcmp(d->lay[0]->px, few, (size_t)n * 4) && doc_consistent(d));
  /* a palette edit recolours every pixel of that entry, and undoes */
  q = d->lay[0]->idx[0];
  memcpy(before, d->pal, 768);
  pal_set_rgb(d, q, 1, 2, 3);
  ok("set entry: pixels of it recoloured", d->lay[0]->px[0] == 1 && d->lay[0]->px[1] == 2 && d->lay[0]->px[2] == 3 && doc_consistent(d));
  doc_undo(d);
  ok("undo set entry: palette and pixels back", !memcmp(d->pal, before, 768) && !memcmp(d->lay[0]->px, few, (size_t)n * 4));
  doc_redo(d);
  ok("redo set entry", d->lay[0]->px[0] == 1 && doc_consistent(d));
  doc_undo(d);
  pal_set_hsv(d, 1, 120, 255, 255);
  ok("set entry by HSV: pure green", d->pal[3] == 0 && d->pal[4] == 255 && d->pal[5] == 0 && doc_consistent(d));
  doc_undo(d);
  /* spread */
  pal_spread(d, 0, 4);
  ok("spread: entry 2 halfway between 0 and 4", abs(d->pal[6] - (before[0] + before[12]) / 2) <= 1 && abs(d->pal[7] - (before[1] + before[13]) / 2) <= 1 && doc_consistent(d));
  doc_undo(d);
  /* exchange: colours only, then remapped */
  look = (u8 *)malloc((size_t)n * 4);
  memcpy(look, d->lay[0]->px, (size_t)n * 4);
  pal_swap(d, 0, 1, 0);
  ok("exchange colours: entries swapped, the picture changes", !memcmp(d->pal, before + 3, 3) && memcmp(d->lay[0]->px, look, (size_t)n * 4) && doc_consistent(d));
  doc_undo(d);
  pal_swap(d, 0, 1, 1);
  ok("exchange with remap: the picture looks the same", !memcmp(d->pal, before + 3, 3) && !memcmp(d->lay[0]->px, look, (size_t)n * 4) && doc_consistent(d));
  doc_undo(d);
  pal_copy(d, 2, 5);
  ok("copy: entry 5 = entry 2", !memcmp(d->pal + 15, d->pal + 6, 3) && doc_consistent(d));
  doc_undo(d);
  pal_sort(d, PS_LUM);
  for (i = 1, k = 1; i < d->npal; i++) if ((d->pal[i * 3] * 77 + d->pal[i * 3 + 1] * 150 + d->pal[i * 3 + 2] * 29) < (d->pal[i * 3 - 3] * 77 + d->pal[i * 3 - 2] * 150 + d->pal[i * 3 - 1] * 29)) k = 0;
  ok("sort by luminance: sorted, the picture unchanged", k && !memcmp(d->lay[0]->px, look, (size_t)n * 4) && doc_consistent(d));
  doc_undo(d);
  pal_sort(d, PS_HUE);
  ok("sort by hue: the picture unchanged", !memcmp(d->lay[0]->px, look, (size_t)n * 4) && doc_consistent(d));
  doc_undo(d);
  /* remove unused: make 4 entries unused first by remapping their pixels */
  used = pal_count_used(d, counts);
  ok("count used: 8", used == 8 && counts[0] > 0);
  d->npal = 12;
  ok("remove unused: 4 taken out", pal_remove_unused(d) == 4 && d->npal == 8 && !memcmp(d->lay[0]->px, look, (size_t)n * 4) && doc_consistent(d));
  doc_undo(d);
  ok("undo remove unused", d->npal == 12);
  ok("count colours: 8", doc_count_colors(d, 1000) == 8);
  pal_make_ehb(d);
  ok("EHB: entries 32-63 halves", d->npal == 64 && d->pal[32 * 3] == d->pal[0] >> 1 && d->pal[63 * 3 + 2] == d->pal[31 * 3 + 2] >> 1);
  doc_undo(d);
  /* replace, remap */
  {
    u8 grey[768];
    int np = pal_preset(PP_GRAY16, grey);
    pal_replace(d, grey, np);
    ok("load palette (replace): indices kept, colours new", d->npal == 16 && !memcmp(d->lay[0]->px, grey + d->lay[0]->idx[0] * 3, 3) && doc_consistent(d));
    doc_undo(d);
    pal_remap(d, grey, np, DI_NONE);
    ok("remap to a grey palette: every pixel grey", d->npal == 16 && d->lay[0]->px[0] == d->lay[0]->px[1] && doc_consistent(d));
    doc_undo(d);
  }
  /* a second layer added to an indexed document, synced */
  {
    Layer *l = layer_new(d, "Top");
    for (k = 0; k < 100; k++) { l->px[k * 4] = 200; l->px[k * 4 + 1] = 10; l->px[k * 4 + 2] = 10; l->px[k * 4 + 3] = 255; }
    doc_add_layer(d, l, -1);
    doc_index_sync_all(d);
    ok("a new layer gets indices", d->lay[1]->idx && doc_consistent(d));
  }
  free(look); free(few);
  doc_free(d);
}

/* ------------------------------------------------------------------ cycling */
static void cycling(void)
{
  Doc *d = doc_new(16, 4, 0, 0);
  u8 cols[16 * 3];
  int i, k, nr;
  long x;
  CycleRange r[MAXCYCLES];
  Buf b;
  u32 acc[MAXCYCLES];
  for (i = 0; i < 16; i++) { cols[i * 3] = (u8)(i * 16); cols[i * 3 + 1] = (u8)(255 - i * 16); cols[i * 3 + 2] = 7; }
  for (x = 0; x < 64; x++) { u8 *p = d->lay[0]->px + x * 4; memcpy(p, cols + (x % 16) * 3, 3); }
  doc_to_indexed(d, 16, QM_MEDIAN, DI_NONE, NULL, 0);
  /* entries in pixel order: entry of pixel x is idx[x] */
  d->ncycle = 1; d->cycle[0].lo = 4; d->cycle[0].hi = 7; d->cycle[0].active = 1; d->cycle[0].rate = 16384; d->cycle[0].reverse = 0;
  {
    u8 e4[3], e7[3], idx0[64];
    memcpy(e4, d->pal + 12, 3); memcpy(e7, d->pal + 21, 3); memcpy(idx0, d->lay[0]->idx, 64);
    ok("cycle_step forward", cycle_step(d, 0, 1) && !memcmp(d->pal + 15, e4, 3) && !memcmp(d->pal + 12, e7, 3));
    ok("cycle_step: indices unchanged, pixels follow", !memcmp(d->lay[0]->idx, idx0, 64) && doc_consistent(d) && !doc_can_redo(d));
    cycle_step(d, 0, -1);
    ok("cycle_step back restores", !memcmp(d->pal + 12, e4, 3) && !memcmp(d->pal + 21, e7, 3));
    d->cycle[0].reverse = 1;
    cycle_step(d, 0, 1);
    ok("reverse flag turns it round", !memcmp(d->pal + 18, e7, 3) || !memcmp(d->pal + 21, e4, 3));
    d->cycle[0].reverse = 0;
  }
  ok("rate 16384: 17 ms", cycle_interval_ms(16384) == 17 && cycle_interval_ms(0) == 0);
  ok("rate 8192: 33 ms, back again", cycle_interval_ms(8192) == 33 && abs(cycle_rate_from_ms(33) - 8192) < 300);
  ok("60.00 Hz shown", cycle_rate_hz100(16384) == 6000);
  memset(acc, 0, sizeof acc);
  d->cycle[0].rate = 8192;                                     /* 30 steps a second */
  k = cycle_advance(d, acc, 100);
  ok("advance 100 ms at 30/s: 3 steps", k == 3);
  ok("timer interval", cycle_timer_ms(d) == 33);
  /* CRNG, CCRT, DRNG out and in */
  d->ncycle = 2;
  d->cycle[1].lo = 0; d->cycle[1].hi = 3; d->cycle[1].active = 0; d->cycle[1].rate = 1000; d->cycle[1].reverse = 1;
  for (k = 0; k < 3; k++) {
    buf_init(&b);
    buf_add(&b, "FORM\0\0\0\0ILBM", 12);
    cycle_write_chunks(&b, d->cycle, d->ncycle, k);
    { u32 l = (u32)b.len - 8; b.data[4] = (char)(l >> 24); b.data[5] = (char)(l >> 16); b.data[6] = (char)(l >> 8); b.data[7] = (char)l; }
    nr = cycle_read_iff((const u8 *)b.data, b.len, r, MAXCYCLES);
    if (k == 0) ok("CRNG round trip", nr == 2 && r[0].lo == 4 && r[0].hi == 7 && r[0].rate == 8192 && r[0].active && !r[0].reverse && r[1].reverse && !r[1].active && r[1].rate == 1000);
    if (k == 1) ok("CCRT round trip (rate within 2%)", nr == 2 && r[0].lo == 4 && r[0].hi == 7 && abs(r[0].rate - 8192) < 170 && r[0].active && !r[1].active && r[1].reverse == 0);  /* (an inactive CCRT has no direction) */
    if (k == 2) ok("DRNG round trip", nr == 2 && r[0].lo == 4 && r[0].hi == 7 && r[0].rate == 8192 && r[0].active && r[1].lo == 0 && r[1].hi == 3);
    buf_free(&b);
  }
  doc_free(d);
}

/* ------------------------------------------------------------------ palette files */
static void palette_files(void)
{
  PalFile p, q;
  int f, i, n;
  u8 *data;
  size_t len;
  char err[128], name[128];
  memset(&p, 0, sizeof p);
  for (i = 0; i < 256; i++) { p.rgb[i * 3] = (u8)i; p.rgb[i * 3 + 1] = (u8)(i * 7); p.rgb[i * 3 + 2] = (u8)(255 - i); }
  strcpy(p.name, "Test Palette");
  p.ncycle = 1; p.cycle[0].lo = 2; p.cycle[0].hi = 9; p.cycle[0].rate = 4000; p.cycle[0].active = 1;
  for (n = 16; n <= 256; n += 240) {
    p.n = n; p.transparent = n == 16 ? 3 : -1;
    for (f = 0; f < PF_N; f++) {
      char fname[64];
      if (!(data = palfile_write(f, &p, &len))) { ok("palette written", 0); continue; }
      sprintf(fname, "pal%d.%s", n, f == PF_RIFF ? "riff.pal" : f == PF_JASC ? "jasc.pal" : palfile_ext[f]);
      save(fname, data, len);
      if (manifest) fprintf(manifest, "palette %s %d %d\n", fname, f, n);
      ok("palette detected", palfile_detect(data, len, fname) == f);
      sprintf(name, "%s with %d entries round trip", palfile_names[f], n);
      ok(name, palfile_read(data, len, fname, &q, err, sizeof err) && q.n == (f == PF_ACT ? n : n) && !memcmp(q.rgb, p.rgb, (size_t)n * 3));
      if (f == PF_ACT || f == PF_PNG) ok("transparent index kept", q.transparent == p.transparent);
      if (f == PF_IFF) ok("IFF palette keeps CRNG", q.ncycle == 1 && q.cycle[0].lo == 2 && q.cycle[0].hi == 9 && q.cycle[0].rate == 4000);
      if (f == PF_GPL || f == PF_ASE) ok("palette name kept", !strcmp(q.name, "Test Palette"));
      /* damaged copies are refused or read safely */
      for (i = 0; i < (int)len; i += (int)(len / 13) + 1) { palfile_read(data, (size_t)i, fname, &q, err, sizeof err); }
      free(data);
    }
  }
  /* an ACO holding HSB, CMYK, Lab and grey swatches */
  {
    static const u8 aco[] = { 0, 1, 0, 4,
      0, 1, 0x55, 0x55, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0,          /* HSB: hue 120, full: green */
      0, 2, 0xFF, 0xFF, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF,          /* CMYK, 0 = full ink: no cyan, full magenta, no yellow, no black: magenta */
      0, 7, 0x27, 0x10, 0, 0, 0, 0, 0, 0,                      /* Lab 100, 0, 0: white */
      0, 8, 0x13, 0x88, 0, 0, 0, 0, 0, 0 };                    /* grey 5000 */
    ok("ACO other colour spaces", palfile_read(aco, sizeof aco, "x.aco", &q, err, sizeof err) && q.n == 4 &&
       q.rgb[0] == 0 && q.rgb[1] == 255 && q.rgb[2] == 0 && q.rgb[3] == 255 && q.rgb[4] == 0 && q.rgb[5] == 255 &&
       q.rgb[6] >= 253 && q.rgb[7] >= 253 && q.rgb[8] >= 253 && abs(q.rgb[9] - 127) <= 1);
  }
  /* the document's palette */
  {
    Doc *d = doc_new(8, 8, 0x123456, 0);
    doc_to_indexed(d, 4, QM_MEDIAN, DI_NONE, NULL, 0);
    palfile_from_doc(d, &q);
    ok("palette from a document", q.n == d->npal && !memcmp(q.rgb, d->pal, (size_t)q.n * 3));
    doc_free(d);
  }
}

/* ------------------------------------------------------------------ ILBM */
static int rgba_eq(const Rgba *a, const u8 *px, i32 w, i32 h, int cmp_alpha)
{
  long k;
  if (a->w != w || a->h != h) return 0;
  for (k = 0; k < (long)w * h; k++) if (memcmp(a->px + k * 4, px + k * 4, cmp_alpha ? 4 : 3)) return 0;
  return 1;
}
static void dump_rgba(const char *name, const Rgba *im)
{
  save(name, im->px, (size_t)im->w * im->h * 4);
}
static void ilbm(void)
{
  i32 w = 100, h = 60;
  long n = (long)w * h, k;
  u8 *src = make_photo(w, h), *data;
  size_t len;
  char err[128], name[160];
  IlbmOpts o;
  Rgba im, back;
  int planes, kind;
  im.w = w; im.h = h; im.px = src;
  /* indexed documents at every depth: exact, through both readers */
  for (planes = 1; planes <= 8; planes++) {
    Doc *d = doc_of(src, w, h), *r;
    u8 *flat;
    doc_to_indexed(d, 1 << planes, QM_MEDIAN, DI_FS, NULL, 0);
    d->ncycle = 1; d->cycle[0].lo = 0; d->cycle[0].hi = (u8)((1 << planes) - 1); d->cycle[0].rate = 5000; d->cycle[0].active = 1;
    d->modeid = 0x00029004; d->aspect_x = 10; d->aspect_y = 11;
    ilbm_opts_default(&o);
    data = ilbm_write_doc(d, &o, &len, err, sizeof err);
    flat = doc_flat_rgba(d);
    sprintf(name, "ILBM %d planes from an indexed doc: pixel-exact through ilbm_read", planes);
    ok(name, data && ilbm_read(data, len, &back, err, sizeof err) && rgba_eq(&back, flat, w, h, 1));
    rgba_free(&back);
    r = data ? ilbm_read_indexed(data, len, err, sizeof err) : NULL;
    sprintf(name, "ILBM %d planes: ilbm_read_indexed keeps palette, indices, cycles, CAMG, aspect", planes);
    ok(name, r && r->mode == MODE_INDEXED && r->npal == (1 << planes) && !memcmp(r->pal, d->pal, (size_t)d->npal * 3) &&
       !memcmp(r->lay[0]->idx, d->lay[0]->idx, (size_t)n) && r->ncycle == 1 && r->cycle[0].hi == (1 << planes) - 1 && r->cycle[0].rate == 5000 &&
       r->modeid == 0x00029004 && r->aspect_x == 10 && r->aspect_y == 11 && ilbm_indexable(data, len) && doc_consistent(r));
    if (planes == 4 && data) {
      /* damaged and truncated copies: refused or read safely (under ASan) */
      size_t cut;
      u8 *bad = (u8 *)malloc(len);
      for (cut = 0; cut < len; cut += len / 37 + 1) { Doc *x = ilbm_read_indexed(data, cut, err, sizeof err); if (x) doc_free(x); }
      for (cut = 12; cut < len && cut < 200; cut += 3) {
        Doc *x;
        memcpy(bad, data, len); bad[cut] ^= 0xA5;
        if ((x = ilbm_read_indexed(bad, len, err, sizeof err)) != NULL) doc_free(x);
      }
      free(bad);
      ok("damaged ILBMs read safely", 1);
    }
    if (planes == 5 && data) { save("idx5.iff", data, len); if (manifest) fprintf(manifest, "ilbm idx5.iff idx5.rgba\n"); back.w = w; back.h = h; back.px = flat; dump_rgba("idx5.rgba", &back); }
    if (r) doc_free(r);
    free(flat); free(data); doc_free(d);
  }
  /* EHB: an indexed doc whose upper 32 are the halves, exact */
  {
    Doc *d = doc_of(src, w, h);
    u8 *flat;
    doc_to_indexed(d, 32, QM_WU, DI_NONE, NULL, 0);
    pal_make_ehb(d);
    for (k = 0; k < n; k += 3) d->lay[0]->idx[k] = (u8)(d->lay[0]->idx[k] + 32);
    doc_pal_changed(d, 0, 63);
    ilbm_opts_default(&o); o.kind = IK_EHB;
    data = ilbm_write_doc(d, &o, &len, err, sizeof err);
    flat = doc_flat_rgba(d);
    ok("EHB from an EHB-palette document: pixel-exact", data && ilbm_read(data, len, &back, err, sizeof err) && rgba_eq(&back, flat, w, h, 1));
    if (data) { save("ehb.iff", data, len); if (manifest) fprintf(manifest, "ilbm ehb.iff ehb.rgba\n"); dump_rgba("ehb.rgba", &back); }
    rgba_free(&back);
    { Doc *r = data ? ilbm_read_indexed(data, len, err, sizeof err) : NULL;
      ok("EHB read as indexed: 64 entries, halves", r && r->npal == 64 && r->pal[40 * 3] == r->pal[8 * 3] >> 1 && !memcmp(r->lay[0]->idx, d->lay[0]->idx, (size_t)n));
      if (r) doc_free(r); }
    free(flat); free(data); doc_free(d);
  }
  /* EHB, HAM6, HAM8, SHAM and 24-bit from RGB */
  for (kind = IK_EHB; kind < IK_N; kind++) {
    double p;
    static const char *const kn[IK_N] = { "normal", "EHB", "HAM6", "HAM8", "24-bit", "SHAM" };
    static const double minp[IK_N] = { 0, 28, 30, 36, 99, 31 };
    ilbm_opts_default(&o); o.kind = kind; o.dither = DI_NONE;
    data = ilbm_write(&im, NULL, 0, NULL, NULL, 0, &o, &len, err, sizeof err);
    if (!data) { ok("written", 0); continue; }
    if (kind == IK_SHAM) {
      ok("SHAM decodes through the line-palette reader", codec_decode(data, len, &back, err, sizeof err) == 1 && back.w == w);
    } else ok("decodes through ilbm_read", ilbm_read(data, len, &back, err, sizeof err) == 1 && back.w == w);
    p = back.px ? psnr(src, back.px, n) : 0;
    sprintf(name, "%s from RGB: PSNR %.1f dB (at least %.0f)", kn[kind], p, minp[kind]);
    ok(name, p >= minp[kind]);
    sprintf(name, "%s.iff", kn[kind]);
    save(name, data, len);
    if (manifest && back.px) { fprintf(manifest, "%s %s.iff %s.rgba src.rgba %.0f\n", kind == IK_HAM6 || kind == IK_HAM8 ? "ham" : kind == IK_SHAM ? "sham" : "ilbm", kn[kind], kn[kind], minp[kind]); }
    sprintf(name, "%s.rgba", kn[kind]);
    if (back.px) dump_rgba(name, &back);
    ok("not indexable when HAM", (kind == IK_HAM6 || kind == IK_HAM8 || kind == IK_SHAM || kind == IK_DEEP24) ? !ilbm_indexable(data, len) : ilbm_indexable(data, len));
    rgba_free(&back); free(data);
  }
  dump_rgba("src.rgba", &im);
  /* masks: a mask plane and a transparent colour carry alpha */
  {
    u8 *t = (u8 *)malloc((size_t)n * 4);
    int m;
    memcpy(t, src, (size_t)n * 4);
    for (k = 0; k < n; k++) if ((k % w) < 20) t[k * 4 + 3] = 0;
    for (m = IMK_PLANE; m <= IMK_TRANSPARENT; m++) {
      Rgba ti;
      int good = 1;
      ti.w = w; ti.h = h; ti.px = t;
      ilbm_opts_default(&o); o.mask = m; o.planes = 5;
      data = ilbm_write(&ti, NULL, 0, NULL, NULL, 0, &o, &len, err, sizeof err);
      ok("masked ILBM written", data != NULL);
      if (!data) continue;
      ok("masked ILBM read", ilbm_read(data, len, &back, err, sizeof err));
      for (k = 0; back.px && k < n; k++) if ((back.px[k * 4 + 3] == 0) != ((k % w) < 20)) good = 0;
      if (m == IMK_PLANE) {
        /* ilbm_read skips the mask plane (as before 0.4); the indexed reader turns it into alpha */
        Doc *r = ilbm_read_indexed(data, len, err, sizeof err);
        good = r != NULL;
        for (k = 0; r && k < n; k++) if ((r->lay[0]->px[k * 4 + 3] == 0) != ((k % w) < 20)) good = 0;
        if (r) doc_free(r);
      }
      ok(m == IMK_PLANE ? "mask plane carries alpha" : "transparent colour carries alpha", good);
      rgba_free(&back); free(data);
    }
    free(t);
  }
  /* planes chosen smaller than the palette: requantized; CAMG chosen from flags */
  {
    Doc *d = doc_of(src, w, h);
    doc_to_indexed(d, 64, QM_MEDIAN, DI_NONE, NULL, 0);
    ilbm_opts_default(&o); o.planes = 3; o.modeid = 0;
    data = ilbm_write_doc(d, &o, &len, err, sizeof err);
    { Doc *r = data ? ilbm_read_indexed(data, len, err, sizeof err) : NULL;
      ok("3 planes from a 64-colour doc: 8 entries", r && r->npal == 8);
      if (r) doc_free(r); }
    free(data); doc_free(d);
  }
  free(src);
}

/* ------------------------------------------------------------------ GIF and PNG writers */
static void writers(void)
{
  i32 w = 150, h = 130;
  long n = (long)w * h, k;
  u8 *src = make_photo(w, h), pal[768], *idx = (u8 *)malloc((size_t)n), *data, cols[64 * 3];
  size_t len;
  int np, il, depth;
  char name[96], err[128];
  Rgba back;
  /* noisy 256 colours (the LZW table fills and clears many times) */
  for (k = 0; k < n; k++) idx[k] = (u8)rnd(256);
  for (k = 0; k < 768; k++) pal[k] = (u8)rnd(256);
  for (il = 0; il < 2; il++) {
    data = gif_encode(idx, w, h, pal, 256, il ? 7 : -1, il, &len);
    sprintf(name, "noise256_%d.gif", il);
    if (data) {
      save(name, data, len);
      if (manifest) fprintf(manifest, "gif %s noise256.idx noise256.pal %d %d %d\n", name, w, h, il ? 7 : -1);
      /* Easel's own GIF reader agrees */
      ok("GIF read back by gif_read", gif_read(data, len, &back, err, sizeof err) && back.w == w && back.h == h);
      for (k = 0; back.px && k < n; k++) {
        const u8 *c = pal + idx[k] * 3;
        if ((il && idx[k] == 7) ? back.px[k * 4 + 3] != 0 : memcmp(back.px + k * 4, c, 3)) { ok("GIF pixels", 0); break; }
      }
      rgba_free(&back); free(data);
    } else ok("gif written", 0);
  }
  save("noise256.idx", idx, (size_t)n); save("noise256.pal", pal, 768);
  /* small palettes: 2, 4 and 16 colours, and flat runs */
  for (np = 2; np <= 16; np *= 2) {
    for (k = 0; k < n; k++) idx[k] = (u8)(((k % w) / 9 + (k / w) / 5) % np);
    data = gif_encode(idx, w, h, pal, np, -1, 0, &len);
    sprintf(name, "flat%d.gif", np);
    ok("small-palette GIF read back", data && gif_read(data, len, &back, err, sizeof err) && !memcmp(back.px + 77 * 4, pal + idx[77] * 3, 3) &&
       !memcmp(back.px + (n - 1) * 4, pal + idx[n - 1] * 3, 3));
    rgba_free(&back);
    if (data) { save(name, data, len); if (manifest) fprintf(manifest, "gif %s flat%d.idx noise256.pal %d %d -1\n", name, np, w, h); free(data); }
    sprintf(name, "flat%d.idx", np); save(name, idx, (size_t)n);
  }
  /* a document: indexed with transparency */
  {
    Doc *d = doc_of(src, w, h);
    u8 *al = NULL, *fi;
    for (k = 0; k < n; k++) if ((k / w) < 10) d->lay[0]->px[k * 4 + 3] = 0;
    doc_to_indexed(d, 100, QM_WU, DI_FS, NULL, 0);
    data = gif_write_doc(d, 256, QM_MEDIAN, DI_FS, 1, &len);
    ok("GIF of an indexed document", data && gif_read(data, len, &back, err, sizeof err) && back.px[3] == 0 && back.px[(20 * w) * 4 + 3] == 255 &&
       !memcmp(back.px + (20 * w) * 4, d->lay[0]->px + (20 * w) * 4, 3));
    rgba_free(&back); free(data);
    /* the same as a palette PNG with tRNS */
    data = doc_png_encode(d, 72, &len);
    fi = doc_flat_indices(d, &al);
    ok("palette PNG of a document read back", data && png_read(data, len, &back, err, sizeof err) && back.px[3] == 0 && !memcmp(back.px + (20 * w) * 4, d->lay[0]->px + (20 * w) * 4, 4));
    if (data) { save("doc_idx.png", data, len); dump_rgba("doc_idx.rgba", &back); if (manifest) fprintf(manifest, "pngrgba doc_idx.png doc_idx.rgba\n"); }
    rgba_free(&back); free(data); free(fi); free(al);
    /* a GIF quantized from RGB */
    {
      Rgba im;
      im.w = w; im.h = h; im.px = src;
      data = gif_encode_rgba(&im, 64, QM_OCTREE, DI_ATKINSON, 0, &len);
      ok("GIF from RGB", data && gif_read(data, len, &back, err, sizeof err) && psnr(src, back.px, n) > 18);
      if (data) { save("rgb.gif", data, len); dump_rgba("rgb_gif.rgba", &back); if (manifest) fprintf(manifest, "gifrgba rgb.gif rgb_gif.rgba\n"); }
      rgba_free(&back); free(data);
    }
    doc_free(d);
  }
  /* palette PNGs at each depth, with and without tRNS */
  for (depth = 1; depth <= 8; depth *= 2) {
    int npl = 1 << depth;
    u8 trns[256];
    for (k = 0; k < n; k++) idx[k] = (u8)(((k % w) * 3 + (k / w)) % npl);
    for (k = 0; k < 256; k++) trns[k] = (u8)(k == 1 ? 0 : k == 2 ? 128 : 255);
    data = png_encode_indexed(idx, w, h, pal, npl, trns, depth > 1 ? 3 : 0, 0, &len);
    sprintf(name, "pal%d.png", depth);
    ok("palette PNG written and read", data && png_read(data, len, &back, err, sizeof err) && !memcmp(back.px, pal + idx[0] * 3, 3) &&
       !memcmp(back.px + 5 * 4, pal + idx[5] * 3, 3) && (depth == 1 || back.px[1 * 4 + 3] == 0 || idx[1] != 1));
    if (data) { save(name, data, len); if (manifest) fprintf(manifest, "png %s pal%d.idx noise256.pal %d %d %d %d\n", name, depth, w, h, npl, depth > 1 ? 3 : 0); }
    sprintf(name, "pal%d.idx", depth); save(name, idx, (size_t)n);
    rgba_free(&back); free(data);
  }
  /* grey PNGs */
  {
    Rgba im;
    u8 *t = (u8 *)malloc((size_t)n * 4);
    memcpy(t, src, (size_t)n * 4);
    im.w = w; im.h = h; im.px = t;
    data = png_encode_gray(&im, 0, &len);
    ok("grey PNG read back", data && png_read(data, len, &back, err, sizeof err) && back.px[0] == back.px[1] && back.px[3] == 255);
    if (data) { save("grey.png", data, len); dump_rgba("grey.rgba", &back); if (manifest) fprintf(manifest, "pngrgba grey.png grey.rgba\n"); }
    rgba_free(&back); free(data);
    for (k = 0; k < n; k++) t[k * 4 + 3] = (u8)(k % 256);
    data = png_encode_gray(&im, 0, &len);
    ok("grey + alpha PNG read back", data && png_read(data, len, &back, err, sizeof err) && back.px[7] == 1);
    if (data) { save("greya.png", data, len); dump_rgba("greya.rgba", &back); if (manifest) fprintf(manifest, "pngrgba greya.png greya.rgba\n"); }
    rgba_free(&back); free(data); free(t);
  }
  (void)cols;
  free(idx); free(src);
}

/* ------------------------------------------------------------------ screen modes, guides */
static void screenmodes(void)
{
  Doc *d = doc_new(320, 256, 0, 0);
  int i, ham6, ham8, ehb, dd;
  long cols;
  u8 ax, ay;
  char warn[1024];
  const AmigaMode *m;
  i = amode_find(0x00021000);
  m = amode_get(i);
  ok("PAL lores in the table", m && m->w == 320 && m->h == 256);
  ok("PAL lores on OCS: 32 colours, HAM6, EHB", amode_limits(m, AC_OCS, &dd, &cols, &ham6, &ham8, &ehb) && cols == 32 && ham6 && !ham8 && ehb);
  ok("PAL lores on AGA: 256, HAM8", amode_limits(m, AC_AGA, &dd, &cols, &ham6, &ham8, &ehb) && cols == 256 && ham8);
  ok("hires on ECS: 16", amode_limits(amode_get(amode_find(0x00029000)), AC_ECS, &dd, &cols, NULL, NULL, NULL) && cols == 16);
  ok("super-hires on OCS: none", !amode_limits(amode_get(amode_find(0x00029020)), AC_OCS, &dd, &cols, NULL, NULL, NULL));
  ok("HAM bit ignored when finding", amode_find(0x00021800) == i);
  ok("aspect of lores", amode_aspect(0x00021000, &ax, &ay) && ax == 10 && ay == 11);
  ok("aspect of NTSC lores 44:52", amode_aspect(0x00011000, &ax, &ay) && ax == 44 && ay == 52);
  ok("old-style CAMG: hires laced", amode_find(0x8004) == amode_find(0x00029004));
  doc_to_indexed(d, 32, QM_MEDIAN, DI_NONE, NULL, 0);
  ok("suggest: PAL lores for 320x256 32 colours on OCS", amode_suggest(d, AC_OCS, 1) == i);
  ok("check: fits", amode_check(d, m, AC_OCS, IK_NORMAL, 5, warn, sizeof warn) == 0);
  ok("check: HAM8 refused on OCS", amode_check(d, m, AC_OCS, IK_HAM8, 0, warn, sizeof warn) == 1 && strstr(warn, "HAM8"));
  ok("check: hires 16 colours", amode_check(d, amode_get(amode_find(0x00029000)), AC_OCS, IK_NORMAL, 5, warn, sizeof warn) >= 1 && strstr(warn, "planes"));
  ok("camg for HAM", amode_camg(m, IK_HAM6) == 0x00021800);
  doc_free(d);
  d = doc_new(1000, 1000, 0, 0);
  ok("too big for every mode", amode_suggest(d, AC_AGA, 1) < 0);
  ok("check: larger than overscan", amode_check(d, m, AC_AGA, IK_NORMAL, 8, warn, sizeof warn) >= 1 && strstr(warn, "larger"));
  doc_free(d);
}
static void guides(void)
{
  Doc *d = doc_new(200, 100, 0, 0);
  double x, y;
  int r, minor;
  ok("guide added", guide_add(d, 1, 50) && guide_add(d, 0, 30) && !guide_add(d, 1, 50));
  x = 53; y = 70; r = snap_point(d, &x, &y, SNAP_GUIDES);
  ok("snap to the vertical guide", r == 1 && x == 50 && y == 70);
  grid_set(d, 25, 25, 0, 0, 5);
  x = 77; y = 47; r = snap_point(d, &x, &y, SNAP_GRID);
  ok("snap to the grid", r == 3 && x == 75 && y == 50);
  x = 81; y = 47; r = snap_point(d, &x, &y, SNAP_GRID | SNAP_SUB);
  ok("snap to subdivisions", r == 3 && x == 80 && y == 45);
  x = 196; y = 3; r = snap_point(d, &x, &y, SNAP_EDGES);
  ok("snap to edges", r == 3 && x == 200 && y == 0);
  x = 120.4; y = 33.3; d->guides.snap = SNAP_PIXEL; r = snap_point(d, &x, &y, SNAP_DOC);
  ok("pixel snap", r == 0 && x == 120 && y == 33);
  ok("guide found and moved", guide_find(d, 1, 52, 4) == 0 && guide_move(d, 1, 0, 300) && d->guides.nv == 0);
  ok("ruler step", ruler_step(1.0, 50, &minor) == 50 && ruler_step(4.0, 50, &minor) == 20 && ruler_step(0.1, 50, &minor) == 500);
  d->aspect_x = 10; d->aspect_y = 11;
  ok("aspect ratio", fabs(aspect_ratio(d) - 1.1) < 1e-9);
  doc_free(d);
}

int main(int argc, char **argv)
{
  char path[512];
  if (argc > 1) outdir = argv[1];
  {
    char cmd[600];
    sprintf(cmd, "mkdir -p '%s'", outdir);
    if (system(cmd) != 0) printf("(could not make %s)\n", outdir);
  }
  sprintf(path, "%s/manifest.txt", outdir);
  manifest = fopen(path, "w");
  quantizers();
  modes();
  editing();
  cycling();
  palette_files();
  ilbm();
  writers();
  screenmodes();
  guides();
  if (manifest) fclose(manifest);
  printf("%d tests, %d failed\n", ntests, nfailed);
  return nfailed != 0;
}
