/*
 * tiffw.c - writing TIFF (0.6): baseline big-endian ("MM") files of one image in strips, written
 * from the TIFF 6.0 specification (Adobe, 1992). RGB and RGB with unassociated alpha, grey and grey
 * with alpha (8 bits a sample), palette (8 bits, a 256-entry ColorMap) and bilevel (1 bit,
 * WhiteIsZero). Compression: none, PackBits (each row packed on its own), LZW (with the code width
 * growing one code early, as every reader expects) or Deflate, the last two with the horizontal
 * predictor on 8-bit continuous-tone samples. Resolution in dots per inch, Software, and the File
 * Info as DocumentName, ImageDescription, Artist, Copyright and an XMP packet (tag 700).
 *
 * Every number is written a byte at a time, most significant first, so the file is the same from
 * the 68k and from the native tests. The picture is converted a strip (about 64 KB) at a time.
 */
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "palette.h"

const char *const tiff_comp_names[TIFFC_N + 1] = { "None", "PackBits", "LZW", "Deflate", NULL };

/* what the samples are */
enum { TK_RGB, TK_RGBA, TK_GRAY, TK_GRAYA, TK_PAL, TK_BILEVEL };
typedef struct {
  i32 w, h;
  int kind;
  const u8 *px;                       /* RGBA (all but TK_PAL) */
  const u8 *idx, *pal;                /* TK_PAL: indices and 256 x 3 colours */
} Src;

static int spp_of(int kind) { return kind == TK_RGBA ? 4 : kind == TK_RGB ? 3 : kind == TK_GRAYA ? 2 : 1; }
static u8 luma(const u8 *p) { return (u8)((p[0] * 77 + p[1] * 150 + p[2] * 29 + 128) >> 8); }

/* row y of the samples into out (rowbytes long) */
static void src_row(const Src *s, i32 y, u8 *out)
{
  const u8 *p = s->px ? s->px + (size_t)y * s->w * 4 : NULL;
  i32 x;
  switch (s->kind) {
  case TK_RGB: for (x = 0; x < s->w; x++, p += 4) { out[x * 3] = p[0]; out[x * 3 + 1] = p[1]; out[x * 3 + 2] = p[2]; } break;
  case TK_RGBA: memcpy(out, p, (size_t)s->w * 4); break;
  case TK_GRAY: for (x = 0; x < s->w; x++, p += 4) out[x] = luma(p); break;
  case TK_GRAYA: for (x = 0; x < s->w; x++, p += 4) { out[x * 2] = luma(p); out[x * 2 + 1] = p[3]; } break;
  case TK_PAL: memcpy(out, s->idx + (size_t)y * s->w, (size_t)s->w); break;
  default:                                               /* WhiteIsZero: a set bit is black (over white) */
    memset(out, 0, (size_t)(s->w + 7) / 8);
    for (x = 0; x < s->w; x++, p += 4) {
      int a = p[3], l = (luma(p) * a + 255 * (255 - a)) / 255;
      if (l < 128) out[x >> 3] |= (u8)(0x80 >> (x & 7));
    }
  }
}

/* ------------------------------------------------------------------ PackBits */
/* one row: runs of 3 or more as repeat packets, the rest as literal packets of up to 128 */
static void packbits(Buf *b, const u8 *s, size_t n)
{
  size_t i = 0;
  while (i < n) {
    size_t r = 1;
    while (i + r < n && r < 128 && s[i + r] == s[i]) r++;
    if (r >= 3 || (r == 2 && i + r == n)) {
      buf_addc(b, (int)(257 - r) & 255); buf_addc(b, s[i]);
      i += r;
    } else {
      size_t l = 0;
      /* literals until a run of three starts or 128 are taken */
      while (i + l < n && l < 128) {
        if (i + l + 2 < n && s[i + l] == s[i + l + 1] && s[i + l] == s[i + l + 2]) break;
        l++;
      }
      buf_addc(b, (int)(l - 1)); buf_add(b, s + i, l);
      i += l;
    }
  }
}

/* ------------------------------------------------------------------ LZW */
#define LZW_HASH 5003                 /* a prime a little over the 4096 codes */
typedef struct { i32 key[LZW_HASH]; u16 code[LZW_HASH]; u32 bits; int nb; } LzwEnc;
/* a code of width bits, most significant bit first */
static void lzw_put(LzwEnc *z, Buf *b, u32 code, int width)
{
  z->bits = (z->bits << width) | code;
  z->nb += width;
  while (z->nb >= 8) { z->nb -= 8; buf_addc(b, (int)((z->bits >> z->nb) & 255)); }
  z->bits &= (1u << z->nb) - 1;
}
/* one strip: a clear code, the codes (the table cleared again before it would pass 4094 entries),
   end of information. The width goes up when the next code to be made needs it, which is one code
   early from the decoder's side, as TIFF's LZW has it */
static void lzw_strip(LzwEnc *z, Buf *b, const u8 *s, size_t n)
{
  u32 next = 258, w;
  int width = 9;
  size_t i;
  z->bits = 0; z->nb = 0;
  for (i = 0; i < LZW_HASH; i++) z->key[i] = -1;
  lzw_put(z, b, 256, 9);
  if (!n) { lzw_put(z, b, 257, 9); if (z->nb) buf_addc(b, (int)((z->bits << (8 - z->nb)) & 255)); return; }
  w = s[0];
  for (i = 1; i < n; i++) {
    i32 key = (i32)((w << 8) | s[i]);
    u32 h = ((u32)s[i] << 4 ^ w) % LZW_HASH;
    while (z->key[h] >= 0 && z->key[h] != key) h = h + 1 == LZW_HASH ? 0 : h + 1;
    if (z->key[h] == key) { w = z->code[h]; continue; }
    lzw_put(z, b, w, width);
    if (next == 4094) {                                  /* the table is full: start again */
      lzw_put(z, b, 256, width);
      for (h = 0; h < LZW_HASH; h++) z->key[h] = -1;
      next = 258; width = 9;
    } else {
      z->key[h] = key; z->code[h] = (u16)next++;
      if (next >= (1u << width) && width < 12) width++;
    }
    w = s[i];
  }
  lzw_put(z, b, w, width);
  next++;                                                /* the decoder makes an entry for that code too */
  if (next >= (1u << width) && width < 12) width++;
  lzw_put(z, b, 257, width);
  if (z->nb) buf_addc(b, (int)((z->bits << (8 - z->nb)) & 255));
}

/* ------------------------------------------------------------------ the directory */
#define MAXENT 24
typedef struct { u16 tag, type; u32 count; u8 val[4]; size_t ext; int isext; } Ent;
typedef struct { Ent e[MAXENT]; int n; Buf ext; } Dir;
static void put16(u8 *p, u32 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static u32 tsize(int type) { return type == 3 ? 2 : type == 4 ? 4 : type == 5 ? 8 : 1; }
/* an entry whose values are already big-endian bytes; more than four go after the directory
   (each at an even offset) */
static void ent_add(Dir *d, int tag, int type, u32 count, const u8 *bytes)
{
  Ent *e = &d->e[d->n++];
  u32 sz = tsize(type) * count;
  e->tag = (u16)tag; e->type = (u16)type; e->count = count;
  memset(e->val, 0, 4);
  if (sz <= 4) { memcpy(e->val, bytes, sz); e->isext = 0; }
  else {
    if (d->ext.len & 1) buf_addc(&d->ext, 0);
    e->ext = d->ext.len; e->isext = 1;
    buf_add(&d->ext, bytes, sz);
  }
}
static void ent_short(Dir *d, int tag, u32 v) { u8 b[2]; put16(b, v); ent_add(d, tag, 3, 1, b); }
static void ent_long(Dir *d, int tag, u32 v) { u8 b[4]; put32(b, v); ent_add(d, tag, 4, 1, b); }
static void ent_text(Dir *d, int tag, const char *s) { if (s && s[0]) ent_add(d, tag, 2, (u32)strlen(s) + 1, (const u8 *)s); }
/* a File Info field as an ASCII tag: in Latin-1, which is what most readers take such tags to be
   (the XMP keeps the exact UTF-8) */
static void ent_latin1(Dir *d, int tag, const char *s)
{
  char tmp[600];
  if (!s[0]) return;
  utf8_to_latin1(s, strlen(s), tmp, sizeof tmp);
  ent_text(d, tag, tmp);
}

/* the whole file: header, the strips, then the directory and the values that don't fit in it */
static u8 *tiff_write(const Src *s, int comp, int dpi, const DocInfo *info, size_t *len)
{
  int spp = spp_of(s->kind), pred = (comp == TIFFC_LZW || comp == TIFFC_DEFLATE) && s->kind != TK_PAL && s->kind != TK_BILEVEL;
  size_t rb = s->kind == TK_BILEVEL ? (size_t)(s->w + 7) / 8 : (size_t)s->w * spp;
  u32 rps = (u32)(65536 / rb), nstrips, i, k;
  u8 *strip = NULL, *offs = NULL, *cnts = NULL, *res = NULL;
  LzwEnc *lz = NULL;
  Buf b;
  Dir dir;
  i32 y;
  if (!s->w || !s->h) return NULL;
  if (rps < 1) rps = 1;
  if (rps > (u32)s->h) rps = (u32)s->h;
  nstrips = ((u32)s->h + rps - 1) / rps;
  buf_init(&b); buf_init(&dir.ext);
  dir.n = 0;
  if (!(strip = (u8 *)malloc(rb * rps)) || !(offs = (u8 *)malloc((size_t)nstrips * 4)) || !(cnts = (u8 *)malloc((size_t)nstrips * 4)) ||
      (comp == TIFFC_LZW && !(lz = (LzwEnc *)malloc(sizeof *lz)))) goto fail;
  buf_add(&b, "MM\0\052\0\0\0\0", 8);
  /* the strips */
  for (i = 0, y = 0; i < nstrips; i++) {
    u32 rows = (u32)s->h - (u32)y < rps ? (u32)s->h - (u32)y : rps, r;
    size_t raw = rows * rb, at = b.len;
    for (r = 0; r < rows; r++) {
      u8 *row = strip + r * rb;
      src_row(s, y + (i32)r, row);
      if (pred) for (k = (u32)(rb - 1); k >= (u32)spp; k--) row[k] = (u8)(row[k] - row[k - spp]);
    }
    switch (comp) {
    case TIFFC_PACKBITS: for (r = 0; r < rows; r++) packbits(&b, strip + r * rb, rb); break;
    case TIFFC_LZW: lzw_strip(lz, &b, strip, raw); break;
    case TIFFC_DEFLATE: {
      size_t zl;
      u8 *z = zlib_compress(strip, raw, &zl);
      if (!z) goto fail;
      buf_add(&b, z, zl);
      free(z);
      break; }
    default: buf_add(&b, strip, raw);
    }
    put32(offs + i * 4, (u32)at); put32(cnts + i * 4, (u32)(b.len - at));
    if (b.len & 1) buf_addc(&b, 0);                      /* strips start on even offsets */
    y += (i32)rows;
  }
  /* the directory, in tag order */
  ent_long(&dir, 256, (u32)s->w);
  ent_long(&dir, 257, (u32)s->h);
  {
    u8 bps[8];
    for (k = 0; k < (u32)spp; k++) put16(bps + k * 2, s->kind == TK_BILEVEL ? 1 : 8);
    ent_add(&dir, 258, 3, (u32)spp, bps);
  }
  ent_short(&dir, 259, comp == TIFFC_PACKBITS ? 32773 : comp == TIFFC_LZW ? 5 : comp == TIFFC_DEFLATE ? 8 : 1);
  ent_short(&dir, 262, s->kind == TK_BILEVEL ? 0 : s->kind == TK_PAL ? 3 : s->kind <= TK_RGBA ? 2 : 1);
  if (info) { ent_latin1(&dir, 269, info->title); ent_latin1(&dir, 270, info->description); }
  ent_add(&dir, 273, 4, nstrips, offs);
  ent_short(&dir, 277, (u32)spp);
  ent_long(&dir, 278, rps);
  ent_add(&dir, 279, 4, nstrips, cnts);
  if (dpi > 0) {
    u8 r[8];
    put32(r, (u32)dpi); put32(r + 4, 1);
    ent_add(&dir, 282, 5, 1, r);
    ent_add(&dir, 283, 5, 1, r);
  }
  ent_short(&dir, 284, 1);
  if (dpi > 0) ent_short(&dir, 296, 2);
  ent_text(&dir, 305, "Easel");
  if (info) ent_latin1(&dir, 315, info->author);
  if (pred) ent_short(&dir, 317, 2);
  if (s->kind == TK_PAL) {
    u8 *cm = (u8 *)malloc(768 * 2);
    if (!cm) goto fail;
    for (k = 0; k < 256; k++)
      for (i = 0; i < 3; i++) put16(cm + (i * 256 + k) * 2, s->pal[k * 3 + i] * 257u);
    ent_add(&dir, 320, 3, 768, cm);
    free(cm);
  }
  if (s->kind == TK_RGBA || s->kind == TK_GRAYA) ent_short(&dir, 338, 2);
  if (info && !docinfo_empty(info)) {
    Buf x;
    buf_init(&x);
    xmp_write(&x, info);
    if (!x.oom) ent_add(&dir, 700, 1, (u32)x.len, (const u8 *)x.data);
    buf_free(&x);
    ent_latin1(&dir, 33432, info->copyright);
  }
  if (dir.ext.oom || b.oom) goto fail;
  {
    size_t ifd = b.len, base = ifd + 2 + (size_t)dir.n * 12 + 4;
    u8 e[12];
    put32((u8 *)b.data + 4, (u32)ifd);
    put16(e, (u32)dir.n); buf_add(&b, e, 2);
    for (i = 0; i < (u32)dir.n; i++) {
      Ent *t = &dir.e[i];
      put16(e, t->tag); put16(e + 2, t->type); put32(e + 4, t->count);
      if (t->isext) put32(e + 8, (u32)(base + t->ext)); else memcpy(e + 8, t->val, 4);
      buf_add(&b, e, 12);
    }
    memset(e, 0, 4); buf_add(&b, e, 4);                  /* no next directory */
    buf_add(&b, dir.ext.data, dir.ext.len);
  }
  if (b.oom) goto fail;
  *len = b.len;
  res = (u8 *)buf_take(&b);
fail:
  free(strip); free(offs); free(cnts); free(lz);
  buf_free(&b); buf_free(&dir.ext);
  return res;
}

/* RGB, or RGBA when anything isn't opaque */
u8 *tiff_encode(const Rgba *im, int comp, int dpi, size_t *len)
{
  Src s;
  long i, n = (long)im->w * im->h;
  if (!im->px || im->w < 1 || im->h < 1) return NULL;
  s.w = im->w; s.h = im->h; s.px = im->px; s.idx = s.pal = NULL; s.kind = TK_RGB;
  for (i = 0; i < n; i++) if (im->px[i * 4 + 3] != 255) { s.kind = TK_RGBA; break; }
  return tiff_write(&s, comp, dpi, NULL, len);
}

/* by the document's mode: RGB(A); grey (with alpha); palette (RGBA instead if anything is
   transparent, which a palette TIFF can't say); bilevel. With the File Info */
u8 *tiff_encode_doc(Doc *d, int comp, size_t *len)
{
  Src s;
  u8 *px, *res = NULL, *idx = NULL, *al = NULL;
  long i, n = (long)d->w * d->h;
  int trans = 0;
  if (!(px = doc_flat_rgba(d))) return NULL;
  for (i = 0; i < n; i++) if (px[i * 4 + 3] != 255) { trans = 1; break; }
  s.w = d->w; s.h = d->h; s.px = px; s.idx = s.pal = NULL;
  switch (d->mode) {
  case MODE_GRAY: s.kind = trans ? TK_GRAYA : TK_GRAY; break;
  case MODE_BITMAP: s.kind = TK_BILEVEL; break;
  case MODE_INDEXED:
    if (!trans && (idx = doc_flat_indices(d, &al)) != NULL) { s.kind = TK_PAL; s.idx = idx; s.pal = d->pal; break; }
    if (!trans) { free(px); return NULL; }
    s.kind = TK_RGBA; break;
  default: s.kind = trans ? TK_RGBA : TK_RGB;
  }
  res = tiff_write(&s, comp, d->dpi, &d->info, len);
  free(px); free(idx); free(al);
  return res;
}
