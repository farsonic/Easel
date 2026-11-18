/*
 * iccio.c - Easel 1.2: ICC profiles in picture files, read and written (PNG iCCP, JPEG APP2
 * ICC_PROFILE, TIFF tag 34675, PSD image resource 1039, WebP ICCP), and the colour model a file's
 * pixels were stored in (see cms.h). Every length and offset is checked against the file.
 */
#include <stdlib.h>
#include <string.h>
#include "cms.h"
#include "codec.h"
#include "zip.h"

#define MAXICC (4UL * 1024 * 1024)

static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u32 be16(const u8 *p) { return (u32)p[0] << 8 | p[1]; }
static u32 le32(const u8 *p) { return (u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0]; }
static u32 le16(const u8 *p) { return (u32)p[1] << 8 | p[0]; }
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static void putle32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static void put16x(u8 *p, u32 v, int le) { if (le) { p[0] = (u8)v; p[1] = (u8)(v >> 8); } else { p[0] = (u8)(v >> 8); p[1] = (u8)v; } }
static void put32x(u8 *p, u32 v, int le) { if (le) putle32(p, v); else put32(p, v); }

/* a copy of n bytes that look like a profile (its own size field no bigger than n), else NULL */
static u8 *icc_copy(const u8 *s, size_t n, u32 *len)
{
  u8 *o;
  u32 sz;
  if (n < 132 || n > MAXICC || memcmp(s + 36, "acsp", 4)) return NULL;
  sz = be32(s);
  if (sz < 132 || sz > n) sz = (u32)n;
  if (!(o = (u8 *)malloc(sz))) return NULL;
  memcpy(o, s, sz);
  *len = sz;
  return o;
}

/* ------------------------------------------------------------------ reading */
static u8 *png_icc(const u8 *d, size_t n, u32 *len)
{
  size_t at = 8;
  while (at + 12 <= n) {
    u32 cl = be32(d + at);
    const u8 *t = d + at + 4;
    if (cl > n - at - 12) return NULL;
    if (!memcmp(t, "IDAT", 4) || !memcmp(t, "IEND", 4)) return NULL;
    if (!memcmp(t, "iCCP", 4)) {
      const u8 *p = t + 4;
      u32 i = 0;
      size_t cap;
      while (i < cl && i < 80 && p[i]) i++;
      if (i + 2 >= cl || p[i + 1] != 0) return NULL;
      p += i + 2; cl -= i + 2;
      if (cl < 6) return NULL;
      for (cap = 64 * 1024; cap <= MAXICC; cap *= 2) {
        u8 *o = (u8 *)malloc(cap);
        long got;
        if (!o) return NULL;
        got = zinflate(p + 2, cl - 2, o, cap);           /* (past zlib's two-byte header) */
        if (got > 0) {
          u8 *r = icc_copy(o, (size_t)got, len);
          free(o);
          return r;
        }
        free(o);
      }
      return NULL;
    }
    at += 12 + cl;
  }
  return NULL;
}
static u8 *jpeg_icc(const u8 *d, size_t n, u32 *len)
{
  /* APP2 segments "ICC_PROFILE\0", seq, count: joined in sequence order */
  size_t at = 2, total = 0;
  int count = 0, seen = 0, k;
  size_t off[256], sz[256];
  u8 *o, *r;
  memset(sz, 0, sizeof sz);
  if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return NULL;
  while (at + 4 <= n && d[at] == 0xFF) {
    int m = d[at + 1];
    u32 sl = be16(d + at + 2);
    if (m == 0xD9 || m == 0xDA) break;
    if (m == 0xFF) { at++; continue; }
    if (sl < 2 || at + 2 + sl > n) break;
    if (m == 0xE2 && sl >= 16 && !memcmp(d + at + 4, "ICC_PROFILE", 12)) {
      int seq = d[at + 16], cnt = d[at + 17];
      if (seq >= 1 && seq <= cnt && !sz[seq - 1]) { off[seq - 1] = at + 18; sz[seq - 1] = sl - 16; count = cnt; seen++; total += sl - 16; }
    }
    at += 2 + sl;
  }
  if (!count || seen != count || total > MAXICC) return NULL;
  if (!(o = (u8 *)malloc(total))) return NULL;
  for (total = 0, k = 0; k < count; k++) { memcpy(o + total, d + off[k], sz[k]); total += sz[k]; }
  r = icc_copy(o, total, len);
  free(o);
  return r;
}
/* the first IFD's entry for tag (type, count, the value or its offset); 0 if absent */
static int tiff_tag(const u8 *d, size_t n, u32 tag, u32 *type, u32 *count, u32 *val)
{
  int le;
  u32 ifd, ne, i;
  if (n < 8) return 0;
  if (d[0] == 'I' && d[1] == 'I') le = 1; else if (d[0] == 'M' && d[1] == 'M') le = 0; else return 0;
  if ((le ? le16(d + 2) : be16(d + 2)) != 42) return 0;
  ifd = le ? le32(d + 4) : be32(d + 4);
  if (ifd > n - 2) return 0;
  ne = le ? le16(d + ifd) : be16(d + ifd);
  if (ne > (n - ifd - 2) / 12) return 0;
  for (i = 0; i < ne; i++) {
    const u8 *e = d + ifd + 2 + 12 * i;
    if ((le ? le16(e) : be16(e)) != tag) continue;
    *type = le ? le16(e + 2) : be16(e + 2);
    *count = le ? le32(e + 4) : be32(e + 4);
    *val = *type == 3 && *count == 1 ? (le ? le16(e + 8) : be16(e + 8)) : (le ? le32(e + 8) : be32(e + 8));
    return 1;
  }
  return 0;
}
static u8 *tiff_icc(const u8 *d, size_t n, u32 *len)
{
  u32 type, count, val;
  if (!tiff_tag(d, n, 34675, &type, &count, &val) || count <= 4 || val > n || count > n - val) return NULL;
  return icc_copy(d + val, count, len);
}
/* the PSD's image resources section: its offset and length */
static int psd_res(const u8 *d, size_t n, size_t *at, size_t *rl)
{
  u32 cm;
  if (n < 34 || memcmp(d, "8BPS", 4)) return 0;
  cm = be32(d + 26);
  if (cm > n - 34) return 0;
  *at = 30 + cm;
  *rl = be32(d + *at);
  if (*rl > n - *at - 4) return 0;
  *at += 4;
  return 1;
}
static u8 *psd_icc(const u8 *d, size_t n, u32 *len)
{
  size_t at, rl, p, end;
  if (!psd_res(d, n, &at, &rl)) return NULL;
  for (p = at, end = at + rl; p + 12 <= end; ) {
    u32 id, nl, sz;
    if (memcmp(d + p, "8BIM", 4)) return NULL;
    id = be16(d + p + 4);
    nl = d[p + 6];
    p += 6 + ((nl + 2) & ~1u);                          /* (the Pascal name padded to even) */
    if (p + 4 > end) return NULL;
    sz = be32(d + p); p += 4;
    if (sz > end - p) return NULL;
    if (id == 1039) return icc_copy(d + p, sz, len);
    p += (sz + 1) & ~1u;
  }
  return NULL;
}
static u8 *webp_icc(const u8 *d, size_t n, u32 *len)
{
  size_t at = 12;
  if (n < 20 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WEBP", 4)) return NULL;
  while (at + 8 <= n) {
    u32 cl = le32(d + at + 4);
    if (cl > n - at - 8) return NULL;
    if (!memcmp(d + at, "ICCP", 4)) return icc_copy(d + at + 8, cl, len);
    at += 8 + ((cl + 1) & ~1u);
  }
  return NULL;
}
u8 *icc_from_file(const u8 *d, size_t n, u32 *len)
{
  switch (codec_detect(d, n)) {
  case FMT_PNG: return png_icc(d, n, len);
  case FMT_JPEG: return jpeg_icc(d, n, len);
  case FMT_TIFF: return tiff_icc(d, n, len);
  case FMT_PSD: return psd_icc(d, n, len);
  case FMT_WEBP: return webp_icc(d, n, len);
  }
  return NULL;
}
int cms_file_model(const u8 *d, size_t n)
{
  switch (codec_detect(d, n)) {
  case FMT_JPEG: {
    size_t at = 2;
    while (at + 10 <= n && d[at] == 0xFF) {
      int m = d[at + 1];
      u32 sl = be16(d + at + 2);
      if (m == 0xFF) { at++; continue; }
      if ((m >= 0xC0 && m <= 0xC3) || (m >= 0xC5 && m <= 0xC7) || (m >= 0xC9 && m <= 0xCB) || (m >= 0xCD && m <= 0xCF)) return d[at + 9] == 4 ? CM_CMYK : CM_RGB;
      if (m == 0xDA || sl < 2) break;
      at += 2 + sl;
    }
    break; }
  case FMT_TIFF: {
    u32 type, count, val;
    if (tiff_tag(d, n, 262, &type, &count, &val)) {
      if (val == 5) return CM_CMYK;
      if (val == 8 || val == 9 || val == 10) return CM_LAB;
    }
    break; }
  case FMT_PSD:
    if (n >= 26) { u32 m = be16(d + 24); if (m == 4) return CM_CMYK; if (m == 9) return CM_LAB; }
    break;
  }
  return CM_RGB;
}

/* ------------------------------------------------------------------ writing */
static u8 *grown(u8 *file, size_t len, size_t at, const u8 *ins, size_t insn, size_t *outlen)
{
  u8 *o = (u8 *)malloc(len + insn);
  if (!o) { free(file); return NULL; }
  memcpy(o, file, at); memcpy(o + at, ins, insn); memcpy(o + at + insn, file + at, len - at);
  free(file);
  *outlen = len + insn;
  return o;
}
static u8 *png_put(u8 *png, size_t *len, const u8 *icc, u32 icclen)
{
  size_t zl, cl, at;
  u8 *z, *ch, *r;
  static const char name[] = "ICC Profile";
  if (*len < 33 || memcmp(png + 12, "IHDR", 4)) return png;
  at = 8 + 12 + be32(png + 8);
  if (at > *len) return png;
  if (!(z = zlib_compress(icc, icclen, &zl))) { free(png); return NULL; }
  cl = sizeof name + 1 + zl;                          /* name, its NUL, method 0, the data */
  if (!(ch = (u8 *)malloc(cl + 12))) { free(z); free(png); return NULL; }
  put32(ch, (u32)cl); memcpy(ch + 4, "iCCP", 4); memcpy(ch + 8, name, sizeof name); ch[8 + sizeof name] = 0;
  memcpy(ch + 8 + sizeof name + 1, z, zl);
  put32(ch + 8 + cl, crc32_update(0, ch + 4, cl + 4));
  free(z);
  r = grown(png, *len, at, ch, cl + 12, len);
  free(ch);
  return r;
}
static u8 *jpeg_put(u8 *jpg, size_t *len, const u8 *icc, u32 icclen)
{
  size_t at = 2, per = 65519, nseg = (icclen + per - 1) / per, k, insn, o;
  u8 *ins, *r;
  if (*len < 4 || jpg[0] != 0xFF || jpg[1] != 0xD8 || nseg > 255 || !nseg) return jpg;
  /* after the JFIF (APP0) and EXIF (APP1) segments */
  while (at + 4 <= *len && jpg[at] == 0xFF && (jpg[at + 1] == 0xE0 || jpg[at + 1] == 0xE1)) at += 2 + be16(jpg + at + 2);
  if (at > *len) return jpg;
  insn = nseg * 18 + icclen;
  if (!(ins = (u8 *)malloc(insn))) { free(jpg); return NULL; }
  for (o = 0, k = 0; k < nseg; k++) {
    size_t part = icclen - k * per < per ? icclen - k * per : per;
    ins[o] = 0xFF; ins[o + 1] = 0xE2; ins[o + 2] = (u8)((part + 16) >> 8); ins[o + 3] = (u8)(part + 16);
    memcpy(ins + o + 4, "ICC_PROFILE", 12);
    ins[o + 16] = (u8)(k + 1); ins[o + 17] = (u8)nseg;
    memcpy(ins + o + 18, icc + k * per, part);
    o += 18 + part;
  }
  r = grown(jpg, *len, at, ins, insn, len);
  free(ins);
  return r;
}
static u8 *tiff_put(u8 *t, size_t *len, const u8 *icc, u32 icclen)
{
  /* a new first IFD at the end of the file: the old entries (their values stay where they are)
     with tag 34675 added in order, the profile just before it */
  int le;
  u32 ifd, ne, i, k, nnext;
  size_t n = *len, iccat, ifdat, total;
  u8 *o;
  if (n < 8) return t;
  if (t[0] == 'I' && t[1] == 'I') le = 1; else if (t[0] == 'M' && t[1] == 'M') le = 0; else return t;
  ifd = le ? le32(t + 4) : be32(t + 4);
  if (ifd > n - 2) return t;
  ne = le ? le16(t + ifd) : be16(t + ifd);
  if (ne > (n - ifd - 6) / 12) return t;
  nnext = le ? le32(t + ifd + 2 + 12 * ne) : be32(t + ifd + 2 + 12 * ne);
  iccat = (n + 1) & ~(size_t)1;
  ifdat = (iccat + icclen + 1) & ~(size_t)1;
  total = ifdat + 2 + 12 * (ne + 1) + 4;
  if (!(o = (u8 *)calloc(1, total))) { free(t); return NULL; }
  memcpy(o, t, n);
  memcpy(o + iccat, icc, icclen);
  put32x(o + 4, (u32)ifdat, le);
  put16x(o + ifdat, ne + 1, le);
  for (i = 0, k = 0; i <= ne; i++) {
    const u8 *e = t + ifd + 2 + 12 * k;
    u8 *d = o + ifdat + 2 + 12 * i;
    u32 tag = k < ne ? (le ? le16(e) : be16(e)) : 0xFFFFFFFFUL;
    if (tag == 34675) { free(o); return t; }             /* (one there already) */
    if (tag > 34675 && i == k) {
      put16x(d, 34675, le); put16x(d + 2, 7, le); put32x(d + 4, icclen, le); put32x(d + 8, (u32)iccat, le);
      continue;
    }
    memcpy(d, e, 12);
    k++;
  }
  put32x(o + ifdat + 2 + 12 * (ne + 1), nnext, le);
  free(t);
  *len = total;
  return o;
}
static u8 *psd_put(u8 *p, size_t *len, const u8 *icc, u32 icclen)
{
  size_t at, rl, insn = 12 + ((icclen + 1) & ~1u);
  u8 *ins, *r;
  if (!psd_res(p, *len, &at, &rl)) return p;
  if (!(ins = (u8 *)calloc(1, insn))) { free(p); return NULL; }
  memcpy(ins, "8BIM", 4); ins[4] = 1039 >> 8; ins[5] = 1039 & 255;  /* (an empty name: two zero bytes) */
  put32(ins + 8, icclen); memcpy(ins + 12, icc, icclen);
  put32(p + at - 4, (u32)(rl + insn));
  r = grown(p, *len, at + rl, ins, insn, len);
  free(ins);
  return r;
}
static u8 *webp_put(u8 *w, size_t *len, const u8 *icc, u32 icclen)
{
  /* a simple VP8L file becomes extended: VP8X (with the ICC flag), ICCP, then the image */
  size_t n = *len, pad = (icclen + 1) & ~1u, total;
  u32 wd, ht, alpha;
  u8 *o;
  if (n < 30 || memcmp(w, "RIFF", 4) || memcmp(w + 8, "WEBP", 4) || memcmp(w + 12, "VP8L", 4) || w[20] != 0x2F) return w;
  wd = 1 + (le16(w + 21) & 0x3FFF);
  ht = 1 + ((le32(w + 21) >> 14) & 0x3FFF);
  alpha = (le32(w + 21) >> 28) & 1;
  total = 12 + 18 + 8 + pad + (n - 12);
  if (!(o = (u8 *)calloc(1, total))) { free(w); return NULL; }
  memcpy(o, "RIFF", 4); putle32(o + 4, (u32)(total - 8)); memcpy(o + 8, "WEBP", 4);
  memcpy(o + 12, "VP8X", 4); putle32(o + 16, 10); o[20] = (u8)(0x20 | (alpha ? 0x10 : 0));
  o[24] = (u8)(wd - 1); o[25] = (u8)((wd - 1) >> 8); o[26] = (u8)((wd - 1) >> 16);
  o[27] = (u8)(ht - 1); o[28] = (u8)((ht - 1) >> 8); o[29] = (u8)((ht - 1) >> 16);
  memcpy(o + 30, "ICCP", 4); putle32(o + 34, icclen); memcpy(o + 38, icc, icclen);
  memcpy(o + 38 + pad, w + 12, n - 12);
  free(w);
  *len = total;
  return o;
}
u8 *icc_embed(int fmt, u8 *file, size_t *len, const u8 *icc, u32 icclen)
{
  if (!file || !icc || icclen < 132) return file;
  switch (fmt) {
  case FMT_PNG: return png_put(file, len, icc, icclen);
  case FMT_JPEG: return jpeg_put(file, len, icc, icclen);
  case FMT_TIFF: return tiff_put(file, len, icc, icclen);
  case FMT_PSD: return psd_put(file, len, icc, icclen);
  case FMT_WEBP: return webp_put(file, len, icc, icclen);
  }
  return file;
}
