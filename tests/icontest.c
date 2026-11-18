/*
 * icontest.c - native tests for Easel 0.6's Workbench icon writer (core/iconwrite.c) and batch
 * conversion (core/batch.c). Every icon is read back with amigafmt.c's own icon reader: GlowIcons,
 * NewIcons and PNG icons must come back pixel for pixel (when the picture has few enough colours),
 * classic icons must come back as the picture mapped to their palette.
 *
 * Build and run from the easel folder (build.sh does it with the rest of the core):
 *   cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. -Iav1/include $CORE tests/icontest.c \
 *      /tmp/easel_dav1d/libdav1d.a -o /tmp/easel_icontest -lm -lpthread
 *   /tmp/easel_icontest [folder for the files it writes, default /tmp/easel_icon_out]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/codec.h"
#include "core/amigafmt.h"
#include "core/iconwrite.h"
#include "core/batch.h"
#include "core/palette.h"
#include "core/adjust.h"

static int ntests, nfailed;
static const char *outdir = "/tmp/easel_icon_out";
static void ok(const char *name, int cond)
{
  ntests++;
  if (!cond) { nfailed++; printf("FAIL: %s\n", name); }
  else if (getenv("ICONTEST_VERBOSE")) printf("ok: %s\n", name);
}
static u32 seed = 7;
static int rnd(int n) { seed = seed * 1103515245UL + 12345; return (int)((seed >> 16) % (u32)n); }

/* a picture of w x h using ncol distinct colours (a fixed random palette), with a transparent
   disc when transp is set */
static void make_pic(Rgba *im, i32 w, i32 h, int ncol, int transp, u8 *pal)
{
  i32 x, y;
  int i;
  rgba_alloc(im, w, h);
  for (i = 0; i < ncol; i++) {
    /* distinct: the index spread over the bytes, plus noise in the low bits of blue */
    pal[i * 3] = (u8)(i * 37 + 11); pal[i * 3 + 1] = (u8)(rnd(256)); pal[i * 3 + 2] = (u8)i;
  }
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      u8 *p = im->px + ((size_t)y * w + x) * 4;
      int c = ((x / 3) + (y / 2) * 7 + rnd(3)) % ncol;
      long dx = x - w / 2, dy = y - h / 2;
      if (transp && dx * dx + dy * dy < (long)(w / 4) * (w / 4)) { p[0] = p[1] = p[2] = 0; p[3] = 0; continue; }
      p[0] = pal[c * 3]; p[1] = pal[c * 3 + 1]; p[2] = pal[c * 3 + 2]; p[3] = 255;
    }
}
static void make_photo(Rgba *im, i32 w, i32 h)
{
  i32 x, y;
  rgba_alloc(im, w, h);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      u8 *p = im->px + ((size_t)y * w + x) * 4;
      p[0] = (u8)(x * 255 / (w - 1)); p[1] = (u8)(y * 255 / (h - 1)); p[2] = (u8)((x + y) * 255 / (w + h - 2)); p[3] = 255;
    }
}
/* the same picture: RGB where opaque, alpha 0 where transparent */
static int same(const Rgba *a, const Rgba *b)
{
  long k, n;
  if (a->w != b->w || a->h != b->h) return 0;
  n = (long)a->w * a->h;
  for (k = 0; k < n; k++) {
    const u8 *p = a->px + k * 4, *q = b->px + k * 4;
    if ((p[3] < 128) != (q[3] < 128)) return 0;
    if (p[3] >= 128 && (p[0] != q[0] || p[1] != q[1] || p[2] != q[2])) return 0;
  }
  return 1;
}
static void save(const char *name, const u8 *d, size_t n)
{
  char path[600];
  snprintf(path, sizeof path, "%s/%s", outdir, name);
  file_write(path, d, n);
}
static int has(const u8 *d, size_t n, const char *s)
{
  size_t l = strlen(s), i;
  for (i = 0; i + l <= n; i++) if (!memcmp(d + i, s, l)) return 1;
  return 0;
}

static void classic(void)
{
  Rgba pic, back, ph;
  u8 pal[8 * 3], dummy[768];
  size_t len = 0;
  u8 *ic;
  IconOpts o;
  i32 x, y;
  int i, inpal, n;
  long k, err = 0;
  char e[100];
  (void)dummy;
  /* a picture in MagicWB's colours 1-7 and transparency: exact through the MWB palette, colour 0 clear */
  n = icon_classic_palette(ICONPAL_MWB, pal);
  ok("classic: MWB has 8 colours", n == 8);
  rgba_alloc(&pic, 40, 30);
  for (y = 0; y < 30; y++)
    for (x = 0; x < 40; x++) {
      u8 *p = pic.px + ((size_t)y * 40 + x) * 4;
      int c = 1 + (x + y * 3) % 7;
      if (x < 5) { p[3] = 0; continue; }
      p[0] = pal[c * 3]; p[1] = pal[c * 3 + 1]; p[2] = pal[c * 3 + 2]; p[3] = 255;
    }
  icon_opts_default(&o, ICON_CLASSIC);
  o.deftool = ICON_TOOL_VIEWER;
  ic = icon_write_ex(&pic, &o, &len, e, sizeof e);
  ok("classic: written", ic != NULL);
  if (ic) {
    save("classic_mwb.info", ic, len);
    ok("classic: magic and version", ic[0] == 0xE3 && ic[1] == 0x10 && ic[2] == 0 && ic[3] == 1);
    ok("classic: detected as an icon", amigafmt_detect(ic, len) == AMF_INFO && codec_detect(ic, len) == FMT_INFO);
    ok("classic: a project", ic[48] == 4);
    ok("classic: default tool kept", has(ic, len, ICON_TOOL_VIEWER));
    ok("classic: depth 3", ic[78 + 9] == 3);
    ok("classic: read back", info_read_ex(ic, len, 0, INFO_PAL_MWB, 1, &back, e, sizeof e));
    ok("classic: exact in its palette", same(&pic, &back));
    rgba_free(&back);
    ok("classic: auto palette is MWB", info_read(ic, len, &back, e, sizeof e) && back.px[(5 + 0) * 4] == pic.px[5 * 4]);
    rgba_free(&back);
    ok("classic: exact length", len == 78 + 20 + (size_t)((40 + 15) / 16) * 2 * 30 * 3 + 4 + strlen(ICON_TOOL_VIEWER) + 1);
    free(ic);
  }
  /* 2.x four colours */
  o.classic_pal = ICONPAL_WB20; o.deftool = NULL;
  ic = icon_write_ex(&pic, &o, &len, e, sizeof e);
  ok("classic 2.x: written, depth 2", ic && ic[78 + 9] == 2);
  if (ic) {
    u8 p4[12];
    icon_classic_palette(ICONPAL_WB20, p4);
    ok("classic 2.x: read back", info_read_ex(ic, len, 0, INFO_PAL_20, 0, &back, e, sizeof e));
    inpal = back.w == 40 && back.h == 30;
    for (k = 0; inpal && k < 40 * 30; k++) {
      int hit = 0;
      for (i = 0; i < 4; i++) if (!memcmp(back.px + k * 4, p4 + i * 3, 3)) hit = 1;
      inpal = hit;
    }
    ok("classic 2.x: every pixel one of the four", inpal);
    rgba_free(&back);
    save("classic_wb20.info", ic, len);
    free(ic);
  }
  rgba_free(&pic);
  /* a photograph, shrunk to 64 and dithered to MWB: every pixel in the palette, and on average close */
  make_photo(&ph, 320, 200);
  icon_opts_default(&o, ICON_CLASSIC);
  ic = icon_write_ex(&ph, &o, &len, e, sizeof e);
  ok("classic photo: written", ic != NULL);
  if (ic && info_read_ex(ic, len, 0, INFO_PAL_MWB, 0, &back, e, sizeof e)) {
    Rgba th;
    ok("classic photo: thumbnail size", back.w == 64 && back.h == 40);
    rgba_thumbnail(&ph, 64, &th);
    inpal = 1;
    for (k = 0; k < (long)back.w * back.h; k++) {
      int hit = 0, c;
      for (i = 0; i < 8; i++) if (!memcmp(back.px + k * 4, pal + i * 3, 3)) hit = 1;
      if (!hit) inpal = 0;
      for (c = 0; c < 3; c++) err += labs((long)back.px[k * 4 + c] - th.px[k * 4 + c]);
    }
    ok("classic photo: every pixel in MWB", inpal);
    /* an 8-grey-ish palette can't show a colour gradient well; the dither keeps the local mean
       roughly right, so compare 8x8 block means */
    {
      long be = 0, nb = 0;
      i32 bx, by;
      for (by = 0; by + 8 <= back.h; by += 8)
        for (bx = 0; bx + 8 <= back.w; bx += 8) {
          long sa = 0, sb = 0;
          for (y = by; y < by + 8; y++)
            for (x = bx; x < bx + 8; x++) {
              const u8 *p = back.px + ((size_t)y * back.w + x) * 4, *q = th.px + ((size_t)y * th.w + x) * 4;
              sa += p[0] + p[1] + p[2]; sb += q[0] + q[1] + q[2];
            }
          be += labs(sa - sb) / (64 * 3); nb++;
        }
      ok("classic photo: dithered brightness close (8x8 blocks within 40 levels)", nb && be / nb < 40);
    }
    rgba_free(&th);
    rgba_free(&back);
    save("classic_photo.info", ic, len);
  }
  free(ic);
  rgba_free(&ph);
}

static void newicons_one(const char *name, i32 w, i32 h, int ncol, int transp, int size)
{
  Rgba pic, back;
  u8 pal[256 * 3];
  size_t len = 0;
  u8 *ic;
  IconOpts o;
  char e[100], t[160];
  make_pic(&pic, w, h, ncol, transp, pal);
  icon_opts_default(&o, ICON_NEWICONS);
  o.size = size; o.selected = 1; o.deftool = ICON_TOOL_EASEL;
  ic = icon_write_ex(&pic, &o, &len, e, sizeof e);
  snprintf(t, sizeof t, "newicons %s: written", name); ok(t, ic != NULL);
  if (!ic) { rgba_free(&pic); return; }
  snprintf(t, sizeof t, "newicons %s.info", name); save(t, ic, len);
  snprintf(t, sizeof t, "newicons %s: has its tool types", name); ok(t, has(ic, len, "IM1=") && has(ic, len, "IM2=") && has(ic, len, "*** DON'T EDIT"));
  snprintf(t, sizeof t, "newicons %s: read back", name); ok(t, info_read(ic, len, &back, e, sizeof e));
  snprintf(t, sizeof t, "newicons %s: pixel exact", name); ok(t, same(&pic, &back));
  rgba_free(&back);
  /* the selected image: the same picture darkened */
  if (info_read_ex(ic, len, 1, INFO_PAL_AUTO, 0, &back, e, sizeof e)) {
    long k, good = back.w == w && back.h == h;
    for (k = 0; good && k < (long)w * h; k++)
      if (pic.px[k * 4 + 3] >= 128 && back.px[k * 4] != pic.px[k * 4] * 5 / 8) good = 0;
    snprintf(t, sizeof t, "newicons %s: selected image darker", name); ok(t, good);
    rgba_free(&back);
  } else { snprintf(t, sizeof t, "newicons %s: selected image read", name); ok(t, 0); }
  /* no tool type line is longer than a tool type should be (127 characters) */
  {
    size_t i = 0, run = 0, longest = 0;
    for (i = 0; i < len; i++) { if (ic[i]) run++; else { if (run > longest) longest = run; run = 0; } }
    snprintf(t, sizeof t, "newicons %s: lines at most 127 characters", name); ok(t, longest <= 127);
  }
  free(ic);
  rgba_free(&pic);
}
static void newicons(void)
{
  newicons_one("2 colours", 37, 29, 2, 0, 64);
  newicons_one("2 colours and transparency", 41, 33, 1, 1, 64);
  newicons_one("5 colours", 50, 31, 5, 0, 64);
  newicons_one("31 colours with transparency", 64, 48, 31, 1, 64);
  newicons_one("200 colours with transparency", 64, 64, 200, 1, 64);
  newicons_one("256 colours", 93, 70, 256, 0, 93);
  newicons_one("255 colours and transparency", 90, 93, 255, 1, 93);
}

static void glow_one(const char *name, i32 w, i32 h, int ncol, int transp, int size)
{
  Rgba pic, back;
  u8 pal[256 * 3];
  size_t len = 0, o;
  u8 *ic;
  IconOpts io;
  char e[100], t[160];
  make_pic(&pic, w, h, ncol, transp, pal);
  icon_opts_default(&io, ICON_GLOW);
  io.size = size; io.deftool = ICON_TOOL_EASEL;
  ic = icon_write_ex(&pic, &io, &len, e, sizeof e);
  snprintf(t, sizeof t, "glowicons %s: written", name); ok(t, ic != NULL);
  if (!ic) { rgba_free(&pic); return; }
  snprintf(t, sizeof t, "glow %s.info", name); save(t, ic, len);
  snprintf(t, sizeof t, "glowicons %s: read back", name); ok(t, info_read(ic, len, &back, e, sizeof e));
  snprintf(t, sizeof t, "glowicons %s: pixel exact", name); ok(t, same(&pic, &back));
  rgba_free(&back);
  if (info_read_ex(ic, len, 1, INFO_PAL_AUTO, 0, &back, e, sizeof e)) {
    long k, good = back.w == w && back.h == h;
    for (k = 0; good && k < (long)w * h; k++)
      if (pic.px[k * 4 + 3] >= 128 && back.px[k * 4 + 1] != pic.px[k * 4 + 1] * 5 / 8) good = 0;
    snprintf(t, sizeof t, "glowicons %s: selected image darker", name); ok(t, good);
    rgba_free(&back);
  } else { snprintf(t, sizeof t, "glowicons %s: selected image read", name); ok(t, 0); }
  /* the classic image under it (the file cut where the FORM ICON starts) */
  for (o = 78; o + 4 <= len && memcmp(ic + o, "FORM", 4); o++) ;
  snprintf(t, sizeof t, "glowicons %s: FORM ICON after the classic parts", name); ok(t, o + 12 <= len && !memcmp(ic + o + 8, "ICON", 4));
  snprintf(t, sizeof t, "glowicons %s: FORM size right", name);
  ok(t, o + 8 + (((size_t)ic[o + 4] << 24) | ((size_t)ic[o + 5] << 16) | ((size_t)ic[o + 6] << 8) | ic[o + 7]) == len);
  if (info_read(ic, o, &back, e, sizeof e)) {
    snprintf(t, sizeof t, "glowicons %s: classic image under it the same size", name); ok(t, back.w == w && back.h == h);
    rgba_free(&back);
  } else { snprintf(t, sizeof t, "glowicons %s: classic image read", name); ok(t, 0); }
  free(ic);
  rgba_free(&pic);
}
static void glowicons(void)
{
  glow_one("2 colours", 30, 20, 2, 0, 64);
  glow_one("transparent, 1 colour", 33, 17, 1, 1, 64);
  glow_one("16 colours", 48, 48, 16, 0, 64);
  glow_one("100 colours, transparency", 64, 50, 100, 1, 64);
  glow_one("256 colours", 64, 64, 256, 0, 64);
  glow_one("256x256, 256 colours", 256, 256, 256, 0, 256);
  glow_one("255 colours and transparency, 200x150", 200, 150, 255, 1, 256);
  /* a photo (more colours than fit): read back the right size, the pixels close */
  {
    Rgba ph, back, th;
    size_t len;
    u8 *ic;
    char e[100];
    make_photo(&ph, 640, 480);
    ic = icon_write(&ph, ICON_GLOW, ICON_TOOL_VIEWER, &len);
    ok("glowicons photo: written", ic != NULL);
    if (ic && info_read(ic, len, &back, e, sizeof e)) {
      long k, err = 0;
      rgba_thumbnail(&ph, 64, &th);
      ok("glowicons photo: 64 x 48", back.w == 64 && back.h == 48);
      for (k = 0; k < 64L * 48 * 4; k++) if (k % 4 != 3) err += labs((long)back.px[k] - th.px[k]);
      ok("glowicons photo: within 6 levels on average (dithered)", err / (64L * 48 * 3) <= 6);
      rgba_free(&back); rgba_free(&th);
      save("glow_photo.info", ic, len);
    }
    free(ic);
    rgba_free(&ph);
  }
}

static void pngicons(void)
{
  Rgba pic, back;
  size_t len;
  u8 *ic;
  i32 x, y;
  char e[100];
  rgba_alloc(&pic, 50, 40);
  for (y = 0; y < 40; y++)
    for (x = 0; x < 50; x++) {
      u8 *p = pic.px + ((size_t)y * 50 + x) * 4;
      p[0] = (u8)(x * 5); p[1] = (u8)(y * 6); p[2] = (u8)rnd(256); p[3] = (u8)(x * 5 + 5);
    }
  ic = icon_write(&pic, ICON_PNG, NULL, &len);
  ok("png icon: written", ic != NULL);
  if (ic) {
    save("png.info", ic, len);
    ok("png icon: a PNG file", codec_detect(ic, len) == FMT_PNG);
    ok("png icon: read back by the icon reader", info_read(ic, len, &back, e, sizeof e));
    ok("png icon: exact, alpha too", back.w == 50 && back.h == 40 && !memcmp(back.px, pic.px, 50 * 40 * 4));
    rgba_free(&back);
    free(ic);
  }
  rgba_free(&pic);
  /* errors */
  memset(&pic, 0, sizeof pic);
  ok("no picture: no icon", icon_write(&pic, ICON_GLOW, NULL, &len) == NULL);
}

/* ------------------------------------------------------------------ batch conversion */
static void batch(void)
{
  Rgba pic, back;
  u8 pal[256 * 3], *png, *d;
  size_t len, n;
  char in[600], out[600], e[160];
  BatchOpts o;
  static const int fmts[] = { FMT_PNG, FMT_JPEG, FMT_BMP, FMT_ILBM, FMT_GIF, FMT_ORA };
  int i;
  make_pic(&pic, 120, 80, 40, 0, pal);
  png = png_encode(&pic, 0, &len);
  snprintf(in, sizeof in, "%s/batch_in.png", outdir);
  file_write(in, png, len);
  free(png);
  ok("batch: format names", batch_format_named("jpg") == FMT_JPEG && batch_format_named("IFF") == FMT_ILBM && batch_format_named("tiff") == FMT_TIFF && batch_format_named("xyz") == FMT_UNKNOWN);
  ok("batch: format from a path", batch_format_of_path("Work:Pics/a.b/x.Gif") == FMT_GIF && batch_format_of_path("Work:Pics.d/x") == FMT_UNKNOWN);
  for (i = 0; i < 6; i++) {
    char t[100];
    batch_opts_default(&o);
    snprintf(out, sizeof out, "%s/batch_out%s", outdir, batch_ext(fmts[i]));
    snprintf(t, sizeof t, "batch: to %s", codec_name(fmts[i]));
    ok(t, batch_convert(in, out, &o, e, sizeof e));
    d = file_read(out, &n);
    snprintf(t, sizeof t, "batch: %s detected", codec_name(fmts[i]));
    ok(t, d && codec_detect(d, n) == fmts[i]);
    if (d && codec_decode(d, n, &back, e, sizeof e)) {
      snprintf(t, sizeof t, "batch: %s %s", codec_name(fmts[i]), fmts[i] == FMT_JPEG ? "same size" : "lossless");
      ok(t, fmts[i] == FMT_JPEG ? back.w == 120 && back.h == 80 : same(&pic, &back));
      rgba_free(&back);
    } else { snprintf(t, sizeof t, "batch: %s decoded", codec_name(fmts[i])); ok(t, 0); }
    free(d);
  }
  /* resize: WIDTH only keeps the proportion; SCALE */
  batch_opts_default(&o);
  o.width = 60;
  snprintf(out, sizeof out, "%s/batch_w60.png", outdir);
  ok("batch: width 60", batch_convert(in, out, &o, e, sizeof e) && (d = file_read(out, &n)) != NULL);
  if (d && codec_decode(d, n, &back, e, sizeof e)) { ok("batch: 60 x 40", back.w == 60 && back.h == 40); rgba_free(&back); }
  free(d); d = NULL;
  batch_opts_default(&o);
  o.scale = 250; o.format = FMT_BMP;
  snprintf(out, sizeof out, "%s/batch_scale.pic", outdir);
  ok("batch: scale 250 per cent, FORMAT=BMP with another extension", batch_convert(in, out, &o, e, sizeof e) && (d = file_read(out, &n)) != NULL);
  if (d && codec_decode(d, n, &back, e, sizeof e)) { ok("batch: 300 x 200 BMP", codec_detect(d, n) == FMT_BMP && back.w == 300 && back.h == 200); rgba_free(&back); }
  free(d); d = NULL;
  /* ILBM with 16 colours: 4 planes, and an icon beside it */
  batch_opts_default(&o);
  o.colors = 16; o.icon = ICON_GLOW + 1; o.deftool = ICON_TOOL_EASEL;
  snprintf(out, sizeof out, "%s/batch_16.iff", outdir);
  ok("batch: ILBM 16 colours with a GlowIcon", batch_convert(in, out, &o, e, sizeof e));
  if ((d = file_read(out, &n)) != NULL) {
    Doc *doc = batch_load(d, n, e, sizeof e);
    ok("batch: ILBM 16 colours opens indexed with 16 entries", doc && doc->mode == MODE_INDEXED && doc->npal <= 16);
    ok("batch: ILBM has 4 planes", n > 28 && !memcmp(d + 12, "BMHD", 4) && d[28] == 4);
    if (doc) doc_free(doc);
    free(d);
  }
  snprintf(out, sizeof out, "%s/batch_16.iff.info", outdir);
  d = file_read(out, &n);
  ok("batch: the icon is there and reads", d && info_read(d, n, &back, e, sizeof e) && back.w == 64);
  if (d) rgba_free(&back);
  free(d);
  /* an indexed ILBM in, kept to its palette going out as ILBM and GIF */
  {
    IlbmOpts io;
    u8 *ib;
    size_t il;
    ilbm_opts_default(&io);
    ib = ilbm_write(&pic, NULL, 0, NULL, NULL, 0, &io, &il, e, sizeof e);
    snprintf(in, sizeof in, "%s/batch_in.iff", outdir);
    if (ib) file_write(in, ib, il);
    free(ib);
    batch_opts_default(&o);
    snprintf(out, sizeof out, "%s/batch_idx.gif", outdir);
    ok("batch: indexed ILBM to GIF", batch_convert(in, out, &o, e, sizeof e));
    d = file_read(out, &n);
    if (d && codec_decode(d, n, &back, e, sizeof e)) { ok("batch: indexed ILBM to GIF lossless", same(&pic, &back)); rgba_free(&back); }
    free(d);
  }
  /* failures say why */
  snprintf(out, sizeof out, "%s/nothing.png", outdir);
  ok("batch: a missing file fails", !batch_convert("/nonexistent/x.png", out, &o, e, sizeof e) && e[0]);
  snprintf(in, sizeof in, "%s/batch_junk.png", outdir);
  file_write(in, "not a picture at all", 20);
  ok("batch: junk fails with a message", !batch_convert(in, out, &o, e, sizeof e) && e[0]);
  rgba_free(&pic);
}

int main(int argc, char **argv)
{
  char cmd[700];
  if (argc > 1) outdir = argv[1];
  snprintf(cmd, sizeof cmd, "mkdir -p '%s'", outdir);
  if (system(cmd) != 0) printf("(could not make %s)\n", outdir);
  adjlayer_init();
  classic();
  newicons();
  glowicons();
  pngicons();
  batch();
  printf("icons (classic, NewIcons, GlowIcons, PNG) read back, and batch conversion:\n");
  printf("%d tests, %d failed\n", ntests, nfailed);
  return nfailed != 0;
}
