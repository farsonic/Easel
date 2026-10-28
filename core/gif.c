/*
 * gif.c - reading the first frame of a GIF (87a or 89a): its LZW-compressed indexes through the
 * local or global colour table, interlaced or not, with the Graphic Control Extension's
 * transparent colour kept as alpha 0. The frame is placed on the logical screen; anything of the
 * screen it doesn't cover is transparent.
 */
#include <stdlib.h>
#include <string.h>
#include "codec.h"

static u32 le16(const u8 *p) { return p[0] | ((u32)p[1] << 8); }

/* step over a run of data sub-blocks (each a length byte and that many bytes, ended by a 0);
   returns the offset after the terminator, or 0 if the data runs out */
static size_t skip_blocks(const u8 *d, size_t n, size_t o)
{
  while (o < n && d[o]) o += (size_t)d[o] + 1;
  return o < n ? o + 1 : 0;
}

/* decode LZW codes (variable width, least significant bit first, from mcs + 1 to 12 bits) from
   s[0..n) into npx indexes. Returns how many pixels were written; damaged codes end it early */
static long lzw(const u8 *s, size_t n, int mcs, u8 *out, long npx)
{
  u16 *prefix = (u16 *)malloc(sizeof(u16) * 4096);
  u8 *suffix = (u8 *)malloc(4096), *stack = (u8 *)malloc(4097);
  int clear = 1 << mcs, eoi = clear + 1, next = clear + 2, size = mcs + 1, old = -1, first = 0, i;
  long o = 0;
  size_t bitpos = 0, nbits = n * 8;
  if (!prefix || !suffix || !stack) { free(prefix); free(suffix); free(stack); return 0; }
  for (i = 0; i < clear; i++) { suffix[i] = (u8)i; prefix[i] = 0xFFFF; }
  while (o < npx && bitpos + (size_t)size <= nbits) {
    int code = 0, k, sp = 0, c;
    for (k = 0; k < size; k++, bitpos++) code |= ((s[bitpos >> 3] >> (bitpos & 7)) & 1) << k;
    if (code == clear) { next = clear + 2; size = mcs + 1; old = -1; continue; }
    if (code == eoi) break;
    if (old < 0) {                                       /* the first code after a clear: a plain index */
      if (code >= clear) break;
      out[o++] = (u8)code; old = first = code;
      continue;
    }
    if (code < next) c = code;
    else if (code == next) { stack[sp++] = (u8)first; c = old; }   /* the string being defined: old + its first byte */
    else break;
    while (c >= clear && sp < 4096) { stack[sp++] = suffix[c]; c = prefix[c]; }
    if (c >= clear) break;
    stack[sp++] = (u8)c;
    first = c;
    while (sp && o < npx) out[o++] = stack[--sp];
    if (next < 4096) {
      prefix[next] = (u16)old; suffix[next] = (u8)first; next++;
      if (next == (1 << size) && size < 12) size++;
    }
    old = code;
  }
  free(prefix); free(suffix); free(stack);
  return o;
}

/* header and logical screen, the global table, then blocks up to the first image: extensions are
   skipped except a Graphic Control one (for its transparent colour). The image's sub-blocks are
   joined and decompressed; every pixel must be there */
int gif_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  const u8 *gct = NULL, *ct;
  int ngct = 0, nct, trans = -1, mcs, inter, ok = 0;
  i32 sw, sh, left, top, iw, ih, cw, ch, y, x;
  size_t o, e, k, total = 0;
  u8 *data = NULL, *idx = NULL;
  const char *msg = "Damaged GIF file";
  out->w = out->h = 0; out->px = NULL;
  if (n < 13 || (memcmp(d, "GIF87a", 6) && memcmp(d, "GIF89a", 6))) { codec_err(err, errcap, "Not a GIF file"); return 0; }
  sw = (i32)le16(d + 6); sh = (i32)le16(d + 8);
  o = 13;
  if (d[10] & 0x80) {
    ngct = 2 << (d[10] & 7);
    gct = d + 13;
    o += (size_t)ngct * 3;
    if (o > n) { codec_err(err, errcap, "Truncated GIF file"); return 0; }
  }
  for (;;) {
    if (o >= n) { codec_err(err, errcap, "Truncated GIF file"); return 0; }
    if (d[o] == 0x21) {                                  /* extension */
      if (o + 2 > n) { codec_err(err, errcap, "Truncated GIF file"); return 0; }
      if (d[o + 1] == 0xF9 && o + 7 < n && d[o + 2] >= 4) trans = (d[o + 3] & 1) ? d[o + 6] : -1;
      if (!(o = skip_blocks(d, n, o + 2))) { codec_err(err, errcap, "Truncated GIF file"); return 0; }
    } else if (d[o] == 0x2C) break;                      /* an image */
    else if (d[o] == 0x3B) { codec_err(err, errcap, "The GIF file has no picture"); return 0; }
    else { codec_err(err, errcap, msg); return 0; }
  }
  if (o + 10 > n) { codec_err(err, errcap, "Truncated GIF file"); return 0; }
  left = (i32)le16(d + o + 1); top = (i32)le16(d + o + 3); iw = (i32)le16(d + o + 5); ih = (i32)le16(d + o + 7);
  inter = d[o + 9] & 0x40;
  ct = gct; nct = ngct;
  if (d[o + 9] & 0x80) { nct = 2 << (d[o + 9] & 7); ct = d + o + 10; o += (size_t)nct * 3; }
  o += 10;
  if (o + 1 > n) { codec_err(err, errcap, "Truncated GIF file"); return 0; }
  mcs = d[o++];
  if (mcs < 1 || mcs > 11) { codec_err(err, errcap, msg); return 0; }
  /* the canvas: the logical screen, grown to hold the frame if the screen is too small */
  cw = left + iw > sw ? left + iw : sw; ch = top + ih > sh ? top + ih : sh;
  if (iw < 1 || ih < 1 || cw > MAXDIM || ch > MAXDIM || (long)cw * ch > CODEC_MAXPIXELS) { codec_err(err, errcap, "Bad GIF picture size"); return 0; }
  /* the compressed data's length, then the data joined up */
  for (e = o; e < n && d[e]; e += (size_t)d[e] + 1) total += d[e];
  if (e >= n && !total) { codec_err(err, errcap, "Truncated GIF file"); return 0; }
  if (!(data = (u8 *)malloc(total + 1)) || !(idx = (u8 *)malloc((size_t)iw * ih))) { msg = "Out of memory"; goto done; }
  for (e = o, k = 0; e < n && d[e]; e += (size_t)d[e] + 1) {
    size_t l = d[e];
    if (e + 1 + l > n) l = n - e - 1;
    memcpy(data + k, d + e + 1, l); k += l;
  }
  if (lzw(data, k, mcs, idx, (long)iw * ih) < (long)iw * ih) { msg = "Truncated GIF file"; goto done; }
  if (!rgba_alloc(out, cw, ch)) { msg = "Out of memory"; goto done; }
  {
    /* interlaced rows come in four passes: every 8th from 0, every 8th from 4, every 4th from 2, every 2nd from 1 */
    static const u8 pstart[4] = { 0, 4, 2, 1 }, pstep[4] = { 8, 8, 4, 2 };
    int pass = 0;
    i32 ry = 0;
    for (y = 0; y < ih && !progress(y, ih); y++) {
      const u8 *s = idx + (size_t)y * iw;
      u8 *row;
      if (inter) {
        while (ry >= ih && pass < 3) { pass++; ry = pstart[pass]; }
        row = out->px + ((size_t)(top + ry) * cw + left) * 4;
        ry += pstep[pass];
      } else row = out->px + ((size_t)(top + y) * cw + left) * 4;
      for (x = 0; x < iw; x++, row += 4) {
        int v = s[x];
        if (v == trans) continue;
        if (ct && v < nct) { row[0] = ct[v * 3]; row[1] = ct[v * 3 + 1]; row[2] = ct[v * 3 + 2]; }
        else if (!ct) row[0] = row[1] = row[2] = (u8)v;
        row[3] = 255;
      }
    }
  }
  ok = 1;
done:
  if (!ok) { rgba_free(out); codec_err(err, errcap, msg); }
  free(data); free(idx);
  return ok;
}
