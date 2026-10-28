/*
 * tgaw.c - writing Targa (0.6), from Truevision's TGA File Format Specification 2.0: true colour
 * (24-bit BGR, or 32-bit BGRA with eight attribute bits when anything isn't opaque), grey (8-bit;
 * grey with alpha goes as 32-bit colour, since few readers take 16-bit grey) and colour-mapped
 * (8-bit indices into a 24- or 32-bit map), raw or run-length encoded (packets never cross a row,
 * as 2.0 asks). Rows are stored top down (descriptor bit 5),
 * and the TGA 2.0 footer is added so the file is recognised for certain.
 */
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "palette.h"

/* the 18-byte header, little-endian */
static void header(Buf *b, int cmtype, int type, int cmlen, int cmdepth, i32 w, i32 h, int depth, int abits)
{
  u8 hd[18];
  memset(hd, 0, sizeof hd);
  hd[1] = (u8)cmtype; hd[2] = (u8)type;
  hd[5] = (u8)cmlen; hd[6] = (u8)(cmlen >> 8); hd[7] = (u8)cmdepth;
  hd[12] = (u8)w; hd[13] = (u8)(w >> 8); hd[14] = (u8)h; hd[15] = (u8)(h >> 8);
  hd[16] = (u8)depth; hd[17] = (u8)(0x20 | abits);
  buf_add(b, hd, 18);
}
/* the footer: no extension or developer area, then the signature */
static u8 *finish(Buf *b, size_t *len)
{
  static const u8 foot[26] = { 0, 0, 0, 0, 0, 0, 0, 0, 'T', 'R', 'U', 'E', 'V', 'I', 'S', 'I', 'O', 'N', '-', 'X', 'F', 'I', 'L', 'E', '.', 0 };
  buf_add(b, foot, 26);
  if (b->oom) { buf_free(b); return NULL; }
  *len = b->len;
  return (u8 *)buf_take(b);
}
/* one row of pixels already in file order (bpp bytes each), raw or as RLE packets: a run of two
   or more identical pixels is a repeat packet, anything else goes in raw packets of up to 128 */
static void put_row(Buf *b, const u8 *row, i32 w, int bpp, int rle)
{
  i32 i = 0;
  if (!rle) { buf_add(b, row, (size_t)w * bpp); return; }
  while (i < w) {
    i32 r = 1;
    while (i + r < w && r < 128 && !memcmp(row + (size_t)(i + r) * bpp, row + (size_t)i * bpp, (size_t)bpp)) r++;
    if (r >= 2) {
      buf_addc(b, 0x80 | (r - 1)); buf_add(b, row + (size_t)i * bpp, (size_t)bpp);
      i += r;
    } else {
      i32 l = 1;
      while (i + l < w && l < 128 && (i + l + 1 >= w || memcmp(row + (size_t)(i + l) * bpp, row + (size_t)(i + l + 1) * bpp, (size_t)bpp))) l++;
      buf_addc(b, l - 1); buf_add(b, row + (size_t)i * bpp, (size_t)l * bpp);
      i += l;
    }
  }
}

/* true colour (gray: grey) from RGBA */
static u8 *tga_rgba(const Rgba *im, int gray, int rle, size_t *len)
{
  long i, n = (long)im->w * im->h;
  int alpha = 0, bpp;
  u8 *row;
  i32 x, y;
  Buf b;
  if (!im->px || im->w < 1 || im->h < 1 || im->w > 65535 || im->h > 65535) return NULL;
  for (i = 0; i < n; i++) if (im->px[i * 4 + 3] != 255) { alpha = 1; break; }
  if (alpha) gray = 0;                 /* grey with alpha as 32-bit colour: few readers take 16-bit grey */
  bpp = gray ? 1 : alpha ? 4 : 3;
  if (!(row = (u8 *)malloc((size_t)im->w * 4))) return NULL;
  buf_init(&b);
  header(&b, 0, gray ? (rle ? 11 : 3) : (rle ? 10 : 2), 0, 0, im->w, im->h, bpp * 8, alpha ? 8 : 0);
  for (y = 0; y < im->h && !progress(y, im->h); y++) {
    const u8 *p = im->px + (size_t)y * im->w * 4;
    for (x = 0; x < im->w; x++, p += 4) {
      u8 *o = row + (size_t)x * bpp;
      if (gray) o[0] = (u8)((p[0] * 77 + p[1] * 150 + p[2] * 29 + 128) >> 8);
      else { o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; if (alpha) o[3] = p[3]; }
    }
    put_row(&b, row, im->w, bpp, rle);
  }
  free(row);
  return finish(&b, len);
}
u8 *tga_encode(const Rgba *im, int rle, size_t *len) { return tga_rgba(im, 0, rle, len); }

/* 8-bit indices into a map of npal entries (24-bit; 32-bit with entry `transparent` clear) */
u8 *tga_encode_indexed(const u8 *idx, i32 w, i32 h, const u8 *pal, int npal, int transparent, int rle, size_t *len)
{
  int i, cd = transparent >= 0 ? 4 : 3;
  i32 y;
  Buf b;
  if (!idx || !pal || w < 1 || h < 1 || w > 65535 || h > 65535 || npal < 1 || npal > 256) return NULL;
  buf_init(&b);
  header(&b, 1, rle ? 9 : 1, npal, cd * 8, w, h, 8, 0);
  for (i = 0; i < npal; i++) {
    buf_addc(&b, pal[i * 3 + 2]); buf_addc(&b, pal[i * 3 + 1]); buf_addc(&b, pal[i * 3]);
    if (cd == 4) buf_addc(&b, i == transparent ? 0 : 255);
  }
  for (y = 0; y < h; y++) put_row(&b, idx + (size_t)y * w, w, 1, rle);
  return finish(&b, len);
}

/* by mode: Indexed and Bitmap colour-mapped (a transparent entry when needed), Grayscale grey,
   RGB true colour */
u8 *tga_write_doc(Doc *d, int rle, size_t *len)
{
  u8 *res = NULL;
  if (d->mode == MODE_INDEXED || d->mode == MODE_BITMAP) {
    u8 *al = NULL, *idx = doc_flat_indices(d, &al), pal[768];
    int npal = d->npal, slot;
    if (!idx) { free(al); return NULL; }
    memcpy(pal, d->pal, 768);
    slot = flat_transparent(idx, al, (long)d->w * d->h, pal, &npal);
    res = tga_encode_indexed(idx, d->w, d->h, pal, npal, slot, rle, len);
    free(idx); free(al);
  } else {
    Rgba im;
    im.w = d->w; im.h = d->h;
    if (!(im.px = doc_flat_rgba(d))) return NULL;
    res = tga_rgba(&im, d->mode == MODE_GRAY, rle, len);
    free(im.px);
  }
  return res;
}
