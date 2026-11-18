/*
 * xwrite.c - more picture writers (Easel 1.1), see xwrite.h: WebP lossless (VP8L, as Google's
 * "WebP Lossless Bitstream Specification" describes it), PDF 1.4 (one page), Netpbm, QOI (the
 * qoiformat.org specification), Windows ICO, DDS (Microsoft's DDS_HEADER, BC1/BC3 blocks), DPX
 * (SMPTE 268M) and FITS (NASA's FITS standard 4.0). Portable C99.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "xwrite.h"
#include "palette.h"

static void le16(Buf *b, u32 v) { buf_addc(b, (int)(v & 255)); buf_addc(b, (int)((v >> 8) & 255)); }
static void le32(Buf *b, u32 v) { le16(b, v & 0xFFFF); le16(b, v >> 16); }
static void be16(Buf *b, u32 v) { buf_addc(b, (int)((v >> 8) & 255)); buf_addc(b, (int)(v & 255)); }
static void be32(Buf *b, u32 v) { be16(b, v >> 16); be16(b, v & 0xFFFF); }
static void put_le32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static void put_be32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static void put_be16(u8 *p, u32 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static u8 *done(Buf *b, size_t *len)
{
  if (b->oom) { buf_free(b); return NULL; }
  *len = b->len;
  return (u8 *)buf_take(b);
}
static int any_alpha(const Rgba *im)
{
  long n = (long)im->w * im->h, k;
  for (k = 0; k < n; k++) if (im->px[k * 4 + 3] != 255) return 1;
  return 0;
}
static int all_grey(const Rgba *im)
{
  long n = (long)im->w * im->h, k;
  const u8 *p = im->px;
  for (k = 0; k < n; k++, p += 4) if (p[0] != p[1] || p[1] != p[2]) return 0;
  return 1;
}

/* ================================================================== WebP lossless */
/* the bit writer: least significant bit first, as VP8L reads */
typedef struct { Buf *b; u32 acc; int n; } BitW;
static void bw_put(BitW *w, u32 v, int n)
{
  w->acc |= (v & ((n >= 32 ? 0 : (1u << n)) - 1)) << w->n;
  w->n += n;
  while (w->n >= 8) { buf_addc(w->b, (int)(w->acc & 255)); w->acc >>= 8; w->n -= 8; }
}
static void bw_flush(BitW *w) { if (w->n > 0) buf_addc(w->b, (int)(w->acc & 255)); w->acc = 0; w->n = 0; }

/* code lengths (at most maxlen) for the frequencies f[0..n): Huffman's, the counts flattened and
   tried again while the longest code is too long */
static void huff_lengths(const u32 *freq, int n, int maxlen, u8 *len)
{
  static u32 f[2 * 512];
  static int parent[2 * 512], alive[2 * 512];
  int i, nn, used, tries;
  for (tries = 0; ; tries++) {
    int longest = 0;
    used = 0;
    for (i = 0; i < n; i++) {
      f[i] = freq[i] ? (tries ? (freq[i] >> tries) | 1 : freq[i]) : 0;
      alive[i] = freq[i] != 0; parent[i] = -1; len[i] = 0;
      if (freq[i]) used++;
    }
    if (used <= 1) { for (i = 0; i < n; i++) if (freq[i]) len[i] = 1; return; }
    nn = n;
    for (;;) {                                           /* join the two rarest until one tree is left */
      int a = -1, b = -1;
      for (i = 0; i < nn; i++) if (alive[i]) {
        if (a < 0 || f[i] < f[a]) { b = a; a = i; }
        else if (b < 0 || f[i] < f[b]) b = i;
      }
      if (b < 0) break;
      f[nn] = f[a] + f[b]; alive[nn] = 1; parent[nn] = -1;
      alive[a] = alive[b] = 0; parent[a] = parent[b] = nn;
      nn++;
    }
    for (i = 0; i < n; i++) if (freq[i]) {
      int d = 0, k = i;
      while (parent[k] >= 0) { k = parent[k]; d++; }
      len[i] = (u8)d;
      if (d > longest) longest = d;
    }
    if (longest <= maxlen) return;
  }
}
/* canonical codes for the lengths, bit-reversed for writing least significant bit first */
static void huff_codes(const u8 *len, int n, u16 *code)
{
  int count[16] = { 0 }, next[16], i, b, c = 0;
  for (i = 0; i < n; i++) if (len[i]) count[len[i]]++;
  for (b = 1; b < 16; b++) { c = (c + count[b - 1]) << 1; next[b] = c; }
  for (i = 0; i < n; i++) if (len[i]) {
    u32 v = (u32)next[len[i]]++, r = 0;
    for (b = 0; b < len[i]; b++) { r = (r << 1) | (v & 1); v >>= 1; }
    code[i] = (u16)r;
  }
}
typedef struct { u8 len[280]; u16 code[280]; u8 wlen[280]; } HCode;   /* wlen: bits written (0 for a one-symbol code) */

/* a prefix code written: simple when at most two symbols under 256 are used, else normal (its code
   lengths through the code-length code, with repeats and runs of zeros) */
static void write_code(BitW *w, const u32 *freq, int n, HCode *h)
{
  static const u8 order[19] = { 17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
  int i, used = 0, s[2] = { 0, 0 }, big = 0;
  for (i = 0; i < n; i++) if (freq[i]) { if (used < 2) s[used] = i; used++; if (i >= 256) big = 1; }
  memset(h->len, 0, sizeof h->len); memset(h->wlen, 0, sizeof h->wlen);
  if (used <= 2 && !big) {
    bw_put(w, 1, 1);
    bw_put(w, used == 2 ? 1 : 0, 1);
    if (s[0] < 2) { bw_put(w, 0, 1); bw_put(w, (u32)s[0], 1); }
    else { bw_put(w, 1, 1); bw_put(w, (u32)s[0], 8); }
    if (used == 2) { bw_put(w, (u32)s[1], 8); h->code[s[0]] = 0; h->code[s[1]] = 1; h->wlen[s[0]] = h->wlen[s[1]] = 1; }
    return;
  }
  huff_lengths(freq, n, 15, h->len);
  huff_codes(h->len, n, h->code);
  for (i = 0; i < n; i++) h->wlen[i] = used == 1 ? 0 : h->len[i];
  {
    /* the lengths as code-length symbols: 0-15, 16 repeat the last non-zero 3-6 times, 17 zeros 3-10, 18 zeros 11-138 */
    static u8 sym[300], extra[300];
    int ns = 0, prev = 8, k, nlen;
    u32 cf[19] = { 0 };
    u8 cl[19];
    u16 cc[19];
    for (i = 0; i < n; ) {
      int v = h->len[i], run = 1;
      while (i + run < n && h->len[i + run] == v) run++;
      if (!v && run >= 3) {
        int r = run > 138 ? 138 : run;
        if (r >= 11) { sym[ns] = 18; extra[ns++] = (u8)(r - 11); }
        else { sym[ns] = 17; extra[ns++] = (u8)(r - 3); }
        i += r; continue;
      }
      if (v && v == prev && run >= 3) {
        int r = run > 6 ? 6 : run;
        sym[ns] = 16; extra[ns++] = (u8)(r - 3);
        i += r; continue;
      }
      sym[ns] = (u8)v; extra[ns++] = 0;
      if (v) prev = v;
      i++;
    }
    for (k = 0; k < ns; k++) cf[sym[k]]++;
    huff_lengths(cf, 19, 7, cl);
    { int nused = 0; for (k = 0; k < 19; k++) if (cl[k]) nused++; if (nused == 1) for (k = 0; k < 19; k++) if (cl[k]) { cl[(k + 1) % 19] = 1; break; } }
    huff_codes(cl, 19, cc);
    for (nlen = 19; nlen > 4 && !cl[order[nlen - 1]]; nlen--) ;
    bw_put(w, 0, 1);
    bw_put(w, (u32)(nlen - 4), 4);
    for (k = 0; k < nlen; k++) bw_put(w, cl[order[k]], 3);
    bw_put(w, 0, 1);                                   /* every symbol's length follows (no max_symbol) */
    for (k = 0; k < ns; k++) {
      bw_put(w, cc[sym[k]], cl[sym[k]]);
      if (sym[k] == 16) bw_put(w, extra[k], 2);
      else if (sym[k] == 17) bw_put(w, extra[k], 3);
      else if (sym[k] == 18) bw_put(w, extra[k], 7);
    }
  }
}
/* a length or distance as its prefix symbol and extra bits */
static void prefix_of(u32 v, int *code, int *nextra, u32 *extra)
{
  u32 x = v - 1;
  int hb = 0;
  if (x < 4) { *code = (int)x; *nextra = 0; *extra = 0; return; }
  while ((x >> (hb + 1)) != 0) hb++;
  *code = 2 * hb + (int)((x >> (hb - 1)) & 1);
  *nextra = hb - 1;
  *extra = x & ((1u << (hb - 1)) - 1);
}

/* the tokens of an image: a literal (TK_LIT: the pixel at the position reached) or a copy */
#define TK_COPY 0x80000000UL
#define WIN 32768
#define MAXLEN 4096
static u32 hash2(const u32 *p) { return ((u32)(p[0] * 0x9E3779B1UL) ^ (u32)(p[1] * 0x85EBCA6BUL)) >> 16 & 0xFFFF; }
static u32 *lz77(const u32 *px, long n, i32 w, long *ntok)
{
  u32 *tok = (u32 *)malloc(sizeof(u32) * (size_t)(n ? n : 1));
  i32 *head = (i32 *)malloc(sizeof(i32) * 65536);
  i32 *prev = (i32 *)malloc(sizeof(i32) * (size_t)(n ? n : 1));
  long i = 0, k = 0;
  if (!tok || !head || !prev) { free(tok); free(head); free(prev); return NULL; }
  for (i = 0; i < 65536; i++) head[i] = -1;
  i = 0;
  while (i < n) {
    long best = 0, bestd = 0, j;
    int tries = 0;
    u32 h = 0;
    if (i + 1 < n) {
      h = hash2(px + i);
      /* the pixel straight above, and the one before, are tried first (no hash needed) */
      for (j = 0; j < 2; j++) {
        long d = j ? 1 : w, m = 0;
        if (d > i) continue;
        while (i + m < n && m < MAXLEN && px[i + m] == px[i + m - d]) m++;
        if (m > best) { best = m; bestd = d; }
      }
      for (j = head[h]; j >= 0 && i - j <= WIN && tries < 16; j = prev[j], tries++) {
        long m = 0;
        if (i + best >= n || px[j + best] != px[i + best]) continue;
        while (i + m < n && m < MAXLEN && px[j + m] == px[i + m]) m++;
        if (m > best) { best = m; bestd = i - j; if (m == MAXLEN) break; }
      }
    }
    if (best >= 3) {
      long e = i + best;
      tok[k++] = TK_COPY | ((u32)(best - 1) << 19) | (u32)bestd;     /* length-1 in 12 bits, distance in 19 */
      for (; i < e; i++) if (i + 1 < n) {
        u32 hh = hash2(px + i);
        prev[i] = head[hh]; head[hh] = (i32)i;
      }
      continue;
    }
    tok[k++] = 0;
    if (i + 1 < n) { prev[i] = head[h]; head[h] = (i32)i; }
    i++;
    if ((i & 4095) == 0 && progress(i, n)) break;
  }
  free(head); free(prev);
  *ntok = k;
  return tok;
}
/* the distance written for a copy: the 2D codes for straight above (1) and just before (2), else
   the distance plus 120 */
static u32 dist_code(u32 d, i32 w) { return d == (u32)w ? 1 : d == 1 ? 2 : d + 120; }

/* an entropy-coded image: (main) no colour cache, no meta prefix codes, five codes, then the data */
static void write_image(BitW *w, const u32 *px, long n, i32 xs, int main_image, int use_lz)
{
  u32 *tok = NULL;
  long ntok = n, t, pos;
  static u32 fg[280], fr[256], fb[256], fa[256], fd[40];
  static HCode hg, hr, hb, ha, hd;
  if (use_lz) tok = lz77(px, n, xs, &ntok);
  memset(fg, 0, sizeof fg); memset(fr, 0, sizeof fr); memset(fb, 0, sizeof fb); memset(fa, 0, sizeof fa); memset(fd, 0, sizeof fd);
  for (t = 0, pos = 0; t < ntok; t++) {
    if (tok && (tok[t] & TK_COPY)) {
      u32 L = ((tok[t] >> 19) & 0xFFF) + 1, D = tok[t] & 0x7FFFF, e;
      int c, ne;
      prefix_of(L, &c, &ne, &e); fg[256 + c]++;
      prefix_of(dist_code(D, xs), &c, &ne, &e); fd[c]++;
      pos += (long)L;
    } else {
      u32 p = px[pos++];
      fg[(p >> 8) & 255]++; fr[(p >> 16) & 255]++; fb[p & 255]++; fa[p >> 24]++;
    }
  }
  bw_put(w, 0, 1);                                     /* no colour cache */
  if (main_image) bw_put(w, 0, 1);                     /* one group of prefix codes */
  write_code(w, fg, 280, &hg);
  write_code(w, fr, 256, &hr);
  write_code(w, fb, 256, &hb);
  write_code(w, fa, 256, &ha);
  write_code(w, fd, 40, &hd);
  for (t = 0, pos = 0; t < ntok; t++) {
    if (tok && (tok[t] & TK_COPY)) {
      u32 L = ((tok[t] >> 19) & 0xFFF) + 1, D = tok[t] & 0x7FFFF, e;
      int c, ne;
      prefix_of(L, &c, &ne, &e);
      bw_put(w, hg.code[256 + c], hg.wlen[256 + c]); if (ne) bw_put(w, e, ne);
      prefix_of(dist_code(D, xs), &c, &ne, &e);
      bw_put(w, hd.code[c], hd.wlen[c]); if (ne) bw_put(w, e, ne);
      pos += (long)L;
    } else {
      u32 p = px[pos++], g = (p >> 8) & 255, r = (p >> 16) & 255, b = p & 255, a = p >> 24;
      bw_put(w, hg.code[g], hg.wlen[g]);
      bw_put(w, hr.code[r], hr.wlen[r]);
      bw_put(w, hb.code[b], hb.wlen[b]);
      bw_put(w, ha.code[a], ha.wlen[a]);
    }
  }
  free(tok);
}

/* the predictors tried for each block (of the 14): left, top, average of the two, Select, ClampAddSubtractFull */
static u32 avg2(u32 a, u32 b) { return (((a ^ b) & 0xFEFEFEFEUL) >> 1) + (a & b); }
static u32 predict(int mode, u32 L, u32 T, u32 TL)
{
  int c, pl = 0, pt = 0;
  u32 out = 0;
  switch (mode) {
  case 1: return L;
  case 2: return T;
  case 7: return avg2(L, T);
  case 11:
    for (c = 0; c < 32; c += 8) {
      int t = (int)((T >> c) & 255), l = (int)((L >> c) & 255), tl = (int)((TL >> c) & 255);
      pl += abs(t - tl); pt += abs(l - tl);
    }
    return pl < pt ? L : T;
  default:                                             /* 12 */
    for (c = 0; c < 32; c += 8) {
      int v = (int)((L >> c) & 255) + (int)((T >> c) & 255) - (int)((TL >> c) & 255);
      out |= (u32)(v < 0 ? 0 : v > 255 ? 255 : v) << c;
    }
    return out;
  }
}
static u32 sub_px(u32 a, u32 b)
{
  return ((((a >> 24) - (b >> 24)) & 255) << 24) | (((((a >> 16) & 255) - ((b >> 16) & 255)) & 255) << 16) |
         (((((a >> 8) & 255) - ((b >> 8) & 255)) & 255) << 8) | (((a & 255) - (b & 255)) & 255);
}
static int res_cost(u32 r)
{
  int c, s = 0;
  for (c = 0; c < 32; c += 8) { int v = (int)((r >> c) & 255); s += v < 128 ? v : 256 - v; }
  return s;
}

u8 *webp_encode_lossless(const Rgba *im, size_t *len)
{
  static const int modes[5] = { 1, 2, 7, 11, 12 };
  i32 w = im->w, h = im->h, x, y, bx, by, bw = 0, bh = 0;
  long n = (long)w * h, k;
  u32 *px, *res = NULL, *pm = NULL;
  Buf b;
  BitW bits;
  u8 *out;
  if (w < 1 || h < 1 || w > 16384 || h > 16384) return NULL;
  if (!(px = (u32 *)malloc(sizeof(u32) * (size_t)n))) return NULL;
  for (k = 0; k < n; k++) {
    const u8 *p = im->px + k * 4;
    u32 g = p[1];
    /* subtract green: red and blue less green */
    px[k] = ((u32)p[3] << 24) | ((u32)((p[0] - g) & 255) << 16) | (g << 8) | (u32)((p[2] - g) & 255);
  }
  /* the predictor transform in 16 x 16 blocks, each with the mode that leaves the smallest residuals */
  bw = (w + 15) >> 4; bh = (h + 15) >> 4;
  if (!(res = (u32 *)malloc(sizeof(u32) * (size_t)n)) || !(pm = (u32 *)malloc(sizeof(u32) * (size_t)bw * bh))) { free(px); free(res); free(pm); return NULL; }
  for (by = 0; by < bh; by++)
    for (bx = 0; bx < bw; bx++) {
      int best = 1, m;
      long bestc = -1;
      for (m = 0; m < 5; m++) {
        long c = 0;
        for (y = by * 16; y < h && y < by * 16 + 16; y++) {
          if (!y) continue;
          for (x = bx * 16; x < w && x < bx * 16 + 16; x++) {
            long i = (long)y * w + x;
            if (!x) continue;
            c += res_cost(sub_px(px[i], predict(modes[m], px[i - 1], px[i - w], px[i - w - 1])));
          }
        }
        if (bestc < 0 || c < bestc) { bestc = c; best = modes[m]; }
      }
      pm[(long)by * bw + bx] = 0xFF000000UL | ((u32)best << 8);
    }
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      long i = (long)y * w + x;
      u32 pr;
      if (!x && !y) pr = 0xFF000000UL;
      else if (!y) pr = px[i - 1];
      else if (!x) pr = px[i - w];
      else pr = predict((int)((pm[(long)(y >> 4) * bw + (x >> 4)] >> 8) & 255), px[i - 1], px[i - w], px[i - w - 1]);
      res[i] = sub_px(px[i], pr);
    }
  free(px);
  buf_init(&b);
  buf_add(&b, "RIFF\0\0\0\0WEBPVP8L\0\0\0\0", 20);
  bits.b = &b; bits.acc = 0; bits.n = 0;
  bw_put(&bits, 0x2F, 8);
  bw_put(&bits, (u32)(w - 1), 14); bw_put(&bits, (u32)(h - 1), 14);
  bw_put(&bits, (u32)any_alpha(im), 1); bw_put(&bits, 0, 3);
  bw_put(&bits, 1, 1); bw_put(&bits, 2, 2);            /* SUBTRACT_GREEN */
  bw_put(&bits, 1, 1); bw_put(&bits, 0, 2);            /* PREDICTOR, 16 x 16 blocks */
  bw_put(&bits, 2, 3);
  write_image(&bits, pm, (long)bw * bh, bw, 0, 0);
  bw_put(&bits, 0, 1);                                 /* no more transforms */
  write_image(&bits, res, n, w, 1, 1);
  bw_flush(&bits);
  free(res); free(pm);
  {
    size_t payload = b.len - 20;
    if (payload & 1) buf_addc(&b, 0);
    if (b.oom || progress_aborted) { buf_free(&b); return NULL; }
    out = (u8 *)b.data;
    put_le32(out + 4, (u32)(b.len - 8));
    put_le32(out + 16, (u32)payload);               /* (the chunk's size leaves the pad byte out) */
  }
  return done(&b, len);
}

/* ================================================================== PDF */
/* text as a PDF string: UTF-16BE with a byte-order mark, in hex */
static void pdf_text(Buf *b, const char *s)
{
  size_t n = strlen(s), o = 0;
  buf_adds(b, "<FEFF");
  while (o < n) {
    int used;
    u32 cp = utf8_get(s + o, n - o, &used);
    o += (size_t)used;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      buf_printf(b, "%04lX%04lX", (unsigned long)(0xD800 + (cp >> 10)), (unsigned long)(0xDC00 + (cp & 0x3FF)));
    } else buf_printf(b, "%04lX", (unsigned long)cp);
  }
  buf_adds(b, ">");
}
u8 *pdf_encode(const Rgba *im, int dpi, int gray, int jpeg_quality, const DocInfo *info, size_t *len)
{
  Buf b;
  long off[8], n = (long)im->w * im->h, k;
  u8 *img = NULL, *alpha = NULL, *zimg = NULL, *zal = NULL;
  size_t ilen = 0, alen = 0;
  long wp, hp, xref;
  int nobj, hasalpha = jpeg_quality ? 0 : any_alpha(im), i;
  if (im->w < 1 || im->h < 1) return NULL;
  if (dpi <= 0) dpi = 72;
  /* the page in hundredths of a point */
  wp = (long)((double)im->w * 7200.0 / dpi + 0.5); hp = (long)((double)im->h * 7200.0 / dpi + 0.5);
  if (jpeg_quality) {
    if (!(zimg = jpeg_encode(im, jpeg_quality, dpi, &ilen))) return NULL;
  } else {
    int nc = gray ? 1 : 3;
    if (!(img = (u8 *)malloc((size_t)n * nc))) return NULL;
    for (k = 0; k < n; k++) {
      const u8 *p = im->px + k * 4;
      if (gray) img[k] = p[0];
      else { img[k * 3] = p[0]; img[k * 3 + 1] = p[1]; img[k * 3 + 2] = p[2]; }
    }
    zimg = zlib_compress(img, (size_t)n * nc, &ilen);
    free(img);
    if (!zimg) return NULL;
    if (hasalpha) {
      if (!(alpha = (u8 *)malloc((size_t)n))) { free(zimg); return NULL; }
      for (k = 0; k < n; k++) alpha[k] = im->px[k * 4 + 3];
      zal = zlib_compress(alpha, (size_t)n, &alen);
      free(alpha);
      if (!zal) { free(zimg); return NULL; }
    }
  }
  buf_init(&b);
  buf_adds(&b, "%PDF-1.4\n%\342\343\317\323\n");
  off[1] = (long)b.len; buf_adds(&b, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
  off[2] = (long)b.len; buf_adds(&b, "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
  off[3] = (long)b.len;
  buf_printf(&b, "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %ld.%02ld %ld.%02ld] /Resources << /XObject << /Im0 5 0 R >> >> /Contents 4 0 R >>\nendobj\n",
             wp / 100, wp % 100, hp / 100, hp % 100);
  {
    char cs[96];
    snprintf(cs, sizeof cs, "q %ld.%02ld 0 0 %ld.%02ld 0 0 cm /Im0 Do Q\n", wp / 100, wp % 100, hp / 100, hp % 100);
    off[4] = (long)b.len;
    buf_printf(&b, "4 0 obj\n<< /Length %ld >>\nstream\n%s", (long)strlen(cs), cs);
    buf_adds(&b, "endstream\nendobj\n");
  }
  off[5] = (long)b.len;
  buf_printf(&b, "5 0 obj\n<< /Type /XObject /Subtype /Image /Width %ld /Height %ld /ColorSpace /%s /BitsPerComponent 8 /Filter /%s",
             (long)im->w, (long)im->h, gray && !jpeg_quality ? "DeviceGray" : "DeviceRGB", jpeg_quality ? "DCTDecode" : "FlateDecode");
  if (hasalpha) buf_adds(&b, " /SMask 6 0 R");
  buf_printf(&b, " /Length %ld >>\nstream\n", (long)ilen);
  buf_add(&b, zimg, ilen);
  buf_adds(&b, "\nendstream\nendobj\n");
  nobj = 6;
  if (hasalpha) {
    off[6] = (long)b.len;
    buf_printf(&b, "6 0 obj\n<< /Type /XObject /Subtype /Image /Width %ld /Height %ld /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode /Length %ld >>\nstream\n",
               (long)im->w, (long)im->h, (long)alen);
    buf_add(&b, zal, alen);
    buf_adds(&b, "\nendstream\nendobj\n");
    nobj = 7;
  }
  off[nobj] = (long)b.len;
  buf_printf(&b, "%d 0 obj\n<< /Producer (Easel)", nobj);
  if (info) {
    if (info->title[0]) { buf_adds(&b, " /Title "); pdf_text(&b, info->title); }
    if (info->author[0]) { buf_adds(&b, " /Author "); pdf_text(&b, info->author); }
    if (info->description[0]) { buf_adds(&b, " /Subject "); pdf_text(&b, info->description); }
    if (info->keywords[0]) { buf_adds(&b, " /Keywords "); pdf_text(&b, info->keywords); }
  }
  buf_adds(&b, " >>\nendobj\n");
  xref = (long)b.len;
  buf_printf(&b, "xref\n0 %d\n0000000000 65535 f \n", nobj + 1);
  for (i = 1; i <= nobj; i++) buf_printf(&b, "%010ld 00000 n \n", off[i]);
  buf_printf(&b, "trailer\n<< /Size %d /Root 1 0 R /Info %d 0 R >>\nstartxref\n%ld\n%%%%EOF\n", nobj + 1, nobj, xref);
  free(zimg); free(zal);
  return done(&b, len);
}

/* ================================================================== Netpbm */
u8 *pnm_encode(const Rgba *im, int kind, size_t *len)
{
  Buf b;
  long n = (long)im->w * im->h, k;
  i32 x, y;
  int grey, alpha;
  if (im->w < 1 || im->h < 1) return NULL;
  grey = all_grey(im); alpha = any_alpha(im);
  if (kind == PNM_AUTO) kind = alpha ? PNM_PAM : grey ? PNM_PGM : PNM_PPM;
  buf_init(&b);
  if (kind == PNM_PBM) {
    buf_printf(&b, "P4\n%ld %ld\n", (long)im->w, (long)im->h);
    for (y = 0; y < im->h; y++) {
      int acc = 0, nb = 0;
      for (x = 0; x < im->w; x++) {
        const u8 *p = im->px + ((long)y * im->w + x) * 4;
        int lum = (p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8;
        acc = (acc << 1) | (lum < 128 && p[3] >= 128);
        if (++nb == 8) { buf_addc(&b, acc); acc = nb = 0; }
      }
      if (nb) buf_addc(&b, acc << (8 - nb));
    }
  } else if (kind == PNM_PGM || kind == PNM_PPM) {
    buf_printf(&b, "P%c\n%ld %ld\n255\n", kind == PNM_PGM ? '5' : '6', (long)im->w, (long)im->h);
    for (k = 0; k < n; k++) {
      const u8 *p = im->px + k * 4;
      if (kind == PNM_PGM) buf_addc(&b, grey ? p[0] : (p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
      else buf_add(&b, p, 3);
    }
  } else {
    buf_printf(&b, "P7\nWIDTH %ld\nHEIGHT %ld\nDEPTH %d\nMAXVAL 255\nTUPLTYPE %s\nENDHDR\n", (long)im->w, (long)im->h,
               grey ? 2 : 4, grey ? "GRAYSCALE_ALPHA" : "RGB_ALPHA");
    for (k = 0; k < n; k++) {
      const u8 *p = im->px + k * 4;
      if (grey) { buf_addc(&b, p[0]); buf_addc(&b, p[3]); }
      else buf_add(&b, p, 4);
    }
  }
  return done(&b, len);
}

/* ================================================================== QOI */
u8 *qoi_encode(const Rgba *im, size_t *len)
{
  Buf b;
  u8 index[64 * 4], prev[4] = { 0, 0, 0, 255 };
  long n = (long)im->w * im->h, k;
  int run = 0, ch = any_alpha(im) ? 4 : 3;
  if (im->w < 1 || im->h < 1) return NULL;
  memset(index, 0, sizeof index);
  buf_init(&b);
  buf_add(&b, "qoif", 4); be32(&b, (u32)im->w); be32(&b, (u32)im->h); buf_addc(&b, ch); buf_addc(&b, 0);
  for (k = 0; k < n; k++) {
    u8 p[4];
    memcpy(p, im->px + k * 4, 4);
    if (ch == 3) p[3] = 255;
    if (!memcmp(p, prev, 4)) {
      if (++run == 62 || k == n - 1) { buf_addc(&b, 0xC0 | (run - 1)); run = 0; }
      continue;
    }
    if (run) { buf_addc(&b, 0xC0 | (run - 1)); run = 0; }
    {
      int h = (p[0] * 3 + p[1] * 5 + p[2] * 7 + p[3] * 11) % 64;
      if (!memcmp(index + h * 4, p, 4)) buf_addc(&b, h);
      else {
        memcpy(index + h * 4, p, 4);
        if (p[3] == prev[3]) {
          int vr = (signed char)(u8)(p[0] - prev[0]), vg = (signed char)(u8)(p[1] - prev[1]), vb = (signed char)(u8)(p[2] - prev[2]);
          int vgr = vr - vg, vgb = vb - vg;
          if (vr >= -2 && vr <= 1 && vg >= -2 && vg <= 1 && vb >= -2 && vb <= 1) buf_addc(&b, 0x40 | ((vr + 2) << 4) | ((vg + 2) << 2) | (vb + 2));
          else if (vgr >= -8 && vgr <= 7 && vg >= -32 && vg <= 31 && vgb >= -8 && vgb <= 7) { buf_addc(&b, 0x80 | (vg + 32)); buf_addc(&b, ((vgr + 8) << 4) | (vgb + 8)); }
          else { buf_addc(&b, 0xFE); buf_add(&b, p, 3); }
        } else { buf_addc(&b, 0xFF); buf_add(&b, p, 4); }
      }
    }
    memcpy(prev, p, 4);
  }
  buf_add(&b, "\0\0\0\0\0\0\0\1", 8);
  return done(&b, len);
}

/* ================================================================== ICO */
/* the picture fitted into s x s and centred, transparent round it */
static int fit_square(const Rgba *im, i32 s, Rgba *out)
{
  Rgba t;
  i32 y;
  if (!rgba_thumbnail(im, s, &t)) return 0;
  if (!rgba_alloc(out, s, s)) { rgba_free(&t); return 0; }
  for (y = 0; y < t.h; y++)
    memcpy(out->px + ((size_t)(y + (s - t.h) / 2) * s + (s - t.w) / 2) * 4, t.px + (size_t)y * t.w * 4, (size_t)t.w * 4);
  rgba_free(&t);
  return 1;
}
u8 *ico_encode(const Rgba *im, int maxsize, size_t *len)
{
  static const i32 std[7] = { 16, 24, 32, 48, 64, 128, 256 };
  i32 sizes[8], big = im->w > im->h ? im->w : im->h;
  u8 *img[8];
  size_t ilen[8];
  int n = 0, i;
  Buf b;
  u32 off;
  if (im->w < 1 || im->h < 1) return NULL;
  if (maxsize <= 0 || maxsize > 256) maxsize = 256;
  for (i = 0; i < 7; i++) if (std[i] <= big && std[i] <= maxsize) sizes[n++] = std[i];
  if (!n) sizes[n++] = big > maxsize ? maxsize : big;
  for (i = 0; i < n; i++) {
    Rgba sq;
    i32 s = sizes[i], x, y;
    img[i] = NULL;
    if (!fit_square(im, s, &sq)) break;
    if (s > 64) img[i] = png_encode(&sq, 0, &ilen[i]);
    else {
      Buf d;
      i32 mrow = ((s + 31) / 32) * 4;
      buf_init(&d);
      le32(&d, 40); le32(&d, (u32)s); le32(&d, (u32)(s * 2)); le16(&d, 1); le16(&d, 32); le32(&d, 0);
      le32(&d, (u32)(s * s * 4 + mrow * s)); le32(&d, 0); le32(&d, 0); le32(&d, 0); le32(&d, 0);
      for (y = s - 1; y >= 0; y--)
        for (x = 0; x < s; x++) {
          const u8 *p = sq.px + ((size_t)y * s + x) * 4;
          buf_addc(&d, p[2]); buf_addc(&d, p[1]); buf_addc(&d, p[0]); buf_addc(&d, p[3]);
        }
      for (y = s - 1; y >= 0; y--) {
        i32 bx;
        for (bx = 0; bx < mrow; bx++) {
          int v = 0, k;
          for (k = 0; k < 8; k++) { x = bx * 8 + k; v = (v << 1) | (x < s && !sq.px[((size_t)y * s + x) * 4 + 3]); }
          buf_addc(&d, v);
        }
      }
      img[i] = d.oom ? (buf_free(&d), (u8 *)NULL) : (ilen[i] = d.len, (u8 *)buf_take(&d));
    }
    rgba_free(&sq);
    if (!img[i]) break;
  }
  if (i < n) { while (i-- > 0) free(img[i]); return NULL; }
  buf_init(&b);
  le16(&b, 0); le16(&b, 1); le16(&b, (u32)n);
  off = 6 + 16 * (u32)n;
  for (i = 0; i < n; i++) {
    buf_addc(&b, sizes[i] >= 256 ? 0 : sizes[i]); buf_addc(&b, sizes[i] >= 256 ? 0 : sizes[i]);
    buf_addc(&b, 0); buf_addc(&b, 0); le16(&b, 1); le16(&b, 32);
    le32(&b, (u32)ilen[i]); le32(&b, off);
    off += (u32)ilen[i];
  }
  for (i = 0; i < n; i++) { buf_add(&b, img[i], ilen[i]); free(img[i]); }
  return done(&b, len);
}

/* ================================================================== DDS */
static u32 to565(int r, int g, int b) { return (u32)(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | ((b * 31 + 127) / 255)); }
static void from565(u32 c, int *rgb)
{
  int r = (int)(c >> 11) & 31, g = (int)(c >> 5) & 63, b = (int)c & 31;
  rgb[0] = (r << 3) | (r >> 2); rgb[1] = (g << 2) | (g >> 4); rgb[2] = (b << 3) | (b >> 2);
}
/* one 4 x 4 block's colours (blk: 16 RGBA pixels): endpoints from the box round the colours, a little
   inset; four colours, or three and transparent (BC1 when something is under half alpha) */
static void bc1_block(const u8 *blk, int allow_trans, Buf *b)
{
  int mn[3] = { 255, 255, 255 }, mx[3] = { 0, 0, 0 }, i, c, trans = 0, pal[4][3], c0rgb[3], c1rgb[3];
  u32 c0, c1, idx = 0;
  for (i = 0; i < 16; i++) {
    const u8 *p = blk + i * 4;
    if (allow_trans && p[3] < 128) { trans = 1; continue; }
    for (c = 0; c < 3; c++) { if (p[c] < mn[c]) mn[c] = p[c]; if (p[c] > mx[c]) mx[c] = p[c]; }
  }
  if (mn[0] > mx[0]) { mn[0] = mn[1] = mn[2] = mx[0] = mx[1] = mx[2] = 0; }
  for (c = 0; c < 3; c++) { int in = (mx[c] - mn[c]) >> 4; mx[c] -= in; mn[c] += in; }
  c0 = to565(mx[0], mx[1], mx[2]); c1 = to565(mn[0], mn[1], mn[2]);
  if (trans ? c0 > c1 : c0 < c1) { u32 t = c0; c0 = c1; c1 = t; }
  from565(c0, c0rgb); from565(c1, c1rgb);
  for (c = 0; c < 3; c++) {
    pal[0][c] = c0rgb[c]; pal[1][c] = c1rgb[c];
    if (c0 > c1) { pal[2][c] = (2 * c0rgb[c] + c1rgb[c]) / 3; pal[3][c] = (c0rgb[c] + 2 * c1rgb[c]) / 3; }
    else { pal[2][c] = (c0rgb[c] + c1rgb[c]) / 2; pal[3][c] = 0; }
  }
  for (i = 15; i >= 0; i--) {
    const u8 *p = blk + i * 4;
    int best = 0, bd = -1, k, nk = c0 > c1 ? 4 : 3;
    if (c0 <= c1 && trans && p[3] < 128) best = 3;
    else for (k = 0; k < nk; k++) {
      int d = 0;
      for (c = 0; c < 3; c++) d += (p[c] - pal[k][c]) * (p[c] - pal[k][c]);
      if (bd < 0 || d < bd) { bd = d; best = k; }
    }
    idx = (idx << 2) | (u32)best;
  }
  le16(b, c0); le16(b, c1); le32(b, idx);
}
static void bc3_alpha(const u8 *blk, Buf *b)
{
  int a0 = 0, a1 = 255, i, k, pal[8];
  u8 ix[16];
  for (i = 0; i < 16; i++) { int a = blk[i * 4 + 3]; if (a > a0) a0 = a; if (a < a1) a1 = a; }
  pal[0] = a0; pal[1] = a1;
  for (k = 1; k < 7; k++) pal[k + 1] = ((7 - k) * a0 + k * a1) / 7;
  for (i = 0; i < 16; i++) {
    int a = blk[i * 4 + 3], best = 0, bd = 999;
    if (a0 != a1) for (k = 0; k < 8; k++) { int d = abs(a - pal[k]); if (d < bd) { bd = d; best = k; } }
    ix[i] = (u8)best;
  }
  buf_addc(b, a0); buf_addc(b, a1);
  for (i = 0; i < 16; i += 8) {                       /* 24 bits for each eight pixels, the first lowest */
    u32 v = 0;
    for (k = 7; k >= 0; k--) v = (v << 3) | ix[i + k];
    buf_addc(b, (int)(v & 255)); buf_addc(b, (int)((v >> 8) & 255)); buf_addc(b, (int)((v >> 16) & 255));
  }
}
/* the next mipmap: half the size (at least 1), each pixel the average of the 2 x 2 under it */
static int halve(const Rgba *in, Rgba *out)
{
  i32 w = in->w > 1 ? in->w / 2 : 1, h = in->h > 1 ? in->h / 2 : 1, x, y;
  if (!rgba_alloc(out, w, h)) return 0;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      int c;
      i32 x0 = x * 2 < in->w ? x * 2 : in->w - 1, y0 = y * 2 < in->h ? y * 2 : in->h - 1;
      i32 x1 = x0 + 1 < in->w ? x0 + 1 : x0, y1 = y0 + 1 < in->h ? y0 + 1 : y0;
      for (c = 0; c < 4; c++)
        out->px[((size_t)y * w + x) * 4 + c] = (u8)((in->px[((size_t)y0 * in->w + x0) * 4 + c] + in->px[((size_t)y0 * in->w + x1) * 4 + c] +
                                                   in->px[((size_t)y1 * in->w + x0) * 4 + c] + in->px[((size_t)y1 * in->w + x1) * 4 + c] + 2) >> 2);
    }
  return 1;
}
static void dds_level(const Rgba *im, int kind, Buf *b)
{
  i32 x, y, bx, by;
  if (kind == DDS_RGBA) {
    long n = (long)im->w * im->h, k;
    for (k = 0; k < n; k++) { const u8 *p = im->px + k * 4; buf_addc(b, p[2]); buf_addc(b, p[1]); buf_addc(b, p[0]); buf_addc(b, p[3]); }
    return;
  }
  for (by = 0; by < im->h; by += 4)
    for (bx = 0; bx < im->w; bx += 4) {
      u8 blk[64];
      for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++) {
          i32 sx = bx + x < im->w ? bx + x : im->w - 1, sy = by + y < im->h ? by + y : im->h - 1;
          memcpy(blk + (y * 4 + x) * 4, im->px + ((size_t)sy * im->w + sx) * 4, 4);
        }
      if (kind == DDS_BC3) { bc3_alpha(blk, b); bc1_block(blk, 0, b); }
      else bc1_block(blk, 1, b);
    }
}
u8 *dds_encode(const Rgba *im, int kind, int mipmaps, size_t *len)
{
  Buf b;
  int levels = 1, i;
  i32 w = im->w, h = im->h;
  Rgba cur, nxt;
  if (w < 1 || h < 1) return NULL;
  if (mipmaps) { i32 m = w > h ? w : h; while (m > 1) { m >>= 1; levels++; } }
  buf_init(&b);
  buf_add(&b, "DDS ", 4);
  le32(&b, 124);
  le32(&b, 0x1007 | (kind == DDS_RGBA ? 0x8 : 0x80000) | (levels > 1 ? 0x20000 : 0));
  le32(&b, (u32)h); le32(&b, (u32)w);
  le32(&b, kind == DDS_RGBA ? (u32)w * 4 : (u32)((w + 3) / 4) * ((h + 3) / 4) * (kind == DDS_BC1 ? 8 : 16));
  le32(&b, 0); le32(&b, (u32)levels);
  for (i = 0; i < 11; i++) le32(&b, 0);
  le32(&b, 32);
  if (kind == DDS_RGBA) { le32(&b, 0x41); le32(&b, 0); le32(&b, 32); le32(&b, 0x00FF0000UL); le32(&b, 0x0000FF00UL); le32(&b, 0x000000FFUL); le32(&b, 0xFF000000UL); }
  else { le32(&b, 0x4); buf_add(&b, kind == DDS_BC1 ? "DXT1" : "DXT5", 4); for (i = 0; i < 5; i++) le32(&b, 0); }
  le32(&b, 0x1000 | (levels > 1 ? 0x400008 : 0)); le32(&b, 0); le32(&b, 0); le32(&b, 0); le32(&b, 0);
  cur = *im;
  for (i = 0; i < levels; i++) {
    dds_level(&cur, kind, &b);
    if (i + 1 < levels) {
      if (!halve(&cur, &nxt)) { b.oom = 1; break; }
      if (i) rgba_free(&cur);
      cur = nxt;
    }
  }
  if (levels > 1 && cur.px != im->px) rgba_free(&cur);
  return done(&b, len);
}

/* ================================================================== DPX */
u8 *dpx_encode(const Rgba *im, int bits10, size_t *len)
{
  u8 *h;
  int nc = any_alpha(im) ? 4 : 3;
  u32 per = (u32)im->w * nc, line = bits10 ? (per + 2) / 3 * 4 : (per + 3) / 4 * 4;
  size_t total = 2048 + (size_t)line * im->h;
  i32 x, y;
  Buf b;
  if (im->w < 1 || im->h < 1) return NULL;
  buf_init(&b);
  if (!buf_reserve(&b, total)) return NULL;
  memset(b.data, 0, total);
  b.len = total;
  h = (u8 *)b.data;
  memcpy(h, "SDPX", 4); put_be32(h + 4, 2048); memcpy(h + 8, "V2.0", 4); put_be32(h + 16, (u32)total);
  put_be32(h + 20, 1); put_be32(h + 24, 1664); put_be32(h + 28, 384); put_be32(h + 32, 0);
  memcpy(h + 160, "Easel", 5);
  put_be16(h + 768, 0); put_be16(h + 770, 1); put_be32(h + 772, (u32)im->w); put_be32(h + 776, (u32)im->h);
  put_be32(h + 780, 0); put_be32(h + 784, 0); put_be32(h + 792, bits10 ? 1023 : 255);
  h[800] = (u8)(nc == 4 ? 51 : 50); h[801] = 2; h[802] = 2; h[803] = (u8)(bits10 ? 10 : 8);
  put_be16(h + 804, bits10 ? 1 : 0); put_be16(h + 806, 0); put_be32(h + 808, 2048);
  put_be32(h + 812, 0); put_be32(h + 816, 0);
  for (y = 0; y < im->h; y++) {
    u8 *o = h + 2048 + (size_t)y * line;
    const u8 *s = im->px + (size_t)y * im->w * 4;
    u32 k = 0;
    for (x = 0; x < im->w; x++)
      for (k = 0; k < (u32)nc; k++) {
        u32 i = (u32)x * nc + k, v = s[x * 4 + k];
        if (!bits10) o[i] = (u8)v;
        else {
          u8 *wd = o + i / 3 * 4;
          u32 cur = ((u32)wd[0] << 24) | ((u32)wd[1] << 16) | ((u32)wd[2] << 8) | wd[3];
          cur |= ((v << 2) | (v >> 6)) << (22 - 10 * (i % 3));
          put_be32(wd, cur);
        }
      }
  }
  return done(&b, len);
}

/* ================================================================== FITS */
static void fits_card(Buf *b, const char *key, const char *val)
{
  char c[81];
  if (val) snprintf(c, sizeof c, "%-8.8s= %20s", key, val);
  else snprintf(c, sizeof c, "%-8.8s", key);
  buf_printf(b, "%-80.80s", c);
}
u8 *fits_encode(const Rgba *im, size_t *len)
{
  Buf b;
  int planes = all_grey(im) ? 1 : 3, c;
  char v[24];
  i32 y;
  if (im->w < 1 || im->h < 1) return NULL;
  buf_init(&b);
  fits_card(&b, "SIMPLE", "T"); fits_card(&b, "BITPIX", "8");
  snprintf(v, sizeof v, "%d", planes == 1 ? 2 : 3); fits_card(&b, "NAXIS", v);
  snprintf(v, sizeof v, "%ld", (long)im->w); fits_card(&b, "NAXIS1", v);
  snprintf(v, sizeof v, "%ld", (long)im->h); fits_card(&b, "NAXIS2", v);
  if (planes == 3) fits_card(&b, "NAXIS3", "3");
  fits_card(&b, "END", NULL);
  while (b.len % 2880) buf_addc(&b, ' ');
  for (c = 0; c < planes; c++)
    for (y = im->h - 1; y >= 0; y--) {
      i32 x;
      const u8 *s = im->px + (size_t)y * im->w * 4 + c;
      for (x = 0; x < im->w; x++) buf_addc(&b, s[x * 4]);
    }
  while (b.len % 2880) buf_addc(&b, 0);
  return done(&b, len);
}

/* ================================================================== JPEG quality */
int jpeg_estimate_quality(const u8 *d, size_t n)
{
  static const u8 lum[64] = { 16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121,
    120, 101, 72, 92, 95, 98, 112, 100, 103, 99 };
  size_t o = 2;
  long sum = -1;
  int q, best = 0;
  long bestd = -1;
  if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return 0;
  /* the first luminance (table 0) quantization table's sum */
  while (o + 4 <= n && sum < 0) {
    size_t len, e;
    if (d[o] != 0xFF) return 0;
    if (d[o + 1] == 0xDA || d[o + 1] == 0xD9) break;
    len = ((size_t)d[o + 2] << 8) | d[o + 3];
    e = o + 2 + len;
    if (len < 2 || e > n) return 0;
    if (d[o + 1] == 0xDB) {
      size_t p = o + 4;
      while (p < e) {
        int pq = d[p] >> 4, tq = d[p] & 15, k;
        size_t tl = pq ? 128 : 64;
        if (p + 1 + tl > e) return 0;
        if (tq == 0) { sum = 0; for (k = 0; k < 64; k++) sum += pq ? (long)(((u32)d[p + 1 + 2 * k] << 8) | d[p + 2 + 2 * k]) : d[p + 1 + k]; break; }
        p += 1 + tl;
      }
    }
    o = e;
  }
  if (sum <= 0) return 0;
  /* the IJG quality whose scaled Annex K table comes closest */
  for (q = 1; q <= 100; q++) {
    long sc = q < 50 ? 5000 / q : 200 - 2 * q, t = 0, dd;
    int k;
    for (k = 0; k < 64; k++) { long v = (lum[k] * sc + 50) / 100; t += v < 1 ? 1 : v > 255 ? 255 : v; }
    dd = labs(t - sum);
    if (bestd < 0 || dd < bestd) { bestd = dd; best = q; }
  }
  return best;
}

/* ================================================================== the table */
static const struct { const char *ext; int fmt; int sub; } exts[] = {
  { "png", FMT_PNG, 0 }, { "jpg", FMT_JPEG, 0 }, { "jpeg", FMT_JPEG, 0 }, { "bmp", FMT_BMP, 0 }, { "iff", FMT_ILBM, 0 },
  { "ilbm", FMT_ILBM, 0 }, { "lbm", FMT_ILBM, 0 }, { "gif", FMT_GIF, 0 }, { "ora", FMT_ORA, 0 }, { "tif", FMT_TIFF, 0 },
  { "tiff", FMT_TIFF, 0 }, { "tga", FMT_TGA, 0 }, { "psd", FMT_PSD, 0 }, { "webp", FMT_WEBP, 0 }, { "pdf", FMT_PDF_OUT, 0 },
  { "pnm", FMT_PNM, PNM_AUTO }, { "pbm", FMT_PNM, PNM_PBM }, { "pgm", FMT_PNM, PNM_PGM }, { "ppm", FMT_PNM, PNM_PPM },
  { "pam", FMT_PNM, PNM_PAM }, { "qoi", FMT_QOI, 0 }, { "ico", FMT_ICO, 0 }, { "dds", FMT_DDS, DDS_BC3 }, { "dpx", FMT_DPX, 0 },
  { "fits", FMT_FITS, 0 }, { "fit", FMT_FITS, 0 }, { "fts", FMT_FITS, 0 } };
#define NEXTS ((int)(sizeof exts / sizeof exts[0]))
int xwrite_handles(int fmt)
{
  return fmt == FMT_WEBP || fmt == FMT_PNM || fmt == FMT_QOI || fmt == FMT_ICO || fmt == FMT_DDS || fmt == FMT_DPX ||
         fmt == FMT_FITS || fmt == FMT_PDF_OUT;
}
static int ext_index(const char *ext)
{
  int i;
  if (!ext) return -1;
  if (*ext == '.') ext++;
  for (i = 0; i < NEXTS; i++) if (str_ieq(ext, exts[i].ext)) return i;
  return -1;
}
int xwrite_format_of_ext(const char *ext) { int i = ext_index(ext); return i < 0 ? FMT_UNKNOWN : exts[i].fmt; }
int xwrite_sub_of_ext(const char *ext) { int i = ext_index(ext); return i < 0 ? 0 : exts[i].sub; }
const char *xwrite_ext(int fmt)
{
  static char e[8];
  int i;
  for (i = 0; i < NEXTS; i++) if (exts[i].fmt == fmt) { snprintf(e, sizeof e, ".%s", fmt == FMT_PNM ? "ppm" : exts[i].ext); return e; }
  return "";
}
const char *xwrite_name(int fmt)
{
  switch (fmt) {
  case FMT_PDF_OUT: return "PDF";
  case FMT_PNM: return "Netpbm";
  default: return codec_name(fmt);
  }
}
u8 *xwrite_doc(Doc *d, int fmt, int sub, int quality, size_t *len)
{
  Rgba im;
  u8 *data = NULL;
  im.w = d->w; im.h = d->h;
  if (!(im.px = doc_flat_rgba(d))) return NULL;
  switch (fmt) {
  case FMT_WEBP: data = webp_encode_lossless(&im, len); break;
  case FMT_PDF_OUT: data = pdf_encode(&im, d->dpi, d->mode == MODE_GRAY || d->mode == MODE_BITMAP, quality, &d->info, len); break;
  case FMT_PNM: data = pnm_encode(&im, sub == PNM_AUTO && d->mode == MODE_BITMAP ? PNM_PBM : sub, len); break;
  case FMT_QOI: data = qoi_encode(&im, len); break;
  case FMT_ICO: data = ico_encode(&im, sub, len); break;
  case FMT_DDS: data = dds_encode(&im, sub, 1, len); break;
  case FMT_DPX: data = dpx_encode(&im, sub, len); break;
  case FMT_FITS: data = fits_encode(&im, len); break;
  }
  rgba_free(&im);
  return data;
}
