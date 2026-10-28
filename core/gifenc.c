/*
 * gifenc.c - writing GIF89a (Easel 0.4): one frame with a global colour table, an optional
 * transparent index (in a Graphic Control Extension), interlaced or not, LZW-compressed as the
 * GIF89a specification describes (variable code sizes from the minimum + 1 up to 12 bits, a clear
 * code when the 4096-entry table fills, data in sub-blocks of up to 255 bytes).
 * Portable C99; every number is written a byte at a time (little-endian, as GIF keeps them).
 */
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "palette.h"

static void le16(Buf *b, u32 v) { buf_addc(b, (int)(v & 255)); buf_addc(b, (int)((v >> 8) & 255)); }

/* the bit packer: codes least significant bit first, flushed into sub-blocks */
typedef struct { Buf *b; u32 acc; int nbits; u8 blk[256]; int nblk; } Bits;
static void blk_flush(Bits *s)
{
  if (!s->nblk) return;
  buf_addc(s->b, s->nblk);
  buf_add(s->b, s->blk, (size_t)s->nblk);
  s->nblk = 0;
}
static void put_code(Bits *s, u32 code, int size)
{
  s->acc |= code << s->nbits;
  s->nbits += size;
  while (s->nbits >= 8) {
    s->blk[s->nblk++] = (u8)(s->acc & 255);
    if (s->nblk == 255) blk_flush(s);
    s->acc >>= 8; s->nbits -= 8;
  }
}

/* the string table as a hash of (prefix code, next index) -> code */
#define GH_SIZE 8192
typedef struct { u32 key[GH_SIZE]; u16 code[GH_SIZE]; } Dict;
static void dict_clear(Dict *t) { memset(t->key, 0, sizeof t->key); }
static int dict_find(const Dict *t, u32 k, u32 *slot)
{
  u32 h = (k * 2654435761UL) >> 19;
  for (;;) {
    h &= GH_SIZE - 1;
    if (!t->key[h]) { *slot = h; return -1; }
    if (t->key[h] == k) { *slot = h; return t->code[h]; }
    h++;
  }
}

u8 *gif_encode(const u8 *idx, i32 w, i32 h, const u8 *pal, int npal, int transparent, int interlace, size_t *len)
{
  Buf b;
  Bits bits;
  Dict *dict;
  int k = 1, mcs, size, i, pass;
  u32 clear, next, prefix = 0, slot;
  long cnt = 0;
  if (!idx || !pal || w < 1 || h < 1 || w > 65535 || h > 65535 || npal < 1 || npal > 256) return NULL;
  while ((1 << k) < npal) k++;
  mcs = k < 2 ? 2 : k;
  if (!(dict = (Dict *)malloc(sizeof *dict))) return NULL;
  buf_init(&b);
  buf_add(&b, "GIF89a", 6);
  le16(&b, (u32)w); le16(&b, (u32)h);
  buf_addc(&b, 0x80 | ((k - 1) << 4) | (k - 1));             /* a global table of 2^k entries */
  buf_addc(&b, 0); buf_addc(&b, 0);                            /* background index, square pixels */
  for (i = 0; i < (1 << k); i++) {
    if (i < npal) buf_add(&b, pal + i * 3, 3);
    else { buf_addc(&b, 0); buf_addc(&b, 0); buf_addc(&b, 0); }
  }
  if (transparent >= 0 && transparent < (1 << k)) {
    buf_addc(&b, 0x21); buf_addc(&b, 0xF9); buf_addc(&b, 4);
    buf_addc(&b, 0x01); le16(&b, 0); buf_addc(&b, transparent); buf_addc(&b, 0);
  }
  buf_addc(&b, 0x2C); le16(&b, 0); le16(&b, 0); le16(&b, (u32)w); le16(&b, (u32)h);
  buf_addc(&b, interlace ? 0x40 : 0);
  buf_addc(&b, mcs);
  memset(&bits, 0, sizeof bits); bits.b = &b;
  clear = 1u << mcs; next = clear + 2; size = mcs + 1;
  dict_clear(dict);
  put_code(&bits, clear, size);
  /* the rows in the order written: straight down, or the four interlace passes */
  for (pass = interlace ? 0 : 3; pass < 4; pass++) {
    static const int start[4] = { 0, 4, 2, 1 }, step[4] = { 8, 8, 4, 2 };
    i32 y0 = interlace ? start[pass] : 0, dy = interlace ? step[pass] : 1, y, x;
    for (y = y0; y < h; y += dy)
      for (x = 0; x < w; x++) {
        u32 c = idx[(long)y * w + x] & ((1u << k) - 1), key;
        int found;
        if (!cnt++) { prefix = c; continue; }
        key = ((prefix << 8) | c) + 1;
        found = dict_find(dict, key, &slot);
        if (found >= 0) { prefix = (u32)found; continue; }
        put_code(&bits, prefix, size);
        if (next < 4096) {
          if (next == (1u << size)) size++;
          dict->key[slot] = key; dict->code[slot] = (u16)next++;
        } else {
          put_code(&bits, clear, size);
          dict_clear(dict);
          next = clear + 2; size = mcs + 1;
        }
        prefix = c;
      }
  }
  put_code(&bits, prefix, size);
  put_code(&bits, clear + 1, size);                            /* end of information */
  if (bits.nbits > 0) { bits.blk[bits.nblk++] = (u8)(bits.acc & 255); bits.nbits = 0; }
  blk_flush(&bits);
  buf_addc(&b, 0);                                             /* the block terminator */
  buf_addc(&b, 0x3B);
  free(dict);
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}

u8 *gif_encode_rgba(const Rgba *im, int maxc, int method, int dither, int interlace, size_t *len)
{
  long n = (long)im->w * im->h, k;
  u8 *idx, *al, pal[768], *out = NULL;
  int npal, slot, anyt = 0;
  if (maxc < 2) maxc = 2;
  if (maxc > 256) maxc = 256;
  for (k = 0; k < n; k++) if (im->px[k * 4 + 3] < 128) { anyt = 1; break; }
  if (!(idx = (u8 *)malloc((size_t)n))) return NULL;
  if (!(al = (u8 *)malloc((size_t)n))) { free(idx); return NULL; }
  for (k = 0; k < n; k++) al[k] = im->px[k * 4 + 3];
  memset(pal, 0, sizeof pal);
  npal = quantize_rgba(im->px, im->w, im->h, anyt && maxc > 2 ? maxc - 1 : maxc, (method & QM_MASK) == QM_FIXED ? (QM_MEDIAN | (method & QM_OCS12)) : method, dither, pal, 0, idx);
  if (npal) {
    slot = anyt ? flat_transparent(idx, al, n, pal, &npal) : -1;
    out = gif_encode(idx, im->w, im->h, pal, npal, slot, interlace, len);
  }
  free(idx); free(al);
  return out;
}

u8 *gif_write_doc(Doc *d, int maxc, int method, int dither, int interlace, size_t *len)
{
  u8 *out = NULL;
  if (d->mode == MODE_INDEXED || d->mode == MODE_BITMAP) {
    u8 *al = NULL, *idx = doc_flat_indices(d, &al), pal[768];
    int npal = d->npal, slot;
    if (!idx) { free(al); return NULL; }
    memcpy(pal, d->pal, sizeof pal);
    slot = flat_transparent(idx, al, (long)d->w * d->h, pal, &npal);
    out = gif_encode(idx, d->w, d->h, pal, npal, slot, interlace, len);
    free(idx); free(al);
  } else {
    Rgba im;
    im.w = d->w; im.h = d->h;
    if (!(im.px = doc_flat_rgba(d))) return NULL;
    if (d->mode == MODE_GRAY) {
      /* a grey document: its own grey ramp, never more than it needs */
      long k, n = (long)d->w * d->h;
      u8 *idx = (u8 *)malloc((size_t)n), pal[768], *al = (u8 *)malloc((size_t)n);
      int npal = 256, i, slot;
      if (idx && al) {
        for (i = 0; i < 256; i++) pal[i * 3] = pal[i * 3 + 1] = pal[i * 3 + 2] = (u8)i;
        for (k = 0; k < n; k++) { idx[k] = im.px[k * 4]; al[k] = im.px[k * 4 + 3]; }
        slot = flat_transparent(idx, al, n, pal, &npal);
        out = gif_encode(idx, d->w, d->h, pal, npal, slot, interlace, len);
      }
      free(idx); free(al);
    } else out = gif_encode_rgba(&im, maxc, method, dither, interlace, len);
    free(im.px);
  }
  return out;
}
