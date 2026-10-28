/*
 * png.c - PNG files: reading every colour type and bit depth (interlaced or not) to RGBA with the
 * transparency kept, and writing 8-bit RGB or RGBA with per-row filters chosen by the usual
 * minimum-sum-of-absolute-differences rule. zip.c does the deflating; this adds the zlib wrapper.
 * CRCs are written but not checked on reading; chunk lengths always are.
 */
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "zip.h"

static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static u32 be16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }
static void put32(Buf *b, u32 v) { buf_addc(b, (int)(v >> 24)); buf_addc(b, (int)((v >> 16) & 255)); buf_addc(b, (int)((v >> 8) & 255)); buf_addc(b, (int)(v & 255)); }

/* ------------------------------------------------------------------ zlib */
/* Adler-32 as zlib defines it, the sums reduced every 5552 bytes so they never overflow 32 bits */
u32 adler32(u32 a, const u8 *p, size_t n)
{
  u32 s1 = a & 0xFFFF, s2 = a >> 16;
  while (n) {
    size_t k = n < 5552 ? n : 5552;
    n -= k;
    while (k--) { s1 += *p++; s2 += s1; }
    s1 %= 65521; s2 %= 65521;
  }
  return (s2 << 16) | s1;
}
/* zip.c's raw deflate inside a zlib header (32 KB window, no dictionary) and Adler-32 trailer */
u8 *zlib_compress(const u8 *in, size_t n, size_t *outlen)
{
  size_t dl;
  u8 *def = zdeflate(in, n, &dl), *o;
  u32 a;
  if (!def) return NULL;
  if (!(o = (u8 *)malloc(dl + 6))) { free(def); return NULL; }
  o[0] = 0x78; o[1] = 0x01;                 /* (0x78 << 8 | 0x01) is a multiple of 31, as required */
  memcpy(o + 2, def, dl);
  free(def);
  a = adler32(1, in, n);
  o[dl + 2] = (u8)(a >> 24); o[dl + 3] = (u8)(a >> 16); o[dl + 4] = (u8)(a >> 8); o[dl + 5] = (u8)a;
  *outlen = dl + 6;
  return o;
}
/* checks the zlib header, then inflates; the Adler-32 is not checked (zinflate stops at the
   final block). Returns the bytes written or -1 */
long zlib_decompress(const u8 *in, size_t n, u8 *out, size_t outcap)
{
  if (n < 3 || (in[0] & 0x0F) != 8 || (in[0] >> 4) > 7 || (((u32)in[0] << 8) | in[1]) % 31 || (in[1] & 0x20)) return -1;
  return zinflate(in + 2, n - 2, out, outcap);
}

/* ------------------------------------------------------------------ reading */
typedef struct {
  u32 w, h;
  int depth, ctype, interlace, chans, bpp;    /* bpp: bits per pixel */
  u8 pal[256][4]; int npal;
  int trns; u32 key[3];                       /* a transparent grey or RGB, at the file's depth */
} PngHead;

/* the Paeth predictor: whichever of left, above and above-left is nearest left + above - above-left */
static int paeth(int a, int b, int c)
{
  int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}
/* undo the filters of h rows of rb bytes (each after its filter byte) in place; fb is the bytes
   per pixel (at least 1). 0 for an unknown filter type */
static int unfilter(u8 *raw, u32 rb, u32 h, int fb)
{
  u32 y, x;
  u8 *prev = NULL;
  for (y = 0; y < h; y++) {
    u8 *r = raw + (size_t)y * (rb + 1), f = r[0], *c = r + 1;
    switch (f) {
    case 0: break;
    case 1: for (x = fb; x < rb; x++) c[x] = (u8)(c[x] + c[x - fb]); break;
    case 2: if (prev) for (x = 0; x < rb; x++) c[x] = (u8)(c[x] + prev[x]); break;
    case 3:
      for (x = 0; x < rb; x++) c[x] = (u8)(c[x] + (((x >= (u32)fb ? c[x - fb] : 0) + (prev ? prev[x] : 0)) >> 1));
      break;
    case 4:
      for (x = 0; x < rb; x++) {
        int a = x >= (u32)fb ? c[x - fb] : 0, b = prev ? prev[x] : 0, cc = prev && x >= (u32)fb ? prev[x - fb] : 0;
        c[x] = (u8)(c[x] + paeth(a, b, cc));
      }
      break;
    default: return 0;
    }
    prev = c;
  }
  return 1;
}
/* one unfiltered row of w pixels to RGBA at out, step bytes apart (4, or more for an Adam7 pass).
   Depths below 8 are scaled up to 0-255; 16-bit samples keep their high byte */
static void png_row(const PngHead *p, const u8 *row, u32 w, u8 *out, u32 step)
{
  u32 x;
  for (x = 0; x < w; x++, out += step) {
    u32 r, g, b, a = 255;
    if (p->depth < 8) {
      u32 bit = x * (u32)p->depth, mx = (1u << p->depth) - 1, v = (row[bit >> 3] >> (8 - p->depth - (bit & 7))) & mx;
      if (p->ctype == 3) { r = p->pal[v][0]; g = p->pal[v][1]; b = p->pal[v][2]; a = p->pal[v][3]; }
      else { if (p->trns && v == p->key[0]) a = 0; r = g = b = v * 255 / mx; }
    } else {
      int s = p->depth / 8;
      const u8 *q = row + (size_t)x * p->chans * s;
      switch (p->ctype) {
      case 0: r = g = b = q[0]; if (p->trns && (s == 2 ? be16(q) : q[0]) == p->key[0]) a = 0; break;
      case 2:
        r = q[0]; g = q[s]; b = q[2 * s];
        if (p->trns && (s == 2 ? be16(q) == p->key[0] && be16(q + 2) == p->key[1] && be16(q + 4) == p->key[2]
                               : q[0] == p->key[0] && q[1] == p->key[1] && q[2] == p->key[2])) a = 0;
        break;
      case 3: r = p->pal[q[0]][0]; g = p->pal[q[0]][1]; b = p->pal[q[0]][2]; a = p->pal[q[0]][3]; break;
      case 4: r = g = b = q[0]; a = q[s]; break;
      default: r = q[0]; g = q[s]; b = q[2 * s]; a = q[3 * s]; break;
      }
    }
    out[0] = (u8)r; out[1] = (u8)g; out[2] = (u8)b; out[3] = (u8)a;
  }
}

/* walks the chunks (each length checked against the file), takes IHDR, PLTE and tRNS, joins the
   IDAT chunks, inflates them whole, unfilters and converts. An Adam7 picture's seven passes are
   placed straight into the output. IEND may be missing if the image data is complete */
int png_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  static const u8 sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  static const u8 a7[7][4] = { { 0, 0, 8, 8 }, { 4, 0, 8, 8 }, { 0, 4, 4, 8 }, { 2, 0, 4, 4 }, { 0, 2, 2, 4 }, { 1, 0, 2, 2 }, { 0, 1, 1, 2 } };
  PngHead *p;
  u8 *idat = NULL, *raw = NULL;
  size_t o;
  unsigned long total = 0, size = 0, k;
  int i, fb, pass, ok = 0, havehdr = 0;
  const char *msg = "Damaged PNG file";
  out->w = out->h = 0; out->px = NULL;
  if (n < 8 || memcmp(d, sig, 8)) { codec_err(err, errcap, "Not a PNG file"); return 0; }
  if (!(p = (PngHead *)calloc(1, sizeof *p))) { codec_err(err, errcap, "Out of memory"); return 0; }
  for (i = 0; i < 256; i++) p->pal[i][3] = 255;
  /* first pass: the header, palette, transparency and how much image data there is */
  for (o = 8; ; ) {
    u32 len;
    const u8 *t, *c;
    if (o == n) break;                                   /* no IEND: allowed if the data is all there */
    if (n - o < 12) { msg = "Truncated PNG file"; goto done; }
    len = be32(d + o); t = d + o + 4; c = d + o + 8;
    if (len > n - o - 12) { msg = "Truncated PNG file"; goto done; }
    if (!havehdr && memcmp(t, "IHDR", 4)) goto done;
    if (!memcmp(t, "IHDR", 4)) {
      if (len < 13 || havehdr) goto done;
      p->w = be32(c); p->h = be32(c + 4); p->depth = c[8]; p->ctype = c[9]; p->interlace = c[12];
      p->chans = p->ctype == 0 ? 1 : p->ctype == 2 ? 3 : p->ctype == 3 ? 1 : p->ctype == 4 ? 2 : p->ctype == 6 ? 4 : 0;
      if (!p->chans || c[10] || c[11] || p->interlace > 1) { msg = "Unsupported PNG type"; goto done; }
      if (p->depth != 1 && p->depth != 2 && p->depth != 4 && p->depth != 8 && p->depth != 16) goto done;
      if ((p->ctype == 2 || p->ctype == 4 || p->ctype == 6) && p->depth < 8) goto done;
      if (p->ctype == 3 && p->depth > 8) goto done;
      if (!p->w || !p->h || p->w > MAXDIM || p->h > MAXDIM || (unsigned long)p->w * p->h > (unsigned long)CODEC_MAXPIXELS) { msg = "The PNG picture is too big"; goto done; }
      p->bpp = p->chans * p->depth;
      havehdr = 1;
    } else if (!memcmp(t, "PLTE", 4)) {
      u32 j;
      p->npal = (int)(len / 3 > 256 ? 256 : len / 3);
      for (j = 0; j < (u32)p->npal; j++) { p->pal[j][0] = c[j * 3]; p->pal[j][1] = c[j * 3 + 1]; p->pal[j][2] = c[j * 3 + 2]; }
    } else if (!memcmp(t, "tRNS", 4)) {
      u32 j;
      if (p->ctype == 3) for (j = 0; j < len && j < 256; j++) p->pal[j][3] = c[j];
      else if (p->ctype == 0 && len >= 2) { p->trns = 1; p->key[0] = be16(c); }
      else if (p->ctype == 2 && len >= 6) { p->trns = 1; p->key[0] = be16(c); p->key[1] = be16(c + 2); p->key[2] = be16(c + 4); }
      if (p->depth == 8) { p->key[0] &= 255; p->key[1] &= 255; p->key[2] &= 255; }
    } else if (!memcmp(t, "IDAT", 4)) {
      if (total + len < total) goto done;
      total += len;
    } else if (!memcmp(t, "IEND", 4)) break;
    o += 12 + (size_t)len;
  }
  if (!havehdr || !total) goto done;
  if (p->ctype == 3 && !p->npal) { msg = "The PNG file has no palette"; goto done; }
  /* the inflated size, worked out from the header */
  if (!p->interlace) size = (unsigned long)p->h * (((unsigned long)p->w * p->bpp + 7) / 8 + 1);
  else for (pass = 0; pass < 7; pass++) {
    u32 pw, ph;
    if (p->w <= a7[pass][0] || p->h <= a7[pass][1]) continue;
    pw = (p->w - a7[pass][0] + a7[pass][2] - 1) / a7[pass][2]; ph = (p->h - a7[pass][1] + a7[pass][3] - 1) / a7[pass][3];
    size += (unsigned long)ph * (((unsigned long)pw * p->bpp + 7) / 8 + 1);
  }
  /* deflate can't squeeze more than about 1032:1, so a tiny file claiming a vast picture is damaged */
  if (size / 1032 > total + 64) goto done;
  if (!(idat = (u8 *)malloc(total)) || !(raw = (u8 *)malloc(size + 1))) { msg = "Out of memory"; goto done; }
  for (o = 8, k = 0; o + 12 <= n; ) {
    u32 len = be32(d + o);
    if (len > n - o - 12) break;
    if (!memcmp(d + o + 4, "IDAT", 4)) { memcpy(idat + k, d + o + 8, len); k += len; }
    if (!memcmp(d + o + 4, "IEND", 4)) break;
    o += 12 + (size_t)len;
  }
  if (zlib_decompress(idat, total, raw, size) != (long)size) goto done;
  if (!rgba_alloc(out, (i32)p->w, (i32)p->h)) { msg = "Out of memory"; goto done; }
  fb = p->bpp >= 8 ? p->bpp / 8 : 1;
  if (!p->interlace) {
    u32 rb = (p->w * (u32)p->bpp + 7) / 8, y;
    if (!unfilter(raw, rb, p->h, fb)) goto done;
    for (y = 0; y < p->h; y++) png_row(p, raw + (size_t)y * (rb + 1) + 1, p->w, out->px + (size_t)y * p->w * 4, 4);
  } else {
    u8 *r = raw;
    for (pass = 0; pass < 7; pass++) {
      u32 sx = a7[pass][0], sy = a7[pass][1], dx = a7[pass][2], dy = a7[pass][3], pw, ph, rb, y;
      if (p->w <= sx || p->h <= sy) continue;
      pw = (p->w - sx + dx - 1) / dx; ph = (p->h - sy + dy - 1) / dy; rb = (pw * (u32)p->bpp + 7) / 8;
      if (!unfilter(r, rb, ph, fb)) goto done;
      for (y = 0; y < ph; y++) png_row(p, r + (size_t)y * (rb + 1) + 1, pw, out->px + ((size_t)(sy + y * dy) * p->w + sx) * 4, dx * 4);
      r += (size_t)ph * (rb + 1);
    }
  }
  ok = 1;
done:
  if (!ok) { rgba_free(out); codec_err(err, errcap, msg); }
  free(p); free(idat); free(raw);
  return ok;
}

/* ------------------------------------------------------------------ writing */
/* one chunk: length, type, data and the CRC of type and data */
static void chunk(Buf *b, const char *type, const u8 *data, u32 len)
{
  u32 crc = crc32_update(0, (const u8 *)type, 4);
  put32(b, len);
  buf_add(b, type, 4);
  if (len) { buf_add(b, data, len); crc = crc32_update(crc, data, len); }
  put32(b, crc);
}
/* the filtered byte for filter f at position x of a row (cur, prev the raw rows, fb bytes a pixel) */
static u8 filt(int f, const u8 *cur, const u8 *prev, u32 x, int fb)
{
  int a = x >= (u32)fb ? cur[x - fb] : 0, b = prev ? prev[x] : 0, c = prev && x >= (u32)fb ? prev[x - fb] : 0;
  switch (f) {
  case 1: return (u8)(cur[x] - a);
  case 2: return (u8)(cur[x] - b);
  case 3: return (u8)(cur[x] - ((a + b) >> 1));
  case 4: return (u8)(cur[x] - paeth(a, b, c));
  }
  return cur[x];
}

/* RGB if every alpha is 255 (a quarter smaller), else RGBA; each row gets whichever of the five
   filters gives the smallest sum of its bytes taken as signed, then the whole lot is deflated */
u8 *png_encode(const Rgba *im, int dpi, size_t *len)
{
  long i, n = (long)im->w * im->h;
  int ch = 3, fb, f, best;
  u32 rb, x, y;
  u8 *filtered = NULL, *rows = NULL, *z = NULL, hdr[13];
  size_t zlen;
  Buf b;
  for (i = 0; i < n; i++) if (im->px[i * 4 + 3] != 255) { ch = 4; break; }
  fb = ch;
  rb = (u32)im->w * ch;
  if (!(filtered = (u8 *)malloc((size_t)im->h * (rb + 1))) || !(rows = (u8 *)malloc((size_t)rb * 2))) { free(filtered); free(rows); return NULL; }
  for (y = 0; y < (u32)im->h; y++) {
    u8 *cur = rows + (y & 1) * rb, *prev = y ? rows + ((y - 1) & 1) * rb : NULL, *o = filtered + (size_t)y * (rb + 1);
    const u8 *s = im->px + (size_t)y * im->w * 4;
    unsigned long sum, bestsum = 0;
    if (ch == 4) memcpy(cur, s, rb);
    else for (x = 0; x < (u32)im->w; x++) { cur[x * 3] = s[x * 4]; cur[x * 3 + 1] = s[x * 4 + 1]; cur[x * 3 + 2] = s[x * 4 + 2]; }
    for (best = 0, f = 0; f < 5; f++) {
      for (sum = 0, x = 0; x < rb; x++) { int v = filt(f, cur, prev, x, fb); sum += (unsigned long)(v < 128 ? v : 256 - v); }
      if (!f || sum < bestsum) { bestsum = sum; best = f; }
    }
    o[0] = (u8)best;
    for (x = 0; x < rb; x++) o[x + 1] = filt(best, cur, prev, x, fb);
  }
  free(rows);
  z = zlib_compress(filtered, (size_t)im->h * (rb + 1), &zlen);
  free(filtered);
  if (!z) return NULL;
  buf_init(&b);
  buf_add(&b, "\211PNG\r\n\032\n", 8);
  hdr[0] = (u8)(im->w >> 24); hdr[1] = (u8)(im->w >> 16); hdr[2] = (u8)(im->w >> 8); hdr[3] = (u8)im->w;
  hdr[4] = (u8)(im->h >> 24); hdr[5] = (u8)(im->h >> 16); hdr[6] = (u8)(im->h >> 8); hdr[7] = (u8)im->h;
  hdr[8] = 8; hdr[9] = (u8)(ch == 4 ? 6 : 2); hdr[10] = hdr[11] = hdr[12] = 0;
  chunk(&b, "IHDR", hdr, 13);
  if (dpi > 0) {
    u8 phys[9];
    u32 ppm = (u32)(((unsigned long)dpi * 10000UL + 127) / 254);
    phys[0] = phys[4] = (u8)(ppm >> 24); phys[1] = phys[5] = (u8)(ppm >> 16); phys[2] = phys[6] = (u8)(ppm >> 8); phys[3] = phys[7] = (u8)ppm;
    phys[8] = 1;                                  /* metres */
    chunk(&b, "pHYs", phys, 9);
  }
  chunk(&b, "IDAT", z, (u32)zlen);
  free(z);
  chunk(&b, "IEND", NULL, 0);
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}

/* ------------------------------------------------------------------ 0.4: palette and grey PNGs */
/* the file around already filtered rows: IHDR (depth, colour type), any extra chunks, IDAT, IEND */
static u8 *png_wrap(i32 w, i32 h, int depth, int ctype, const u8 *filtered, size_t flen, const u8 *pal, int npal,
                    const u8 *trns, int ntrns, int dpi, size_t *len)
{
  u8 hdr[13], *z;
  size_t zlen;
  Buf b;
  if (!(z = zlib_compress(filtered, flen, &zlen))) return NULL;
  buf_init(&b);
  buf_add(&b, "\211PNG\r\n\032\n", 8);
  hdr[0] = (u8)(w >> 24); hdr[1] = (u8)(w >> 16); hdr[2] = (u8)(w >> 8); hdr[3] = (u8)w;
  hdr[4] = (u8)(h >> 24); hdr[5] = (u8)(h >> 16); hdr[6] = (u8)(h >> 8); hdr[7] = (u8)h;
  hdr[8] = (u8)depth; hdr[9] = (u8)ctype; hdr[10] = hdr[11] = hdr[12] = 0;
  chunk(&b, "IHDR", hdr, 13);
  if (pal) chunk(&b, "PLTE", pal, (u32)npal * 3);
  if (trns && ntrns > 0) chunk(&b, "tRNS", trns, (u32)ntrns);
  if (dpi > 0) {
    u8 phys[9];
    u32 ppm = (u32)(((unsigned long)dpi * 10000UL + 127) / 254);
    phys[0] = phys[4] = (u8)(ppm >> 24); phys[1] = phys[5] = (u8)(ppm >> 16); phys[2] = phys[6] = (u8)(ppm >> 8); phys[3] = phys[7] = (u8)ppm;
    phys[8] = 1;
    chunk(&b, "pHYs", phys, 9);
  }
  chunk(&b, "IDAT", z, (u32)zlen);
  free(z);
  chunk(&b, "IEND", NULL, 0);
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}
/* colour type 3: 1, 2, 4 or 8 bits a pixel (the fewest that hold npal entries), rows unfiltered as
   the PNG specification advises for palette pictures; tRNS gives the first ntrns entries' alpha */
u8 *png_encode_indexed(const u8 *idx, i32 w, i32 h, const u8 *pal, int npal, const u8 *trns, int ntrns, int dpi, size_t *len)
{
  int depth = npal <= 2 ? 1 : npal <= 4 ? 2 : npal <= 16 ? 4 : 8, per = 8 / depth;
  u32 rb = (u32)((w * depth + 7) / 8), x, y;
  u8 *f, *out;
  if (!idx || !pal || w < 1 || h < 1 || npal < 1 || npal > 256) return NULL;
  if (!(f = (u8 *)calloc((size_t)h, rb + 1))) return NULL;
  for (y = 0; y < (u32)h; y++) {
    u8 *o = f + (size_t)y * (rb + 1) + 1;
    const u8 *s = idx + (size_t)y * w;
    for (x = 0; x < (u32)w; x++) o[x / per] |= (u8)((s[x] & ((1 << depth) - 1)) << ((per - 1 - x % per) * depth));
  }
  if (ntrns > npal) ntrns = npal;
  out = png_wrap(w, h, depth, 3, f, (size_t)h * (rb + 1), pal, npal, trns, ntrns, dpi, len);
  free(f);
  return out;
}
/* colour type 0 (grey) or 4 (grey and alpha, when any pixel isn't opaque), 8 bits; the grey is the
   luminance (Rec. 601), filtered as png_encode does */
u8 *png_encode_gray(const Rgba *im, int dpi, size_t *len)
{
  long i, n = (long)im->w * im->h;
  int ch = 1, f, best;
  u32 rb, x, y;
  u8 *filtered, *rows, *out;
  for (i = 0; i < n; i++) if (im->px[i * 4 + 3] != 255) { ch = 2; break; }
  rb = (u32)im->w * ch;
  if (!(filtered = (u8 *)malloc((size_t)im->h * (rb + 1))) || !(rows = (u8 *)malloc((size_t)rb * 2))) { free(filtered); return NULL; }
  for (y = 0; y < (u32)im->h; y++) {
    u8 *cur = rows + (y & 1) * rb, *prev = y ? rows + ((y - 1) & 1) * rb : NULL, *o = filtered + (size_t)y * (rb + 1);
    const u8 *s = im->px + (size_t)y * im->w * 4;
    unsigned long sum, bestsum = 0;
    for (x = 0; x < (u32)im->w; x++) {
      cur[x * ch] = (u8)((s[x * 4] * 77 + s[x * 4 + 1] * 150 + s[x * 4 + 2] * 29 + 128) >> 8);
      if (ch == 2) cur[x * 2 + 1] = s[x * 4 + 3];
    }
    for (best = 0, f = 0; f < 5; f++) {
      for (sum = 0, x = 0; x < rb; x++) { int v = filt(f, cur, prev, x, ch); sum += (unsigned long)(v < 128 ? v : 256 - v); }
      if (!f || sum < bestsum) { bestsum = sum; best = f; }
    }
    o[0] = (u8)best;
    for (x = 0; x < rb; x++) o[x + 1] = filt(best, cur, prev, x, ch);
  }
  free(rows);
  out = png_wrap(im->w, im->h, 8, ch == 2 ? 4 : 0, filtered, (size_t)im->h * (rb + 1), NULL, 0, NULL, 0, dpi, len);
  free(filtered);
  return out;
}
