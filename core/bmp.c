/*
 * bmp.c - Windows (and OS/2) bitmaps: reading 1-, 4- and 8-bit palette pictures (uncompressed or
 * RLE4 / RLE8), 16-, 24- and 32-bit ones (plain or with bitfield masks, alpha where a mask or the
 * pixels give one), stored bottom up or top down; writing 24-bit, or 32-bit BGRA when there is
 * transparency. Every field is little-endian and read a byte at a time.
 */
#include <stdlib.h>
#include <string.h>
#include "codec.h"

static u32 le16(const u8 *p) { return p[0] | ((u32)p[1] << 8); }
static u32 le32(const u8 *p) { return p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

/* a channel described by a bitfield mask: where it starts and how wide it is (8 bits at most are
   kept: wider fields lose their low bits) */
typedef struct { u32 mask; int shift, bits; } Field;
static void field(Field *f, u32 mask)
{
  f->mask = mask; f->shift = 0; f->bits = 0;
  if (!mask) return;
  while (!(mask & 1)) { mask >>= 1; f->shift++; }
  while (mask & 1) { mask >>= 1; f->bits++; }
}
/* the channel's value in a pixel, scaled to 0-255; dflt if the mask is empty */
static int field_get(const Field *f, u32 px, int dflt)
{
  u32 v, mx;
  int b = f->bits;
  if (!b) return dflt;
  v = (px & f->mask) >> f->shift;
  if (b > 8) { v >>= b - 8; b = 8; }
  mx = (1UL << b) - 1;
  return (int)(v * 255 / mx);
}

/* RLE8 (rle4 0) or RLE4 into idx (w * h indexes, rows as stored: bottom up). Runs and absolute
   stretches past the picture are clipped; 0 if the data stops before the end-of-picture code
   with rows still to come */
static int unrle(const u8 *s, size_t n, int rle4, u8 *idx, i32 w, i32 h)
{
  size_t i = 0;
  i32 x = 0, y = 0;
  while (i + 1 < n) {
    int c = s[i], v = s[i + 1];
    i += 2;
    if (c) {                                             /* a run of c pixels */
      int k;
      for (k = 0; k < c; k++, x++)
        if (x < w && y < h) idx[(long)y * w + x] = (u8)(rle4 ? (k & 1 ? v & 15 : v >> 4) : v);
    } else if (v == 0) { x = 0; y++; }                   /* end of line */
    else if (v == 1) return 1;                           /* end of picture */
    else if (v == 2) {                                   /* move right and up */
      if (i + 2 > n) return 0;
      x += s[i]; y += s[i + 1]; i += 2;
    } else {                                             /* v pixels as they are, padded to 16 bits */
      size_t bytes = rle4 ? ((size_t)v + 1) / 2 : (size_t)v;
      int k;
      if (i + bytes > n) return 0;
      for (k = 0; k < v; k++, x++)
        if (x < w && y < h) idx[(long)y * w + x] = (u8)(rle4 ? (k & 1 ? s[i + k / 2] & 15 : s[i + k / 2] >> 4) : s[i + k]);
      i += bytes + (bytes & 1);
    }
    if (y >= h) return 1;
  }
  return y >= h - 1;
}

/* the file header, then the info header (12 bytes for OS/2, 40 and up for Windows: masks come in
   the V2+ header or, for a 40-byte one, just after it), the palette, the pixels */
int bmp_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  u32 off, hs, comp = 0, bpp, ncol, palsz = 4, i, stride;
  i32 w, h, x, y;
  int topdown = 0, usealpha = 0;
  u8 pal[256][3];
  size_t palpos;
  Field fr, fg, fb, fa;
  u8 *idx = NULL;
  out->w = out->h = 0; out->px = NULL;
  if (n < 26 || d[0] != 'B' || d[1] != 'M') { codec_err(err, errcap, "Not a BMP file"); return 0; }
  off = le32(d + 10); hs = le32(d + 14);
  if (hs == 12) {
    w = (i32)le16(d + 18); h = (i32)le16(d + 20); bpp = le16(d + 24); palsz = 3;
  } else if (hs >= 40 && hs <= 1024 && n >= 54) {
    u32 uh = le32(d + 22);
    w = (i32)le32(d + 18);
    if (uh == 0x80000000UL) { codec_err(err, errcap, "Damaged BMP file"); return 0; }
    if (uh & 0x80000000UL) { h = (i32)(0 - uh); topdown = 1; } else h = (i32)uh;
    bpp = le16(d + 28); comp = le32(d + 30);
  } else { codec_err(err, errcap, "Unsupported BMP header"); return 0; }
  if (w < 1 || h < 1 || w > MAXDIM || h > MAXDIM || (long)w * h > CODEC_MAXPIXELS) { codec_err(err, errcap, "Bad BMP picture size"); return 0; }
  if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) { codec_err(err, errcap, "Unsupported BMP depth"); return 0; }
  if (comp == 6) comp = 3;                               /* BI_ALPHABITFIELDS: bitfields with alpha */
  if (!(comp == 0 || (comp == 1 && bpp == 8) || (comp == 2 && bpp == 4) || (comp == 3 && (bpp == 16 || bpp == 32)))) {
    codec_err(err, errcap, "Unsupported BMP compression"); return 0;
  }
  /* channel masks: the defaults, or the header's */
  palpos = 14 + hs;
  if (bpp == 16) { field(&fr, 0x7C00); field(&fg, 0x03E0); field(&fb, 0x001F); }
  else { field(&fr, 0xFF0000UL); field(&fg, 0xFF00); field(&fb, 0xFF); }
  field(&fa, 0);
  if (comp == 3) {
    size_t mp = 54;
    u32 am = 0;
    if (hs == 40) { palpos += le32(d + 30) == 6 ? 16 : 12; if (le32(d + 30) == 6 && n >= 70) am = le32(d + 66); }
    else if (hs >= 56) am = le32(d + 66);
    if (n < mp + 12) { codec_err(err, errcap, "Truncated BMP file"); return 0; }
    field(&fr, le32(d + mp)); field(&fg, le32(d + mp + 4)); field(&fb, le32(d + mp + 8));
    if (am && n >= 70) { field(&fa, am); usealpha = 1; }
  } else if (hs >= 56 && bpp == 32 && n >= 70 && le32(d + 66)) {
    /* a V3+ header may give an alpha mask even for plain 32-bit */
    field(&fa, le32(d + 66)); usealpha = 1;
  }
  /* the palette (whatever of it the file really holds) */
  memset(pal, 0, sizeof pal);
  ncol = bpp <= 8 ? (hs >= 40 && le32(d + 46) ? le32(d + 46) : 1u << bpp) : 0;
  if (ncol > 256) ncol = 256;
  for (i = 0; i < ncol && palpos + (i + 1) * palsz <= n; i++) {
    const u8 *p = d + palpos + i * palsz;
    pal[i][0] = p[2]; pal[i][1] = p[1]; pal[i][2] = p[0];
  }
  if (off >= n) { codec_err(err, errcap, "Truncated BMP file"); return 0; }
  if (!rgba_alloc(out, w, h)) { codec_err(err, errcap, "Out of memory"); return 0; }
  if (comp == 1 || comp == 2) {
    if (!(idx = (u8 *)calloc((size_t)w * h, 1))) { rgba_free(out); codec_err(err, errcap, "Out of memory"); return 0; }
    if (!unrle(d + off, n - off, comp == 2, idx, w, h)) { free(idx); rgba_free(out); codec_err(err, errcap, "Truncated BMP file"); return 0; }
    for (y = 0; y < h; y++)
      for (x = 0; x < w; x++) {
        u8 *o = out->px + ((long)(h - 1 - y) * w + x) * 4, *c = pal[idx[(long)y * w + x]];
        o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = 255;
      }
    free(idx);
    return 1;
  }
  /* uncompressed: every row must be there (the last one's padding may be missing) */
  stride = (((u32)w * bpp + 31) / 32) * 4;
  if ((unsigned long)(n - off) < (unsigned long)stride * (h - 1) + ((u32)w * bpp + 7) / 8) {
    rgba_free(out); codec_err(err, errcap, "Truncated BMP file"); return 0;
  }
  /* plain 32-bit: the fourth byte is alpha only if some pixel has it non-zero */
  if (bpp == 32 && comp == 0 && !usealpha) {
    for (y = 0; y < h && !usealpha; y++)
      for (x = 0; x < w; x++) if (d[off + (size_t)y * stride + (size_t)x * 4 + 3]) { usealpha = 1; break; }
    if (usealpha) field(&fa, 0xFF000000UL);
  }
  for (y = 0; y < h; y++) {
    const u8 *r = d + off + (size_t)y * stride;
    u8 *o = out->px + (size_t)(topdown ? y : h - 1 - y) * w * 4;
    for (x = 0; x < w; x++, o += 4) {
      if (bpp <= 8) {
        u32 bit = (u32)x * bpp, v = (r[bit >> 3] >> (8 - bpp - (bit & 7))) & ((1u << bpp) - 1);
        o[0] = pal[v][0]; o[1] = pal[v][1]; o[2] = pal[v][2]; o[3] = 255;
      } else if (bpp == 24) {
        o[0] = r[x * 3 + 2]; o[1] = r[x * 3 + 1]; o[2] = r[x * 3]; o[3] = 255;
      } else {
        u32 v = bpp == 16 ? le16(r + x * 2) : le32(r + x * 4);
        o[0] = (u8)field_get(&fr, v, 0); o[1] = (u8)field_get(&fg, v, 0); o[2] = (u8)field_get(&fb, v, 0);
        o[3] = (u8)(usealpha ? field_get(&fa, v, 255) : 255);
      }
    }
  }
  return 1;
}

static void w16(Buf *b, u32 v) { buf_addc(b, (int)(v & 255)); buf_addc(b, (int)((v >> 8) & 255)); }
static void w32(Buf *b, u32 v) { w16(b, v & 0xFFFF); w16(b, v >> 16); }

/* bottom up, rows padded to 4 bytes; a BITMAPINFOHEADER for 24-bit, a BITMAPV4HEADER with BGRA
   bitfields (and sRGB) for 32-bit. 72 dpi is recorded */
u8 *bmp_encode(const Rgba *im, size_t *len)
{
  long i, n = (long)im->w * im->h;
  int alpha = 0;
  u32 hs, bpp, stride, x, size;
  i32 y;
  Buf b;
  for (i = 0; i < n; i++) if (im->px[i * 4 + 3] != 255) { alpha = 1; break; }
  hs = alpha ? 108 : 40; bpp = alpha ? 32 : 24;
  stride = (((u32)im->w * bpp + 31) / 32) * 4;
  size = stride * (u32)im->h;
  buf_init(&b);
  if (!buf_reserve(&b, 14 + hs + size)) return NULL;
  buf_add(&b, "BM", 2); w32(&b, 14 + hs + size); w32(&b, 0); w32(&b, 14 + hs);
  w32(&b, hs); w32(&b, (u32)im->w); w32(&b, (u32)im->h); w16(&b, 1); w16(&b, bpp);
  w32(&b, alpha ? 3 : 0); w32(&b, size); w32(&b, 2835); w32(&b, 2835); w32(&b, 0); w32(&b, 0);
  if (alpha) {
    w32(&b, 0x00FF0000UL); w32(&b, 0x0000FF00UL); w32(&b, 0x000000FFUL); w32(&b, 0xFF000000UL);
    buf_add(&b, "BGRs", 4);                              /* LCS_sRGB, as the little-endian number 'sRGB' */
    for (x = 0; x < 48; x++) buf_addc(&b, 0);            /* endpoints and gamma, unused for sRGB */
  }
  if (b.oom) { buf_free(&b); return NULL; }
  /* the pixels straight into the reserved space */
  for (y = im->h - 1; y >= 0; y--) {
    const u8 *s = im->px + (size_t)y * im->w * 4;
    u8 *o = (u8 *)b.data + b.len;
    for (x = 0; x < (u32)im->w; x++, s += 4) {
      *o++ = s[2]; *o++ = s[1]; *o++ = s[0];
      if (alpha) *o++ = s[3];
    }
    for (x = (u32)im->w * (bpp / 8); x < stride; x++) *o++ = 0;
    b.len += stride;
  }
  b.data[b.len] = 0;
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}
