/*
 * xread2.c - Cineon, DICOM and OpenEXR read; Cineon and Radiance HDR written (see xread2.h).
 * Every file is hostile: each length, offset and count is checked before it is used.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "xread2.h"
#include "zip.h"

static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u32 le32(const u8 *p) { return (u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0]; }
static u32 le16(const u8 *p) { return (u32)p[1] << 8 | p[0]; }
static void err_set(char *err, size_t cap, const char *m) { codec_err(err, cap, m); }
static int alloc_rgba(Rgba *out, u32 w, u32 h, char *err, size_t errcap)
{
  if (!w || !h || w > MAXDIM || h > MAXDIM || (double)w * h > CODEC_MAXPIXELS) { err_set(err, errcap, "Bad picture size"); return 0; }
  if (!rgba_alloc(out, (i32)w, (i32)h)) { codec_oom = 1; err_set(err, errcap, "Not enough memory for the picture"); return 0; }
  return 1;
}
static double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static int enc8(double lin)
{
  double v = clamp01(lin);
  v = v <= 0.0031308 ? 12.92 * v : 1.055 * pow(v, 1 / 2.4) - 0.055;
  return (int)(v * 255 + 0.5);
}
static double dec8(int v) { double c = v / 255.0; return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }

int xread2_detect(const u8 *d, size_t n)
{
  if (n >= 1024 && (be32(d) == 0x802A5FD7UL || le32(d) == 0x802A5FD7UL)) return XR_CINEON;
  if (n >= 132 && !memcmp(d + 128, "DICM", 4)) return XR_DICOM;
  if (n >= 8 && d[0] == 0x76 && d[1] == 0x2F && d[2] == 0x31 && d[3] == 0x01) return XR_EXR;
  return XR_NONE;
}
const char *xread2_name(int k) { return k == XR_CINEON ? "Cineon" : k == XR_DICOM ? "DICOM" : k == XR_EXR ? "OpenEXR" : "unknown"; }

/* ------------------------------------------------------------------ Cineon */
/* the 10-bit log code to linear light with the usual reference black 95, white 685, 0.002 density a
   step and 0.6 negative gamma */
static double cin_lin(double code)
{
  static double black = -1;
  if (black < 0) black = pow(10.0, (95 - 685) * 0.002 / 0.6);
  return (pow(10.0, (code - 685) * 0.002 / 0.6) - black) / (1 - black);
}
static int cineon_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  int le = be32(d) != 0x802A5FD7UL, nc, bits, packing, c;
  u32 (*g32)(const u8 *) = le ? le32 : be32;
  u32 off = g32(d + 4), w, h, x, y;
  u8 lut[1024];
  size_t at, need;
  nc = d[193];
  bits = d[196 + 2];
  w = g32(d + 196 + 4); h = g32(d + 196 + 8);
  packing = d[681];
  if (nc != 1 && nc != 3) { err_set(err, errcap, "Only grey and RGB Cineon files are supported"); return 0; }
  for (c = 1; c < nc; c++) if (d[196 + 28 * c + 2] != bits || g32(d + 196 + 28 * c + 4) != w || g32(d + 196 + 28 * c + 8) != h) { err_set(err, errcap, "Cineon channels differ in size"); return 0; }
  if (bits != 8 && bits != 10 && bits != 16) { err_set(err, errcap, "Unsupported Cineon sample size"); return 0; }
  if (d[680] != 0) { err_set(err, errcap, "Only pixel-interleaved Cineon files are supported"); return 0; }
  if (off < 1024 || off >= n) { err_set(err, errcap, "Damaged Cineon header"); return 0; }
  if (!w || !h || w > MAXDIM || h > MAXDIM) { err_set(err, errcap, "Bad Cineon picture size"); return 0; }
  /* how many bytes a line takes */
  if (bits == 10 && nc == 3) need = (size_t)w * 4;
  else if (bits == 10) need = ((size_t)w + 2) / 3 * 4;
  else need = (size_t)w * nc * (bits / 8);
  if ((double)need * h > (double)(n - off)) { err_set(err, errcap, "Truncated Cineon file"); return 0; }
  if (bits == 10 && packing != 5 && packing != 0) { err_set(err, errcap, "Unsupported Cineon packing"); return 0; }
  for (x = 0; x < 1024; x++) lut[x] = (u8)enc8(cin_lin(x));
  if (!alloc_rgba(out, w, h, err, errcap)) return 0;
  for (y = 0, at = off; y < h && !progress(y, h); y++, at += need) {
    u8 *o = out->px + (size_t)y * w * 4;
    const u8 *s = d + at;
    for (x = 0; x < w; x++, o += 4) {
      u32 v[3];
      if (bits == 10 && nc == 3) { u32 wd = g32(s + 4 * x); v[0] = wd >> 22 & 1023; v[1] = wd >> 12 & 1023; v[2] = wd >> 2 & 1023; }
      else if (bits == 10) { u32 wd = g32(s + 4 * (x / 3)); v[0] = wd >> (22 - 10 * (x % 3)) & 1023; }
      else for (c = 0; c < nc; c++) v[c] = bits == 8 ? (u32)s[x * nc + c] * 1023 / 255 : (le ? le16(s + 2 * (x * nc + c)) : ((u32)s[2 * (x * nc + c)] << 8 | s[2 * (x * nc + c) + 1])) >> 6;
      if (nc == 1) v[1] = v[2] = v[0];
      o[0] = lut[v[0]]; o[1] = lut[v[1]]; o[2] = lut[v[2]]; o[3] = 255;
    }
  }
  return 1;
}
static void p32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static void pf32(u8 *p, float f) { u32 v; memcpy(&v, &f, 4); p32(p, v); }
u8 *cineon_encode(const Rgba *im, size_t *len)
{
  u32 w = (u32)im->w, h = (u32)im->h, x, y, k;
  size_t size = 1024 + (size_t)w * h * 4;
  u16 code[256];
  u8 *b;
  double black = pow(10.0, (95 - 685) * 0.002 / 0.6);
  if (!im->px || !w || !h || (double)w * h * 4 > 512.0 * 1024 * 1024) return NULL;
  if (!(b = (u8 *)calloc(1, size))) return NULL;
  for (k = 0; k < 256; k++) {
    double lin = dec8((int)k) * (1 - black) + black, v = 685 + log10(lin) * 0.6 / 0.002;
    code[k] = (u16)(v < 0 ? 0 : v > 1023 ? 1023 : (int)(v + 0.5));
  }
  p32(b, 0x802A5FD7UL); p32(b + 4, 1024); p32(b + 8, 1024); p32(b + 12, 0); p32(b + 16, 0); p32(b + 20, (u32)size);
  memcpy(b + 24, "V4.5", 4);
  b[192] = 0; b[193] = 3;
  for (k = 0; k < 3; k++) {
    u8 *c = b + 196 + 28 * k;
    c[0] = 0; c[1] = (u8)(k + 1); c[2] = 10;
    p32(c + 4, w); p32(c + 8, h);
    pf32(c + 12, 0); pf32(c + 16, 0); pf32(c + 20, 1023); pf32(c + 24, 2.048f);
  }
  b[680] = 0; b[681] = 5; b[682] = 0; b[683] = 0;
  pf32(b + 980, 1.7f);
  for (y = 0; y < h; y++) {
    const u8 *s = im->px + (size_t)y * w * 4;
    u8 *o = b + 1024 + (size_t)y * w * 4;
    for (x = 0; x < w; x++, s += 4, o += 4) {
      int r = s[0], g = s[1], bl = s[2], a = s[3];
      if (a != 255) { r = (r * a + 255 * (255 - a) + 127) / 255; g = (g * a + 255 * (255 - a) + 127) / 255; bl = (bl * a + 255 * (255 - a) + 127) / 255; }
      p32(o, (u32)code[r] << 22 | (u32)code[g] << 12 | (u32)code[bl] << 2);
    }
  }
  *len = size;
  return b;
}

/* ------------------------------------------------------------------ DICOM */
typedef struct { const u8 *d; size_t n, p; int explicit; } Dcm;
static int dcm_long_vr(const u8 *vr)
{
  static const char *const l[] = { "OB", "OW", "OF", "SQ", "UT", "UN", "OD", "OL", "UC", "UR", "OV", "SV", "UV" };
  int i;
  for (i = 0; i < 13; i++) if (vr[0] == l[i][0] && vr[1] == l[i][1]) return 1;
  return 0;
}
/* the next element: its tag, VR (explicit only) and value; 0 at the end or on damage. A sequence
   of undefined length is skipped to its delimiter (value NULL) */
static int dcm_skip_undefined(Dcm *s, int depth);
static int dcm_next(Dcm *s, u32 *tag, const u8 **val, u32 *len, int *sq)
{
  u32 g, e, l;
  int expl = s->explicit;
  if (s->p + 8 > s->n) return 0;
  g = le16(s->d + s->p); e = le16(s->d + s->p + 2);
  *tag = g << 16 | e;
  if (g == 2) expl = 1;                                  /* (the file meta group is always explicit) */
  *sq = 0;
  if (g == 0xFFFE) { l = le32(s->d + s->p + 4); s->p += 8; *val = NULL; *len = 0; if (l != 0xFFFFFFFFUL && e == 0xE000) { if (l > s->n - s->p) return 0; s->p += l; } return 1; }
  if (expl) {
    const u8 *vr = s->d + s->p + 4;
    if (dcm_long_vr(vr)) {
      if (s->p + 12 > s->n) return 0;
      l = le32(s->d + s->p + 8); s->p += 12;
    } else { l = le16(s->d + s->p + 6); s->p += 8; }
    *sq = vr[0] == 'S' && vr[1] == 'Q';
  } else { l = le32(s->d + s->p + 4); s->p += 8; *sq = l == 0xFFFFFFFFUL; }
  if (l == 0xFFFFFFFFUL) {
    *val = NULL; *len = 0;
    if (*tag == 0x7FE00010UL) return 1;                   /* (encapsulated pixel data: compressed) */
    return dcm_skip_undefined(s, 0);
  }
  if (l > s->n - s->p) return 0;
  *val = s->d + s->p; *len = l;
  s->p += l;
  return 1;
}
static int dcm_skip_undefined(Dcm *s, int depth)
{
  /* items and elements up to the sequence delimiter (FFFE,E0DD) */
  if (depth > 16) return 0;
  while (s->p + 8 <= s->n) {
    u32 g = le16(s->d + s->p), e = le16(s->d + s->p + 2), l = le32(s->d + s->p + 4);
    if (g == 0xFFFE) {
      s->p += 8;
      if (e == 0xE0DD) return 1;
      if (e == 0xE000 && l != 0xFFFFFFFFUL) { if (l > s->n - s->p) return 0; s->p += l; }
      continue;                                          /* (an item of undefined length: its elements follow) */
    }
    {
      u32 tag, len;
      const u8 *v;
      int sq;
      if (!dcm_next(s, &tag, &v, &len, &sq)) return 0;
    }
  }
  return 0;
}
static double dcm_num(const u8 *v, u32 len)
{
  char b[32];
  u32 k;
  if (!v || !len) return 0;
  for (k = 0; k < len && k < 31 && v[k] != '\\'; k++) b[k] = (char)v[k];
  b[k] = 0;
  return atof(b);
}
static int dicom_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  Dcm s;
  u32 tag, len, rows = 0, cols = 0, spp = 1, bits = 16, pixrep = 0, planar = 0, frames = 1;
  const u8 *v, *pix = NULL;
  u32 pixlen = 0, x, y;
  int sq, mono1 = 0, rgb = 0, have_win = 0, big = 0;     /* (big-endian files are refused: big stays 0) */
  double wc = 0, ww = 0, slope = 1, inter = 0;
  char ts[72] = "";
  s.d = d; s.n = n; s.p = 132; s.explicit = 1;
  for (;;) {
    /* past the file meta group, the transfer syntax says how the rest is coded */
    if (ts[0] && s.p + 2 <= n && le16(d + s.p) != 2) { s.explicit = strcmp(ts, "1.2.840.10008.1.2") != 0; ts[0] = 0; }
    if (!dcm_next(&s, &tag, &v, &len, &sq)) break;
    switch (tag) {
    case 0x00020010UL: if (v && len < sizeof ts) { memcpy(ts, v, len); ts[len] = 0; while (len && (ts[len - 1] == 0 || ts[len - 1] == ' ')) ts[--len] = 0; }
      if (strcmp(ts, "1.2.840.10008.1.2") && strcmp(ts, "1.2.840.10008.1.2.1")) { err_set(err, errcap, "Only uncompressed little-endian DICOM files are supported"); return 0; }
      break;
    case 0x00280002UL: if (v && len >= 2) spp = le16(v); break;
    case 0x00280004UL: if (v) { mono1 = len >= 11 && !memcmp(v, "MONOCHROME1", 11); rgb = len >= 3 && !memcmp(v, "RGB", 3); } break;
    case 0x00280006UL: if (v && len >= 2) planar = le16(v); break;
    case 0x00280008UL: frames = (u32)dcm_num(v, len); if (!frames) frames = 1; break;
    case 0x00280010UL: if (v && len >= 2) rows = le16(v); break;
    case 0x00280011UL: if (v && len >= 2) cols = le16(v); break;
    case 0x00280100UL: if (v && len >= 2) bits = le16(v); break;
    case 0x00280103UL: if (v && len >= 2) pixrep = le16(v); break;
    case 0x00281050UL: wc = dcm_num(v, len); have_win |= 1; break;
    case 0x00281051UL: ww = dcm_num(v, len); have_win |= 2; break;
    case 0x00281052UL: inter = dcm_num(v, len); break;
    case 0x00281053UL: slope = dcm_num(v, len); if (slope == 0) slope = 1; break;
    case 0x7FE00010UL:
      if (!v) { err_set(err, errcap, "Compressed DICOM pixel data isn't supported"); return 0; }
      pix = v; pixlen = len; break;
    }
    if (pix) break;
  }
  (void)frames;
  if (!pix) { err_set(err, errcap, "No DICOM pixel data"); return 0; }
  if (bits != 8 && bits != 16) { err_set(err, errcap, "Unsupported DICOM sample size"); return 0; }
  if (!(spp == 1 || (spp == 3 && rgb))) { err_set(err, errcap, "Unsupported DICOM colour space"); return 0; }
  if (!rows || !cols || (double)rows * cols * spp * (bits / 8) > pixlen) { err_set(err, errcap, "Damaged DICOM pixel data"); return 0; }
  if (!alloc_rgba(out, cols, rows, err, errcap)) return 0;
  {
    long np = (long)rows * cols, k;
    double lo = 1e30, hi = -1e30;
    if (spp == 1) {
      double *val = (double *)malloc((size_t)np * sizeof(double));
      if (!val) { rgba_free(out); codec_oom = 1; err_set(err, errcap, "Not enough memory for the picture"); return 0; }
      for (k = 0; k < np; k++) {
        double t;
        if (bits == 8) t = pixrep ? (double)(signed char)pix[k] : pix[k];
        else { u32 r = big ? ((u32)pix[2 * k] << 8 | pix[2 * k + 1]) : le16(pix + 2 * k); t = pixrep ? (double)(i16)(u16)r : r; }
        t = t * slope + inter;
        val[k] = t;
        if (t < lo) lo = t;
        if (t > hi) hi = t;
      }
      if (have_win == 3 && ww >= 1) { lo = wc - 0.5 - (ww - 1) / 2; hi = wc - 0.5 + (ww - 1) / 2; }
      for (k = 0; k < np; k++) {
        double t = hi > lo ? (val[k] - lo) / (hi - lo) : 0;
        int g = (int)(clamp01(t) * 255 + 0.5);
        u8 *o = out->px + k * 4;
        if (mono1) g = 255 - g;
        o[0] = o[1] = o[2] = (u8)g; o[3] = 255;
      }
      free(val);
    } else {
      for (y = 0; y < rows; y++) for (x = 0; x < cols; x++) {
        long i = (long)y * cols + x;
        u8 *o = out->px + i * 4;
        int c;
        for (c = 0; c < 3; c++) {
          long at = planar ? (long)c * np + i : i * 3 + c;
          o[c] = bits == 8 ? pix[at] : (u8)((big ? pix[2 * at] : pix[2 * at + 1]));
        }
        o[3] = 255;
      }
    }
  }
  return 1;
}

/* ------------------------------------------------------------------ OpenEXR */
static float half_to_float(u32 h)
{
  u32 s = h >> 15 & 1, e = h >> 10 & 31, m = h & 1023;
  float f;
  if (e == 0) f = (float)ldexp((double)m, -24);
  else if (e == 31) f = m ? 0.0f : 65504.0f;            /* (infinities kept large, NaN as 0) */
  else f = (float)ldexp((double)(m | 1024), (int)e - 25);
  return s ? -f : f;
}
static float u32_to_float(u32 v) { float f; memcpy(&f, &v, 4); return f == f && f < 1e30f && f > -1e30f ? f : 0.0f; }
typedef struct { char name[32]; int type, xs, ys; } ExrChan;
/* undo ZIP's and RLE's byte predictor and interleave */
static void exr_unpredict(u8 *t, size_t n, u8 *o)
{
  size_t i, half = (n + 1) / 2;
  for (i = 1; i < n; i++) t[i] = (u8)(t[i - 1] + t[i] - 128);
  for (i = 0; i < n / 2; i++) { o[2 * i] = t[i]; o[2 * i + 1] = t[half + i]; }
  if (n & 1) o[n - 1] = t[half - 1];
}
static long exr_rle(const u8 *in, size_t n, u8 *out, size_t cap)
{
  size_t p = 0, o = 0;
  while (p < n) {
    int c = (signed char)in[p++];
    if (c < 0) {
      size_t k = (size_t)-c;
      if (k > n - p || k > cap - o) return -1;
      memcpy(out + o, in + p, k); p += k; o += k;
    } else {
      size_t k = (size_t)c + 1;
      if (p >= n || k > cap - o) return -1;
      memset(out + o, in[p++], k); o += k;
    }
  }
  return (long)o;
}
/* ---- PIZ: OpenEXR's Huffman coder and Haar wavelet (after ImfHuf.cpp and ImfWav.cpp) ---- */
#define HUF_ENCSIZE ((1 << 16) + 1)
typedef struct { const u8 *p, *e; u32 c; int lc; } Bits;
static int bits_get(Bits *b, int n, u32 *v)
{
  while (b->lc < n) { if (b->p >= b->e) return 0; b->c = (b->c << 8) | *b->p++; b->lc += 8; }
  b->lc -= n;
  *v = (b->c >> b->lc) & ((1UL << n) - 1);
  return 1;
}
/* the decoded u16s into raw (nraw of them); 0 on damage */
static int huf_uncompress(const u8 *in, size_t n, u16 *raw, long nraw)
{
  u32 im, iM, nbits, i, k;
  u8 *len = NULL;
  u32 *first = NULL, *cnt = NULL, **syms = NULL, *symbuf = NULL;
  Bits b;
  long o = 0;
  int ok = 0, l;
  if (n < 20) return 0;
  im = le32(in); iM = le32(in + 4); nbits = le32(in + 12);
  if (im >= HUF_ENCSIZE || iM >= HUF_ENCSIZE || im > iM) return 0;
  if (!(len = (u8 *)calloc(HUF_ENCSIZE, 1)) || !(first = (u32 *)calloc(59, sizeof(u32))) || !(cnt = (u32 *)calloc(59, sizeof(u32))) ||
      !(syms = (u32 **)calloc(59, sizeof(u32 *))) || !(symbuf = (u32 *)malloc(HUF_ENCSIZE * sizeof(u32)))) goto done;
  b.p = in + 20; b.e = in + n; b.c = 0; b.lc = 0;
  for (i = im; i <= iM; i++) {
    u32 v;
    if (!bits_get(&b, 6, &v)) goto done;
    if (v == 63) {
      u32 z;
      if (!bits_get(&b, 8, &z)) goto done;
      z += 6;
      if (i + z > iM + 1) goto done;
      i += z - 1;                                       /* (len stays 0) */
    } else if (v >= 59) {
      u32 z = v - 59 + 2;
      if (i + z > iM + 1) goto done;
      i += z - 1;
    } else len[i] = (u8)v;
  }
  /* canonical codes: per length, increasing with the symbol, longer lengths taking the small values */
  for (i = 0; i < HUF_ENCSIZE; i++) cnt[len[i]]++;
  {
    u32 c = 0, nc;
    for (l = 58; l > 0; l--) { nc = (c + cnt[l]) >> 1; first[l] = c; c = nc; }
  }
  {
    u32 at = 0;
    for (l = 1; l <= 58; l++) { syms[l] = symbuf + at; at += cnt[l]; cnt[l] = 0; }
    for (i = 0; i < HUF_ENCSIZE; i++) if (len[i]) syms[len[i]][cnt[len[i]]++] = i;
  }
  /* the data: nbits of codes, the bytes after the table */
  {
    Bits d;
    u32 used = 0, code = 0;
    int cl = 0;
    d.p = b.p; d.e = in + n; d.c = 0; d.lc = 0;
    if ((double)nbits > 8.0 * (double)(in + n - b.p)) goto done;
    while (used < nbits) {
      u32 bit;
      if (!bits_get(&d, 1, &bit)) goto done;
      used++;
      code = code << 1 | bit; cl++;
      if (cl > 58) goto done;
      if (cnt[cl] && code >= first[cl] && code - first[cl] < cnt[cl]) {
        u32 s = syms[cl][code - first[cl]];
        if (s == iM) {                                  /* the run-length symbol: the last value again */
          u32 rep;
          if (used + 8 > nbits || !bits_get(&d, 8, &rep) || o == 0 || o + (long)rep > nraw) goto done;
          used += 8;
          for (k = 0; k < rep; k++) { raw[o] = raw[o - 1]; o++; }
        } else {
          if (o >= nraw) goto done;
          raw[o++] = (u16)s;
        }
        code = 0; cl = 0;
      }
    }
    ok = o == nraw;
  }
done:
  free(len); free(first); free(cnt); free(syms); free(symbuf);
  return ok;
}
static void wdec14(u16 l, u16 h, u16 *a, u16 *b)
{
  i16 ls = (i16)l, hs = (i16)h;
  int hi = hs, ai = ls + (hi & 1) + (hi >> 1);
  *a = (u16)(i16)ai; *b = (u16)(i16)(ai - hi);
}
static void wdec16(u16 l, u16 h, u16 *a, u16 *b)
{
  int m = l, d = h, bb = (m - (d >> 1)) & 0xFFFF, aa = (d + bb - 0x8000) & 0xFFFF;
  *b = (u16)bb; *a = (u16)aa;
}
static void wdec(int w14, u16 l, u16 h, u16 *a, u16 *b) { if (w14) wdec14(l, h, a, b); else wdec16(l, h, a, b); }
static void wav2_decode(u16 *in, int nx, int ox, int ny, int oy, u16 mx)
{
  int w14 = mx < (1 << 14), n = nx > ny ? ny : nx, p = 1, p2;
  while (p <= n) p <<= 1;
  p >>= 1; p2 = p; p >>= 1;
  while (p >= 1) {
    long oy1 = (long)oy * p, oy2 = (long)oy * p2, ox1 = (long)ox * p, ox2 = (long)ox * p2;
    long yi, ey = (long)oy * (ny - p2), xi = 0, ex;
    u16 i00, i01, i10, i11;
    for (yi = 0; yi <= ey; yi += oy2) {
      u16 *py = in + yi;
      ex = (long)ox * (nx - p2);
      for (xi = 0; xi <= ex; xi += ox2) {
        u16 *px = py + xi, *p01 = px + ox1, *p10 = px + oy1, *p11 = p10 + ox1;
        wdec(w14, *px, *p10, &i00, &i10);
        wdec(w14, *p01, *p11, &i01, &i11);
        wdec(w14, i00, i01, px, p01);
        wdec(w14, i10, i11, p10, p11);
      }
      if (nx & p) { u16 *px = py + xi, *p10 = px + oy1; wdec(w14, *px, *p10, &i00, p10); *px = i00; }
    }
    if (ny & p) {
      u16 *py = in + yi;
      ex = (long)ox * (nx - p2);
      for (xi = 0; xi <= ex; xi += ox2) { u16 *px = py + xi, *p01 = px + ox1; wdec(w14, *px, *p01, &i00, p01); *px = i00; }
    }
    p2 = p; p >>= 1;
  }
}
/* one PIZ block into out (little-endian bytes, the lines and channels as the file lays them) */
static int piz_block(const u8 *in, size_t n, const ExrChan *ch, int nch, int w, int lines, u8 *out, size_t outlen)
{
  u8 *bitmap = (u8 *)calloc(8192, 1);
  u16 *lut = (u16 *)malloc(65536 * sizeof(u16)), *tmp = NULL, maxv;
  u32 mn, mx, hl;
  size_t p = 4;
  long nraw = (long)(outlen / 2), k;
  int i, c, ok = 0, y;
  if (!bitmap || !lut || n < 4) goto done;
  mn = le16(in); mx = le16(in + 2);
  if (mx >= 8192) goto done;
  if (mn <= mx) { if (n < 4 + (mx - mn + 1)) goto done; memcpy(bitmap + mn, in + 4, mx - mn + 1); p += mx - mn + 1; }
  {
    int kk = 0;
    for (i = 0; i < 65536; i++) if (i == 0 || (bitmap[i >> 3] & (1 << (i & 7)))) lut[kk++] = (u16)i;
    maxv = (u16)(kk - 1);
    while (kk < 65536) lut[kk++] = 0;
  }
  if (p + 4 > n) goto done;
  hl = le32(in + p); p += 4;
  if (hl > n - p) goto done;
  if (!(tmp = (u16 *)malloc((size_t)nraw * sizeof(u16)))) goto done;
  if (!huf_uncompress(in + p, hl, tmp, nraw)) goto done;
  {
    long at = 0;
    for (c = 0; c < nch; c++) {
      int size = ch[c].type == 1 ? 1 : 2, j;
      for (j = 0; j < size; j++) wav2_decode(tmp + at + j, w, size, lines, w * size, maxv);
      at += (long)w * lines * size;
    }
  }
  for (k = 0; k < nraw; k++) tmp[k] = lut[tmp[k]];
  {
    long o = 0, at[16];
    long a = 0;
    for (c = 0; c < nch; c++) { at[c] = a; a += (long)w * lines * (ch[c].type == 1 ? 1 : 2); }
    for (y = 0; y < lines; y++)
      for (c = 0; c < nch; c++) {
        long m = (long)w * (ch[c].type == 1 ? 1 : 2);
        for (k = 0; k < m; k++) { u16 v = tmp[at[c] + k]; out[o++] = (u8)v; out[o++] = (u8)(v >> 8); }
        at[c] += m;
      }
  }
  ok = 1;
done:
  free(bitmap); free(lut); free(tmp);
  return ok;
}
/* ---- round 3: PXR24 and B44/B44A (after the OpenEXR file layout; Pxr24 and B44 compressors) ---- */
/* PXR24: zlib of, line by line and channel by channel, byte planes of the differences (UINT 4 planes,
   HALF 2, FLOAT 3: its top 24 bits) */
static int pxr24_block(const u8 *in, size_t n, const ExrChan *ch, int nch, int w, int lines, u8 *out, size_t outlen)
{
  size_t raw = 0, at = 0, o = 0;
  u8 *t;
  int y, c, x;
  long got;
  for (c = 0; c < nch; c++) raw += (size_t)w * (ch[c].type == 0 ? 4 : ch[c].type == 1 ? 2 : 3);
  raw *= (size_t)lines;
  if (!(t = (u8 *)malloc(raw ? raw : 1))) return 0;
  got = zlib_decompress(in, n, t, raw);
  if (got < 0) { free(t); return 0; }
  for (y = 0; y < lines; y++)
    for (c = 0; c < nch; c++) {
      u32 pix = 0;
      const u8 *p0 = t + at, *p1 = p0 + w, *p2 = p1 + w, *p3 = p2 + w;
      if (ch[c].type == 0) {
        for (x = 0; x < w; x++) { pix += ((u32)p0[x] << 24) | ((u32)p1[x] << 16) | ((u32)p2[x] << 8) | p3[x]; if (o + 4 > outlen) goto bad; out[o++] = (u8)pix; out[o++] = (u8)(pix >> 8); out[o++] = (u8)(pix >> 16); out[o++] = (u8)(pix >> 24); }
        at += (size_t)w * 4;
      } else if (ch[c].type == 1) {
        for (x = 0; x < w; x++) { pix = (pix + (((u32)p0[x] << 8) | p1[x])) & 0xFFFF; if (o + 2 > outlen) goto bad; out[o++] = (u8)pix; out[o++] = (u8)(pix >> 8); }
        at += (size_t)w * 2;
      } else {
        for (x = 0; x < w; x++) { pix += ((u32)p0[x] << 24) | ((u32)p1[x] << 16) | ((u32)p2[x] << 8); if (o + 4 > outlen) goto bad; out[o++] = (u8)pix; out[o++] = (u8)(pix >> 8); out[o++] = (u8)(pix >> 16); out[o++] = (u8)(pix >> 24); }
        at += (size_t)w * 3;
      }
    }
  free(t);
  return o == outlen;
bad:
  free(t);
  return 0;
}
/* B44: each HALF channel in 4 x 4 blocks of 14 bytes (or 3 for a flat one in B44A); other channels raw */
static void b44_unpack14(const u8 *b, u16 *s)
{
  u16 shift = (u16)(b[2] >> 2), bias = (u16)(0x20u << shift);
  int i;
  s[0] = (u16)((b[0] << 8) | b[1]);
  s[4] = (u16)(s[0] + ((((b[2] << 4) | (b[3] >> 4)) & 0x3fu) << shift) - bias);
  s[8] = (u16)(s[4] + ((((b[3] << 2) | (b[4] >> 6)) & 0x3fu) << shift) - bias);
  s[12] = (u16)(s[8] + ((b[4] & 0x3fu) << shift) - bias);
  s[1] = (u16)(s[0] + ((u32)(b[5] >> 2) << shift) - bias);
  s[5] = (u16)(s[4] + ((((b[5] << 4) | (b[6] >> 4)) & 0x3fu) << shift) - bias);
  s[9] = (u16)(s[8] + ((((b[6] << 2) | (b[7] >> 6)) & 0x3fu) << shift) - bias);
  s[13] = (u16)(s[12] + ((b[7] & 0x3fu) << shift) - bias);
  s[2] = (u16)(s[1] + ((u32)(b[8] >> 2) << shift) - bias);
  s[6] = (u16)(s[5] + ((((b[8] << 4) | (b[9] >> 4)) & 0x3fu) << shift) - bias);
  s[10] = (u16)(s[9] + ((((b[9] << 2) | (b[10] >> 6)) & 0x3fu) << shift) - bias);
  s[14] = (u16)(s[13] + ((b[10] & 0x3fu) << shift) - bias);
  s[3] = (u16)(s[2] + ((u32)(b[11] >> 2) << shift) - bias);
  s[7] = (u16)(s[6] + ((((b[11] << 4) | (b[12] >> 4)) & 0x3fu) << shift) - bias);
  s[11] = (u16)(s[10] + ((((b[12] << 2) | (b[13] >> 6)) & 0x3fu) << shift) - bias);
  s[15] = (u16)(s[14] + ((b[13] & 0x3fu) << shift) - bias);
  for (i = 0; i < 16; i++) s[i] = (u16)((s[i] & 0x8000) ? (s[i] & 0x7fff) : (u16)~s[i]);
}
static int b44_block(const u8 *in, size_t n, const ExrChan *ch, int nch, int w, int lines, u8 *out, size_t outlen)
{
  size_t p = 0, at[16], a = 0;
  int c, x, y, i, j;
  u8 *plane;
  if (!(plane = (u8 *)malloc(outlen ? outlen : 1))) return 0;
  /* each channel's block, whole, one after the other */
  for (c = 0; c < nch; c++) {
    size_t sz = (size_t)w * lines * (ch[c].type == 1 ? 2 : 4);
    at[c] = a;
    if (ch[c].type != 1) {
      if (sz > n - p || a + sz > outlen) goto bad;
      memcpy(plane + a, in + p, sz); p += sz;
    } else {
      for (y = 0; y < lines; y += 4)
        for (x = 0; x < w; x += 4) {
          u16 s[16];
          if (n - p < 3) goto bad;
          if (in[p + 2] >= (13 << 2)) { s[0] = (u16)((in[p] << 8) | in[p + 1]); s[0] = (u16)((s[0] & 0x8000) ? (s[0] & 0x7fff) : (u16)~s[0]); for (i = 1; i < 16; i++) s[i] = s[0]; p += 3; }
          else { if (n - p < 14) goto bad; b44_unpack14(in + p, s); p += 14; }
          for (i = 0; i < 4 && y + i < lines; i++)
            for (j = 0; j < 4 && x + j < w; j++) {
              size_t q = a + ((size_t)(y + i) * w + x + j) * 2;
              plane[q] = (u8)s[i * 4 + j]; plane[q + 1] = (u8)(s[i * 4 + j] >> 8);
            }
        }
    }
    a += sz;
  }
  if (a != outlen) goto bad;
  /* the planes as the lines and channels a block lays them out */
  {
    size_t o = 0;
    for (y = 0; y < lines; y++)
      for (c = 0; c < nch; c++) {
        size_t l = (size_t)w * (ch[c].type == 1 ? 2 : 4);
        memcpy(out + o, plane + at[c] + (size_t)y * l, l);
        o += l;
      }
  }
  free(plane);
  return 1;
bad:
  free(plane);
  return 0;
}

/* one chunk (scanline block or tile) of bw x lines pixels into buf, laid out line by line, channel by channel */
static int exr_chunk(int comp, const u8 *data, u32 sz, const ExrChan *ch, int nch, int bw, int lines, u8 *buf, u8 *t, size_t want)
{
  if (sz == want) { memcpy(buf, data, want); return 1; }   /* (a chunk that didn't compress is stored) */
  if (comp == 1) { long got = exr_rle(data, sz, t, want); if (got != (long)want) return 0; exr_unpredict(t, want, buf); return 1; }
  if (comp == 2 || comp == 3) {
    long got;
    if (sz < 2) return 0;
    got = zinflate(data + 2, sz - 2, t, want);
    if (got != (long)want) return 0;
    exr_unpredict(t, want, buf);
    return 1;
  }
  if (comp == 4) { ExrChan cc[16]; memcpy(cc, ch, sizeof cc); return piz_block(data, sz, cc, nch, bw, lines, buf, want); }
  if (comp == 5) return pxr24_block(data, sz, ch, nch, bw, lines, buf, want);
  if (comp == 6 || comp == 7) return b44_block(data, sz, ch, nch, bw, lines, buf, want);
  return 0;
}

float *exr_read_float(const u8 *d, size_t n, i32 *ow, i32 *oh, char *err, size_t errcap)
{
  size_t p = 8;
  ExrChan ch[16];
  int nch = -1, comp = -1, c, lpb, slot[16], tiled, tmode = -1;
  i32 x0 = 0, y0 = 0, x1 = -1, y1 = -1, w, h, tw = 0, th = 0;
  long nchunks, k, ntx = 1;
  float *f = NULL;
  u8 *buf = NULL, *t = NULL;
  size_t linebytes = 0;
  u32 flags;
  if (n < 8) { err_set(err, errcap, "Not an OpenEXR file"); return NULL; }
  flags = le32(d + 4);
  if ((flags & 0xFF) != 2) { err_set(err, errcap, "Unsupported OpenEXR version"); return NULL; }
  if (flags & 0x1800) { err_set(err, errcap, "Deep and multi-part OpenEXR files aren't supported"); return NULL; }
  tiled = (flags & 0x200) != 0;                          /* round 3: single-part tiled files */
  /* the header's attributes */
  for (;;) {
    const u8 *name = d + p, *type;
    size_t nl, tl;
    u32 sz;
    if (p >= n) goto trunc;
    if (!d[p]) { p++; break; }
    for (nl = 0; p + nl < n && d[p + nl] && nl < 256; nl++) ;
    if (p + nl >= n || nl >= 256) goto trunc;
    type = name + nl + 1;
    for (tl = 0; p + nl + 1 + tl < n && type[tl] && tl < 256; tl++) ;
    if (p + nl + 1 + tl + 5 > n) goto trunc;
    sz = le32(type + tl + 1);
    p += nl + 1 + tl + 1 + 4;
    if (sz > n - p) goto trunc;
    if (nl == 8 && !memcmp(name, "channels", 8)) {
      size_t q = p, e = p + sz;
      nch = 0;
      while (q < e && d[q]) {
        size_t l;
        for (l = 0; q + l < e && d[q + l]; l++) ;
        if (q + l + 17 > e || nch >= 16) { err_set(err, errcap, "Damaged OpenEXR channel list"); return NULL; }
        memset(ch[nch].name, 0, sizeof ch[nch].name);
        memcpy(ch[nch].name, d + q, l < 31 ? l : 31);
        q += l + 1;
        ch[nch].type = (int)le32(d + q); ch[nch].xs = (int)le32(d + q + 8); ch[nch].ys = (int)le32(d + q + 12);
        if (ch[nch].type < 0 || ch[nch].type > 2 || ch[nch].xs != 1 || ch[nch].ys != 1) { err_set(err, errcap, "Unsupported OpenEXR channels (subsampled)"); return NULL; }
        q += 16; nch++;
      }
    } else if (nl == 11 && !memcmp(name, "compression", 11) && sz >= 1) comp = d[p];
    else if (nl == 10 && !memcmp(name, "dataWindow", 10) && sz >= 16) {
      x0 = (i32)le32(d + p); y0 = (i32)le32(d + p + 4); x1 = (i32)le32(d + p + 8); y1 = (i32)le32(d + p + 12);
    } else if (nl == 5 && !memcmp(name, "tiles", 5) && sz >= 9) {
      tw = (i32)le32(d + p); th = (i32)le32(d + p + 4); tmode = d[p + 8] & 15;
    }
    p += sz;
  }
  if (nch < 1 || comp < 0 || x1 < x0 || y1 < y0) { err_set(err, errcap, "Damaged OpenEXR header"); return NULL; }
  if ((double)x1 - x0 + 1 > MAXDIM || (double)y1 - y0 + 1 > MAXDIM) { err_set(err, errcap, "Bad OpenEXR picture size"); return NULL; }
  w = x1 - x0 + 1; h = y1 - y0 + 1;
  if ((double)w * h > CODEC_MAXPIXELS) { err_set(err, errcap, "Bad OpenEXR picture size"); return NULL; }
  switch (comp) {
  case 0: case 1: case 2: lpb = 1; break;
  case 3: case 5: lpb = 16; break;
  case 4: case 6: case 7: lpb = 32; break;
  default: err_set(err, errcap, "Unsupported OpenEXR compression (DWA isn't read)"); return NULL;
  }
  if (tiled) {
    if (tw < 1 || th < 1 || tw > 4096 || th > 4096 || tmode < 0 || tmode > 2) { err_set(err, errcap, "Damaged OpenEXR tile description"); return NULL; }
    ntx = (w + tw - 1) / tw;
    nchunks = ntx * ((h + th - 1) / th);                 /* level 0's tiles come first in every level mode */
    lpb = th;
  } else { tw = w; nchunks = (h + lpb - 1) / lpb; }
  for (c = 0; c < nch; c++) linebytes += (size_t)tw * (ch[c].type == 1 ? 2 : 4);
  /* which channel goes where: R G B A, or Y (grey) */
  for (c = 0; c < nch; c++) {
    const char *nm = strrchr(ch[c].name, '.') ? strrchr(ch[c].name, '.') + 1 : ch[c].name;
    slot[c] = !strcmp(nm, "R") ? 0 : !strcmp(nm, "G") ? 1 : !strcmp(nm, "B") ? 2 : !strcmp(nm, "A") ? 3 : !strcmp(nm, "Y") ? 4 : -1;
  }
  if ((double)nchunks * 8 > (double)(n - p)) goto trunc;
  if (!(f = (float *)malloc((size_t)w * h * 4 * sizeof(float))) || !(buf = (u8 *)malloc(linebytes * lpb)) || !(t = (u8 *)malloc(linebytes * lpb))) {
    free(f); free(buf); free(t); codec_oom = 1; err_set(err, errcap, "Not enough memory for the picture"); return NULL;
  }
  for (k = 0; k < (long)w * h; k++) { f[k * 4] = f[k * 4 + 1] = f[k * 4 + 2] = 0; f[k * 4 + 3] = 1; }
  for (k = 0; k < nchunks && !progress(k, nchunks); k++) {
    u32 off = le32(d + p + 8 * k), hi = le32(d + p + 8 * k + 4), sz;
    i32 cy, cx = 0, lines, yy, bw = w;
    const u8 *data;
    size_t want, lb = linebytes;
    if (hi || off > n - 8) goto damaged;
    if (tiled) {
      i32 tx, ty;
      if (off > n - 20) goto damaged;
      tx = (i32)le32(d + off); ty = (i32)le32(d + off + 4);
      if (le32(d + off + 8) || le32(d + off + 12) || tx < 0 || ty < 0 || tx >= ntx || (long)ty * tw >= 0x7FFFFFFFL) goto damaged;
      sz = le32(d + off + 16);
      if (sz > n - off - 20) goto damaged;
      data = d + off + 20;
      cx = tx * tw; cy = y0 + ty * th;
      if (cy > y1 || cx >= w) goto damaged;
      bw = w - cx < tw ? w - cx : tw;
      lines = y1 - cy + 1 < th ? y1 - cy + 1 : th;
      for (lb = 0, c = 0; c < nch; c++) lb += (size_t)bw * (ch[c].type == 1 ? 2 : 4);
    } else {
      cy = (i32)le32(d + off); sz = le32(d + off + 4);
      if (sz > n - off - 8 || cy < y0 || cy > y1) goto damaged;
      data = d + off + 8;
      lines = y1 - cy + 1 < lpb ? y1 - cy + 1 : lpb;
    }
    want = lb * (size_t)lines;
    if (!exr_chunk(comp, data, sz, ch, nch, bw, lines, buf, t, want)) goto damaged;
    for (yy = 0; yy < lines; yy++) {
      const u8 *q = buf + lb * yy;
      float *o = f + ((size_t)(cy - y0 + yy) * w + cx) * 4;
      for (c = 0; c < nch; c++) {
        int s = slot[c], x;
        for (x = 0; x < bw; x++) {
          float v = ch[c].type == 1 ? half_to_float(le16(q + 2 * x)) : ch[c].type == 2 ? u32_to_float(le32(q + 4 * x)) : (float)le32(q + 4 * x);
          if (s == 4) { o[x * 4] = o[x * 4 + 1] = o[x * 4 + 2] = v; }
          else if (s >= 0) o[x * 4 + s] = v;
        }
        q += (size_t)bw * (ch[c].type == 1 ? 2 : 4);
      }
    }
  }
  free(buf); free(t);
  *ow = w; *oh = h;
  return f;
trunc:
  err_set(err, errcap, "Truncated OpenEXR file");
  return NULL;
damaged:
  free(f); free(buf); free(t);
  err_set(err, errcap, "Damaged OpenEXR data");
  return NULL;
}
void tonemap_float(const float *f, long np, u8 *rgba)
{
  double sum = 0, scale;
  long i;
  for (i = 0; i < np; i++) {
    double l = 0.2126 * f[i * 4] + 0.7152 * f[i * 4 + 1] + 0.0722 * f[i * 4 + 2];
    sum += log(1e-4 + (l > 0 ? l : 0));
  }
  scale = np ? 0.18 / exp(sum / np) : 1;
  for (i = 0; i < np; i++) {
    int c;
    for (c = 0; c < 3; c++) { double v = f[i * 4 + c] * scale; if (!(v > 0)) v = 0; rgba[i * 4 + c] = (u8)enc8(v / (1 + v)); }
    { double a = f[i * 4 + 3]; rgba[i * 4 + 3] = (u8)(clamp01(a) * 255 + 0.5); }
  }
}
static int exr_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  i32 w, h;
  float *f = exr_read_float(d, n, &w, &h, err, errcap);
  if (!f) return 0;
  if (!alloc_rgba(out, (u32)w, (u32)h, err, errcap)) { free(f); return 0; }
  tonemap_float(f, (long)w * h, out->px);
  free(f);
  return 1;
}

int xread2_decode(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  out->w = out->h = 0; out->px = NULL;
  switch (xread2_detect(d, n)) {
  case XR_CINEON: return cineon_read(d, n, out, err, errcap);
  case XR_DICOM: return dicom_read(d, n, out, err, errcap);
  case XR_EXR: return exr_read(d, n, out, err, errcap);
  }
  err_set(err, errcap, "Unknown file format");
  return 0;
}

/* ------------------------------------------------------------------ Radiance HDR written */
u8 *hdr_encode(const Rgba *im, size_t *len)
{
  Buf b;
  i32 x, y;
  u8 *row;
  char hdr[96];
  if (!im->px || im->w < 1 || im->h < 1) return NULL;
  if (!(row = (u8 *)malloc((size_t)im->w * 4))) return NULL;
  buf_init(&b);
  buf_adds(&b, "#?RADIANCE\nSOFTWARE=Easel\nFORMAT=32-bit_rle_rgbe\n\n");
  snprintf(hdr, sizeof hdr, "-Y %ld +X %ld\n", (long)im->h, (long)im->w);
  buf_adds(&b, hdr);
  for (y = 0; y < im->h; y++) {
    const u8 *s = im->px + (size_t)y * im->w * 4;
    int c;
    for (x = 0; x < im->w; x++) {
      double v[3], m;
      int e, k;
      for (k = 0; k < 3; k++) v[k] = dec8(s[x * 4 + k]) * s[x * 4 + 3] / 255.0;
      m = v[0] > v[1] ? v[0] : v[1]; if (v[2] > m) m = v[2];
      if (m < 1e-32) { row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = row[x * 4 + 3] = 0; continue; }
      m = frexp(m, &e) * 256.0 / m;
      for (k = 0; k < 3; k++) { int q = (int)(v[k] * m); row[x * 4 + k] = (u8)(q > 255 ? 255 : q); }
      row[x * 4 + 3] = (u8)(e + 128);
    }
    if (im->w < 8 || im->w > 32767) { buf_add(&b, row, (size_t)im->w * 4); continue; }
    /* new-style: each channel run-length coded on its own */
    buf_addc(&b, 2); buf_addc(&b, 2); buf_addc(&b, (im->w >> 8) & 255); buf_addc(&b, im->w & 255);
    for (c = 0; c < 4; c++) {
      i32 i = 0;
      while (i < im->w) {
        i32 r = 1;
        while (i + r < im->w && r < 127 && row[(i + r) * 4 + c] == row[i * 4 + c]) r++;
        if (r >= 3) { buf_addc(&b, 128 + r); buf_addc(&b, row[i * 4 + c]); i += r; continue; }
        {
          i32 st = i, cnt = 0;
          while (i < im->w && cnt < 128) {
            i32 rr = 1;
            while (i + rr < im->w && rr < 3 && row[(i + rr) * 4 + c] == row[i * 4 + c]) rr++;
            if (rr >= 3) break;
            i++; cnt++;
          }
          buf_addc(&b, cnt);
          for (; st < i; st++) buf_addc(&b, row[st * 4 + c]);
        }
      }
    }
  }
  free(row);
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}
