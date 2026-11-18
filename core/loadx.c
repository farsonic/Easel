/*
 * loadx.c - Easel 1.2: what codec_load_doc adds to a document after reading it: the file's ICC
 * profile and colour model (cms.h), a palette PNG or BMP opened as an Indexed document with its
 * own palette, and a 1-bit TIFF or PBM opened in Bitmap mode.
 */
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "cms.h"
#include "palette.h"
#include "loadx.h"

static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u32 le32(const u8 *p) { return (u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0]; }
static u32 le16(const u8 *p) { return (u32)p[1] << 8 | p[0]; }

/* a palette PNG's PLTE (n entries into pal); 0 if it isn't one */
static int png_palette(const u8 *d, size_t n, u8 *pal)
{
  size_t at = 8;
  if (n < 33 || memcmp(d + 12, "IHDR", 4) || d[25] != 3) return 0;
  while (at + 12 <= n) {
    u32 cl = be32(d + at);
    if (cl > n - at - 12) return 0;
    if (!memcmp(d + at + 4, "PLTE", 4)) {
      if (cl % 3 || cl < 3 || cl > 768) return 0;
      memcpy(pal, d + at + 8, cl);
      return (int)(cl / 3);
    }
    if (!memcmp(d + at + 4, "IDAT", 4)) return 0;
    at += 12 + cl;
  }
  return 0;
}
/* a 1-8 bit BMP's colour table */
static int bmp_palette(const u8 *d, size_t n, u8 *pal)
{
  u32 hs, bpp, used, k;
  if (n < 54 || d[0] != 'B' || d[1] != 'M') return 0;
  hs = le32(d + 14);
  if (hs < 40 || hs > n - 14) return 0;
  bpp = le16(d + 28);
  if (bpp != 1 && bpp != 4 && bpp != 8) return 0;
  used = le32(d + 46);
  if (!used || used > (1u << bpp)) used = 1u << bpp;
  if (14 + hs + 4 * used > n) return 0;
  for (k = 0; k < used; k++) { const u8 *q = d + 14 + hs + 4 * k; pal[3 * k] = q[2]; pal[3 * k + 1] = q[1]; pal[3 * k + 2] = q[0]; }
  return (int)used;
}
/* 1 if a TIFF holds one sample of one bit a pixel, or the file is a PBM */
static int one_bit(const u8 *d, size_t n)
{
  int fmt = codec_detect(d, n);
  if (fmt == FMT_PNM) return n >= 2 && d[0] == 'P' && (d[1] == '1' || d[1] == '4');
  if (fmt == FMT_TIFF && n >= 8) {
    int le = d[0] == 'I';
    u32 ifd = le ? le32(d + 4) : be32(d + 4), ne, i, bits = 1, spp = 1;
    if (ifd > n - 2) return 0;
    ne = le ? le16(d + ifd) : (u32)d[ifd] << 8 | d[ifd + 1];
    if (ne > (n - ifd - 2) / 12) return 0;
    for (i = 0; i < ne; i++) {
      const u8 *e = d + ifd + 2 + 12 * i;
      u32 tag = le ? le16(e) : (u32)e[0] << 8 | e[1], cnt = le ? le32(e + 4) : be32(e + 4);
      u32 v = le ? le16(e + 8) : (u32)e[8] << 8 | e[9];
      if (tag == 258 && cnt == 1) bits = v;
      if (tag == 277) spp = v;
      if (tag == 258 && cnt > 1) bits = 0;
    }
    return bits == 1 && spp == 1;
  }
  return 0;
}
/* 1 if every pixel of the document's one layer is opaque black or white */
static int black_and_white(const Doc *doc)
{
  long k, np = (long)doc->w * doc->h;
  const u8 *p = doc->lay[0]->px;
  for (k = 0; k < np; k++, p += 4) {
    if (p[3] != 255 || p[0] != p[1] || p[1] != p[2] || (p[0] != 0 && p[0] != 255)) return 0;
  }
  return 1;
}

void codec_load_extras(Doc *doc, const u8 *d, size_t n)
{
  u8 pal[768];
  int np, fmt;
  u32 len = 0;
  if (!doc) return;
  fmt = codec_detect(d, n);
  /* the profile (an RGB or grey one: the pixels of a CMYK or Lab file are sRGB by now) and the colour model */
  if (!doc->icc) {
    u8 *icc = icc_from_file(d, n, &len);
    IccProfile *p = icc ? icc_parse(icc, len, NULL, 0) : NULL;
    if (p && (icc_space(p) == ICC_RGB || icc_space(p) == ICC_GRAY)) { doc->icc = icc; doc->icclen = len; icc = NULL; }
    icc_free(p);
    free(icc);
  }
  if (doc->mode == MODE_RGB) doc->cmode = (u8)cms_file_model(d, n);
  /* palette files open indexed with their palette (only a flat, one-layer picture) */
  if (doc->nlay != 1 || !doc->lay[0]->px || doc->mode != MODE_RGB) return;
  np = fmt == FMT_PNG ? png_palette(d, n, pal) : fmt == FMT_BMP ? bmp_palette(d, n, pal) : 0;
  if (np >= 1) {
    if (np == 1) { pal[3] = pal[0]; pal[4] = pal[1]; pal[5] = pal[2]; np = 2; }
    if (doc_to_indexed(doc, np, QM_FIXED, DI_NONE, pal, np)) { doc_forget_undo(doc); doc->dirty = 0; }
    return;
  }
  if (one_bit(d, n) && black_and_white(doc)) {
    if (doc_to_bitmap(doc, BMM_THRESHOLD)) { doc_forget_undo(doc); doc->dirty = 0; }
  }
}

static IccProfile *open_cmyk;
int codec_cmyk_begin(const u8 *d, size_t n)
{
  u32 len;
  u8 *icc;
  if (cms_file_model(d, n) != CM_CMYK) return 0;
  if ((icc = icc_from_file(d, n, &len)) != NULL) {
    open_cmyk = icc_parse(icc, len, NULL, 0);
    free(icc);
    if (open_cmyk && icc_space(open_cmyk) != ICC_CMYK) { icc_free(open_cmyk); open_cmyk = NULL; }
  }
  if (!cms_cmyk_begin(open_cmyk)) { icc_free(open_cmyk); open_cmyk = NULL; return 0; }
  return 1;
}
void codec_cmyk_end(void)
{
  cms_cmyk_end();
  icc_free(open_cmyk);
  open_cmyk = NULL;
}
